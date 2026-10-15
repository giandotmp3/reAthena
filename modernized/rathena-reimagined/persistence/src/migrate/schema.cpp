#include "rathena/persistence/migrate/schema.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace rathena::persistence::migrate {
namespace {

std::string lower(std::string_view s) {
	std::string out(s);
	for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

/// MariaDB and MySQL 5.7 report integer display widths (`int(11) unsigned`); MySQL 8.0.19+ drops
/// them (`int unsigned`). The width has no effect on storage, so it is not part of the shape.
std::string normalize_type(std::string_view type) {
	std::string t = lower(type);
	for (const std::string_view base : {"tinyint", "smallint", "mediumint", "bigint", "int"}) {
		if (!t.starts_with(base)) continue;
		if (t.size() > base.size() && t[base.size()] == '(') {
			const auto close = t.find(')', base.size());
			if (close != std::string::npos) t.erase(base.size(), close - base.size() + 1);
		}
		break;
	}
	return t;
}

} // namespace

const TableSchema* SchemaSnapshot::find(std::string_view table) const noexcept {
	const auto it = std::find_if(tables.begin(), tables.end(), [&](const TableSchema& t) {
		return t.name.size() == table.size() &&
		       std::equal(t.name.begin(), t.name.end(), table.begin(), [](char a, char b) {
			       return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		       });
	});
	return it == tables.end() ? nullptr : &*it;
}

std::string schema_fingerprint(const SchemaSnapshot& s) {
	std::vector<std::string> tables;
	for (const auto& t : s.tables) {
		std::vector<std::string> cols;
		for (const auto& c : t.columns)
			cols.push_back(lower(c.name) + " " + normalize_type(c.type) + (c.nullable ? " null" : " not null"));
		std::sort(cols.begin(), cols.end());
		std::string line = lower(t.name) + "(";
		for (const auto& c : cols) line += c + ",";
		line += ")";
		tables.push_back(std::move(line));
	}
	std::sort(tables.begin(), tables.end());

	std::uint64_t h = 0xcbf29ce484222325ULL; // FNV-1a 64
	for (const auto& line : tables) {
		for (const char c : line) {
			h ^= static_cast<unsigned char>(c);
			h *= 0x100000001b3ULL;
		}
		h ^= '\n';
		h *= 0x100000001b3ULL;
	}
	char buf[32];
	std::snprintf(buf, sizeof buf, "fnv1a64:%016llx", static_cast<unsigned long long>(h));
	return buf;
}

} // namespace rathena::persistence::migrate
