#include "script_host/sandbox.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "script_host/ids.hpp"
#include "script_host/numeric.hpp"

#ifdef SCRIPT_HOST_WITH_LUAU
#include "lua.h"
#include "luacode.h"
#include "lualib.h"
#endif

namespace script_host {

// ---- MemoryAccountant -------------------------------------------------------
void MemoryAccountant::set_cap(const std::string& mod, std::uint64_t bytes) { slots_[mod].cap = bytes; }
bool MemoryAccountant::charge(const std::string& mod, std::uint64_t bytes) {
  auto& s = slots_[mod];
  if (s.disabled || s.used + bytes > s.cap) {
    s.disabled = true;
    ++s.failed_allocs;
    return false;
  }
  s.used += bytes;
  s.peak = std::max(s.peak, s.used);
  return true;
}
void MemoryAccountant::release(const std::string& mod, std::uint64_t bytes) {
  auto& s = slots_[mod];
  s.used = bytes > s.used ? 0 : s.used - bytes;
}
std::uint64_t MemoryAccountant::used(const std::string& mod) const {
  auto it = slots_.find(mod);
  return it == slots_.end() ? 0 : it->second.used;
}
bool MemoryAccountant::disabled(const std::string& mod) const {
  auto it = slots_.find(mod);
  return it != slots_.end() && it->second.disabled;
}
void MemoryAccountant::reenable(const std::string& mod) { slots_[mod].disabled = false; }

#ifndef SCRIPT_HOST_WITH_LUAU
// ---- no-Luau build: the type exists, every operation reports why ------------
struct ModSandbox::Impl {};
static const char* kNoLuau = "script-host was built without Luau (set LUAU_ROOT / RATHENA_LUAU_ROOT)";
ModSandbox::ModSandbox(ModEnvironmentSpec spec, MemoryAccountant*) : spec_(std::move(spec)), impl_(new Impl) {}
ModSandbox::~ModSandbox() = default;
RunResult ModSandbox::run(const std::string&, const std::string&) { return {RunStatus::Error, kNoLuau}; }
std::vector<RegisteredHook> ModSandbox::take_hooks() { return {}; }
std::vector<RegisteredMigrate> ModSandbox::take_migrates() { return {}; }
std::vector<std::string> ModSandbox::take_shapes() { return {}; }
void ModSandbox::call_hook(int, HookContext&) { throw HookFailure(HookFailure::Kind::Error, kNoLuau); }
RunResult ModSandbox::call_ref(int, const std::string&) { return {RunStatus::Error, kNoLuau}; }
RunResult ModSandbox::call_migrate(int, std::uint32_t, const Row&, Row&) { return {RunStatus::Error, kNoLuau}; }
void ModSandbox::release_ref(int) {}
CoStep ModSandbox::co_start(const std::string&, const std::string&) { CoStep s; s.result = {RunStatus::Error, kNoLuau}; return s; }
CoStep ModSandbox::co_resume(std::uint64_t, const DialogueResponse&) { CoStep s; s.result = {RunStatus::Error, kNoLuau}; return s; }
void ModSandbox::co_cancel(std::uint64_t) {}
std::size_t ModSandbox::co_count() const { return 0; }
BudgetMeter& ModSandbox::meter() { static BudgetMeter m{BudgetLimits{}}; return m; }
std::uint64_t ModSandbox::memory_used() const { return 0; }
std::uint64_t ModSandbox::memory_peak() const { return 0; }
bool ModSandbox::disabled() const { return false; }
void ModSandbox::reenable() {}
void ModSandbox::begin_tick() {}
#else

namespace {
constexpr const char* kCtxMeta = "script_host.ctx";
constexpr const char* kScopeMeta = "script_host.scope";
constexpr std::size_t kMaxText = 4096;
constexpr int kMaxOptions = 64;
}  // namespace

struct ModSandbox::Impl {
  ModSandbox* self;
  std::unique_ptr<MemoryAccountant> own_acct;
  MemoryAccountant* acct;
  MemoryAccountant::Slot* slot;
  BudgetMeter meter;
  lua_State* L = nullptr;
  lua_State* env = nullptr;
  int env_ref = 0;

  // per-execution state
  std::string cur_hook = "<load>";
  std::string cur_hook_id;  // hook id for RNG streams
  bool budget_tripped = false, perm_denied = false, loading = false;
  std::string trip_msg;
  HookContext* ctx = nullptr;
  std::uint64_t call_id = 0;
  lua_State* dialogue_thread = nullptr;

  std::vector<RegisteredHook> hooks;
  std::vector<RegisteredMigrate> migrates;
  std::vector<std::string> shapes;

  struct Co { lua_State* th; int ref; };
  std::map<std::uint64_t, Co> cos;
  std::uint64_t next_co = 1;

  const ModEnvironmentSpec& spec() const { return self->spec_; }
  Host& host() { return self->host_; }
  StorageApi* storage() const { return self->storage_; }
  RngStreams* rng() const { return self->rng_; }

