// Bounded on-disk write-ahead log: one CRC-protected segment file per batch.
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>

#include "rathena/persist/codec.hpp"
#include "rathena/persist/db_config.hpp"
#include "rathena/persist/service.hpp"

namespace rathena::persist {
namespace fs = std::filesystem;
namespace {

constexpr char kMagic[4] = {'R', 'A', 'W', 'L'};
constexpr std::uint32_t kFormat = 1;
constexpr std::size_t kHeaderBytes = 4 + 4 + 8 + 8 + 4;  // magic, format, seq, payload length, crc

std::string seg_name(std::uint64_t seq) {
	char buf[40];
	std::snprintf(buf, sizeof buf, "%020llu.wal", static_cast<unsigned long long>(seq));
	return buf;
}

bool parse_seg(const std::string& name, std::uint64_t& seq) {
	if (name.size() != 24 || name.compare(20, 4, ".wal") != 0) return false;
	std::uint64_t v = 0;
	for (std::size_t i = 0; i < 20; ++i) {
		if (name[i] < '0' || name[i] > '9') return false;
		v = v * 10 + static_cast<std::uint64_t>(name[i] - '0');
	}
	seq = v;
	return true;
}

[[noreturn]] void io_fail(const std::string& what, int err) {
	throw WalIoError("WAL " + what + " failed: " + std::strerror(err));
}

void write_all(int fd, const char* data, std::size_t n) {
	while (n > 0) {
		const auto w = ::write(fd, data, n);
		if (w < 0) {
			if (errno == EINTR) continue;
			io_fail("write", errno);
		}
		data += w;
		n -= static_cast<std::size_t>(w);
	}
}

void fsync_dir(const fs::path& dir) {
	const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
	if (fd < 0) io_fail("open directory", errno);
	const int rc = ::fsync(fd);
	const int e = errno;
	::close(fd);
	if (rc != 0) io_fail("directory fsync", e);
}

std::string random_id() {
	std::random_device rd;
	std::ostringstream o;
	o << std::hex << rd() << rd();
	return o.str();
}

} // namespace

WalConfig load_wal_config_from_env(const char* var) {
	const char* v = std::getenv(var);
	if (v == nullptr || *v == '\0') throw DbConfigError(std::string("environment variable ") + var + " is not set");
	WalConfig c;
	c.dir = v;
	return c;
}

struct Wal::Impl {
	WalConfig cfg;
	mutable std::mutex mu;           // guards segs, next_seq, corrupt, id
	std::map<std::uint64_t, std::uint64_t> segs;  // seq -> file size
	std::uint64_t next_seq{1};
	std::string id;
	std::vector<std::string> corrupt;
	std::atomic<std::uint64_t> bytes{0};
	int lock_fd{-1};

	void write_id_file() {
		const fs::path tmp = cfg.dir / "WAL_ID.tmp";
		const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd < 0) io_fail("create id file", errno);
		try {
			write_all(fd, id.data(), id.size());
			if (cfg.fsync != WalFsync::Never && ::fsync(fd) != 0) io_fail("fsync", errno);
		} catch (...) { ::close(fd); throw; }
		::close(fd);
		std::error_code ec;
		fs::rename(tmp, cfg.dir / "WAL_ID", ec);
		if (ec) throw WalIoError("WAL id file rename failed: " + ec.message());
		if (cfg.fsync == WalFsync::FileAndDir) fsync_dir(cfg.dir);
	}
};

