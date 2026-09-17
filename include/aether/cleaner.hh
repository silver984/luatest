#pragma once
#include <vector>

namespace aether::util {

class cleaner final {
public:
	cleaner() = delete;

	static void schedule_for_cleanup(void (*fn)()) {
		if (std::ranges::find(scheduled_, fn) == scheduled_.end()) {
			scheduled_.push_back(fn);
		}
	}

	static void cleanup() {
		for (auto it = scheduled_.begin(); it != scheduled_.end();) {
			(*it)();
			it = scheduled_.erase(it);
		}
	}

private:
	static inline std::vector<void (*)()> scheduled_;
};

} // namespace aether::util