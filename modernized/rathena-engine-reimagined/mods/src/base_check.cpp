#include "mods/base_check.hpp"

#include <algorithm>
#include <fstream>

namespace mods {
namespace {
bool is_word(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
}  // namespace

std::vector<BaseViolation> check_base_purity(const std::filesystem::path& base_dir,
                                             const std::set<std::string>& forbidden) {
  std::vector<BaseViolation> out;
  namespace fs = std::filesystem;
  std::vector<fs::path> files;
  for (const auto& e : fs::recursive_directory_iterator(base_dir))
    if (e.is_regular_file()) files.push_back(e.path());
  std::sort(files.begin(), files.end());
  for (const auto& f : files) {
    std::ifstream in(f);
    std::string line;
    for (unsigned n = 1; std::getline(in, line); ++n) {
      for (const auto& name : forbidden) {
        if (name.empty()) continue;
        for (auto pos = line.find(name); pos != std::string::npos;
             pos = line.find(name, pos + 1)) {
          bool left = pos == 0 || !is_word(line[pos - 1]);
          std::size_t end = pos + name.size();
          bool right = end >= line.size() || !is_word(line[end]);
          if (left && right) out.push_back({f, n, name});
        }
      }
    }
  }
  return out;
}

std::set<std::string> load_forbidden_names(const std::filesystem::path& file) {
  std::set<std::string> out;
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    auto h = line.find('#');
    if (h != std::string::npos) line.erase(h);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\r' || line.back() == '\t'))
      line.pop_back();
    std::size_t s = 0;
    while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) ++s;
    if (s < line.size()) out.insert(line.substr(s));
  }
  return out;
}

}  // namespace mods
