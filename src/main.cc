#include <aether/apple.hh>
#include <aether/cherry.hh>
#include <aether/cleaner.hh>

int main() {
	sol::state lua;

	using enum sol::lib;
	lua.open_libraries(base, string, table, math, utf8);

	using apple_intercept = aether::intercept_t<aether::apple>;
	sol::usertype<apple_intercept> apple_ut =
	        aether::new_intercepted_lua_usertype<aether::apple>(lua, "apple", sol::constructors<apple_intercept()>());

	apple_ut["banana"]  = &apple_intercept::banana;
	apple_ut["mango"]   = &apple_intercept::mango;
	apple_ut["truffle"] = &apple_intercept::truffle;
	apple_ut["toffee"]  = &apple_intercept::toffee;
	apple_ut["monkey"]  = &apple_intercept::monkey;

	sol::protected_function_result script_res =
	        lua.safe_script_file("test.lua", sol::environment(lua, sol::create, lua.globals()),
	                             [](sol::this_state, sol::protected_function_result res) -> sol::protected_function_result {
		                             sol::error e = res;
		                             fmt::println(fmt::runtime(e.what()));
		                             return res;
	                             });

	if (!script_res.valid()) {
		aether::util::cleaner::cleanup();
		return -1;
	}

	apple_intercept a;
	aether::apple* ptr = &a;
	ptr->banana();
	fmt::println("{}", ptr->mango());
	ptr->truffle("mnemonic", 16);
	ptr->toffee();

	aether::cherry b;
	b.banana();

	aether::util::cleaner::cleanup();
	return 0;
}