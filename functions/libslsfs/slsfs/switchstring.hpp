#pragma once
#ifndef SLSFS_SWITCHSTRING_HPP__
#define SLSFS_SWITCHSTRING_HPP__

namespace slsfs::sswitch
{

using lli = long long int;

constexpr inline
lli hash(char const * str, int h = 0) {
    return (!str[h] ? 5381 : (hash(str, h+1)*33) ^ str[h] ) % (2147483647);
}

constexpr inline
lli operator "" _(char const * p, std::size_t) { return hash(p); }

inline
lli hash(std::string const & s) { return hash (s.c_str()); }

} // namespace sswitch


#endif // SLSFS_SWITCHSTRING_HPP__
