#include "tooling/spike.hpp"
#include "tooling/common.hpp"
namespace tooling::spike {
SpikeResult damage_aoe_burst(int, bool) { throw NotImplemented("M0(a)"); }
SpikeResult legacy_script_interpreter(const std::string&) { throw NotImplemented("M0(b)"); }
SpikeResult hooks_at_scale(int, int) { throw NotImplemented("M0(c)"); }
SpikeResult numeric_fidelity() { throw NotImplemented("M0(d)"); }
}  // namespace tooling::spike
