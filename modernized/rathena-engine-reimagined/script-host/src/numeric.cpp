#include "script_host/numeric.hpp"

#include "script_host/not_implemented.hpp"

namespace script_host {

void NumCtx::add(std::int64_t d) { v_ += d; }
void NumCtx::mul_pct(std::int64_t) { not_implemented("NumCtx::mul_pct"); }
void NumCtx::idiv(std::int64_t) { not_implemented("NumCtx::idiv"); }
void NumCtx::f32_step(double) { not_implemented("NumCtx::f32_step"); }
std::uint32_t RngStreams::main_next() { not_implemented("RngStreams::main_next"); }
std::uint32_t RngStreams::hook_next(const std::string&, const std::string&) { not_implemented("RngStreams::hook_next"); }

}  // namespace script_host
