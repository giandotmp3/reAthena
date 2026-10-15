// Drift guard (spec §5.6): every typed knob's code default must equal the value shipped in
// the legacy conf/ tree. Reads the repository's conf/ files directly, so a change to either
// side without the other fails here instead of silently diverging again.
#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "rathena/core/typed_config.hpp"

using namespace rathena::core;
namespace fs = std::filesystem;

namespace {

const fs::path kLegacyConf{RATHENA_LEGACY_CONF_DIR};

std::string trim(std::string s) {
	auto not_space = [](unsigned char c) { return !std::isspace(c); };
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
	s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
	return s;
}

std::optional<std::int64_t> to_number(std::string v) {
	std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (v == "yes" || v == "on" || v == "true") return 1;
	if (v == "no" || v == "off" || v == "false") return 0;
	try {
		std::size_t used = 0;
		const auto n = std::stoll(v, &used, 0);
		if (used == v.size()) return n;
	} catch (...) {
	}
	return std::nullopt;
}

// Parses legacy "key: value" lines (// comments). The first occurrence of a key wins,
// matching the legacy loader's import order for the base files read here.
std::map<std::string, std::string> read_conf(const std::vector<fs::path>& files) {
	std::map<std::string, std::string> out;
	for (const auto& f : files) {
		std::ifstream in(f);
		std::string line;
		while (std::getline(in, line)) {
			if (auto c = line.find("//"); c != std::string::npos) line.erase(c);
			const auto colon = line.find(':');
			if (colon == std::string::npos) continue;
			auto key = trim(line.substr(0, colon));
			auto val = trim(line.substr(colon + 1));
			if (key.empty() || key == "import") continue;
			out.emplace(std::move(key), std::move(val));
		}
	}
	return out;
}

std::vector<fs::path> battle_files() {
	std::vector<fs::path> files;
	for (const auto& e : fs::directory_iterator(kLegacyConf / "battle"))
		if (e.path().extension() == ".conf") files.push_back(e.path());
	std::sort(files.begin(), files.end());
	return files;
}

template <class C>
void expect_schema_matches(const std::map<std::string, std::string>& shipped, bool every_knob_must_exist) {
	for (const KnobInfo& k : C::schema()) {
		const auto it = shipped.find(std::string(k.name));
		if (it == shipped.end()) {
			if (every_knob_must_exist) ADD_FAILURE() << k.name << ": not found in shipped conf";
			continue;
		}
		const auto value = to_number(it->second);
		ASSERT_TRUE(value.has_value()) << k.name << ": shipped value '" << it->second << "' is not numeric";
		EXPECT_EQ(k.shipped_default, *value) << k.name << ": code default differs from shipped conf";
	}
}

} // namespace

TEST(SPEC_5_6_ShippedConfParity, BattleKnobDefaultsEqualConfBattle) {
	if (!fs::is_directory(kLegacyConf / "battle")) GTEST_SKIP() << "legacy conf/ not present next to this tree";
	expect_schema_matches<BattleConfig>(read_conf(battle_files()), /*every_knob_must_exist=*/true);
}

TEST(SPEC_5_6_ShippedConfParity, InterCharLoginKnobDefaultsEqualShippedConf) {
	if (!fs::is_directory(kLegacyConf)) GTEST_SKIP() << "legacy conf/ not present next to this tree";
	expect_schema_matches<InterConfig>(read_conf({kLegacyConf / "inter_athena.conf"}), true);
	expect_schema_matches<CharConfig>(read_conf({kLegacyConf / "char_athena.conf"}), true);
	expect_schema_matches<LoginPolicyConfig>(read_conf({kLegacyConf / "login_athena.conf", kLegacyConf / "char_athena.conf"}), true);
}
