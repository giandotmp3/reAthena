// Mass-save: 1,000 character batches. The sim (producer) thread must only pay for the pointer
// hand-off; this test measures exactly that and reports the numbers.
#include <algorithm>
#include <iostream>

#include "harness.hpp"

using namespace rathena::persist;
using namespace rathena::persist::testing_support;
using namespace std::chrono_literals;

namespace {
#ifdef RATHENA_PERSIST_SANITIZED
constexpr double kMedianBudgetUs = 2000.0;  // sanitizer builds are 5-20x slower
constexpr double kP90BudgetUs = 5000.0;
#else
constexpr double kMedianBudgetUs = 500.0;
constexpr double kP90BudgetUs = 1000.0;
#endif
// The maximum is reported but not asserted: on a shared VM a single sample can include a
// multi-millisecond scheduler gap (measured here: a bare clock-read loop sees gaps of 1.5-2.5 ms
// every second) that has nothing to do with the hand-off.
double us(std::chrono::steady_clock::duration d) { return std::chrono::duration<double, std::micro>(d).count(); }
} // namespace

TEST(PERSIST_MASS_001, HandingOff1000CharacterBatchesCostsTheProducerMicrosecondsNotMilliseconds) {
	Stack s;
	Wal wal({scratch_dir("mass001"), 1u << 26});
	WriteBehindOptions o;
	o.retry_interval = 5ms;
	o.max_queued_batches = 1024;
	WriteBehindService svc(s.db, wal, o);
	constexpr int kRounds = 25;
	constexpr std::uint64_t kChars = 1000;
	auto stat = [](std::vector<double> v) {
		std::sort(v.begin(), v.end());
		return std::array<double, 4>{v.front(), v[v.size() / 2], v[v.size() * 9 / 10], v.back()};
	};
	std::uint64_t rev = 0;
	std::array<double, 4> h{}, m{}, p{};
	// Timing on a shared CI VM: up to three full measurements, the budget must be met by one of
	// them (each attempt is 25 rounds of 1,000 characters; all numbers are printed).
	for (int attempt = 1; attempt <= 3; ++attempt) {
		std::vector<double> handoff_us, mark_us, persist_ms;
		for (int round = 0; round < kRounds; ++round) {
			++rev;
			const auto m0 = std::chrono::steady_clock::now();
			for (std::uint64_t c = 1; c <= kChars; ++c) {
				svc.mark(make_record(c, rev, std::string(1500, static_cast<char>('a' + round % 20))));  // character blob
				for (int k = 0; k < 4; ++k) svc.mark(make_kv(c, "m", ("k" + std::to_string(k)).c_str(), static_cast<std::int64_t>(rev), rev));
			}
			const auto m1 = std::chrono::steady_clock::now();
			const auto h0 = std::chrono::steady_clock::now();
			const auto ho = svc.handoff();            // <- the only thing the tick pays at the boundary
			const auto h1 = std::chrono::steady_clock::now();
			ASSERT_EQ(ho.records, kChars);
			ASSERT_EQ(ho.kv, kChars * 4);
			handoff_us.push_back(us(h1 - h0));
			mark_us.push_back(us(m1 - m0) / static_cast<double>(kChars * 5));
			const auto p0 = std::chrono::steady_clock::now();
			ASSERT_TRUE(svc.wait_durable(ho.epoch, 30s));
			persist_ms.push_back(us(std::chrono::steady_clock::now() - p0) / 1000.0);
		}
		h = stat(handoff_us);
		m = stat(mark_us);
		p = stat(persist_ms);
		std::cout << "[mass-save] attempt " << attempt << ": 1000 characters x (1 record 1.5 KB + 4 kv) per round, " << kRounds << " rounds\n"
		          << "[mass-save] producer hand-off      : min " << h[0] << " us, median " << h[1] << " us, p90 " << h[2] << " us, max " << h[3] << " us\n"
		          << "[mass-save] producer mark (per row): min " << m[0] << " us, median " << m[1] << " us, p90 " << m[2] << " us, max " << m[3] << " us\n"
		          << "[mass-save] writer persist (round) : min " << p[0] << " ms, median " << p[1] << " ms, p90 " << p[2] << " ms, max " << p[3] << " ms (off the tick)\n";
		if (h[1] < kMedianBudgetUs && h[2] < kP90BudgetUs) break;
	}
	RecordProperty("handoff_median_us", std::to_string(h[1]));
	RecordProperty("handoff_max_us", std::to_string(h[3]));
	EXPECT_LT(h[1], kMedianBudgetUs);
	EXPECT_LT(h[2], kP90BudgetUs);
	svc.shutdown();
	EXPECT_EQ(scalar(s.raw_view(), "SELECT COUNT(*) FROM persist_records"), 1000);
	EXPECT_EQ(scalar(s.raw_view(), "SELECT COUNT(*) FROM kv_entries"), 4000);
	EXPECT_EQ(scalar(s.raw_view(), "SELECT MIN(revision) FROM persist_records"), static_cast<std::int64_t>(rev));
}

TEST(PERSIST_MASS_002, MassLogoutOf1000CharactersIsOneBatchOneTransactionAndAllBecomeDurable) {
	Stack s;
	Wal wal({scratch_dir("mass002"), 1u << 26});
	WriteBehindService svc(s.db, wal, WriteBehindOptions{5ms, 256, {}});
	for (std::uint64_t c = 1; c <= 1000; ++c) svc.mark(make_record(c, 1, "final"));
	s.db.clear_log();
	const auto h = svc.handoff();
	EXPECT_EQ(h.records, 1000u);
	ASSERT_TRUE(svc.wait_durable(h.epoch, 30s));
	std::size_t begins = 0;
	for (const auto& l : s.db.log()) if (l == "BEGIN") ++begins;
	EXPECT_EQ(begins, 1u);  // one transaction for the whole logout wave
	for (std::uint64_t c : {1u, 500u, 1000u}) EXPECT_TRUE(svc.barrier(character(c), 1s));
	EXPECT_EQ(scalar(s.raw_view(), "SELECT COUNT(*) FROM persist_records"), 1000);
}
