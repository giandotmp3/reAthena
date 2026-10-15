// Pure helpers of the PostgreSQL backend; built with or without libpq so they are always tested.
#include "rathena/persistence/postgres.hpp"

namespace rathena::persistence {

std::string to_postgres_placeholders(std::string_view sql, std::size_t* count) {
	std::string out;
	out.reserve(sql.size() + 8);
	std::size_t n = 0;
	std::size_t i = 0;
	while (i < sql.size()) {
		const char c = sql[i];
		if (c == '\'' || c == '"') {
			// Quoted literal or identifier; a doubled quote is an escaped quote and stays inside.
			const auto end = sql.find(c, i + 1);
			const auto stop = end == std::string_view::npos ? sql.size() : end + 1;
			out.append(sql.substr(i, stop - i));
			i = stop;
		} else if (c == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
			const auto end = sql.find('\n', i);
			const auto stop = end == std::string_view::npos ? sql.size() : end;
			out.append(sql.substr(i, stop - i));
			i = stop;
		} else if (c == '/' && i + 1 < sql.size() && sql[i + 1] == '*') {
			const auto end = sql.find("*/", i + 2);
			const auto stop = end == std::string_view::npos ? sql.size() : end + 2;
			out.append(sql.substr(i, stop - i));
			i = stop;
		} else if (c == '$' && i + 1 < sql.size() && sql[i + 1] == '$') {
			const auto end = sql.find("$$", i + 2);
			const auto stop = end == std::string_view::npos ? sql.size() : end + 2;
			out.append(sql.substr(i, stop - i));
			i = stop;
		} else if (c == '?') {
			out += '$' + std::to_string(++n);
			++i;
		} else {
			out += c;
			++i;
		}
	}
	if (count != nullptr) *count = n;
	return out;
}

} // namespace rathena::persistence
