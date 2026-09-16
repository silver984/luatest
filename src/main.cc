#include <cstddef>
#include <functional>
#include <sol/raii.hpp>
#include <sol/types.hpp>
#include <string>
#include <string_view>
#include <unordered_map>

#include <fmt/color.h>
#include <fmt/core.h>

#include <sol/forward.hpp>
#include <sol/sol.hpp>

struct string_hash final {
	using is_transparent = void;
	using hash_type      = std::hash<std::string_view>;

	size_t operator()(std::string_view str) const {
		return hash_type{}(str);
	}

	size_t operator()(std::string const& str) const {
		return hash_type{}(str);
	}

	size_t operator()(char const* str) const {
		return hash_type{}(str);
	}
};

template <typename T>
using string_map = std::unordered_map<std::string, T, string_hash, std::equal_to<>>;

class intercepted_apple;

class apple {
public:
	virtual void banana() {
		fmt::println("hello from apple::banana");
	}

	using intercept_type = intercepted_apple;
};

struct lua_detour final {
	sol::table owner;
	sol::protected_function fn;
	int priority;
	bool enabled;
};

struct lua_hook final {
	std::vector<std::shared_ptr<lua_detour>> detours;
};

template <typename T>
concept interceptable = requires { typename T::intercept_type; };

template <typename Intercept, interceptable Base>
struct intercepted : public Base {
	template <typename Fn, typename... Args>
	auto divert(std::string_view lua_key, Fn destination, Args&&... args) {
		auto* self = static_cast<Base*>(this);
		auto it    = hooks.find(lua_key);

		if (it == hooks.end()) {
			return std::invoke(destination, self, std::forward<Args>(args)...);
		}

		auto& detours = it->second.detours;
		using ret     = std::invoke_result_t<Fn, Base*, Args&&...>;

		auto chain = [&](auto& inner_chain, Args&&... chain_args, size_t i = 0) -> ret {
			if (i >= detours.size()) {
				return std::invoke(destination, self, std::forward<Args>(chain_args)...);
			}

			auto& detour = detours[i]->fn;
			auto super   = [&](Args&&... super_args) -> ret {
				return inner_chain(inner_chain, std::forward<Args>(super_args)..., i + 1);
			};

			sol::protected_function_result res = detour(self, super, std::forward<Args>(chain_args)...);

			if (!res.valid()) {
				sol::error e = res;
				fmt::println(fmt::runtime(e.what()));
				return super(std::forward<Args>(chain_args)...);
			}

			if constexpr (!std::is_void_v<ret>) {
				if (res.return_count() <= 0) {
					return super(std::forward<Args>(chain_args)...);
				}
				return res.get<ret>();
			}
		};

		return chain(chain, args...);
	}

	static void cleanup() {
		hooks.clear();
	}

	static inline string_map<lua_hook> hooks;
};

struct intercepted_apple final : intercepted<intercepted_apple, apple> {
	void banana() override {
		// todo: profile how long this takes
		return this->divert("banana", [](apple* s) -> void {
			return s->apple::banana();
		});
	}
};

struct cleaner final {
	static void schedule_for_cleanup(void (*fn)()) {
		if (std::ranges::find(all_scheduled, fn) == all_scheduled.end()) {
			all_scheduled.push_back(fn);
		}
	}

	static void cleanup() {
		for (auto it = all_scheduled.begin(); it != all_scheduled.end();) {
			(*it)();
			it = all_scheduled.erase(it);
		}
	}

	static inline std::vector<void (*)()> all_scheduled;
};

int main() {
	sol::state lua;
	using enum sol::lib;
	lua.open_libraries(base, string, table, math, utf8);

	sol::usertype<intercepted_apple> apple_ut = lua.new_usertype<intercepted_apple>("apple");

	// todo: move this inside `intercepted<T, U>` once mature
	apple_ut["modify"] = [](sol::this_state s) -> sol::table {
		sol::state_view lua_v(s);
		sol::table out       = lua_v.create_table();
		sol::table metatable = lua_v.create_table();

		metatable[sol::meta_function::new_index] = [](sol::this_state s, sol::table t, sol::object key, sol::object val) -> void {
			t.raw_set(key, val);

			if (key.get_type() != sol::type::string || val.get_type() != sol::type::function) {
				return;
			}

			std::string key_str     = key.as<std::string>();
			sol::object maybe_bound = sol::state_view(s)["apple"][key_str];

			if (!maybe_bound.valid() || maybe_bound.get_type() != sol::type::function) {
				return;
			}

			auto [it, _]  = intercepted_apple::hooks.try_emplace(key_str);
			auto& detours = it->second.detours;

			auto tmp      = std::make_shared<lua_detour>();
			tmp->owner    = t;
			tmp->fn       = val.as<sol::protected_function>();
			tmp->priority = (int)detours.size();
			tmp->enabled  = true;

			detours.emplace_back(std::move(tmp));

			cleaner::schedule_for_cleanup(&intercepted_apple::cleanup);
			fmt::print(fg(fmt::color::dark_gray), "enabled function hook for \"apple:{}\" | table: {} | detour: {}\n", key_str,
			           t.pointer(), val.pointer());
		};

		out[sol::metatable_key] = metatable;
		return out;
	};

	apple_ut[sol::meta_function::construct] = sol::constructors<intercepted_apple()>();
	apple_ut["banana"]                      = &intercepted_apple::banana;

	auto scr_err = [](sol::this_state, sol::protected_function_result res) -> sol::protected_function_result {
		sol::error e = res;
		fmt::println(fmt::runtime(e.what()));
		return res;
	};

	sol::environment scr_env(lua, sol::create, lua.globals());
	sol::protected_function_result scr_res = lua.safe_script_file("test.lua", scr_env, scr_err);

	if (!scr_res.valid()) {
		cleaner::cleanup();
		return -1;
	}

	// test
	intercepted_apple a;
	apple* ptr = &a;
	ptr->banana();

	cleaner::cleanup();
	return 0;
}