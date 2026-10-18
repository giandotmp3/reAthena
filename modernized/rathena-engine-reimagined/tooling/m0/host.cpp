// M0 spike host: embeds the real Luau VM the way script-host will (sandbox, interrupt callback,
// limiting allocator, native ctx with int64 methods) so M0(a)(c)(d) are measured, not guessed.
//
//   m0host [--interrupt] [--memcap=BYTES] [--sandbox] script.luau [args...]
//
// Scripts see: ctx.new(dmg) -> native int64 context with methods, num.*, rng.*, clock(), native_hit(...)
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include "lua.h"
#include "lualib.h"
#include "luacode.h"

namespace {

// ---- limiting allocator (per state; per-mod accounting needs one state per mod) -------------
struct Mem { size_t used = 0, cap = SIZE_MAX, peak = 0; };
void* alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* m = static_cast<Mem*>(ud);
    if (nsize == 0) { if (ptr) { m->used -= osize; free(ptr); } return nullptr; }
    size_t old = ptr ? osize : 0;
    if (m->used - old + nsize > m->cap) return nullptr;  // OOM inside the VM, not a crash
    void* p = realloc(ptr, nsize);
    if (!p) return nullptr;
    m->used += nsize - old;
    if (m->used > m->peak) m->peak = m->used;
    return p;
}

// ---- interrupt: counts back-edge/call checks, aborts past a wall-clock deadline ----------------
uint64_t g_interrupts = 0;
double g_deadline = 0;
double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void interrupt_cb(lua_State* L, int) {
    ++g_interrupts;
    if (g_deadline > 0 && (g_interrupts & 1023) == 0 && now() > g_deadline) luaL_error(L, "script budget exceeded");
}

// ---- native damage context: int64 value + methods (the architecture's `ctx`) -------------------
struct Ctx { int64_t dmg; };
Ctx* check(lua_State* L, int i) { return static_cast<Ctx*>(luaL_checkudata(L, i, "Ctx")); }
int c_new(lua_State* L) { auto* c = static_cast<Ctx*>(lua_newuserdata(L, sizeof(Ctx))); c->dmg = (int64_t)luaL_checknumber(L, 1); luaL_getmetatable(L, "Ctx"); lua_setmetatable(L, -2); return 1; }
int c_get(lua_State* L) { lua_pushnumber(L, (double)check(L, 1)->dmg); return 1; }
int c_geti(lua_State* L) { lua_pushinteger(L, (int)check(L, 1)->dmg); return 1; }
int c_set(lua_State* L) { check(L, 1)->dmg = (int64_t)luaL_checknumber(L, 2); return 0; }
int c_add(lua_State* L) { check(L, 1)->dmg += (int64_t)luaL_checknumber(L, 2); return 0; }
int c_mulpct(lua_State* L) { auto* c = check(L, 1); c->dmg = c->dmg * (int64_t)luaL_checknumber(L, 2) / 100; return 0; }       // int64, truncating (legacy)
int c_mul(lua_State* L) { auto* c = check(L, 1); c->dmg *= (int64_t)luaL_checknumber(L, 2); return 0; }
int c_idiv(lua_State* L) { auto* c = check(L, 1); c->dmg /= (int64_t)luaL_checknumber(L, 2); return 0; }
int c_min1(lua_State* L) { auto* c = check(L, 1); if (c->dmg < 1) c->dmg = 1; return 0; }
int c_f32(lua_State* L) { auto* c = check(L, 1); c->dmg = (int64_t)((float)c->dmg * (float)luaL_checknumber(L, 2)); return 0; } // float32 step
int c_defpct(lua_State* L) { auto* c = check(L, 1); c->dmg = c->dmg * (100 - (int64_t)luaL_checknumber(L, 2)) / 100 - (int64_t)luaL_checknumber(L, 3); return 0; }

// ---- rng: deterministic xorshift, counts draws (legacy draw-count parity) ---------------------
uint64_t g_rng = 88172645463325252ull, g_draws = 0;
uint32_t rnd32() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; ++g_draws; return (uint32_t)(g_rng >> 11); }
int r_rnd(lua_State* L) { lua_pushnumber(L, rnd32() % (uint32_t)luaL_checknumber(L, 1)); return 1; }
int r_draws(lua_State* L) { lua_pushnumber(L, (double)g_draws); return 1; }
int r_seed(lua_State* L) { g_rng = (uint64_t)luaL_checknumber(L, 1); g_draws = 0; return 0; }

int n_clock(lua_State* L) { lua_pushnumber(L, now()); return 1; }
int n_budget(lua_State* L) { double b = luaL_checknumber(L, 1); g_deadline = b > 0 ? now() + b : 0; return 0; }
int n_interrupts(lua_State* L) { lua_pushnumber(L, (double)g_interrupts); return 1; }

