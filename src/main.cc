#include <cstddef>
#include <functional>
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

template <typename T>
concept interceptable = requires { typename T::intercept_type; };

struct lua_detour final {
	sol::protected_function fn;
	size_t priority;
};

template <typename Intercept, interceptable Base>
struct intercepted : public Base {
	template <typename Fn, typename... Args>
	auto divert(std::string_view lua_key, Fn destination, Args... args) {
		using ret  = std::invoke_result_t<Fn, Base*, Args...>;
		Base* self = dynamic_cast<Base*>(this);

		if (hooks.empty() || !hooks.contains(lua_key)) {
			return std::invoke(destination, self, std::forward<Args>(args)...);
		}

		auto curhook  = hooks.find(lua_key);
		auto& detours = curhook->second;
		auto chain    = [&](auto& inner_chain, Args... chain_args, size_t i = 0) -> ret {
			if (i >= detours.size()) {
				return std::invoke(destination, self, chain_args...);
			}

			auto& detour = detours[i].fn;
			auto super   = [&](Args... super_args) -> ret {
				return inner_chain(inner_chain, super_args..., i + 1);
			};

			sol::protected_function_result res = detour(self, super, chain_args...);

			if (!res.valid()) {
				sol::error e = res;
				fmt::println(fmt::runtime(e.what()));
				return super(chain_args...);
			}

			if constexpr (!std::is_void_v<ret>) {
				if (res.return_count() <= 0) {
					return super(chain_args...);
				}
				return res.get<ret>();
			}
		};

		return chain(chain, args...);
	}

	static void remove_detours() {
		hooks.clear();
	}

	static inline string_map<std::vector<lua_detour>> hooks;
};

struct intercept_cleaner final {
	static void schedule_for_cleanup(void (*fn)()) {
		if (std::ranges::find(scheduled, fn) == scheduled.end()) {
			scheduled.push_back(fn);
		}
	}

	static void cleanup() {
		for (auto it = scheduled.begin(); it != scheduled.end();) {
			(*it)();
			it = scheduled.erase(it);
		}
	}

	static inline std::vector<void (*)()> scheduled;
};

struct intercepted_apple final : intercepted<intercepted_apple, apple> {
	void banana() override {
		return this->divert("banana", [](apple* s) -> void {
			return s->apple::banana();
		});
	}
};

int main() {
	sol::state lua;
	using enum sol::lib;
	lua.open_libraries(base, string, table, math, utf8);

	sol::usertype<intercepted_apple> lua_apple = lua.new_usertype<intercepted_apple>("apple");
	lua_apple["modify"]                        = [](sol::this_state s) -> sol::table {
		sol::state_view lua_v(s);
		sol::table out = lua_v.create_table();
		sol::table mt  = lua_v.create_table();

		mt[sol::meta_function::new_index] = [](sol::this_state s, sol::table t, sol::object key, sol::object value) -> void {
			t.raw_set(key, value);

			if (key.get_type() != sol::type::string || value.get_type() != sol::type::function) {
				return;
			}

			sol::state_view lua_v(s);
			std::string key_str     = key.as<std::string>();
			sol::object maybe_bound = lua_v["apple"][key_str];

			if (!maybe_bound.valid() || maybe_bound.get_type() != sol::type::function) {
				return;
			}

			auto& detours = intercepted_apple::hooks[key_str];
			// todo: implement priority
			detours.emplace_back(lua_detour{
			        .fn       = value.as<sol::protected_function>(),
			        .priority = detours.size(),
			});

			intercept_cleaner::schedule_for_cleanup(&intercepted_apple::remove_detours);
			fmt::print(fg(fmt::color::dark_gray), "enabled function hook for \"apple:{}\" | table: {} | detour: {}\n", key_str,
			           t.pointer(), value.pointer());
		};

		out[sol::metatable_key] = mt;
		return out;
	};

	lua_apple[sol::meta_function::construct] = sol::constructors<intercepted_apple()>();
	lua_apple["banana"]                      = &intercepted_apple::banana;

	auto scr_err = [](sol::this_state, sol::protected_function_result res) -> sol::protected_function_result {
		sol::error e = res;
		fmt::println(fmt::runtime(e.what()));
		return res;
	};

	sol::environment scr_env(lua, sol::create, lua.globals());
	sol::protected_function_result scr_res = lua.safe_script_file("test.lua", scr_env, scr_err);

	if (!scr_res.valid()) {
		intercept_cleaner::cleanup();
		return -1;
	}

	// test
	intercepted_apple a;
	apple* ptr = &a;
	ptr->banana();

	intercept_cleaner::cleanup();
	return 0;
}