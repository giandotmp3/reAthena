#include "rathena/persist/domain.hpp"

namespace rathena::persist {

bool is_zero_or_empty(const Value& v) noexcept {
	struct V {
		bool operator()(std::int64_t x) const { return x == 0; }
		bool operator()(double x) const { return x == 0.0; }
		bool operator()(const std::string& x) const { return x.empty(); }
		bool operator()(bool x) const { return !x; }
		bool operator()(const std::shared_ptr<const ValueTable>&) const { return false; }
	};
	return std::visit(V{}, v.data);
}

std::size_t encoded_size(const Value& v) noexcept {
	struct V {
		std::size_t operator()(std::int64_t) const { return 8; }
		std::size_t operator()(double) const { return 8; }
		std::size_t operator()(const std::string& x) const { return x.size(); }
		std::size_t operator()(bool) const { return 1; }
		std::size_t operator()(const std::shared_ptr<const ValueTable>& t) const {
			std::size_t n = 0;
			if (t) for (const auto& [k, val] : t->entries) n += k.size() + encoded_size(val);
			return n;
		}
	};
	return std::visit(V{}, v.data);
}

} // namespace rathena::persist

namespace rathena::persist {
std::string owner_ref_text(const KvKey& k) {
	return k.owner_text.empty() ? std::to_string(k.owner.id) : k.owner_text;
}
} // namespace rathena::persist
