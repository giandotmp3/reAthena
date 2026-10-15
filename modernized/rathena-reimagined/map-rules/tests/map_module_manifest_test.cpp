// Checker for manifest/map_modules.tsv (architecture §3.1: every src/map/*.cpp has exactly one
// owner; CI-enforced, and any new upstream file fails until it is assigned).
//
// The legacy tree is only read, never written. If RATHENA_LEGACY_MAP_DIR does not exist (e.g. the
// unit was copied elsewhere) the filesystem-coverage test skips; the static checks still run.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Row {
	std::string path;
	std::string owner;
	std::string secondary;
};

const std::set<std::string> kModules = {"script", "proto",  "world",     "mobai",      "combat",
                                        "progression", "skills", "items", "trade",      "social",
                                        "companions",  "anticheat", "gm", "ops"};

// Every .cpp named in the architecture §3.1 table, with the module(s) it is listed under.
const std::map<std::string, std::set<std::string>> kArchitectureTable = {
    {"script.cpp", {"script"}},       {"npc.cpp", {"script"}},          {"npc_chat.cpp", {"script"}},
    {"mapreg.cpp", {"script"}},       {"clif.cpp", {"proto", "anticheat"}},
    {"map.cpp", {"world", "ops"}},    {"unit.cpp", {"world"}},          {"path.cpp", {"world"}},
    {"navi.cpp", {"world"}},          {"instance.cpp", {"world"}},      {"battleground.cpp", {"world"}},
    {"duel.cpp", {"world"}},          {"mob.cpp", {"mobai", "items"}},  {"battle.cpp", {"combat"}},
    {"status.cpp", {"combat"}},       {"pc.cpp", {"progression", "items"}},
    {"achievement.cpp", {"progression"}}, {"quest.cpp", {"progression"}}, {"skill.cpp", {"skills"}},
    {"skills/**", {"skills"}},        {"itemdb.cpp", {"items"}},        {"storage.cpp", {"items"}},
    {"cashshop.cpp", {"items"}},      {"trade.cpp", {"trade"}},         {"vending.cpp", {"trade"}},
    {"buyingstore.cpp", {"trade"}},   {"searchstore.cpp", {"trade"}},   {"party.cpp", {"social"}},
    {"guild.cpp", {"social"}},        {"clan.cpp", {"social"}},         {"mail.cpp", {"social"}},
    {"chat.cpp", {"social"}},         {"channel.cpp", {"social"}},      {"pet.cpp", {"companions"}},
    {"homunculus.cpp", {"companions"}}, {"mercenary.cpp", {"companions"}}, {"elemental.cpp", {"companions"}},
    {"atcommand.cpp", {"gm"}},        {"pc_groups.cpp", {"gm"}},        {"log.cpp", {"ops"}},
    {"chrif.cpp", {"ops"}},           {"intif.cpp", {"ops"}},
};

std::vector<Row> load_manifest() {
	std::vector<Row> rows;
	std::ifstream in(std::string(MAP_RULES_MANIFEST_DIR) + "/map_modules.tsv");
	EXPECT_TRUE(in) << "manifest/map_modules.tsv missing";
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		std::istringstream ls(line);
		Row r;
		std::getline(ls, r.path, '\t');
		std::getline(ls, r.owner, '\t');
		std::getline(ls, r.secondary, '\t');
		rows.push_back(r);
	}
	return rows;
}

const Row* owner_of(const std::vector<Row>& rows, const std::string& rel) {
	for (const auto& r : rows) {
		if (r.path == rel) return &r;
		if (r.path.size() > 3 && r.path.ends_with("/**")) {
			const std::string prefix = r.path.substr(0, r.path.size() - 2); // keep trailing '/'
			if (rel.starts_with(prefix)) return &r;
		}
	}
	return nullptr;
}

} // namespace

TEST(MapModuleManifest, EveryRowHasExactlyOneValidOwnerAndNoDuplicates) {
	const auto rows = load_manifest();
	ASSERT_FALSE(rows.empty());
	std::set<std::string> seen;
	for (const auto& r : rows) {
		EXPECT_TRUE(kModules.contains(r.owner)) << r.path << ": unknown module '" << r.owner << "'";
		EXPECT_TRUE(seen.insert(r.path).second) << r.path << " assigned more than once";
		if (r.secondary != "-") {
			std::istringstream ss(r.secondary);
			std::string m;
			while (std::getline(ss, m, ',')) {
				EXPECT_TRUE(kModules.contains(m)) << r.path << ": unknown secondary module '" << m << "'";
				EXPECT_NE(m, r.owner) << r.path << ": secondary repeats the owner";
			}
		}
	}
}

TEST(MapModuleManifest, EveryArchitectureListedFileAssignedToAListedModule) {
	const auto rows = load_manifest();
	for (const auto& [path, modules] : kArchitectureTable) {
		const Row* r = nullptr;
		for (const auto& row : rows) {
			if (row.path == path) r = &row;
		}
		ASSERT_NE(r, nullptr) << path << " (architecture §3.1) has no owner in map_modules.tsv";
		EXPECT_TRUE(modules.contains(r->owner)) << path << " owned by '" << r->owner
		                                        << "', which §3.1 does not list for it";
	}
}

TEST(MapModuleManifest, EveryLegacyMapCppHasAnOwner) {
	const fs::path dir{RATHENA_LEGACY_MAP_DIR};
	std::error_code ec;
	if (!fs::is_directory(dir, ec)) {
		GTEST_SKIP() << "legacy src/map not found at " << dir.string() << " (set -DRATHENA_LEGACY_MAP_DIR)";
	}
	const auto rows = load_manifest();
	std::size_t checked = 0;
	for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); ++it) {
		if (!it->is_regular_file() || it->path().extension() != ".cpp") continue;
		const std::string rel = fs::relative(it->path(), dir).generic_string();
		EXPECT_NE(owner_of(rows, rel), nullptr) << "src/map/" << rel << " has no owner; assign it in map_modules.tsv";
		++checked;
	}
	EXPECT_GT(checked, 0u);
}
