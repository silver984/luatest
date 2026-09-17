#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <fmt/color.h>
#include <fmt/core.h>

#include <sol/sol.hpp>

namespace aether::_detail {

struct string_hash_ final {
	using is_transparent = void;
	using hash_type      = std::hash<std::string_view>;
	size_t operator()(std::string_view str) const { return hash_type{}(str); }
	size_t operator()(std::string const& str) const { return hash_type{}(str); }
	size_t operator()(char const* str) const { return hash_type{}(str); }
};

class intercepted_apple_;

template <typename T_>
concept interceptable_        = requires { typename T_::intercept_type; };
constexpr size_t MAX_DETOURS_ = 32;

template <typename T_, size_t N_>
class fools_vector_ final {
public:
	void push(T_ const& val) {
		if (!is_full()) {
			data_[size_++] = val;
		}
	}

	[[nodiscard]] bool is_full() const { return size_ >= N_; }
	[[nodiscard]] size_t size() const { return size_; }
	[[nodiscard]] T_* at(size_t i) { return i < size_ ? &data_[i] : nullptr; }

private:
	std::array<T_, N_> data_;
	size_t size_ = 0;
};

class cleaner_ final {
public:
	cleaner_() = delete;

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

} // namespace aether::_detail

namespace aether {

template <typename T>
using string_map = std::unordered_map<std::string, T, _detail::string_hash_, std::equal_to<>>;

class apple {
public:
	virtual void banana() { fmt::println("hello from apple::banana"); }
	[[nodiscard]] virtual int mango() { return value_; }
	using intercept_type = _detail::intercepted_apple_;

private:
	int value_ = 32;
};

// wip
struct lua_detour final {
	sol::table owner;
	sol::protected_function fn;
	int priority;
	bool enabled;
};

struct lua_hook final {
	_detail::fools_vector_<lua_detour, _detail::MAX_DETOURS_> detours;
};

template <_detail::interceptable_ T_>
[[nodiscard]] sol::usertype<typename T_::intercept_type> new_intercepted_usertype(sol::state_view lua, std::string_view name) {
	using intercept_type              = typename T_::intercept_type;
	sol::usertype<intercept_type> out = lua.new_usertype<intercept_type>(name);

	out["modify"] = [name](sol::this_state s) -> sol::table {
		sol::state_view lua_v(s);
		sol::table table     = lua_v.create_table();
		sol::table metatable = lua_v.create_table();

		metatable[sol::meta_function::new_index] = [name](sol::this_state s, sol::table t, sol::object key,
		                                                  sol::object val) -> void {
			t.raw_set(key, val);

			if (key.get_type() != sol::type::string || val.get_type() != sol::type::function) {
				return;
			}

			std::string key_str = key.as<std::string>();

			{
				sol::object maybe_bound = sol::state_view(s)["apple"][key_str];
				if (!maybe_bound.valid() || maybe_bound.get_type() != sol::type::function) {
					return;
				}
			}

			auto [it, _]  = intercept_type::hooks_.try_emplace(key_str);
			auto& detours = it->second.detours;

			if (detours.is_full()) {
				fmt::print(fg(fmt::color::red), "failed to register function hook for \"{}:{}\" due to overflow\n", name,
				           key_str);
				return;
			}

			detours.push({
			        .owner    = t,
			        .fn       = val.as<sol::protected_function>(),
			        .priority = (int)detours.size(),
			        .enabled  = true,
			});

			_detail::cleaner_::schedule_for_cleanup(&intercept_type::cleanup_);
			fmt::print(fg(fmt::color::dark_gray), "registered function hook for \"{}:{}\" | table: {} | detour: {}\n", name,
			           key_str, t.pointer(), val.pointer());
		};

		table[sol::metatable_key] = metatable;
		return table;
	};

	return out;
};

} // namespace aether

namespace aether::_detail {

// basically just a wrapper over a lambda. this was created because sol2 seems to produce undefined behavior for lua super calls. im not
// sure why this ever happens, but it seems to happen on clang only >_>
template <typename T_>
class invokable final {
public:
	explicit invokable(T_ fn)
	        : fn_(fn) {}
	[[nodiscard]] T_ get() const { return fn_; }
	template <typename... Args>
	auto operator()(Args&&... args) {
		return fn_(std::forward<Args>(args)...);
	}

private:
	T_ fn_;
};

template <_detail::interceptable_ Base_>
struct intercepted_ : public Base_ {
	template <typename Fn, typename... Args>
	auto divert_(std::string_view lua_key, Fn dest, Args&&... args) {
		Base_* self = this;
		auto it     = hooks_.find(lua_key);

		if (it == hooks_.end()) {
			return std::invoke(dest, self, std::forward<Args>(args)...);
		}

		auto& detours = it->second.detours;
		using ret     = std::invoke_result_t<Fn, Base_*, Args&&...>;

		auto chain = [&](auto& inner_chain, Args&&... chain_args, size_t i = 0) -> ret {
			lua_detour* detour = detours.at(i);

			if (!detour) {
				return std::invoke(dest, self, std::forward<Args>(chain_args)...);
			}

			invokable super([&](Args&&... super_args) -> ret {
				return inner_chain(inner_chain, std::forward<Args>(super_args)..., i + 1);
			});

			sol::protected_function_result res = detour->fn(self, super.get(), std::forward<Args>(chain_args)...);

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

	static void cleanup_() { hooks_.clear(); }
	static inline string_map<lua_hook> hooks_;
};

struct intercepted_apple_ final : intercepted_<apple> {
	// todo: profile how long diversions take

	void banana() final override {
		return this->divert_("banana", [](apple* s) -> void {
			return s->apple::banana();
		});
	}

	[[nodiscard]] int mango() final override {
		return this->divert_("mango", [](apple* s) -> int {
			return s->apple::mango();
		});
	}
};

} // namespace aether::_detail

int main() {
	sol::state lua;

	using enum sol::lib;
	lua.open_libraries(base, string, table, math, utf8);

	using apple_intercept = aether::apple::intercept_type;
	using cleaner         = aether::_detail::cleaner_;

	sol::usertype<apple_intercept> apple_ut = aether::new_intercepted_usertype<aether::apple>(lua, "apple");
	apple_ut[sol::meta_function::construct] = sol::constructors<apple_intercept()>();
	apple_ut["banana"]                      = &apple_intercept::banana;
	apple_ut["mango"]                       = &apple_intercept::mango;

	sol::protected_function_result script_res =
	        lua.safe_script_file("test.lua", sol::environment(lua, sol::create, lua.globals()),
	                             [](sol::this_state, sol::protected_function_result res) -> sol::protected_function_result {
		                             sol::error e = res;
		                             fmt::println(fmt::runtime(e.what()));
		                             return res;
	                             });

	if (!script_res.valid()) {
		cleaner::cleanup();
		return -1;
	}

	// test
	apple_intercept a;
	aether::apple* ptr = &a;
	ptr->banana();
	fmt::println("{}", ptr->mango());

	cleaner::cleanup();
	return 0;
}