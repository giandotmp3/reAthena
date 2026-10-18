#pragma once
// M0 measurement spikes. Each writes numbers plus a go/no-go verdict.
#include <string>

namespace tooling::spike {

struct SpikeResult { std::string spike; double metric; std::string unit; bool go; std::string note; };

SpikeResult damage_aoe_burst(int targets, bool budget_interrupt);      // M0(a) us per hit
SpikeResult legacy_script_interpreter(const std::string& file);        // M0(b)
SpikeResult hooks_at_scale(int ccu, int hooks);                        // M0(c)
SpikeResult numeric_fidelity();                                        // M0(d) E-1..E-10

}  // namespace tooling::spike