// native baseline: the same PRE weapon pipeline written in C++ (what a native-defaults stage costs)
int64_t native_hit(int64_t atkmin, int64_t atkmax, int64_t batk, int64_t ratio, int64_t def, int64_t vit, int64_t ele, int64_t card1, int64_t card2, int64_t tmod) {
    int64_t d = atkmin + (atkmax > atkmin ? rnd32() % (atkmax - atkmin) : 0);
    d += batk; d = d * ratio / 100; d = d * (100 - def) / 100 - vit; if (d < 1) d = 1;
    d = d * ele / 100; d = d * card1 / 100; d = d * card2 / 100; d = d * tmod / 100; if (d < 1) d = 1; return d;
}
int n_native_hit(lua_State* L) {
    lua_pushnumber(L, (double)native_hit((int64_t)lua_tonumber(L,1),(int64_t)lua_tonumber(L,2),(int64_t)lua_tonumber(L,3),(int64_t)lua_tonumber(L,4),(int64_t)lua_tonumber(L,5),(int64_t)lua_tonumber(L,6),(int64_t)lua_tonumber(L,7),(int64_t)lua_tonumber(L,8),(int64_t)lua_tonumber(L,9),(int64_t)lua_tonumber(L,10)));
    return 1;
}

void reg(lua_State* L, const char* tbl, const luaL_Reg* fns) { luaL_register(L, tbl, fns); lua_pop(L, 1); }

}  // namespace

int main(int argc, char** argv) {
    bool interrupt = false, sandbox = false; size_t cap = SIZE_MAX; const char* script = nullptr; double budget_s = 0;
    int firstarg = argc;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--interrupt")) interrupt = true;
        else if (!strcmp(argv[i], "--sandbox")) sandbox = true;
        else if (!strncmp(argv[i], "--memcap=", 9)) cap = strtoull(argv[i] + 9, nullptr, 10);
        else if (!strncmp(argv[i], "--budget=", 9)) budget_s = atof(argv[i] + 9);
        else if (!script) script = argv[i]; else { firstarg = i; break; }
    }
    if (!script) { fprintf(stderr, "usage: m0host [--interrupt] [--sandbox] [--memcap=N] [--budget=SEC] script.luau\n"); return 2; }

    Mem mem; mem.cap = cap;
    lua_State* L = lua_newstate(alloc, &mem);
    luaL_openlibs(L);

    luaL_newmetatable(L, "Ctx");
    lua_newtable(L);
    const luaL_Reg methods[] = {{"get", c_get}, {"geti", c_geti}, {"set", c_set}, {"add", c_add}, {"mul_pct", c_mulpct}, {"mul", c_mul}, {"idiv", c_idiv}, {"min1", c_min1}, {"f32_step", c_f32}, {"def_pct", c_defpct}, {nullptr, nullptr}};
    for (auto* m = methods; m->name; ++m) { lua_pushcfunction(L, m->func, m->name); lua_setfield(L, -2, m->name); }
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    const luaL_Reg ctxlib[] = {{"new", c_new}, {nullptr, nullptr}}; reg(L, "ctx", ctxlib);
    const luaL_Reg rnglib[] = {{"rnd", r_rnd}, {"draws", r_draws}, {"seed", r_seed}, {nullptr, nullptr}}; reg(L, "rng", rnglib);
    lua_pushcfunction(L, n_clock, "clock"); lua_setglobal(L, "clock");
    lua_pushcfunction(L, n_budget, "set_budget"); lua_setglobal(L, "set_budget");
    lua_pushcfunction(L, n_interrupts, "interrupts"); lua_setglobal(L, "interrupts");
    lua_pushcfunction(L, n_native_hit, "native_hit"); lua_setglobal(L, "native_hit");
    lua_pushnumber(L, interrupt ? 1 : 0); lua_setglobal(L, "INTERRUPT_ON");

    lua_newtable(L);  // argv
    for (int i = firstarg; i < argc; ++i) { lua_pushstring(L, argv[i]); lua_rawseti(L, -2, i - firstarg + 1); }
    lua_setglobal(L, "ARGS");

    if (interrupt) lua_callbacks(L)->interrupt = interrupt_cb;
    if (sandbox) luaL_sandbox(L);
    lua_State* T = sandbox ? lua_newthread(L) : L;
    if (sandbox) luaL_sandboxthread(T);
    if (budget_s > 0) g_deadline = now() + budget_s;

    std::ifstream f(script); std::stringstream ss; ss << f.rdbuf(); std::string src = ss.str();
    size_t bc_size = 0; char* bc = luau_compile(src.data(), src.size(), nullptr, &bc_size);
    int rc = luau_load(T, script, bc, bc_size, 0); free(bc);
    if (rc != 0) { fprintf(stderr, "load error: %s\n", lua_tostring(T, -1)); return 1; }
    int st = lua_pcall(T, 0, 0, 0);
    if (st != 0) { fprintf(stderr, "runtime error: %s\n", lua_tostring(T, -1)); }
    printf("[host] mem used=%zu peak=%zu interrupts=%llu\n", mem.used, mem.peak, (unsigned long long)g_interrupts);
    return st != 0;
}
