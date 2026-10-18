// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
//
// TEST-ONLY reference copy of legacy rAthena path code (src/map/path.cpp,
// plus the BHEAP macros of src/common/db.hpp and map_getcellp of
// src/map/map.cpp), trimmed to the pieces needed for differential testing
// against engine::path_search / path_search_long. It is compiled into the
// test binary only and is never linked into the engine_core library. The
// algorithm bodies are verbatim apart from the shims described in the .cpp.
#pragma once
#include <vector>

namespace legacy_ref {

struct Map {
  int xs = 0, ys = 0;
  // Legacy cell bits: gat types floor (1,1), wall (0,0), cliff (0,1).
  struct Cell { bool walkable = false, shootable = false; };
  std::vector<Cell> cell;
};

enum cell_chk { CELL_CHKWALL, CELL_CHKNOPASS };

// walkpath_data.path as a list of directions (0=N(+y) 1=NW 2=W 3=SW 4=S 5=SE 6=E 7=NE)
bool path_search(const Map& m, int x0, int y0, int x1, int y1, std::vector<int>* dirs_out);
bool path_search_long(const Map& m, int x0, int y0, int x1, int y1, cell_chk cell);

}  // namespace legacy_ref
