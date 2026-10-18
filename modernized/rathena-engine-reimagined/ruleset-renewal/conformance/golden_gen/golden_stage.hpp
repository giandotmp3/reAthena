// Golden generator probe (copied into src/map/ of the legacy COPY; never into the repository).
#ifndef GOLDEN_STAGE_HPP
#define GOLDEN_STAGE_HPP
#include <vector>
#include <cstdint>
struct GoldenStage { const char* name; long long d1, d2; };
inline std::vector<GoldenStage>* g_golden_stages = nullptr;
#define GSTAGE(name, a, b) do { if (g_golden_stages) g_golden_stages->push_back({ name, (long long)(a), (long long)(b) }); } while (0)
#endif
