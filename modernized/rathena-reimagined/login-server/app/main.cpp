// login-server entry point (STUB). Listens on ${LOGIN_BIND}:6900 once the Asio
// IO edge is wired. Refuses to start without inter-server secrets from the
// environment (BR-LOGIN-025, BR-INTER-016) and without a DB URL.
#include <cstdio>
#include <cstdlib>

#include "rathena/login/config.hpp"

int main() {
  using namespace rathena::login;
  const auto creds = load_inter_server_credentials([](const char* k) -> const char* { return std::getenv(k); });
  if (!creds) {
    std::fprintf(stderr, "login-server: INTER_SERVER_USER / INTER_SERVER_PASS must be set in the environment\n");
    return 2;
  }
  const char* db = std::getenv("LOGIN_DB_URL");
  if (db == nullptr || *db == '\0') db = std::getenv("DATABASE_URL");
  if (db == nullptr || *db == '\0') {
    std::fprintf(stderr, "login-server: LOGIN_DB_URL or DATABASE_URL must be set\n");
    return 2;
  }
  std::fprintf(stderr, "login-server: scaffold only; network loop not implemented\n");
  return 1;
}
