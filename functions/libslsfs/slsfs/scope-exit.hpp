#pragma once

#ifndef SCOPE_EXIT_HPP__
#define SCOPE_EXIT_HPP__

#include <functional>

#define BASIC_SCOPE_EXIT_CONCAT_IMPL(x, y) x##y
#define BASIC_SCOPE_EXIT_CONCAT(x, y) BASIC_SCOPE_EXIT_CONCAT_IMPL(x, y)
#define SCOPE_DEFER ::basic_scope_exit::scope_exit BASIC_SCOPE_EXIT_CONCAT(SCOPE_EXIT_UNIQUE_VAR_, __LINE__) = \
        ::basic_scope_exit::make_scope_exit
// usage:
// int * p = new int;
// SCOPE_DEFER([&p]{ delete p; });

namespace basic_scope_exit
{
    template <typename Callable>
    class scope_exit
    {
        Callable c_;
    public:
        scope_exit(Callable && func): c_{std::forward<Callable>(func)} {}
        ~scope_exit() { std::invoke(c_); }
    };

    template <typename Callable>
    auto make_scope_exit(Callable && f) -> scope_exit<Callable>
    {
        return scope_exit<Callable>(std::forward<Callable>(f));
    }
} // namespace basic_scope_exit

#endif // SCOPE_EXIT_HPP__
