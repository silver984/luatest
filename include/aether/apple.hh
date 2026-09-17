#pragma once
#include <aether/intercept.hh>

#include <string_view>

namespace aether::_lua_impl {
class intercepted_apple_;
}

namespace aether {

class apple {
public:
	virtual void banana();
	[[nodiscard]] virtual int mango();
	virtual void truffle(std::string_view str, int val);
	virtual void toffee();

	int monkey            = 16;
	using _intercept_type = _lua_impl::intercepted_apple_;

private:
	int value_ = 32;
};

} // namespace aether

namespace aether::_lua_impl {

struct intercepted_apple_ : intercepted_v_<apple> {
	void banana() override;
	[[nodiscard]] int mango() override;
	void truffle(std::string_view, int) override;
	void toffee() override;
};

} // namespace aether::_lua_impl