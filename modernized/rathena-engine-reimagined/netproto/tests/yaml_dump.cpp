// netproto_yaml_dump FILE...: prints one line of canonical JSON per file using netproto's own YAML reader.
// Used by tests/test_yaml_differential.py to compare the reader with PyYAML on every proto file.
#include <fstream>
#include <iostream>
#include <sstream>

#include "netproto/yaml.hpp"

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    std::ifstream f(argv[i], std::ios::binary);
    if (!f) { std::cerr << "cannot open " << argv[i] << "\n"; return 2; }
    std::ostringstream o;
    o << f.rdbuf();
    try {
      std::cout << netproto::yaml::to_json(netproto::yaml::parse(o.str())) << "\n";
    } catch (const std::exception& e) {
      std::cerr << argv[i] << ": " << e.what() << "\n";
      return 1;
    }
  }
  return 0;
}
