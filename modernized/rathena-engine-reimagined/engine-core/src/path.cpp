#include "engine/path/path.hpp"

namespace engine {

PathResult path_search(const MapGrid&, Point, Point, PathMode, const PathConfig&) {
  throw NotImplemented("path_search");
}
bool path_search_long(const MapGrid&, Point, Point) {
  throw NotImplemented("path_search_long");
}

}  // namespace engine
