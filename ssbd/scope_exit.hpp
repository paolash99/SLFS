#pragma once

#ifndef SCOPE_EXIT_HPP__
#define SCOPE_EXIT_HPP__

#include <functional>

#define BASIC_SCOPE_EXIT_CONCAT_IMPL(x, y) x##y
#define BASIC_SCOPE_EXIT_CONCAT(x, y) BASIC_SCOPE_EXIT_CONCAT_IMPL(x, y)
#define SCOPE_DEFER ::basic::scope_exit BASIC_SCOPE_EXIT_CONCAT(UNIQUE_VAR_, __LINE__) = \
        ::basic::make_scope_exit


namespace basic
{

template <typename Callable>
class scope_exit
{
    Callable c_;
public:
    scope_exit(Callable && func): c_{std::forward<Callable>(func)} {}

    // no std::invoke here because of c++11
    ~scope_exit() { std::invoke(c_); }
};

template <typename Callable>
auto make_scope_exit(Callable && f) -> scope_exit<Callable>
{
    return scope_exit<Callable>(std::forward<Callable>(f));
}

} // namespace mqueue

#endif // SCOPE_EXIT_HPP__
