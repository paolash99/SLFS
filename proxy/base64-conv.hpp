#pragma once
#ifndef BASE64_CONV_HPP__
#define BASE64_CONV_HPP__

#include <Poco/Crypto/DigestEngine.h>
#include <Poco/Base64Encoder.h>
#include <Poco/Base64Decoder.h>

namespace slsfs::base64
{

template<typename InputIterator>
auto encode (InputIterator begin, InputIterator end) -> std::string
{
    std::stringstream ss;
    Poco::Base64Encoder encoder {ss};
    std::for_each(begin, end,
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

template<typename OutputIterator>
void decode (std::string& base64str, OutputIterator out)
{
    std::stringstream ss;
    for (char& c : base64str)
        if (c == '_')
            c = '/';

    ss << base64str;
    Poco::Base64Decoder decoder {ss};

    std::copy(std::istreambuf_iterator<char>(decoder),
              std::istreambuf_iterator<char>(),
              out);
}

} // namespace slsfs::base64

#endif // BASE64_CONV_HPP__
