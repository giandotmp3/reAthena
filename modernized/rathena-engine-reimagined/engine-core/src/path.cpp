// A* and straight-line search, ported from legacy src/map/path.cpp.
//
// Decisions where the spec was silent (all read from the legacy code):
//  * Neighbour expansion order: (x+1,y-1) (x+1,y) (x+1,y+1) (x,y+1)
//    (x-1,y+1) (x-1,y) (x-1,y-1) (x,y-1).
//  * Open set is the legacy BHEAP variant ("BHEAP_PUSH2/POP2", matching the
//    client): ties on f_cost are resolved by the exact sift rules below, which
//    decide which of several equal-length paths is returned.
//  * Hash collision: node slot = (x + y*W) & (S-1) with W = max_walkpath and
//    S = W*W rounded up to a power of two. A slot holding another cell makes
//    add_path return an error and the whole search fails. Quirk kept: a zeroed
//    slot looks like cell (0,0), so (0,0) can never enter the open set (it is
//    on the map border and the last row/column are solid anyway, but (0,0) as
//    a start cell works since the start is seeded directly).
#include "engine/path/path.hpp"

#include <algorithm>
#include <cstdlib>

namespace engine {
namespace {

// Mirrors map_getcellp(): the last row/column and anything outside is
// "out of range": blocks NoPass, never blocks Wall.
bool blocked(const MapGrid& g, int x, int y, LineBlock mode) {
  if (x < 0 || x >= g.width() - 1 || y < 0 || y >= g.height() - 1)
    return mode == LineBlock::NoPass;
  CellFlags f = g.cell(x, y);
  return mode == LineBlock::NoPass ? !(f & cell::Walkable) : !(f & cell::Shootable);
}
bool no_pass(const MapGrid& g, int x, int y) { return blocked(g, x, y, LineBlock::NoPass); }

struct Node {
  Node* parent = nullptr;
  int16_t x = 0, y = 0;
  int16_t g_cost = 0, f_cost = 0;
  int16_t flag = 0;  // 0 open, 1 closed
};
constexpr int16_t kOpen = 0, kClosed = 1;

// Binary heap with the legacy sift rules (BHEAP_PUSH2 / POP2 / UPDATE).
class OpenSet {
public:
  size_t size() const { return v_.size(); }
  Node* top() const { return v_[0]; }
  void reset() { v_.clear(); }
  void push(Node* n) {
    v_.push_back(n);
    sift_down(0, v_.size() - 1);
  }
  void pop() {
    v_[0] = v_.back();
    v_.pop_back();
    if (v_.empty()) return;
    sift_up(0);
  }
  bool update(Node* n) {
    size_t i = 0;
    while (i < v_.size() && v_[i] != n) ++i;
    if (i == v_.size()) return false;
    sift_down(0, i);
    sift_up(i);
    return true;
  }

private:
  static int cmp(const Node* a, const Node* b) { return a->f_cost - b->f_cost; }
  void sift_down(size_t start, size_t i) {  // legacy BHEAP_SIFTDOWN
    while (i > start) {
      size_t parent = (i - 1) / 2;
      if (cmp(v_[parent], v_[i]) <= 0) break;
      std::swap(v_[parent], v_[i]);
      i = parent;
    }
  }
  void sift_up(size_t idx) {  // legacy BHEAP_SIFTUP
    size_t i = idx;
    size_t l = i * 2 + 1;
    while (l < v_.size()) {
      size_t r = i * 2 + 2;
      if (r >= v_.size() || cmp(v_[l], v_[r]) < 0) {
        std::swap(v_[i], v_[l]);
        i = l;
      } else {
        std::swap(v_[i], v_[r]);
        i = r;
      }
      l = i * 2 + 1;
    }
    sift_down(idx, i);
  }
  std::vector<Node*> v_;
};

int heuristic(int x0, int y0, int x1, int y1) {
  return kCostOrthogonal * (std::abs(x1 - x0) + std::abs(y1 - y0));
}

struct Search {
  std::vector<Node> tp;
  size_t mask;
  int width;
  OpenSet open;

  size_t index(int x, int y) const { return size_t(x + y * width) & mask; }

