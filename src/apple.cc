#include <aether/apple.hh>

#include <fmt/format.h>

namespace aether {

void apple::banana() { fmt::println("hello from apple::banana"); }

int apple::mango() {
	fmt::println("hello from apple::mango");
	return value_;
}

void apple::truffle(std::string_view str, int val) { fmt::println("hello from apple::mango | received: {}, {}", str, val); }
void apple::toffee() { fmt::println("hello from apple::toffee | monkey: {}", monkey); }

} // namespace aether

namespace aether::_lua_impl {

void intercepted_apple_::banana() {
	this->divert("banana", [](intercepted_apple_* s) -> void {
		s->apple::banana();
	});
}

int intercepted_apple_::mango() {
	return this->divert("mango", [](intercepted_apple_* s) -> int {
		return s->apple::mango();
	});
}

void intercepted_apple_::truffle(std::string_view a0, int a1) {
	this->divert(
	        "truffle",
	        [](intercepted_apple_* s, std::string_view la0, int la1) -> void {
		        s->apple::truffle(la0, la1);
	        },
	        a0, a1);
}

void intercepted_apple_::toffee() {
	this->divert("toffee", [](intercepted_apple_* s) -> void {
		s->apple::toffee();
	});
}

} // namespace aether::_lua_impl