  Impl(ModSandbox* s, MemoryAccountant* shared)
      : self(s),
        own_acct(shared ? nullptr : new MemoryAccountant),
        acct(shared ? shared : own_acct.get()),
        slot(&acct->slot(s->spec_.mod_id)),
        meter([&] {
          BudgetLimits b = s->spec_.budget;
          if (s->spec_.granted.count(Permission::UnboundedLoop)) b.unbounded_loop_permission = true;
          return b;
        }()) {}
};

namespace {
using Impl = ModSandbox::Impl;

Impl* impl_of(lua_State* L) { return static_cast<Impl*>(lua_callbacks(L)->userdata); }

// ---- allocator: per-state cap, attributes to the mod's slot ------------------
void* limiting_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  auto* s = static_cast<MemoryAccountant::Slot*>(ud);
  if (nsize == 0) {
    if (ptr) { s->used -= osize; std::free(ptr); }
    return nullptr;
  }
  size_t old = ptr ? osize : 0;
  if (s->used - old + nsize > s->cap) { ++s->failed_allocs; return nullptr; }
  void* p = std::realloc(ptr, nsize);
  if (!p) { ++s->failed_allocs; return nullptr; }
  s->used += nsize - old;
  if (s->used > s->peak) s->peak = s->used;
  return p;
}

// ---- interrupt: step / wall-clock budget --------------------------------------
void interrupt_cb(lua_State* L, int gc) {
  if (gc >= 0) return;
  Impl* im = impl_of(L);
  if (!im) return;
  if (!im->budget_tripped) {
    try {
      im->meter.step(im->spec().mod_id, im->cur_hook);
    } catch (const BudgetExceeded& e) {
      im->budget_tripped = true;
      im->trip_msg = e.what();
    }
  }
  // Once tripped, every safepoint re-raises, so pcall inside the script cannot
  // swallow the abort: the next loop back-edge or call raises again.
  if (im->budget_tripped) luaL_error(L, "script budget exceeded");
}

int err_handler(lua_State* L) {
  Impl* im = impl_of(L);
  const char* m = lua_tostring(L, 1);
  if (!m || (im && im->budget_tripped)) return 1;
  std::string s = m;
  s += "\n";
  s += lua_debugtrace(L);
  lua_pushlstring(L, s.data(), s.size());
  return 1;
}

template <class F>
int guarded(lua_State* L, F&& f) {
  // Only catch host exception types: Luau's own lua_exception derives from
  // std::exception and must pass through to the VM.
  std::string err;
  bool perm = false;
  int n = 0;
  try {
    n = f();
  } catch (const StorageError& e) {
    err = e.what();
    perm = e.code == StorageError::Code::PermissionDenied;
  } catch (const std::runtime_error& e) {
    err = e.what();
  } catch (const std::logic_error& e) {
    err = e.what();
  }
  if (!err.empty()) {
    if (perm) impl_of(L)->perm_denied = true;
    luaL_error(L, "%s", err.c_str());
  }
  return n;
}

// ---- Value <-> Lua ------------------------------------------------------------
void push_value(lua_State* L, const Value& v, int depth) {
  if (depth > kMaxValueDepth) luaL_error(L, "stored value nested too deeply");
  switch (v.index()) {
    case 0: lua_pushnil(L); break;
    case 1: lua_pushnumber(L, static_cast<double>(std::get<std::int64_t>(v))); break;
    case 2: lua_pushnumber(L, std::get<double>(v)); break;
    case 3: lua_pushboolean(L, std::get<bool>(v)); break;
    case 4: { auto& s = std::get<std::string>(v); lua_pushlstring(L, s.data(), s.size()); break; }
    default: {
      auto& t = std::get<std::shared_ptr<ValueTable>>(v);
      lua_newtable(L);
      if (t) {
        for (auto& [k, e] : t->array) { push_value(L, e, depth + 1); lua_rawseti(L, -2, static_cast<int>(k)); }
        for (auto& [k, e] : t->entries) { push_value(L, e, depth + 1); lua_setfield(L, -2, k.c_str()); }
      }
    }
  }
}

// Converts a Lua value to a Value. Returns an error string (empty = ok); never
// raises, so the caller can release host resources before raising.
std::string to_value(lua_State* L, int idx, int depth, Value& out) {
  if (idx < 0) idx = lua_gettop(L) + idx + 1;
  if (depth > kMaxValueDepth) return "value nested too deeply (or cyclic); limit is 16 levels";
  switch (lua_type(L, idx)) {
    case LUA_TNIL: out = std::monostate{}; return {};
    case LUA_TBOOLEAN: out = lua_toboolean(L, idx) != 0; return {};
    case LUA_TNUMBER: {
      double d = lua_tonumber(L, idx);
      if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9007199254740992.0) out = static_cast<std::int64_t>(d);
      else out = d;
      return {};
    }
    case LUA_TSTRING: {
      size_t n;
      const char* s = lua_tolstring(L, idx, &n);
      out = std::string(s, n);
      return {};
    }
    case LUA_TTABLE: {
      auto t = std::make_shared<ValueTable>();
      lua_pushnil(L);
      while (lua_next(L, idx) != 0) {
        Value v;
        std::string e = to_value(L, -1, depth + 1, v);
        if (!e.empty()) { lua_pop(L, 2); return e; }
        if (lua_type(L, -2) == LUA_TSTRING) {
          size_t n;
          const char* k = lua_tolstring(L, -2, &n);
          t->entries[std::string(k, n)] = std::move(v);
        } else if (lua_type(L, -2) == LUA_TNUMBER) {
          double d = lua_tonumber(L, -2);
          if (d != std::floor(d) || std::fabs(d) >= 9007199254740992.0) { lua_pop(L, 2); return "table keys must be strings or integers"; }
          t->array[static_cast<std::int64_t>(d)] = std::move(v);
        } else {
          lua_pop(L, 2);
          return "table keys must be strings or integers";
        }
        lua_pop(L, 1);
      }
      out = Value{t};
      return {};
    }
    default:
      return std::string("cannot store a ") + lua_typename(L, lua_type(L, idx)) + " (allowed: nil, boolean, number, string, table)";
  }
}

// ---- ctx userdata ---------------------------------------------------------------
struct CtxBox { std::uint64_t call_id; };

HookContext& ctx_of(lua_State* L, bool mutate) {
  auto* b = static_cast<CtxBox*>(luaL_checkudata(L, 1, kCtxMeta));
  Impl* im = impl_of(L);
  if (!im->ctx || b->call_id != im->call_id) luaL_error(L, "stale ctx handle (used after its hook returned)");
  if (mutate && im->ctx->read_only) luaL_error(L, "ctx is read-only in observe hooks");
  return *im->ctx;
}

std::int64_t check_i64(lua_State* L, int i) {
  double d = luaL_checknumber(L, i);
  if (!std::isfinite(d) || d != std::floor(d) || d < -9223372036854775808.0 || d >= 9223372036854775808.0)
    luaL_argerror(L, i, "integer expected");
  return static_cast<std::int64_t>(d);
}

int c_get(lua_State* L) { lua_pushnumber(L, static_cast<double>(ctx_of(L, false).num.value())); return 1; }
int c_get_str(lua_State* L) { auto s = std::to_string(ctx_of(L, false).num.value()); lua_pushlstring(L, s.data(), s.size()); return 1; }
int c_set(lua_State* L) { auto& c = ctx_of(L, true); c.num.set(check_i64(L, 2)); return 0; }
#define CTX_I64_METHOD(NAME, EXPR)                                  \
  int NAME(lua_State* L) {                                          \
    auto& c = ctx_of(L, true);                                      \
    std::int64_t a = check_i64(L, 2);                               \
    return guarded(L, [&] { c.num.EXPR(a); return 0; });            \
  }
