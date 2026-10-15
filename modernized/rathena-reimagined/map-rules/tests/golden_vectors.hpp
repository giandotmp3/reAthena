// Golden vectors captured from the legacy servers by tools/golden/capture.sh: one flat JSON object
// of integer fields per line, e.g. {"agi": 1, "dex": 45, "result": 141}.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace golden {

using Vector = std::map<std::string, std::int64_t>;

/// Reads `<MAP_RULES_GOLDEN_DIR>/<name>`; an empty result means the file is missing.
inline std::vector<Vector> load(const std::string& name) {
	std::vector<Vector> out;
	std::ifstream in(std::filesystem::path(MAP_RULES_GOLDEN_DIR) / name);
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty()) continue;
		Vector v;
		std::size_t i = 0;
		while ((i = line.find('"', i)) != std::string::npos) {
			const std::size_t key_end = line.find('"', i + 1);
			const std::size_t colon = line.find(':', key_end);
			if (key_end == std::string::npos || colon == std::string::npos)
				throw std::runtime_error("malformed golden vector: " + line);
			std::size_t used = 0;
			v[line.substr(i + 1, key_end - i - 1)] = std::stoll(line.substr(colon + 1), &used);
			i = colon + 1 + used;
		}
		out.push_back(std::move(v));
	}
	return out;
}

/// Field accessor that fails loudly on a missing key.
inline std::int64_t at(const Vector& v, const char* key) {
	const auto it = v.find(key);
	if (it == v.end()) throw std::runtime_error(std::string("golden vector has no field ") + key);
	return it->second;
}

/// The vector as text, for failure messages.
inline std::string str(const Vector& v) {
	std::string s;
	for (const auto& [k, val] : v) s += (s.empty() ? "" : ", ") + k + "=" + std::to_string(val);
	return "{" + s + "}";
}

} // namespace golden
