#pragma once
// Namespaced identifiers. Definitions: "mod:kind/name". Events: "namespace:event".
#include <optional>
#include <string>
#include <string_view>

namespace script_host {

struct NamespacedId {
  std::string mod, kind, name;
  static std::optional<NamespacedId> parse(std::string_view s);
  std::string str() const { return mod + ":" + kind + "/" + name; }
  bool operator==(const NamespacedId&) const = default;
};

struct EventName {
  std::string ns, event;
  static std::optional<EventName> parse(std::string_view s);
  std::string str() const { return ns + ":" + event; }
  bool operator==(const EventName&) const = default;
};

}  // namespace script_host
