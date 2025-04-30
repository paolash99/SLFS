#pragma once
#ifndef VERSION_HPP__
#define VERSION_HPP__

#include <chrono>
#include <atomic>

namespace slsfsdf
{

auto version() -> std::uint32_t
{
    static std::atomic<char> counter = 0;

    auto const now = std::chrono::system_clock::now();
    auto epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    static decltype(epoch) lastgen;

    if (epoch == lastgen)
        counter++;
    else
    {
        counter = 0;
        lastgen = epoch;
    }

    // assume higher bits are discarded
    std::uint32_t value = static_cast<std::uint32_t>(epoch);
    value = (value << (sizeof(counter) * 8)) | counter;
    return value;
}

}// namespace slsfsdf

#endif // VERSION_HPP__