CTX_I64_METHOD(c_add, add)
CTX_I64_METHOD(c_sub, sub)
CTX_I64_METHOD(c_mul, mul)
CTX_I64_METHOD(c_mul_pct, mul_pct)
CTX_I64_METHOD(c_idiv, idiv)
#undef CTX_I64_METHOD
int c_f32(lua_State* L) {
  auto& c = ctx_of(L, true);
  double f = luaL_checknumber(L, 2);
  return guarded(L, [&] { c.num.f32_step(f); return 0; });
}
int c_min1(lua_State* L) { ctx_of(L, true).num.min1(); return 0; }
int c_cancel(lua_State* L) {
  auto& c = ctx_of(L, false);
  size_t n;
  const char* s = luaL_checklstring(L, 2, &n);
  c.cancel(std::string(s, std::min<size_t>(n, 256)));
  return 0;
}
int c_enqueue(lua_State* L) {
  auto& c = ctx_of(L, false);
  size_t a, b, d;
  const char* kind = luaL_checklstring(L, 2, &a);
  const char* target = luaL_checklstring(L, 3, &b);
  const char* payload = luaL_optlstring(L, 4, "{}", &d);
  if (a > 128 || b > 128 || d > 4096) luaL_error(L, "enqueue: argument too long");
  if (!c.effects) luaL_error(L, "enqueue: no effect queue");
  c.effects->push({std::string(kind, a), std::string(target, b), std::string(payload, d)});
  return 0;
}