Wal::Wal(WalConfig cfg) : impl_(std::make_unique<Impl>()) {
	if (cfg.dir.empty()) throw PersistError("WAL directory is not configured (set RATHENA_WAL_DIR)");
	impl_->cfg = std::move(cfg);
	auto& c = impl_->cfg;
	std::error_code ec;
	fs::create_directories(c.dir, ec);
	if (ec) throw WalIoError("cannot create WAL directory: " + ec.message());
	const fs::path lock = c.dir / "LOCK";
	impl_->lock_fd = ::open(lock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (impl_->lock_fd < 0) io_fail("open lock file", errno);
	if (::flock(impl_->lock_fd, LOCK_EX | LOCK_NB) != 0) {
		::close(impl_->lock_fd);
		throw WalIoError("WAL directory is in use by another instance");
	}
	std::uint64_t total = 0;
	for (const auto& e : fs::directory_iterator(c.dir)) {
		const auto name = e.path().filename().string();
		std::uint64_t seq = 0;
		if (name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0 && name != "WAL_ID.tmp") {
			fs::remove(e.path(), ec);  // a crash between create and rename: never visible, safe to drop
		} else if (parse_seg(name, seq)) {
			const auto sz = static_cast<std::uint64_t>(e.file_size());
			impl_->segs[seq] = sz;
			total += sz;
			impl_->next_seq = std::max(impl_->next_seq, seq + 1);
		}
	}
	impl_->bytes.store(total);
	std::ifstream idf(c.dir / "WAL_ID");
	std::string id;
	if (!impl_->segs.empty() && idf && std::getline(idf, id) && !id.empty()) {
		impl_->id = id;
	} else {
		// No pending segments (or no id): a fresh generation. Database markers of older
		// generations are obsolete because all their segments were consumed.
		impl_->id = random_id();
		impl_->write_id_file();
	}
}

Wal::~Wal() {
	if (impl_ && impl_->lock_fd >= 0) ::close(impl_->lock_fd);
}

const std::string& Wal::id() const { return impl_->id; }
std::uint64_t Wal::bytes_used() const { return impl_->bytes.load(); }
bool Wal::accepting_writes() const { return impl_->bytes.load() < impl_->cfg.max_bytes; }
bool Wal::empty() const { std::scoped_lock l(impl_->mu); return impl_->segs.empty(); }
std::size_t Wal::segments() const { std::scoped_lock l(impl_->mu); return impl_->segs.size(); }
std::vector<std::string> Wal::corrupt_segments() const { std::scoped_lock l(impl_->mu); return impl_->corrupt; }

std::uint64_t Wal::append(const DirtyBatch& batch) {
	const std::string payload = encode_batch(batch);
	const std::uint64_t total = kHeaderBytes + payload.size();
	std::scoped_lock l(impl_->mu);
	const auto used = impl_->bytes.load();
	if (used + total > impl_->cfg.max_bytes)
		throw WalFullError("WAL is full: " + std::to_string(used) + " of " + std::to_string(impl_->cfg.max_bytes) +
		                   " bytes used, batch needs " + std::to_string(total) + "; writes are refused");
	const std::uint64_t seq = impl_->next_seq;
	ByteWriter h;
	for (char ch : kMagic) h.u8(static_cast<std::uint8_t>(ch));
	h.u32(kFormat);
	h.u64(seq);
	h.u64(payload.size());
	h.u32(crc32(payload));
	const auto& cfg = impl_->cfg;
	const fs::path final_path = cfg.dir / seg_name(seq);
	const fs::path tmp = cfg.dir / (seg_name(seq) + ".tmp");
	const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0) io_fail("create segment", errno);
	try {
		write_all(fd, h.bytes().data(), h.bytes().size());
		write_all(fd, payload.data(), payload.size());
		if (cfg.fsync != WalFsync::Never && ::fsync(fd) != 0) io_fail("fsync", errno);
	} catch (...) {
		::close(fd);
		std::error_code ec;
		fs::remove(tmp, ec);  // a refused/failed append leaves no partial bytes behind
		throw;
	}
	::close(fd);
	if (::rename(tmp.c_str(), final_path.c_str()) != 0) {
		const int e = errno;
		std::error_code ec;
		fs::remove(tmp, ec);
		io_fail("rename", e);
	}
	if (cfg.fsync == WalFsync::FileAndDir) fsync_dir(cfg.dir);
	impl_->segs[seq] = total;
	impl_->next_seq = seq + 1;
	impl_->bytes.fetch_add(total);
	return seq;
}

std::size_t Wal::replay(const std::function<void(const DirtyBatch&)>& sink) {
	return replay_seq([&](std::uint64_t, const DirtyBatch& b) { sink(b); });
}

std::size_t Wal::replay_seq(const std::function<void(std::uint64_t, const DirtyBatch&)>& sink) {
	std::size_t delivered = 0;
	for (;;) {
		std::uint64_t seq = 0;
		std::uint64_t size = 0;
		{
			std::scoped_lock l(impl_->mu);
			if (impl_->segs.empty()) return delivered;
			seq = impl_->segs.begin()->first;
			size = impl_->segs.begin()->second;
		}
		const fs::path path = impl_->cfg.dir / seg_name(seq);
		std::optional<DirtyBatch> batch;
		{
			std::ifstream in(path, std::ios::binary);
			std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			try {
				if (!in && !in.eof()) throw DecodeError("unreadable");
				if (data.size() < kHeaderBytes || std::memcmp(data.data(), kMagic, 4) != 0) throw DecodeError("bad header");
				ByteReader r(std::string_view(data).substr(4));
				if (r.u32() != kFormat) throw DecodeError("unknown format");
				if (r.u64() != seq) throw DecodeError("sequence mismatch");
				const auto len = r.u64();
				const auto crc = r.u32();
				if (len != data.size() - kHeaderBytes) throw DecodeError("length mismatch");
				const std::string_view payload = std::string_view(data).substr(kHeaderBytes);
				if (crc32(payload) != crc) throw DecodeError("crc mismatch");
				batch = decode_batch(payload);
			} catch (const DecodeError&) {
				batch.reset();
			}
		}
		if (!batch) {
			// Corrupt segment: set it aside (never silently deleted), report it, keep going.
			std::error_code ec;
			fs::rename(path, path.string() + ".corrupt", ec);
			std::scoped_lock l(impl_->mu);
			impl_->corrupt.push_back(seg_name(seq));
			impl_->segs.erase(seq);
			impl_->bytes.fetch_sub(size);
			continue;
		}
		sink(seq, *batch);  // may throw: the segment stays and is re-delivered next time
		{
			std::error_code ec;
			fs::remove(path, ec);
			if (ec) throw WalIoError("cannot remove consumed WAL segment: " + ec.message());
			if (impl_->cfg.fsync == WalFsync::FileAndDir) fsync_dir(impl_->cfg.dir);
			std::scoped_lock l(impl_->mu);
			impl_->segs.erase(seq);
			impl_->bytes.fetch_sub(size);
		}
		++delivered;
	}
}

} // namespace rathena::persist
