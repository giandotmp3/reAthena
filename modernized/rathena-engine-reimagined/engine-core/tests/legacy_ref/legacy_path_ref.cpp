// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
//
// TEST-ONLY reference copy, see legacy_path_ref.hpp. Source: src/map/path.cpp
// (path_search, path_search_long, add_path, heap_*), src/common/db.hpp (BHEAP
// macros), src/map/map.cpp (map_getcellp, CHKWALL / CHKNOPASS branches only).
//
// Deviations from the legacy text, all mechanical:
//  * map_data / mapcell replaced by legacy_ref::Map; CELL_NOSTACK is off.
//  * VECTOR_* / aMalloc replaced by a std::vector-backed shim.
//  * flag&1 (easy search) and flag&2 are dropped; only the A* branch is kept.
//  * wpd output is a vector of directions; MAX_WALKPATH-sized arrays kept as
//    constants so sizeof(wpd->path) == MAX_WALKPATH (enum directions is int8).
//  * path_search_long keeps spd bookkeeping out (it never affects the result).
#include "legacy_path_ref.hpp"

#include <cstdlib>
#include <cstring>
#include <utility>

namespace legacy_ref {

#define MOVE_COST 10
#define MOVE_DIAGONAL_COST 14
#define MAX_WALKPATH 32

typedef short int16;
typedef int int32;

#define SET_OPEN 0
#define SET_CLOSED 1
#define PATH_DIR_NORTH 1
#define PATH_DIR_WEST 2
#define PATH_DIR_SOUTH 4
#define PATH_DIR_EAST 8

enum directions { DIR_CENTER = -1, DIR_NORTH = 0, DIR_NORTHWEST = 1, DIR_WEST = 2, DIR_SOUTHWEST = 3,
                  DIR_SOUTH = 4, DIR_SOUTHEAST = 5, DIR_EAST = 6, DIR_NORTHEAST = 7, DIR_MAX };

// ---- shim for the VECTOR/BHEAP macro layer --------------------------------
template <typename T> struct Vec { std::vector<T> v; };
#define VECTOR_INDEX(h, i) ((h).v[i])
#define VECTOR_LENGTH(h) ((h).v.size())
#define VECTOR_PUSH(h, val) ((h).v.push_back(val))
template <typename T> static T shim_pop(Vec<T>& h) { T t = h.v.back(); h.v.pop_back(); return t; }
#define VECTOR_POP(h) shim_pop(h)
#define BHEAP_LENGTH(h) VECTOR_LENGTH(h)
#define BHEAP_PEEK(h) VECTOR_INDEX(h, 0)
#define BHEAP_RESET(h) ((h).v.clear())
#define ARR_FIND(__start, __end, __var, __cmp) \
	do { for ((__var) = (__start); (__var) < (__end); ++(__var)) if (__cmp) break; } while (0)

// ---- BHEAP macros, verbatim from src/common/db.hpp -------------------------
#define BHEAP_PUSH2(__heap,__val,__topcmp) \
	do{ \
		size_t _i_ = VECTOR_LENGTH(__heap); \
		VECTOR_PUSH(__heap,__val); /* insert at end */ \
		BHEAP_SIFTDOWN(__heap,0,_i_,__topcmp); \
	}while(0)

#define BHEAP_POP2(__heap,__topcmp) \
	do{ \
		VECTOR_INDEX(__heap,0) = VECTOR_POP(__heap); /* put last at index */ \
		if( !VECTOR_LENGTH(__heap) ) /* removed last, nothing to do */ \
			break; \
		BHEAP_SIFTUP(__heap,0,__topcmp); \
	}while(0)

#define BHEAP_SIFTDOWN(__heap,__startidx,__idx,__topcmp) \
	do{ \
		size_t _i2_ = __idx; \
		while( _i2_ > __startidx ) \
		{ /* restore heap property in parents */ \
			size_t _parent_ = (_i2_-1)/2; \
			if( __topcmp(VECTOR_INDEX(__heap,_parent_),VECTOR_INDEX(__heap,_i2_)) <= 0 ) \
				break; /* done */ \
			std::swap(VECTOR_INDEX(__heap,_parent_),VECTOR_INDEX(__heap,_i2_)); \
			_i2_ = _parent_; \
		} \
	}while(0)

#define BHEAP_SIFTUP(__heap,__idx,__topcmp) \
	do{ \
		size_t _i_ = __idx; \
		size_t _lchild_ = _i_*2 + 1; \
		while( _lchild_ < VECTOR_LENGTH(__heap) ) \
		{ /* restore heap property in childs */ \
			size_t _rchild_ = _i_*2 + 2; \
			if( _rchild_ >= VECTOR_LENGTH(__heap) || __topcmp(VECTOR_INDEX(__heap,_lchild_),VECTOR_INDEX(__heap,_rchild_)) < 0 ) \
			{ /* left child */ \
				std::swap(VECTOR_INDEX(__heap,_i_),VECTOR_INDEX(__heap,_lchild_)); \
				_i_ = _lchild_; \
			} \
			else \
			{ /* right child */ \
				std::swap(VECTOR_INDEX(__heap,_i_),VECTOR_INDEX(__heap,_rchild_)); \
				_i_ = _rchild_; \
			} \
			_lchild_ = _i_*2 + 1; \
		} \
		BHEAP_SIFTDOWN(__heap,__idx,_i_,__topcmp); \
	}while(0)

#define BHEAP_UPDATE(__heap,__idx,__topcmp) \
	do{ \
		BHEAP_SIFTDOWN(__heap,0,__idx,__topcmp); \
		BHEAP_SIFTUP(__heap,__idx,__topcmp); \
	}while(0)

// ---- path.cpp ----------------------------------------------------------------
struct path_node {
	struct path_node *parent;
	int16 x;
	int16 y;
	int16 g_cost;
	int16 f_cost;
	int16 flag;
};
typedef Vec<path_node*> node_heap;
static node_heap g_open_set;

#define NODE_MINTOPCMP(i,j) ((i)->f_cost - (j)->f_cost)
#define calc_index(x,y) (((x)+(y)*MAX_WALKPATH) & (MAX_WALKPATH*MAX_WALKPATH-1))
#define heuristic(x0, y0, x1, y1)	(MOVE_COST * (abs((x1) - (x0)) + abs((y1) - (y0))))

static directions walk_choices[3][3] =
{
	{DIR_NORTHWEST,DIR_NORTH,DIR_NORTHEAST},
	{DIR_WEST,DIR_CENTER,DIR_EAST},
	{DIR_SOUTHWEST,DIR_SOUTH,DIR_SOUTHEAST},
};

// map_getcellp(), CHKWALL / CHKNOPASS branches (CELL_NOSTACK off).
static int32 map_getcellp(const Map* m, int16 x, int16 y, cell_chk cellchk)
{
	//NOTE: this intentionally overrides the last row and column
	if (x < 0 || x >= m->xs - 1 || y < 0 || y >= m->ys - 1)
		return (cellchk == CELL_CHKNOPASS);
	Map::Cell cell = m->cell[x + y * m->xs];
	switch (cellchk) {
		case CELL_CHKWALL: return (!cell.walkable && !cell.shootable);
		case CELL_CHKNOPASS: return (!cell.walkable);
	}
	return 0;
}

bool path_search_long(const Map& map, int x0_, int y0_, int x1_, int y1_, cell_chk cell)
{
	int32 dx, dy;
	int32 wx = 0, wy = 0;
	int32 weight;
	const Map* mapdata = &map;
	int16 x0 = x0_, y0 = y0_, x1 = x1_, y1 = y1_;

	dx = (x1 - x0);
	if (dx < 0) {
		std::swap(x0, x1);
		std::swap(y0, y1);
		dx = -dx;
	}
	dy = (y1 - y0);

	if (dx > abs(dy)) {
		weight = dx;
	} else {
		weight = abs(y1 - y0);
	}

	while (x0 != x1 || y0 != y1)
	{
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
		if ((x0 != x1 || y0 != y1) && map_getcellp(mapdata,x0,y0,cell))
			return false;
	}

	return true;
}

static void heap_push_node(node_heap *heap, path_node *node)
{
	BHEAP_PUSH2(*heap, node, NODE_MINTOPCMP);
}

static int32 heap_update_node(node_heap *heap, path_node *node)
{
	size_t i;
	ARR_FIND(0, BHEAP_LENGTH(*heap), i, VECTOR_INDEX(*heap, i) == node);
	if (i == BHEAP_LENGTH(*heap)) {
		return 1;
	}
	BHEAP_UPDATE(*heap, i, NODE_MINTOPCMP);
	return 0;
}

static int32 add_path(node_heap *heap, path_node *tp, int16 x, int16 y, int32 g_cost, path_node *parent, int32 h_cost)
{
	int32 i = calc_index(x, y);

	if (tp[i].x == x && tp[i].y == y) { // We processed this node before
		if (g_cost < tp[i].g_cost) { // New path to this node is better than old one
			// Update costs and parent
			tp[i].g_cost = g_cost;
			tp[i].parent = parent;
			tp[i].f_cost = g_cost + h_cost;
			if (tp[i].flag == SET_CLOSED) {
				heap_push_node(heap, &tp[i]); // Put it in open set again
			}
			else if (heap_update_node(heap, &tp[i])) {
				return 1;
			}
			tp[i].flag = SET_OPEN;
		}
		return 0;
	}

	if (tp[i].x || tp[i].y) // Index is already taken; see `tp` array FIXME for details
		return 1;

	// New node
	tp[i].x = x;
	tp[i].y = y;
	tp[i].g_cost = g_cost;
	tp[i].parent = parent;
	tp[i].f_cost = g_cost + h_cost;
	tp[i].flag = SET_OPEN;
	heap_push_node(heap, &tp[i]);
	return 0;
}

bool path_search(const Map& map, int x0_, int y0_, int x1_, int y1_, std::vector<int>* dirs_out)
{
	int32 i, x, y, dx = 0, dy = 0;
	const Map* mapdata = &map;
	int16 x0 = x0_, y0 = y0_, x1 = x1_, y1 = y1_;
	const cell_chk cell = CELL_CHKNOPASS;
	directions path[MAX_WALKPATH];
	size_t path_len = 0;

	//Do not check starting cell as that would get you stuck.
	if (x0 < 0 || x0 >= mapdata->xs || y0 < 0 || y0 >= mapdata->ys /*|| map_getcellp(mapdata,x0,y0,cell)*/)
		return false;

	// Check destination cell
	if (x1 < 0 || x1 >= mapdata->xs || y1 < 0 || y1 >= mapdata->ys || map_getcellp(mapdata,x1,y1,cell))
		return false;

	{
		path_node tp[MAX_WALKPATH * MAX_WALKPATH];
		path_node *current, *it;
		int32 xs = mapdata->xs - 1;
		int32 ys = mapdata->ys - 1;
		int32 len = 0;
		int32 j;

		BHEAP_RESET(g_open_set);

		memset(tp, 0, sizeof(tp));

		// Start node
		i = calc_index(x0, y0);
		tp[i].parent = nullptr;
		tp[i].x      = x0;
		tp[i].y      = y0;
		tp[i].g_cost = 0;
		tp[i].f_cost = heuristic(x0, y0, x1, y1);
		tp[i].flag   = SET_OPEN;

		heap_push_node(&g_open_set, &tp[i]); // Put start node to 'open' set

		for(;;) {
			int32 e = 0; // error flag
			int32 allowed_dirs = 0;
			int32 g_cost;

			if (BHEAP_LENGTH(g_open_set) == 0) {
				return false;
			}

			current = BHEAP_PEEK(g_open_set); // Look for the lowest f_cost node in the 'open' set
			BHEAP_POP2(g_open_set, NODE_MINTOPCMP); // Remove it from 'open' set

			x      = current->x;
			y      = current->y;
			g_cost = current->g_cost;

			current->flag = SET_CLOSED; // Add current node to 'closed' set

			if (x == x1 && y == y1) {
				break;
			}

			if (y < ys && !map_getcellp(mapdata, x, y+1, cell)) allowed_dirs |= PATH_DIR_NORTH;
			if (y >  0 && !map_getcellp(mapdata, x, y-1, cell)) allowed_dirs |= PATH_DIR_SOUTH;
			if (x < xs && !map_getcellp(mapdata, x+1, y, cell)) allowed_dirs |= PATH_DIR_EAST;
			if (x >  0 && !map_getcellp(mapdata, x-1, y, cell)) allowed_dirs |= PATH_DIR_WEST;

#define chk_dir(d) ((allowed_dirs & (d)) == (d))
			// Process neighbors of current node
			if (chk_dir(PATH_DIR_SOUTH|PATH_DIR_EAST) && !map_getcellp(mapdata, x+1, y-1, cell))
				e += add_path(&g_open_set, tp, x+1, y-1, g_cost + MOVE_DIAGONAL_COST, current, heuristic(x+1, y-1, x1, y1)); // (x+1, y-1) 5
			if (chk_dir(PATH_DIR_EAST))
				e += add_path(&g_open_set, tp, x+1, y, g_cost + MOVE_COST, current, heuristic(x+1, y, x1, y1)); // (x+1, y) 6
			if (chk_dir(PATH_DIR_NORTH|PATH_DIR_EAST) && !map_getcellp(mapdata, x+1, y+1, cell))
				e += add_path(&g_open_set, tp, x+1, y+1, g_cost + MOVE_DIAGONAL_COST, current, heuristic(x+1, y+1, x1, y1)); // (x+1, y+1) 7
			if (chk_dir(PATH_DIR_NORTH))
				e += add_path(&g_open_set, tp, x, y+1, g_cost + MOVE_COST, current, heuristic(x, y+1, x1, y1)); // (x, y+1) 0
			if (chk_dir(PATH_DIR_NORTH|PATH_DIR_WEST) && !map_getcellp(mapdata, x-1, y+1, cell))
				e += add_path(&g_open_set, tp, x-1, y+1, g_cost + MOVE_DIAGONAL_COST, current, heuristic(x-1, y+1, x1, y1)); // (x-1, y+1) 1
			if (chk_dir(PATH_DIR_WEST))
				e += add_path(&g_open_set, tp, x-1, y, g_cost + MOVE_COST, current, heuristic(x-1, y, x1, y1)); // (x-1, y) 2
			if (chk_dir(PATH_DIR_SOUTH|PATH_DIR_WEST) && !map_getcellp(mapdata, x-1, y-1, cell))
				e += add_path(&g_open_set, tp, x-1, y-1, g_cost + MOVE_DIAGONAL_COST, current, heuristic(x-1, y-1, x1, y1)); // (x-1, y-1) 3
			if (chk_dir(PATH_DIR_SOUTH))
				e += add_path(&g_open_set, tp, x, y-1, g_cost + MOVE_COST, current, heuristic(x, y-1, x1, y1)); // (x, y-1) 4
#undef chk_dir
			if (e) {
				return false;
			}
		}

		for (it = current; it->parent != nullptr; it = it->parent, len++);
		if (len > MAX_WALKPATH) // legacy: len > sizeof(wpd->path); enum directions is int8 there, so sizeof == MAX_WALKPATH
			return false;

		// Recreate path
		path_len = len;

		for (it = current, j = len-1; j >= 0; it = it->parent, j--) {
			dx = it->x - it->parent->x;
			dy = it->y - it->parent->y;
			path[j] = walk_choices[-dy + 1][dx + 1];
		}
	}

	if (dirs_out) {
		dirs_out->clear();
		for (size_t k = 0; k < path_len; k++) dirs_out->push_back((int)path[k]);
	}
	return true;
}

}  // namespace legacy_ref
