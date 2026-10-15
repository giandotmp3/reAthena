// Records the legacy random stream for a seed (tools/golden/capture.sh rng).
// Built against src/common/random.cpp; prints one JSON object per draw:
//   {"seed":S,"i":N,"raw":R}      raw 32-bit outputs of the legacy generator after rnd_seed(S)
//   {"seed":S,"i":N,"rnd":V}      rnd() after a fresh rnd_seed(S) (libstdc++ distribution)
#include <cstdio>

#include <common/random.hpp>

int main() {
	for (const uint32 seed : {1u, 5489u, 20261015u, 4294967295u}) {
		rnd_seed(seed);
		for (int i = 0; i < 32; ++i)
			std::printf("{\"seed\":%u,\"i\":%d,\"raw\":%u}\n", seed, i, static_cast<unsigned>(generator()));
		rnd_seed(seed);
		for (int i = 0; i < 32; ++i)
			std::printf("{\"seed\":%u,\"i\":%d,\"rnd\":%d}\n", seed, i, rnd());
	}
	return 0;
}