// ---- events / timers / definitions / print / rng ----------------------------------
std::string opt_string_field(lua_State* L, int idx, const char* f) {
  lua_getfield(L, idx, f);
  std::string s;
  if (lua_type(L, -1) == LUA_TSTRING) { size_t n; const char* p = lua_tolstring(L, -1, &n); s.assign(p, n); }
  else if (!lua_isnil(L, -1)) { lua_pop(L, 1); luaL_error(L, "spec.%s must be a string", f); }
  lua_pop(L, 1);
  return s;
}
std::vector<std::string> string_list_field(lua_State* L, int idx, const char* f) {
  std::vector<std::string> out;
  lua_getfield(L, idx, f);
  if (lua_isnil(L, -1)) { lua_pop(L, 1); return out; }
  if (!lua_istable(L, -1)) { lua_pop(L, 1); luaL_error(L, "spec.%s must be an array of strings", f); }
  int n = lua_objlen(L, -1);
  if (n > 64) { lua_pop(L, 1); luaL_error(L, "spec.%s has too many entries", f); }
  for (int i = 1; i <= n; ++i) {
    lua_rawgeti(L, -1, i);
    if (lua_type(L, -1) != LUA_TSTRING) { lua_pop(L, 2); luaL_error(L, "spec.%s entries must be strings", f); }
    out.emplace_back(lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return out;
}
bool opt_bool_field(lua_State* L, int idx, const char* f) {
  lua_getfield(L, idx, f);
  bool b = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return b;
}
int opt_int_field(lua_State* L, int idx, const char* f) {
  lua_getfield(L, idx, f);
  int v = 0;
  if (lua_type(L, -1) == LUA_TNUMBER) {
    double d = lua_tonumber(L, -1);
    if (d != std::floor(d) || std::fabs(d) > 1e9) { lua_pop(L, 1); luaL_error(L, "spec.%s must be a small integer", f); }
    v = static_cast<int>(d);
  } else if (!lua_isnil(L, -1)) { lua_pop(L, 1); luaL_error(L, "spec.%s must be a number", f); }
  lua_pop(L, 1);
  return v;
}

int ev_on(lua_State* L) {
  Impl* im = impl_of(L);
  if (!im->loading) luaL_error(L, "events.on is only available while the mod chunk loads");
  const char* ev = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);
  luaL_checktype(L, 3, LUA_TFUNCTION);
  if (!EventName::parse(ev)) luaL_error(L, "bad event name '%s' (expected namespace:event)", ev);
  HookSpec hs;
  hs.mod = im->spec().mod_id;
  hs.event = ev;
  hs.id = opt_string_field(L, 2, "id");
  if (hs.id.empty() || hs.id.find('/') != std::string::npos) luaL_error(L, "spec.id is required and may not contain '/'");
  hs.priority = opt_int_field(L, 2, "priority");
  hs.after = string_list_field(L, 2, "after");
  hs.before = string_list_field(L, 2, "before");
  std::string ph = opt_string_field(L, 2, "phase");
  if (ph.empty() || ph == "on") hs.phase = Phase::On;
  else if (ph == "pre") hs.phase = Phase::Pre;
  else if (ph == "post") hs.phase = Phase::Post;
  else luaL_error(L, "spec.phase must be pre, on or post");
  std::string md = opt_string_field(L, 2, "mode");
  if (md.empty() || md == "observe") hs.mode = Mode::Observe;
  else if (md == "modify") hs.mode = Mode::Modify;
  else if (md == "replace") hs.mode = Mode::Replace;
  else luaL_error(L, "spec.mode must be observe, modify or replace");
  std::string cat = opt_string_field(L, 2, "category");
  if (cat.empty() || cat == "damage_stage") hs.category = HookCategory::DamageStage;
  else if (cat == "skill") hs.category = HookCategory::Skill;
  else if (cat == "status") hs.category = HookCategory::Status;
  else if (cat == "item") hs.category = HookCategory::Item;
  else if (cat == "formula") hs.category = HookCategory::Formula;
  else luaL_error(L, "unknown spec.category '%s'", cat.c_str());
  hs.precedence = opt_bool_field(L, 2, "precedence");
  hs.draws_main_rng = opt_bool_field(L, 2, "draws_main_rng");
  if (opt_bool_field(L, 2, "permit_protected")) {
    // Only an explicit grant lets a mod touch protected stages.
    luaL_error(L, "spec.permit_protected cannot be set by a script; it is granted by the loader");
  }
  int ref = lua_ref(L, 3);
  im->hooks.push_back({std::move(hs), ref});
  return 0;
}

int st_migrate(lua_State* L) {
  Impl* im = impl_of(L);
  if (!im->loading) luaL_error(L, "storage.migrate is only available while the mod chunk loads");
  double v = luaL_checknumber(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  if (v != std::floor(v) || v < 1 || v > 4e9) luaL_argerror(L, 1, "version must be a positive integer");
  int ref = lua_ref(L, 2);
  im->migrates.push_back({static_cast<std::uint32_t>(v), ref});
  return 0;
}

int def_declare(lua_State* L) {
  Impl* im = impl_of(L);
  if (!im->loading) luaL_error(L, "definitions.declare is only available while the mod chunk loads");
  const char* id = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);
  std::vector<std::string> fields;
  int n = lua_objlen(L, 2);
  for (int i = 1; i <= n && i <= 256; ++i) {
    lua_rawgeti(L, 2, i);
    if (lua_type(L, -1) != LUA_TSTRING) { lua_pop(L, 1); luaL_error(L, "definitions.declare: fields must be strings"); }
    fields.emplace_back(lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  std::sort(fields.begin(), fields.end());
  std::string line = id;
  line += ":";
  for (auto& f : fields) line += f + ",";
  im->shapes.push_back(line);
  return 0;
}

int tm_after(lua_State* L) {
  Impl* im = impl_of(L);
  double ms = luaL_checknumber(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  if (!(ms >= 0) || ms > 86400000.0 * 365) luaL_argerror(L, 1, "delay out of range");
  if (!im->host().add_timer) luaL_error(L, "timers are not available in this host");
  int ref = lua_ref(L, 2);
  im->host().add_timer(static_cast<std::uint64_t>(ms), ref);
  return 0;
}

int sb_print(lua_State* L) {
  Impl* im = impl_of(L);
  std::string line;
  int n = lua_gettop(L);
  for (int i = 1; i <= n && line.size() < 1024; ++i) {
    size_t l;
    const char* s = luaL_tolstring(L, i, &l);
    if (i > 1) line += '\t';
    line.append(s, std::min<size_t>(l, 1024));
    lua_pop(L, 1);
  }
  if (im->host().log) im->host().log(line);
  return 0;
}

int sb_collectgarbage(lua_State* L) {
  const char* opt = luaL_optstring(L, 1, "collect");
  if (!std::strcmp(opt, "count")) {
    lua_pushnumber(L, static_cast<double>(lua_gc(L, LUA_GCCOUNT, 0)) + static_cast<double>(lua_gc(L, LUA_GCCOUNTB, 0)) / 1024.0);
    return 1;
  }
  luaL_error(L, "collectgarbage: only \"count\" is available to mods");
  return 0;
}

// string.rep with the int cast fixed: the stock function casts the repeat count
// to int (2^31 wraps to a non-positive value and returns ""), and it asks the
// allocator for the whole result at once. Refuse results that cannot fit in the
// mod's remaining memory budget as a memory failure.
int bounded_rep(lua_State* L) {
  Impl* im = impl_of(L);
  size_t l;
  luaL_checklstring(L, 1, &l);
  double n = luaL_checknumber(L, 2);
  if (n > 0 && l > 0) {
    long double want = static_cast<long double>(l) * static_cast<long double>(n);
    long double room = im->slot->cap > im->slot->used ? static_cast<long double>(im->slot->cap - im->slot->used) : 0;
    if (want > room) {
      ++im->slot->failed_allocs;
      luaL_error(L, "not enough memory");
    }
    if (n > 2147483647.0) luaL_error(L, "resulting string too large");
  }
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_pushvalue(L, 1);
  lua_pushvalue(L, 2);
  lua_call(L, 2, 1);
  return 1;
}

int rng_roll(lua_State* L) {
  Impl* im = impl_of(L);
  double n = luaL_checknumber(L, 1);
  if (n < 1 || n > 4294967295.0 || n != std::floor(n)) luaL_argerror(L, 1, "n must be an integer in 1..2^32-1");
  if (!im->rng()) luaL_error(L, "no RNG attached to this host");
  RngStreams* r = im->rng();
  std::string mod = im->spec().mod_id, hook = im->cur_hook_id.empty() ? im->cur_hook : im->cur_hook_id;
  std::uint32_t v = r->hook_next(mod, hook);
  lua_pushnumber(L, static_cast<double>(v % static_cast<std::uint32_t>(n)));
  return 1;
}
int rng_main(lua_State* L) {
  Impl* im = impl_of(L);
  double n = luaL_checknumber(L, 1);
  if (n < 1 || n > 4294967295.0 || n != std::floor(n)) luaL_argerror(L, 1, "n must be an integer in 1..2^32-1");
  if (!im->rng()) luaL_error(L, "no RNG attached to this host");
  RngStreams* r = im->rng();
  std::string mod = im->spec().mod_id, hook = im->cur_hook_id.empty() ? im->cur_hook : im->cur_hook_id;
  std::uint32_t v = 0;
  std::string err;
  try {
    v = r->main_next_from_hook(mod, hook);
  } catch (const ParityViolation& e) {
    err = e.what();
  }
  if (!err.empty()) luaL_error(L, "%s", err.c_str());
  lua_pushnumber(L, static_cast<double>(v % static_cast<std::uint32_t>(n)));
  return 1;
}

// ---- storage scopes -------------------------------------------------------------
struct ScopeBox {
  int scope;
  char owner[64];
  char owner_mod[64];
};

int make_scope(lua_State* L, Scope s, const std::string& owner, const std::string& owner_mod) {
  if (owner.size() >= sizeof(ScopeBox::owner)) luaL_error(L, "storage owner id too long");
  if (owner_mod.size() >= sizeof(ScopeBox::owner_mod)) luaL_error(L, "mod id too long");
  auto* b = static_cast<ScopeBox*>(lua_newuserdata(L, sizeof(ScopeBox)));
  b->scope = static_cast<int>(s);
  std::memset(b->owner, 0, sizeof b->owner);
  std::memset(b->owner_mod, 0, sizeof b->owner_mod);
  std::memcpy(b->owner, owner.data(), owner.size());
  std::memcpy(b->owner_mod, owner_mod.data(), owner_mod.size());
  luaL_getmetatable(L, kScopeMeta);
  lua_setmetatable(L, -2);
  return 1;
}

std::string id_arg(lua_State* L, int i) {
  if (lua_type(L, i) == LUA_TNUMBER) {
    double d = lua_tonumber(L, i);
    if (d != std::floor(d) || std::fabs(d) >= 9007199254740992.0) luaL_argerror(L, i, "id must be an integer or string");
    return std::to_string(static_cast<std::int64_t>(d));
  }
  return luaL_checkstring(L, i);
}
void need_perm(lua_State* L, Permission p, const char* what) {
  Impl* im = impl_of(L);
  if (!im->spec().granted.count(p)) {
    im->perm_denied = true;
    luaL_error(L, "permission denied: %s", what);
  }
}
int st_character(lua_State* L) { return make_scope(L, Scope::Character, id_arg(L, 1), impl_of(L)->spec().mod_id); }
int st_account(lua_State* L) {
  need_perm(L, Permission::StorageAccount, "account storage requires the storage.account permission");
  return make_scope(L, Scope::Account, id_arg(L, 1), impl_of(L)->spec().mod_id);
}
int st_world(lua_State* L) { return make_scope(L, Scope::World, "world", impl_of(L)->spec().mod_id); }

ScopeBox* scope_arg(lua_State* L) {
  auto* b = static_cast<ScopeBox*>(luaL_checkudata(L, 1, kScopeMeta));
  if (!impl_of(L)->storage()) luaL_error(L, "no storage backend attached to this host");
  return b;
}
int sc_mod(lua_State* L) {
  auto* b = scope_arg(L);
  std::string other = luaL_checkstring(L, 2);
  if (other != impl_of(L)->spec().mod_id) need_perm(L, Permission::StorageReadOther, "reading another mod's storage requires storage.read:<mod>");
  return make_scope(L, static_cast<Scope>(b->scope), b->owner, other);
}
int sc_get(lua_State* L) {
  auto* b = scope_arg(L);
  std::string key = luaL_checkstring(L, 2);
  Impl* im = impl_of(L);
  std::optional<Value> v;
  int rc = guarded(L, [&] {
    v = im->storage()->get(static_cast<Scope>(b->scope), b->owner, b->owner_mod, im->spec().mod_id, key);
    return 0;
  });
  (void)rc;
  if (!v) lua_pushnil(L);
  else push_value(L, *v, 0);
  return 1;
}
int sc_set(lua_State* L) {
  auto* b = scope_arg(L);
  std::string key = luaL_checkstring(L, 2);
  Impl* im = impl_of(L);
  if (b->owner_mod != im->spec().mod_id) {
    im->perm_denied = true;
    luaL_error(L, "permission denied: a mod can only write its own storage");
  }
  Value v;
  std::string e = to_value(L, 3, 0, v);
  if (!e.empty()) luaL_error(L, "storage set '%s': %s", key.c_str(), e.c_str());
  return guarded(L, [&] {
    if (std::holds_alternative<std::monostate>(v)) im->storage()->erase(static_cast<Scope>(b->scope), b->owner, b->owner_mod, key);
    else im->storage()->set(static_cast<Scope>(b->scope), b->owner, b->owner_mod, key, std::move(v));
    return 0;
  });
}
int sc_keys(lua_State* L) {
  auto* b = scope_arg(L);
  Impl* im = impl_of(L);
  if (b->owner_mod != im->spec().mod_id) {
    im->perm_denied = true;
    luaL_error(L, "permission denied: key listing is limited to a mod's own storage");
  }
  auto keys = im->storage()->keys(static_cast<Scope>(b->scope), b->owner, b->owner_mod);
  lua_newtable(L);
  int i = 1;
  for (auto& k : keys) { lua_pushlstring(L, k.data(), k.size()); lua_rawseti(L, -2, i++); }
  return 1;
}

// ---- dialogue yields ----------------------------------------------------------------
void need_dialogue(lua_State* L, const char* fn) {
  if (impl_of(L)->dialogue_thread != L) luaL_error(L, "%s() can only be called directly from a dialogue coroutine", fn);
}
int d_say(lua_State* L) {
  size_t n;
  const char* s = luaL_checklstring(L, 1, &n);
  if (n > kMaxText) luaL_argerror(L, 1, "text too long");
  need_dialogue(L, "say");
  lua_pushstring(L, "say");
  lua_pushlstring(L, s, n);
  return lua_yield(L, 2);
}
int d_choose(lua_State* L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int n = lua_objlen(L, 1);
  if (n < 1 || n > kMaxOptions) luaL_argerror(L, 1, "choose needs 1..64 options");
  for (int i = 1; i <= n; ++i) {
    lua_rawgeti(L, 1, i);
    size_t l;
    if (lua_type(L, -1) != LUA_TSTRING) luaL_argerror(L, 1, "options must be strings");
    lua_tolstring(L, -1, &l);
    if (l > 256) luaL_argerror(L, 1, "option too long");
    lua_pop(L, 1);
  }
  need_dialogue(L, "choose");
  lua_pushstring(L, "choose");
  lua_pushvalue(L, 1);
  return lua_yield(L, 2);
}
int d_input(lua_State* L) {
  size_t n;
  const char* s = luaL_checklstring(L, 1, &n);
  if (n > kMaxText) luaL_argerror(L, 1, "prompt too long");
  need_dialogue(L, "input");
  lua_pushstring(L, "input");
  lua_pushlstring(L, s, n);
  return lua_yield(L, 2);
}
int d_wait(lua_State* L) {
  double ms = luaL_checknumber(L, 1);
  if (!(ms >= 0) || ms > 9007199254740992.0) luaL_argerror(L, 1, "ms out of range");
  need_dialogue(L, "wait");
  lua_pushstring(L, "wait");
  lua_pushnumber(L, std::floor(ms));
  return lua_yield(L, 2);
}

// ---- state construction -----------------------------------------------------------------
void reg_table(lua_State* L, const char* name, const luaL_Reg* fns) {
  luaL_register(L, name, fns);
  lua_pop(L, 1);
}

void add_methods(lua_State* L, const char* meta, const luaL_Reg* fns) {
  luaL_newmetatable(L, meta);
  lua_newtable(L);
  for (auto* f = fns; f->name; ++f) {
    lua_pushcfunction(L, f->func, f->name);
    lua_setfield(L, -2, f->name);
  }
  lua_setreadonly(L, -1, true);
  lua_setfield(L, -2, "__index");
  lua_pushstring(L, "locked");
  lua_setfield(L, -2, "__metatable");
  lua_setreadonly(L, -1, true);
  lua_pop(L, 1);
}

// Replaces unbounded natives before the libraries are frozen.
std::string prelude(std::uint32_t max_sort) {
  return "local sort, error, type = table.sort, error, type\n"
         "local LIMIT = " + std::to_string(max_sort) + "\n"
         "local function lt(a, b) return a < b end\n"
         // The default comparator runs inside the native sort and is not
         // interruptible (M0(c)); always sorting through a Luau comparator makes
         // every comparison a safepoint.
         "table.sort = function(t, cmp)\n"
         "  if type(t) ~= 'table' then error('invalid argument #1 to sort (table expected)', 2) end\n"
         "  if #t > LIMIT then error('table.sort: too many elements (limit ' .. LIMIT .. ')', 2) end\n"
         "  return sort(t, cmp or lt)\n"
         "end\n"
         "getfenv = nil setfenv = nil loadstring = nil\n";
}

const luaL_Reg kCtxMethods[] = {{"get", c_get}, {"get_str", c_get_str}, {"set", c_set}, {"add", c_add}, {"sub", c_sub},
                                {"mul", c_mul}, {"mul_pct", c_mul_pct}, {"idiv", c_idiv}, {"f32_step", c_f32},
                                {"min1", c_min1}, {"cancel", c_cancel}, {"enqueue", c_enqueue}, {nullptr, nullptr}};
const luaL_Reg kScopeMethods[] = {{"get", sc_get}, {"set", sc_set}, {"keys", sc_keys}, {"mod", sc_mod}, {nullptr, nullptr}};
const luaL_Reg kEvents[] = {{"on", ev_on}, {nullptr, nullptr}};
const luaL_Reg kStorage[] = {{"character", st_character}, {"account", st_account}, {"world", st_world},
                             {"migrate", st_migrate}, {nullptr, nullptr}};
const luaL_Reg kDialogue[] = {{"say", d_say}, {"choose", d_choose}, {"input", d_input}, {"wait", d_wait}, {nullptr, nullptr}};
const luaL_Reg kTimers[] = {{"after", tm_after}, {nullptr, nullptr}};
const luaL_Reg kDefinitions[] = {{"declare", def_declare}, {nullptr, nullptr}};
const luaL_Reg kRng[] = {{"roll", rng_roll}, {"main", rng_main}, {nullptr, nullptr}};

bool load_chunk(lua_State* th, const std::string& chunk, const std::string& src, std::string& err) {
  lua_CompileOptions opts = {};
  opts.optimizationLevel = 1;
  opts.debugLevel = 1;
  size_t bsz = 0;
  char* bc = luau_compile(src.data(), src.size(), &opts, &bsz);
  std::string name = "=" + chunk;
  int rc = luau_load(th, name.c_str(), bc, bsz, 0);
  std::free(bc);
  if (rc != 0) {
    err = lua_tostring(th, -1) ? lua_tostring(th, -1) : "load error";
    lua_pop(th, 1);
    return false;
  }
  return true;
}

struct ThreadGuard {
  lua_State* env;
  ~ThreadGuard() { lua_pop(env, 1); }
};

}  // namespace

ModSandbox::ModSandbox(ModEnvironmentSpec spec, MemoryAccountant* shared) : spec_(std::move(spec)) {
  impl_ = std::make_unique<Impl>(this, shared);
  Impl& im = *impl_;
  im.slot->cap = spec_.memory_cap_bytes;
  im.slot->used = 0;
  im.L = lua_newstate(limiting_alloc, im.slot);
  if (!im.L) {
    im.slot->disabled = true;
    return;
  }
  lua_State* L = im.L;
  lua_callbacks(L)->userdata = impl_.get();
  lua_callbacks(L)->interrupt = interrupt_cb;

  // Libraries: no os, no debug (and Luau has no io / require).
  luaopen_base(L); lua_pop(L, 1);
  luaopen_coroutine(L); lua_pop(L, 1);
  luaopen_table(L); lua_pop(L, 1);
  luaopen_string(L); lua_pop(L, 1);
  luaopen_math(L); lua_pop(L, 1);
  luaopen_bit32(L); lua_pop(L, 1);
  luaopen_utf8(L); lua_pop(L, 1);
  luaopen_buffer(L); lua_pop(L, 1);
  luaopen_vector(L); lua_pop(L, 1);

  // Bounded natives and removed globals.
  std::string pre = prelude(spec_.max_sort_elements);
  {
    std::string err;
    lua_State* t = lua_newthread(L);
    if (load_chunk(t, "=prelude", pre, err)) {
      if (lua_pcall(t, 0, 0, 0) != 0) im.slot->disabled = true;
    } else {
      im.slot->disabled = true;
    }
    lua_pop(L, 1);
  }
  lua_getglobal(L, "string");
  lua_getfield(L, -1, "rep");
  lua_pushcclosurek(L, bounded_rep, "rep", 1, nullptr);
  lua_setfield(L, -2, "rep");
  lua_pop(L, 1);
  lua_pushcfunction(L, sb_print, "print");
  lua_setglobal(L, "print");
  lua_pushcfunction(L, sb_collectgarbage, "collectgarbage");
  lua_setglobal(L, "collectgarbage");

  // Engine API.
  add_methods(L, kCtxMeta, kCtxMethods);
  add_methods(L, kScopeMeta, kScopeMethods);
  reg_table(L, "events", kEvents);
  reg_table(L, "storage", kStorage);
  reg_table(L, "dialogue", kDialogue);
  reg_table(L, "timers", kTimers);
  reg_table(L, "definitions", kDefinitions);
  reg_table(L, "rng", kRng);
  for (auto* g = kDialogue; g->name; ++g) { lua_pushcfunction(L, g->func, g->name); lua_setglobal(L, g->name); }

  luaL_sandbox(L);  // shared libraries, string metatable and globals become read-only
  im.env = lua_newthread(L);
  im.env_ref = lua_ref(L, -1);
  lua_pop(L, 1);
  luaL_sandboxthread(im.env);  // mod-private global table proxying the frozen one
}

ModSandbox::~ModSandbox() {
  if (impl_ && impl_->L) lua_close(impl_->L);
}

BudgetMeter& ModSandbox::meter() { return impl_->meter; }
std::uint64_t ModSandbox::memory_used() const { return impl_->slot->used; }
std::uint64_t ModSandbox::memory_peak() const { return impl_->slot->peak; }
bool ModSandbox::disabled() const { return impl_->slot->disabled; }
void ModSandbox::reenable() {
  // Luau does not collect on allocation failure, so the garbage the abort left
  // behind is still counted against the cap; reclaim it before running again.
  if (impl_->L) lua_gc(impl_->L, LUA_GCCOLLECT, 0);
  impl_->slot->disabled = false;
}
void ModSandbox::begin_tick() { impl_->meter.begin_tick(); }
std::vector<RegisteredHook> ModSandbox::take_hooks() { return std::exchange(impl_->hooks, {}); }
std::vector<RegisteredMigrate> ModSandbox::take_migrates() { return std::exchange(impl_->migrates, {}); }
std::vector<std::string> ModSandbox::take_shapes() { return std::exchange(impl_->shapes, {}); }

namespace {

// Runs the function (and nargs arguments) already on `th`'s stack under the
// budget, with a traceback handler, and classifies the outcome.
RunResult exec(Impl& im, lua_State* th, int nargs, int nres, const std::string& label, std::uint64_t step_limit = 0) {
  im.budget_tripped = false;
  im.perm_denied = false;
  im.cur_hook = label;
  const std::uint64_t failed_before = im.slot->failed_allocs;
  lua_pushcfunction(th, err_handler, "errh");
  lua_insert(th, -(nargs + 2));
  int hidx = lua_gettop(th) - nargs - 1;
  im.meter.reset_dispatch(step_limit);
  int rc = lua_pcall(th, nargs, nres, hidx);
  im.meter.end_dispatch();
  RunResult r;
  if (rc == 0 && im.slot->failed_allocs == failed_before) {
    lua_remove(th, hidx);
    return r;
  }
  if (rc == 0) {
    // The script survived an allocation failure (e.g. pcall around a bomb); the
    // cap was still exceeded, so the mod is disabled all the same.
    im.slot->disabled = true;
    r.status = RunStatus::MemoryExceeded;
    r.error = "[" + im.spec().mod_id + "] " + label + ": memory cap of " + std::to_string(im.slot->cap) + " bytes exceeded; mod disabled";
    lua_settop(th, hidx - 1);
    return r;
  }
  std::string msg = lua_tostring(th, -1) ? lua_tostring(th, -1) : "(non-string error)";
  lua_settop(th, hidx - 1);
  const std::string prefix = "[" + im.spec().mod_id + "] " + label + ": ";
  if (im.budget_tripped) {
    r.status = RunStatus::BudgetExceeded;
    r.error = prefix + im.trip_msg;
  } else if (rc == LUA_ERRMEM || (im.slot->failed_allocs != failed_before && msg.find("not enough memory") != std::string::npos)) {
    r.status = RunStatus::MemoryExceeded;
    im.slot->disabled = true;
    r.error = prefix + "memory cap of " + std::to_string(im.slot->cap) + " bytes exceeded; mod disabled";
  } else if (im.perm_denied) {
    r.status = RunStatus::PermissionDenied;
    r.error = prefix + msg;
  } else {
    r.status = RunStatus::Error;
    r.error = prefix + msg;
  }
  return r;
}

RunResult disabled_result(const Impl& im) {
  return {RunStatus::MemoryExceeded, "[" + im.spec().mod_id + "] mod is disabled after exceeding its memory cap"};
}

HookFailure::Kind failure_kind(RunStatus s) {
  switch (s) {
    case RunStatus::BudgetExceeded: return HookFailure::Kind::Budget;
    case RunStatus::MemoryExceeded: return HookFailure::Kind::Memory;
    case RunStatus::PermissionDenied: return HookFailure::Kind::Permission;
    default: return HookFailure::Kind::Error;
  }
}

}  // namespace

RunResult ModSandbox::run(const std::string& chunk_name, const std::string& source) {
  Impl& im = *impl_;
  if (!im.L) return {RunStatus::MemoryExceeded, "state could not be created"};
  if (im.slot->disabled) return disabled_result(im);
  try {
    lua_State* th = lua_newthread(im.env);
    ThreadGuard g{im.env};
    std::string err;
    if (!load_chunk(th, spec_.mod_id + ":" + chunk_name, source, err))
      return {RunStatus::Error, "[" + spec_.mod_id + "] " + chunk_name + ": " + err};
    im.loading = true;
    im.cur_hook_id.clear();
    RunResult r = exec(im, th, 0, 0, chunk_name);
    im.loading = false;
    return r;
  } catch (const std::exception& e) {
    im.loading = false;
    im.slot->disabled = true;  // the VM raised outside a protected call: state is suspect
    return {RunStatus::MemoryExceeded, "[" + spec_.mod_id + "] VM failure: " + e.what()};
  }
}

void ModSandbox::call_hook(int ref, HookContext& ctx) {
  Impl& im = *impl_;
  if (im.slot->disabled) throw HookFailure(HookFailure::Kind::Memory, disabled_result(im).error);
  std::string label = ctx.hook ? ctx.hook->qualified() : "<hook>";
  RunResult r;
  try {
    lua_State* th = lua_newthread(im.env);
    ThreadGuard g{im.env};
    lua_getref(th, ref);
    if (!lua_isfunction(th, -1)) throw HookFailure(HookFailure::Kind::Error, "[" + spec_.mod_id + "] " + label + ": hook function reference is gone");
    auto* box = static_cast<CtxBox*>(lua_newuserdata(th, sizeof(CtxBox)));
    box->call_id = ++im.call_id;
    luaL_getmetatable(th, kCtxMeta);
    lua_setmetatable(th, -2);
    im.ctx = &ctx;
    im.loading = false;
    im.cur_hook_id = ctx.hook ? ctx.hook->id : std::string();
    r = exec(im, th, 1, 0, label);
    im.ctx = nullptr;
  } catch (const HookFailure&) {
    im.ctx = nullptr;
    throw;
  } catch (const std::exception& e) {
    im.ctx = nullptr;
    im.slot->disabled = true;
    throw HookFailure(HookFailure::Kind::Memory, "[" + spec_.mod_id + "] VM failure: " + e.what());
  }
  if (r.status != RunStatus::Ok) throw HookFailure(failure_kind(r.status), r.error);
}

RunResult ModSandbox::call_ref(int ref, const std::string& label) {
  Impl& im = *impl_;
  if (im.slot->disabled) return disabled_result(im);
  try {
    lua_State* th = lua_newthread(im.env);
    ThreadGuard g{im.env};
    lua_getref(th, ref);
    if (!lua_isfunction(th, -1)) return {RunStatus::Error, "[" + spec_.mod_id + "] " + label + ": function reference is gone"};
    im.loading = false;
    im.cur_hook_id = label;
    return exec(im, th, 0, 0, label);
  } catch (const std::exception& e) {
    im.slot->disabled = true;
    return {RunStatus::MemoryExceeded, "[" + spec_.mod_id + "] VM failure: " + e.what()};
  }
}

RunResult ModSandbox::call_migrate(int ref, std::uint32_t from, const Row& in, Row& out) {
  Impl& im = *impl_;
  if (im.slot->disabled) return disabled_result(im);
  try {
    lua_State* th = lua_newthread(im.env);
    ThreadGuard g{im.env};
    lua_getref(th, ref);
    lua_pushnumber(th, from);
    push_value(th, in.value, 0);
    im.loading = false;
    im.cur_hook_id = "storage.migrate";
    RunResult r = exec(im, th, 2, 2, "storage.migrate");
    if (r.status != RunStatus::Ok) return r;
    // Contract: return new_version, new_value.
    if (lua_type(th, 1) != LUA_TNUMBER) return {RunStatus::Error, "[" + spec_.mod_id + "] migrate must return new_version, new_value"};
    double d = lua_tonumber(th, 1);
    if (d != std::floor(d) || d < 1 || d > 4e9) return {RunStatus::Error, "[" + spec_.mod_id + "] migrate returned a bad version"};
    std::uint32_t nv = static_cast<std::uint32_t>(d);
    Value v;
    std::string e = to_value(th, 2, 0, v);
    if (!e.empty()) return {RunStatus::Error, "[" + spec_.mod_id + "] migrate result: " + e};
    out = Row{nv, std::move(v)};
    return {};
  } catch (const std::exception& e) {
    im.slot->disabled = true;
    return {RunStatus::MemoryExceeded, "[" + spec_.mod_id + "] VM failure: " + e.what()};
  }
}

void ModSandbox::release_ref(int ref) { lua_unref(impl_->env, ref); }

// ---- coroutines ---------------------------------------------------------------------------
namespace {

CoStep co_run(Impl& im, std::uint64_t id, lua_State* th, int nargs) {
  CoStep step;
  step.id = id;
  im.budget_tripped = false;
  im.perm_denied = false;
  im.cur_hook = "dialogue";
  im.cur_hook_id = "dialogue";
  const std::uint64_t failed_before = im.slot->failed_allocs;
  im.dialogue_thread = th;
  im.meter.reset_dispatch(im.meter.limits().steps_per_slice);
  int rc = lua_resume(th, im.env, nargs);
  im.meter.end_dispatch();
  im.dialogue_thread = nullptr;
  if (rc == LUA_YIELD && im.slot->failed_allocs != failed_before) {
    im.slot->disabled = true;
    step.kind = CoStep::Kind::Failed;
    step.result = {RunStatus::MemoryExceeded, "[" + im.spec().mod_id + "] dialogue: memory cap exceeded; mod disabled"};
    return step;
  }
  if (rc == LUA_YIELD) {
    const char* kind = lua_tostring(th, 1);
    std::string k = kind ? kind : "";
    if (k == "say" || k == "input") {
      size_t n;
      const char* s = lua_tolstring(th, 2, &n);
      std::string text(s ? s : "", n);
      if (k == "say") step.request = SayRequest{std::move(text)};
      else step.request = InputRequest{std::move(text)};
    } else if (k == "choose") {
      ChooseRequest c;
      int n = lua_objlen(th, 2);
      for (int i = 1; i <= n; ++i) {
        lua_rawgeti(th, 2, i);
        size_t l;
        const char* s = lua_tolstring(th, -1, &l);
        c.options.emplace_back(s ? s : "", l);
        lua_pop(th, 1);
      }
      step.request = std::move(c);
    } else {
      step.request = WaitRequest{static_cast<std::uint64_t>(lua_tonumber(th, 2))};
    }
    lua_settop(th, 0);
    step.kind = CoStep::Kind::Yield;
    return step;
  }
  if (rc == 0 && im.slot->failed_allocs != failed_before) {
    im.slot->disabled = true;
    step.kind = CoStep::Kind::Failed;
    step.result = {RunStatus::MemoryExceeded, "[" + im.spec().mod_id + "] dialogue: memory cap exceeded; mod disabled"};
    return step;
  }
  if (rc == 0) {
    step.kind = CoStep::Kind::Done;
    return step;
  }
  std::string msg = lua_tostring(th, -1) ? lua_tostring(th, -1) : "(non-string error)";
  const std::string prefix = "[" + im.spec().mod_id + "] dialogue: ";
  step.kind = CoStep::Kind::Failed;
  if (im.budget_tripped) {
    step.result = {RunStatus::BudgetExceeded, prefix + im.trip_msg};
  } else if (rc == LUA_ERRMEM || (im.slot->failed_allocs != failed_before && msg.find("not enough memory") != std::string::npos)) {
    im.slot->disabled = true;
    step.result = {RunStatus::MemoryExceeded, prefix + "memory cap exceeded; mod disabled"};
  } else {
    const char* tr = lua_debugtrace(th);
    step.result = {im.perm_denied ? RunStatus::PermissionDenied : RunStatus::Error, prefix + msg + "\n" + (tr ? tr : "")};
  }
  return step;
}

}  // namespace

CoStep ModSandbox::co_start(const std::string& chunk_name, const std::string& source) {
  Impl& im = *impl_;
  CoStep fail;
  if (!im.L) { fail.result = {RunStatus::Error, "state could not be created"}; return fail; }
  if (im.slot->disabled) { fail.result = disabled_result(im); return fail; }
  try {
    lua_State* th = lua_newthread(im.env);
    int ref = lua_ref(im.env, -1);
    lua_pop(im.env, 1);
    std::string err;
    if (!load_chunk(th, spec_.mod_id + ":" + chunk_name, source, err)) {
      lua_unref(im.env, ref);
      fail.result = {RunStatus::Error, "[" + spec_.mod_id + "] " + chunk_name + ": " + err};
      return fail;
    }
    std::uint64_t id = im.next_co++;
    im.cos[id] = {th, ref};
    CoStep s = co_run(im, id, th, 0);
    if (s.kind != CoStep::Kind::Yield) co_cancel(id);
    return s;
  } catch (const std::exception& e) {
    im.slot->disabled = true;
    fail.result = {RunStatus::MemoryExceeded, "[" + spec_.mod_id + "] VM failure: " + e.what()};
    return fail;
  }
}

CoStep ModSandbox::co_resume(std::uint64_t id, const DialogueResponse& r) {
  Impl& im = *impl_;
  CoStep fail;
  fail.id = id;
  auto it = im.cos.find(id);
  if (it == im.cos.end()) { fail.result = {RunStatus::Error, "unknown coroutine"}; return fail; }
  if (im.slot->disabled) { co_cancel(id); fail.result = disabled_result(im); return fail; }
  lua_State* th = it->second.th;
  try {
    int nargs = 0;
    if (r.choice) { lua_pushnumber(th, static_cast<double>(*r.choice)); nargs = 1; }
    else if (r.text) { lua_pushlstring(th, r.text->data(), r.text->size()); nargs = 1; }
    CoStep s = co_run(im, id, th, nargs);
    if (s.kind != CoStep::Kind::Yield) co_cancel(id);
    return s;
  } catch (const std::exception& e) {
    im.slot->disabled = true;
    fail.result = {RunStatus::MemoryExceeded, "[" + spec_.mod_id + "] VM failure: " + e.what()};
    return fail;
  }
}

void ModSandbox::co_cancel(std::uint64_t id) {
  Impl& im = *impl_;
  auto it = im.cos.find(id);
  if (it == im.cos.end()) return;
  lua_resetthread(it->second.th);
  lua_unref(im.env, it->second.ref);
  im.cos.erase(it);
}

std::size_t ModSandbox::co_count() const { return impl_->cos.size(); }

#endif  // SCRIPT_HOST_WITH_LUAU

}  // namespace script_host
