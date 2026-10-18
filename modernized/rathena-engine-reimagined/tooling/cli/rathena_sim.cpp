// rathena-sim: the headless harness CLI. Subcommands are implemented in src/sim_*.cpp.
#include <cstdio>
#include <iostream>

#include "tooling/sim_cli.hpp"

int main(int argc, char** argv) { return tooling::sim::cli_main(argc, argv, std::cout, std::cerr); }
