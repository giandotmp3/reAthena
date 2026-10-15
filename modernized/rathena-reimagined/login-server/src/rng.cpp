#include "rathena/login/rng.hpp"

#include <random>

#include "rathena/login/types.hpp"

namespace rathena::login {

std::uint32_t SeededRng::next_u32() {
  std::uint64_t z = (state_ += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return static_cast<std::uint32_t>((z ^ (z >> 31)) >> 32);
}

std::uint32_t SystemRng::next_u32() {
  thread_local std::random_device rd;
  return rd();
}

std::string ipv4_to_string(Ipv4 ip) {
  return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 0xFF) + "." +
         std::to_string((ip >> 8) & 0xFF) + "." + std::to_string(ip & 0xFF);
}

}  // namespace rathena::login
