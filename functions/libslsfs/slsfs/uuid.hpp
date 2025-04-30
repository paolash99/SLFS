#pragma once

#ifndef UUID_HPP__
#define UUID_HPP__

#include "serializer.hpp"

#include <Poco/Crypto/DigestEngine.h>
#include <Poco/Base64Encoder.h>
#include <Poco/Base64Decoder.h>

#include <sstream>
#include <random>
#include <vector>

namespace slsfs::uuid
{

struct uuid : public pack::key_t
{
    template<typename CharType = char>
    auto to_vector() const -> std::vector<CharType>
    {
        std::vector<CharType> v;
        v.reserve(pack::key_t::size());

        std::copy(pack::key_t::begin(), pack::key_t::end(), std::back_inserter(v));
        return v;
    }

    auto encode_base64() const -> std::string
    {
        std::stringstream ss;
        Poco::Base64Encoder encoder{ss};
        std::for_each(pack::key_t::begin(), pack::key_t::end(),
                      [&encoder] (auto c) {
                          static_assert(sizeof(c) == sizeof(char));
                          encoder << c;
                      });
        encoder.close();
        std::string raw_encode = ss.str();
        for (char& c : raw_encode)
            if (c == '/')
                c = '_';
        return raw_encode;
    }

    auto short_hash() const -> std::string
    {
        std::string const base64 = encode_base64();
        return base64.substr(0, 6);
    }
};

template <typename RangeType>
struct hash_compare
{
    static
    auto hash (RangeType const& id) -> std::size_t
    {
        std::size_t seed = 0x1b873594;
        pack::hash::range(seed, id.begin(), id.end());
        return seed;
    }

    static
    bool equal (RangeType const& lhs, RangeType const& rhs) {
        return lhs == rhs;
    }
};

inline
auto to_uuid(pack::key_t &key) -> uuid& {
    return *static_cast<uuid*>(&key);
}

inline
auto to_uuid(pack::key_t const &key) -> uuid const & {
    return *static_cast<uuid const*>(&key);
}

uuid get_uuid(std::string const& buffer)
{
    uuid id;
    Poco::Crypto::DigestEngine engine{"SHA256"};
    engine.update(buffer.data(), buffer.size());
    Poco::DigestEngine::Digest const& digest = engine.digest();

    std::copy(digest.begin(), digest.end(), id.begin());
    //std::cout << Poco::DigestEngine::digestToHex(digest) << "\n";
    return id;
}

uuid gen_uuid_static_seed(std::string const& any)
{
    static std::mt19937 rng;
    uuid id;
    Poco::Crypto::DigestEngine engine{"SHA256"};

    int sum = std::accumulate(any.begin(), any.end(), 0);
    rng.seed(sum);
    engine.update(any.data(), any.size());

    Poco::DigestEngine::Digest const& digest = engine.digest();

    std::copy(digest.begin(), digest.end(), id.begin());
//    std::cout << Poco::DigestEngine::digestToHex(digest) << "\n";
    return id;
}

uuid gen_uuid()
{
    static std::mt19937 rng;
    uuid id;
    std::random_device rd;
    Poco::Crypto::DigestEngine engine{"SHA256"};

    rng.seed(rd());
    int const r1 = rng();
    engine.update(&r1, sizeof(r1));

    rng.seed(rd());
    int const r2 = rng();
    engine.update(&r2, sizeof(r2));

    Poco::DigestEngine::Digest const& digest = engine.digest();

    std::copy(digest.begin(), digest.end(), id.begin());
//    std::cout << Poco::DigestEngine::digestToHex(digest) << "\n";
    return id;
}

auto up_cast(pack::key_t const& key) -> uuid const& {
    return *static_cast<uuid const*>(&key);
}

auto encode_base64(pack::key_t const& key) -> std::string {
    return up_cast(key).encode_base64();
}

auto decode_base64(std::string& base64str) -> uuid
{
    std::stringstream ss;
    for (char& c : base64str)
        if (c == '_')
            c = '/';

    ss << base64str;
    Poco::Base64Decoder decoder {ss};

    uuid id;
    std::copy(std::istreambuf_iterator<char>(decoder),
              std::istreambuf_iterator<char>(),
              id.begin());
    return id;
}

auto operator << (std::ostream &os, uuid const& id) -> std::ostream&
{
    os << id.encode_base64();
    return os;
}

auto hash(uuid const& k) -> std::size_t
{
    std::size_t seed = 0x1b873593;
    pack::hash::range(seed, k.begin(), k.end());
    return seed;
}

int gen_rand_number()
{
    static thread_local std::mt19937 rng(std::random_device{}());
    static thread_local std::uniform_int_distribution<int> dist(0, 9999);

    return dist(rng);
}

} // namespace uuid
#endif // UUID_HPP__