  // Returns 1 on error (slot collision / heap inconsistency), like legacy.
  int add_path(int x, int y, int g_cost, Node* parent, int h_cost) {
    Node& n = tp[index(x, y)];
    if (n.x == x && n.y == y) {  // processed before
      if (g_cost < n.g_cost) {
        n.g_cost = static_cast<int16_t>(g_cost);
        n.parent = parent;
        n.f_cost = static_cast<int16_t>(g_cost + h_cost);
        if (n.flag == kClosed) open.push(&n);
        else if (!open.update(&n)) return 1;
        n.flag = kOpen;
      }
      return 0;
    }
    if (n.x || n.y) return 1;  // slot taken by another cell
    n.x = static_cast<int16_t>(x);
    n.y = static_cast<int16_t>(y);
    n.g_cost = static_cast<int16_t>(g_cost);
    n.parent = parent;
    n.f_cost = static_cast<int16_t>(g_cost + h_cost);
    n.flag = kOpen;
    open.push(&n);
    return 0;
  }
};

// Naming follows the legacy source, where "NORTH" means y+1.
constexpr int PATH_DIR_NORTH = 1, PATH_DIR_WEST = 2, PATH_DIR_SOUTH = 4, PATH_DIR_EAST = 8;

}  // namespace

PathResult path_search(const MapGrid& grid, Point from, Point to, PathMode mode,
                       const PathConfig& cfg) {
  PathResult res;
  const int x0 = from.x, y0 = from.y, x1 = to.x, y1 = to.y;
  if (cfg.max_walkpath < 1) return res;
  if (x0 < 0 || x0 >= grid.width() || y0 < 0 || y0 >= grid.height()) return res;
  if (x1 < 0 || x1 >= grid.width() || y1 < 0 || y1 >= grid.height() || no_pass(grid, x1, y1))
    return res;
  // Node coordinates are stored as int16 like the legacy structure.
  if (grid.width() > 32767 || grid.height() > 32767) return res;

  size_t slots = 1;
  while (slots < size_t(cfg.max_walkpath) * size_t(cfg.max_walkpath)) slots <<= 1;
  Search s;
  s.tp.assign(slots, Node{});
  s.mask = slots - 1;
  s.width = cfg.max_walkpath;

  Node& start = s.tp[s.index(x0, y0)];
  start.parent = nullptr;
  start.x = static_cast<int16_t>(x0);
  start.y = static_cast<int16_t>(y0);
  start.g_cost = 0;
  start.f_cost = static_cast<int16_t>(heuristic(x0, y0, x1, y1));
  start.flag = kOpen;
  s.open.push(&start);

  Node* current = nullptr;
  const int xs = grid.width() - 1, ys = grid.height() - 1;
  for (;;) {
    if (s.open.size() == 0) return res;
    current = s.open.top();
    s.open.pop();
    const int x = current->x, y = current->y, g = current->g_cost;
    current->flag = kClosed;
    if (x == x1 && y == y1) break;

    // Diagonals need both adjoining orthogonals free (no corner cutting).
    int allowed = 0;
    if (y < ys && !no_pass(grid, x, y + 1)) allowed |= PATH_DIR_NORTH;
    if (y > 0 && !no_pass(grid, x, y - 1)) allowed |= PATH_DIR_SOUTH;
    if (x < xs && !no_pass(grid, x + 1, y)) allowed |= PATH_DIR_EAST;
    if (x > 0 && !no_pass(grid, x - 1, y)) allowed |= PATH_DIR_WEST;
    auto chk = [&](int d) { return (allowed & d) == d; };

    int e = 0;
    if (chk(PATH_DIR_SOUTH | PATH_DIR_EAST) && !no_pass(grid, x + 1, y - 1))
      e += s.add_path(x + 1, y - 1, g + kCostDiagonal, current, heuristic(x + 1, y - 1, x1, y1));
    if (chk(PATH_DIR_EAST))
      e += s.add_path(x + 1, y, g + kCostOrthogonal, current, heuristic(x + 1, y, x1, y1));
    if (chk(PATH_DIR_NORTH | PATH_DIR_EAST) && !no_pass(grid, x + 1, y + 1))
      e += s.add_path(x + 1, y + 1, g + kCostDiagonal, current, heuristic(x + 1, y + 1, x1, y1));
    if (chk(PATH_DIR_NORTH))
      e += s.add_path(x, y + 1, g + kCostOrthogonal, current, heuristic(x, y + 1, x1, y1));
    if (chk(PATH_DIR_NORTH | PATH_DIR_WEST) && !no_pass(grid, x - 1, y + 1))
      e += s.add_path(x - 1, y + 1, g + kCostDiagonal, current, heuristic(x - 1, y + 1, x1, y1));
    if (chk(PATH_DIR_WEST))
      e += s.add_path(x - 1, y, g + kCostOrthogonal, current, heuristic(x - 1, y, x1, y1));
    if (chk(PATH_DIR_SOUTH | PATH_DIR_WEST) && !no_pass(grid, x - 1, y - 1))
      e += s.add_path(x - 1, y - 1, g + kCostDiagonal, current, heuristic(x - 1, y - 1, x1, y1));
    if (chk(PATH_DIR_SOUTH))
      e += s.add_path(x, y - 1, g + kCostOrthogonal, current, heuristic(x, y - 1, x1, y1));
    if (e) return res;  // hash collision (or heap inconsistency): search fails
  }

  int len = 0;
  for (Node* it = current; it->parent; it = it->parent) ++len;
  if (len > cfg.max_walkpath) return res;

  res.steps.resize(size_t(len));
  int j = len - 1;
  for (Node* it = current; it->parent; it = it->parent, --j) {
    res.steps[size_t(j)] = {it->x, it->y};
    bool diag = it->x != it->parent->x && it->y != it->parent->y;
    res.cost += diag ? kCostDiagonal : kCostOrthogonal;
  }

  if (mode == PathMode::OfficialWalkpath && len > cfg.official_walkpath_len &&
      !path_search_long(grid, from, to, LineBlock::NoPass)) {
    res.steps.clear();
    res.cost = 0;
    return res;
  }
  res.found = true;
  return res;
}

bool path_search_long(const MapGrid& grid, Point from, Point to, LineBlock block) {
  int x0 = from.x, y0 = from.y, x1 = to.x, y1 = to.y;
  int dx = x1 - x0;
  if (dx < 0) {
    std::swap(x0, x1);
    std::swap(y0, y1);
    dx = -dx;
  }
  int dy = y1 - y0;
  int weight = (dx > std::abs(dy)) ? dx : std::abs(y1 - y0);
  int wx = 0, wy = 0;
  while (x0 != x1 || y0 != y1) {
    wx += dx;
    wy += dy;
    if (wx >= weight) {
      wx -= weight;
      x0++;
    }
    if (wy >= weight) {
      wy -= weight;
      y0++;
    } else if (wy < 0) {
      wy += weight;
      y0--;
    }
    if ((x0 != x1 || y0 != y1) && blocked(grid, x0, y0, block)) return false;
  }
  return true;
}

Point path_blownpos(const MapGrid& grid, Point from, int dx, int dy, int count,
                    bool halt_on_block, const std::function<bool()>& coin, int max_count) {
  auto pass = [&](int x, int y) { return !no_pass(grid, x, y); };
  int x0 = from.x, y0 = from.y;
  if (count > max_count) count = max_count;
  dx = (dx > 0) ? 1 : (dx < 0 ? -1 : 0);
  dy = (dy > 0) ? 1 : (dy < 0 ? -1 : 0);
  while (count > 0 && (dx != 0 || dy != 0)) {
    if (!pass(x0 + dx, y0 + dy)) {
      if (halt_on_block) break;
      bool fx = dx != 0 && pass(x0 + dx, y0);
      bool fy = dy != 0 && pass(x0, y0 + dy);
      if (fx && fy) {
        if (coin && coin()) dx = 0; else dy = 0;
      }
      if (!fx) dx = 0;
      if (!fy) dy = 0;
    }
    x0 += dx;
    y0 += dy;
    count--;
  }
  return {x0, y0};
}

}  // namespace engine
