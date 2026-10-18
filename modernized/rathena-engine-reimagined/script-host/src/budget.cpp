#include "script_host/budget.hpp"

#include "script_host/not_implemented.hpp"

namespace script_host {

void BudgetMeter::step(const std::string&, const std::string&, std::uint64_t) { not_implemented("BudgetMeter::step"); }
void BudgetMeter::reset_dispatch() { steps_ = 0; }
bool BudgetMeter::strike(const std::string&, std::uint64_t) { not_implemented("BudgetMeter::strike"); }

}  // namespace script_host
