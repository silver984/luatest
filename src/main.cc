#include <fmt/core.h>
#include <sol/sol.hpp>
#include <vector>

template <typename T>
struct modifiable {
	modifiable() noexcept          = default;
	virtual ~modifiable() noexcept = default;
	static inline std::vector<sol::table> modifications;
	template <typename Original, typename... Args>
	auto lua_intercept(std::string_view lua_index, Original original, Args... args) {
		using ret      = std::invoke_result_t<Original, T*, Args...>;
		auto intercept = [=, this](auto&& intercept_, Args... intercept_args, size_t i = 0) -> ret {
			if (i >= modifications.size()) {
				return std::invoke(original, dynamic_cast<T*>(this), intercept_args...);
			}

			sol::object maybe_detour   = modifications[i][lua_index];
			bool const is_detour_valid = maybe_detour.valid() && (maybe_detour.get_type() == sol::type::function);

			std::function<ret(Args...)> super = [=](Args... super_args) -> ret {
				return intercept_(intercept_, super_args..., (i + 1));
			};

			if (!is_detour_valid) {
				return super(intercept_args...);
			}

			sol::protected_function detour     = maybe_detour.as<sol::protected_function>();
			sol::protected_function_result res = detour(dynamic_cast<T*>(this), super, intercept_args...);

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
};

class apple final : public modifiable<apple> {
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

	sol::usertype<apple> lua_apple           = lua.new_usertype<apple>("apple");
	lua_apple[sol::meta_function::construct] = sol::constructors<apple()>();
	lua_apple["banana"]                      = &apple::banana;
	lua_apple["cantaloupe"]                  = &apple::cantaloupe;
	lua_apple["elderberry"]                  = &apple::elderberry;
	lua_apple["modify"]                      = [](sol::this_state s) -> sol::table {
		sol::state_view lua_v(s);
		sol::table out = lua_v.create_table();
		apple::modifications.push_back(out);
		return out;
	};

	auto scr_on_err = [](lua_State*, sol::protected_function_result res) -> sol::protected_function_result {
		sol::error e = res;
		fmt::println(fmt::runtime(e.what()));
		return res;
	};
	sol::environment scr_env(lua, sol::create, lua.globals());
	sol::protected_function_result scr_res = lua.safe_script_file("test.lua", scr_env, scr_on_err);

	if (!scr_res.valid()) {
		return -1;
	}

	// test
	apple a;
	a.banana();
	bool v = a.cantaloupe();
	fmt::println("a.cantaloupe was {}", v);
	a.elderberry(4);

	apple::modifications.clear();
	return 0;
}