#include "rathena/map_rules/attr_fix_loader.hpp"

#include <array>
#include <charconv>
#include <optional>
#include <string_view>

namespace rathena::map_rules::combat {
namespace {

std::optional<Element> element_from_name(std::string_view name) {
	static constexpr std::array<std::pair<std::string_view, Element>, kElementCount> kNames{{
		{"Neutral", Element::Neutral}, {"Water", Element::Water}, {"Earth", Element::Earth},
		{"Fire", Element::Fire},       {"Wind", Element::Wind},   {"Poison", Element::Poison},
		{"Holy", Element::Holy},       {"Dark", Element::Shadow}, {"Ghost", Element::Ghost},
		{"Undead", Element::Undead},
	}};
	for (const auto& [n, e] : kNames) {
		if (n == name) return e;
	}
	return std::nullopt;
}

std::string_view trim(std::string_view s) {
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
	return s;
}

std::optional<std::int32_t> to_int(std::string_view s) {
	std::int32_t v = 0;
	const auto* end = s.data() + s.size();
	const auto [ptr, ec] = std::from_chars(s.data(), end, v);
	if (ec != std::errc{} || ptr != end) return std::nullopt;
	return v;
}

std::size_t indent_of(std::string_view line) {
	std::size_t n = 0;
	while (n < line.size() && line[n] == ' ') ++n;
	return n;
}

} // namespace

AttrFixLoadResult load_attr_fix_yaml(std::istream& in) {
	AttrFixLoadResult result;
	AttrFixTable table;
	std::array<std::array<std::array<bool, kElementCount>, kElementCount>, kElementLevels> seen{};
	std::size_t cells = 0;

	enum class Section { Preamble, Header, Body, Footer } section = Section::Preamble;
	std::optional<std::int32_t> level;
	std::optional<Element> attacker;

	std::string raw;
	for (std::size_t lineno = 1; std::getline(in, raw); ++lineno) {
		auto fail = [&](std::string why) {
			result.error = "line " + std::to_string(lineno) + ": " + why;
			return result;
		};
		std::string_view line = raw;
		if (const auto hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
		if (trim(line).empty()) continue;

		const std::size_t indent = indent_of(line);
		const std::string_view body = trim(line);

		if (indent == 0) {
			if (body == "Header:") section = Section::Header;
			else if (body == "Body:") section = Section::Body;
			else if (body == "Footer:") section = Section::Footer;
			else return fail("unexpected top-level key '" + std::string(body) + "'");
			continue;
		}
		if (section == Section::Header) {
			if (body == "Type: ATTRIBUTE_DB" || body.rfind("Version:", 0) == 0) continue;
			return fail("unexpected header entry '" + std::string(body) + "'");
		}
		if (section == Section::Footer) continue; // Imports are resolved by the caller
		if (section != Section::Body) return fail("content before Body:");

		const auto colon = body.find(':');
		if (colon == std::string_view::npos) return fail("expected 'key: value'");
		std::string_view key = trim(body.substr(0, colon));
		const std::string_view value = trim(body.substr(colon + 1));

		if (indent == 2 && key.rfind("- ", 0) == 0) {
			key = trim(key.substr(2));
			if (key != "Level") return fail("expected '- Level: N'");
			level = to_int(value);
			if (!level || *level < 1 || *level > static_cast<std::int32_t>(kElementLevels)) return fail("level must be 1..4");
			attacker.reset();
		} else if (indent == 4 && value.empty()) {
			if (!level) return fail("attacker element outside a Level entry");
			attacker = element_from_name(key);
			if (!attacker) return fail("unknown attacker element '" + std::string(key) + "'");
		} else if (indent == 6) {
			if (!attacker) return fail("defender entry outside an attacker element");
			const auto defender = element_from_name(key);
			if (!defender) return fail("unknown defender element '" + std::string(key) + "'");
			const auto pct = to_int(value);
			if (!pct) return fail("percentage must be an integer");
			auto& cell = seen[static_cast<std::size_t>(*level - 1)][static_cast<std::size_t>(*attacker)]
			                 [static_cast<std::size_t>(*defender)];
			if (cell) return fail("duplicate entry for '" + std::string(key) + "'");
			cell = true;
			++cells;
			table.set(*attacker, *defender, *level, *pct);
		} else {
			return fail("unexpected indentation");
		}
	}

	constexpr std::size_t kExpected = kElementLevels * kElementCount * kElementCount;
	if (cells != kExpected) {
		result.error = "incomplete table: " + std::to_string(cells) + " of " + std::to_string(kExpected) + " cells";
		return result;
	}
	result.table = table;
	return result;
}

} // namespace rathena::map_rules::combat
