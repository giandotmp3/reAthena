// M0(c): is one lua_State per mod affordable? Measures create time and resident memory per sandboxed state,
// and verifies a per-state allocator cap isolates a memory-hungry mod from the others.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "lua.h"
#include "lualib.h"
#include "luacode.h"
struct Mem { size_t used = 0, cap = SIZE_MAX; };
static void* alloc(void* ud, void* p, size_t o, size_t n) {
    auto* m = static_cast<Mem*>(ud);
    if (!n) { if (p) { m->used -= o; free(p); } return nullptr; }
    size_t old = p ? o : 0; if (m->used - old + n > m->cap) return nullptr;
    void* q = realloc(p, n); if (!q) return nullptr; m->used += n - old; return q;
}
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int main() {
    const int N = 200;
    std::vector<Mem*> mems; std::vector<lua_State*> st;
    double t0 = now();
    for (int i = 0; i < N; ++i) { auto* m = new Mem; mems.push_back(m); lua_State* L = lua_newstate(alloc, m); luaL_openlibs(L); luaL_sandbox(L); st.push_back(L); }
    double dt = now() - t0; size_t total = 0; for (auto* m : mems) total += m->used;
    printf("%d sandboxed states: %.2f ms total (%.3f ms each), %.1f KB each (%.2f MB total)\n", N, dt * 1e3, dt * 1e3 / N, total / 1024.0 / N, total / 1048576.0);
    // isolation: cap state 0 at 4 MB, make it blow up; the others must be unaffected
    mems[0]->cap = mems[0]->used + 4 * 1024 * 1024;
    const char* bomb = "local t = {} for i = 1, 1e8 do t[i] = {i} end";
    size_t bs; char* bc = luau_compile(bomb, strlen(bomb), nullptr, &bs);
    luau_load(st[0], "bomb", bc, bs, 0); free(bc);
    int rc = lua_pcall(st[0], 0, 0, 0);
    printf("memory-bomb in state 0: rc=%d (%s), used=%.1f MB (cap 4 MB over baseline)\n", rc, rc ? lua_tostring(st[0], -1) : "ran", mems[0]->used / 1048576.0);
    const char* ok = "local s = 0 for i = 1, 1000 do s = s + i end return s";
    bc = luau_compile(ok, strlen(ok), nullptr, &bs); luau_load(st[1], "ok", bc, bs, 0); free(bc);
    rc = lua_pcall(st[1], 0, 1, 0);
    printf("neighbour state 1 after the bomb: rc=%d result=%g\n", rc, lua_tonumber(st[1], -1));
    for (auto* L : st) lua_close(L);
}
