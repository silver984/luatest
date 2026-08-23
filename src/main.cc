#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

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

template <typename T>
struct lua_modifiable {
	lua_modifiable() noexcept          = default;
	virtual ~lua_modifiable() noexcept = default;

	[[nodiscard]] static sol::usertype<T> create_lua_usertype(sol::state_view lua, std::string_view name) {
		sol::usertype<T> out = lua.new_usertype<T>(name);
		out["modify"]        = [name](sol::this_state s) -> sol::table {
			sol::state_view lua_v(s);
			sol::table out                    = lua_v.create_table();
			sol::table mt                     = lua_v.create_table();
			mt[sol::meta_function::new_index] = [name](sol::this_state s, sol::table t, sol::object key,
			                                           sol::object value) -> void {
				t.raw_set(key, value);

				if (key.get_type() != sol::type::string || value.get_type() != sol::type::function) {
					return;
				}

				sol::state_view lua_v(s);
				std::string key_str     = key.as<std::string>();
				sol::object maybe_bound = lua_v[name][key_str];

				if (!maybe_bound.valid() || maybe_bound.get_type() != sol::type::function) {
					return;
				}

				T::hooked_functions[key_str].emplace_back(value.as<sol::protected_function>());
				fmt::println("enabled function hook for \"{}:{}\" | table: {} | detour: {}", name, key_str, t.pointer(),
				             value.pointer());
			};
			out[sol::metatable_key] = mt;
			return out;
		};
		return out;
	}

	template <typename Original, typename... Args>
	auto lua_intercept(std::string_view key, Original original, Args... args) {
		T* this_ = dynamic_cast<T*>(this);

		if (!T::hooked_functions.contains(key)) {
			return std::invoke(original, this_, args...);
		}

		using ret    = std::invoke_result_t<Original, T*, Args...>;
		auto detours = T::hooked_functions[std::string(key)];

		auto intercept = [&detours, original, this_](auto&& intercept_, Args... intercept_args, size_t i = 0) -> ret {
			if (i >= detours.size()) {
				return std::invoke(original, this_, intercept_args...);
			}

			sol::protected_function detour    = detours[i];
			std::function<ret(Args...)> super = [intercept_, i](Args... super_args) -> ret {
				return intercept_(intercept_, super_args..., (i + 1));
			};

			sol::protected_function_result res = detour(this_, super, intercept_args...);

			if (!res.valid()) {
				sol::error e = res;
				fmt::println(fmt::runtime(e.what()));
				return super(intercept_args...);
			}

			if constexpr (!std::is_void_v<ret>) {
				if (res.return_count() <= 0) {
					return super(intercept_args...);
				}
				return res.get<ret>();
			}
		};

		return intercept(intercept, args...);
	}

	static inline string_map<std::vector<sol::protected_function>> hooked_functions;
};

class apple final : public lua_modifiable<apple> {
public:
	apple() noexcept;
	~apple() noexcept override;

	void banana();
	bool cantaloupe();
	int elderberry(int num);

private:
	void fuji_();
	struct proto;
};

struct apple::proto final {
	static void banana(apple*) {
		fmt::println("banana");
	}

	static bool cantaloupe(apple*) {
		fmt::println("cantaloupe");
		return true;
	}

	static int elderberry(apple* this_, int a) {
		fmt::println("elderberry | {}", a);
		this_->fuji_();
		return a;
	}
};

apple::apple() noexcept  = default;
apple::~apple() noexcept = default;

void apple::banana() {
	this->lua_intercept("banana", &proto::banana);
}

bool apple::cantaloupe() {
	return this->lua_intercept("cantaloupe", &proto::cantaloupe);
}

int apple::elderberry(int a) {
	return this->lua_intercept("elderberry", &proto::elderberry, a);
}

void apple::fuji_() {
	fmt::println("fuji_");
}

int main() {
	sol::state lua;
	using enum sol::lib;
	lua.open_libraries(base, string, table, math, utf8);

	sol::usertype<apple> lua_apple           = apple::create_lua_usertype(lua, "apple");
	lua_apple[sol::meta_function::construct] = sol::constructors<apple()>();
	lua_apple["banana"]                      = &apple::banana;
	lua_apple["cantaloupe"]                  = &apple::cantaloupe;
	lua_apple["elderberry"]                  = &apple::elderberry;

	auto scr_on_err = [](lua_State*, sol::protected_function_result res) -> sol::protected_function_result {
		sol::error e = res;
		fmt::println(fmt::runtime(e.what()));
		return res;
	};
	sol::environment scr_env(lua, sol::create, lua.globals());
	sol::protected_function_result scr_res = lua.safe_script_file("test.lua", scr_env, scr_on_err);

	if (!scr_res.valid()) {
		apple::hooked_functions.clear();
		return -1;
	}

	// test
	apple a;
	a.banana();
	bool v = a.cantaloupe();
	fmt::println("a.cantaloupe was {}", v);
	a.elderberry(4);

	apple::hooked_functions.clear();
	return 0;
}