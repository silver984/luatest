#pragma once
#include <aether/cleaner.hh>

#include <fmt/color.h>
#include <fmt/format.h>

#include <sol/sol.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace aether::_lua_impl {

// wip
struct detour_ final {
	sol::table owner;
	sol::protected_function fn;
	int priority;
	bool enabled;
};

template <typename T_, size_t N_>
class blind_array_ final {
public:
	void try_push(T_ const& val) {
		if (is_full()) {
			throw std::runtime_error("overflow");
		}
		data_[size_++] = val;
	}

	[[nodiscard]] bool is_full() const { return size_ >= N_; }
	[[nodiscard]] size_t size() const { return size_; }
	[[nodiscard]] T_* at(size_t i) { return i < size_ ? &data_[i] : nullptr; }

private:
	T_ data_[N_];
	size_t size_ = 0;
};

struct hook_ final {
	static constexpr size_t MAX_DETOURS = 32;
	blind_array_<detour_, MAX_DETOURS> detours;
};

struct string_hash_ final {
	using is_transparent = void;
	using hash_type      = std::hash<std::string_view>;
	size_t operator()(std::string_view str) const { return hash_type{}(str); }
	size_t operator()(std::string const& str) const { return hash_type{}(str); }
	size_t operator()(char const* str) const { return hash_type{}(str); }
};

template <typename T>
using string_map_ = std::unordered_map<std::string, T, string_hash_, std::equal_to<>>;

template <typename T_>
concept interceptable_ = requires { typename T_::_intercept_type; };

template <typename Interception_, interceptable_ Base_>
        requires std::same_as<Interception_, typename Base_::_intercept_type>
struct intercepted_ : public Base_ {
	using Base_::Base_;

	template <typename Fn, typename... Args>
	auto divert(std::string_view lua_key, Fn dest, Args... args) {
		Interception_* self = static_cast<Interception_*>(this);
		auto it             = hooks.find(lua_key);

		if (it == hooks.end()) {
			return std::invoke(dest, self, args...);
		}

		auto& detours = it->second.detours;
		using ret     = std::invoke_result_t<Fn, Interception_*, Args...>;

		auto chain = [&](auto& inner_chain, Args... chain_args, size_t i = 0) -> ret {
			detour_* detour = detours.at(i);

			if (!detour) {
				return std::invoke(dest, self, chain_args...);
			}

			auto super = [&](Args... super_args) -> ret {
				return inner_chain(inner_chain, super_args..., i + 1);
			};
			sol::protected_function_result res = detour->fn(self, sol::as_function(super), chain_args...);

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

	static void cleanup() { hooks.clear(); }
	static inline string_map_<hook_> hooks;
};

template <_lua_impl::interceptable_ T_>
using intercepted_v_ = _lua_impl::intercepted_<typename T_::_intercept_type, T_>;

} // namespace aether::_lua_impl

namespace aether {

template <_lua_impl::interceptable_ T>
using intercept_t = typename T::_intercept_type;

template <_lua_impl::interceptable_ T_, typename... Args_>
[[nodiscard]] sol::usertype<intercept_t<T_>> new_intercepted_lua_usertype(sol::state_view lua, std::string_view name, Args_&&... args) {
	sol::usertype<intercept_t<T_>> out = lua.new_usertype<intercept_t<T_>>(name, std::forward<Args_>(args)...);

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

			auto [it, _]  = intercept_t<T_>::hooks.try_emplace(key_str);
			auto& detours = it->second.detours;

			try {
				detours.try_push({
				        .owner    = t,
				        .fn       = val.as<sol::protected_function>(),
				        .priority = (int)detours.size(),
				        .enabled  = true,
				});
			} catch (std::runtime_error const& e) {
				fmt::print(fg(fmt::color::red), "failed to register function hook for \"{}:{}\" | what: {}\n", name,
				           key_str, e.what());
				return;
			}

			util::cleaner::schedule_for_cleanup(&intercept_t<T_>::cleanup);
			fmt::print(fg(fmt::color::dark_gray), "registered function hook for \"{}:{}\" | table: {} | detour: {}\n", name,
			           key_str, t.pointer(), val.pointer());
		};

		table[sol::metatable_key] = metatable;
		return table;
	};

	return out;
};

} // namespace aether