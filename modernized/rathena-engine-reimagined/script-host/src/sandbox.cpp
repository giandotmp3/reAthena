#include "script_host/sandbox.hpp"

#include "script_host/not_implemented.hpp"

namespace script_host {

void MemoryAccountant::set_cap(const std::string&, std::uint64_t) { not_implemented("MemoryAccountant::set_cap"); }
bool MemoryAccountant::charge(const std::string&, std::uint64_t) { not_implemented("MemoryAccountant::charge"); }
void MemoryAccountant::release(const std::string&, std::uint64_t) { not_implemented("MemoryAccountant::release"); }
std::uint64_t MemoryAccountant::used(const std::string&) const { not_implemented("MemoryAccountant::used"); }
bool MemoryAccountant::disabled(const std::string&) const { not_implemented("MemoryAccountant::disabled"); }
RunResult ModSandbox::run(const std::string&, const std::string&) { not_implemented("ModSandbox::run"); }

}  // namespace script_host
