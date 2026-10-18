#include "netproto/common.hpp"

namespace netproto {
void not_implemented(const char* what) {
  throw NotImplementedError(std::string("not implemented: ") + what);
}
}  // namespace netproto
