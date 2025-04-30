
#pragma once
#ifndef OPENER_HPP__
#define OPENER_HPP__

#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <string>
#include <cstring>

namespace slsfs
{

constexpr uint16_t kBindingRequest = 0x0001;
constexpr uint16_t kBindingResponse = 0x0101;
constexpr uint16_t kXorMappedAddress = 0x0020;

// https://datatracker.ietf.org/doc/html/rfc5389#section-6
struct __attribute__((packed)) StunRequest
{
    StunRequest()
    {
        for (int i = 0; i < transaction_id_.size(); i++) {
            transaction_id_[i] = rand() % 256;
        }
    }
    const int16_t stun_message_type_ = htons(kBindingRequest);
    const int16_t message_length_ = htons(0x0000);
    const int32_t magic_cookie_ = htonl(0x2112A442);
    std::array<uint8_t, 12> transaction_id_;
};

// https://datatracker.ietf.org/doc/html/rfc5389#section-7
struct __attribute__((packed)) StunResponse
{
    int16_t stun_message_type_;
    int16_t message_length_;
    int32_t magic_cookie_;
    std::array<uint8_t, 12> transaction_id_;
    std::array<uint8_t, 1000> attributes_;
};

struct IPAndPort
{
    std::string ip_;
    int16_t port_ = 0;
};

// Converts a host name into an IP address.
std::string HostnameToIP(std::string const& hostname)
{
    char ip[100];
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_in* h;
    int rv;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(hostname.c_str(), "http", &hints, &servinfo)) != 0)
        return "";

    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        h = (struct sockaddr_in*)p->ai_addr;
        strcpy(ip, inet_ntoa(h->sin_addr));
    }
    freeaddrinfo(servinfo);
    return std::string(ip);
}

// Returns true on success
bool PerformStunRequest(std::string const& stun_server_ip,
                        short stun_server_port,
                        short local_port,
                        IPAndPort& ip_and_port) {
    struct sockaddr_in servaddr;
    struct sockaddr_in localaddr;

    StunRequest stun_request;
    StunResponse stun_response;

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    inet_pton(AF_INET, stun_server_ip.c_str(), &servaddr.sin_addr);
    servaddr.sin_port = htons(stun_server_port);

    bzero(&localaddr, sizeof(localaddr));
    localaddr.sin_family = AF_INET;
    localaddr.sin_port = htons(local_port);

    int err = bind(sockfd, (struct sockaddr*)&localaddr, sizeof(localaddr));
    if (err < 0)
    {
        printf("bind error\n");
        return false;
    }

    printf("Sending STUN request to %s:%d\n", stun_server_ip.c_str(),
           stun_server_port);
    err = sendto(sockfd, &stun_request, sizeof(stun_request), 0,
                 (struct sockaddr*)&servaddr, sizeof(servaddr));
    if (err < 0)
    {
        printf("sendto error\n");
        return false;
    }

    err = recvfrom(sockfd, &stun_response, sizeof(stun_response), 0, NULL, 0);
    if (err < 0) {
        printf("recvfrom error\n");
        return false;
    }

    bool succesfully_received_response = false;

    if (stun_response.transaction_id_ == stun_request.transaction_id_)
    {
        if (stun_response.stun_message_type_ != htons(kBindingResponse))
        {
            printf("incorrect message type\n");
            return false;
        }
        auto const& attributes = stun_response.attributes_;
        int16_t attributes_length =
            std::min<int16_t>(htons(stun_response.message_length_),
                              stun_response.attributes_.size());
        int i = 0;
        while (i < attributes_length)
        {
            auto attribute_type = htons(*(int16_t*)(&attributes[i]));
            auto attribute_length = htons(*(int16_t*)(&attributes[i + 2]));
            if (attribute_type == kXorMappedAddress) {
                int16_t port = ntohs(*(int16_t*)(&attributes[i + 6]));
                port ^= 0x2112;
                std::string ip = std::to_string(attributes[i + 8] ^ 0x21) + "." +
                    std::to_string(attributes[i + 9] ^ 0x12) + "." +
                    std::to_string(attributes[i + 10] ^ 0xA4) + "." +
                    std::to_string(attributes[i + 11] ^ 0x42) + ".";
                ip_and_port.ip_ = ip;
                ip_and_port.port_ = port;
                succesfully_received_response = true;
                break;
            }
            i += (4 + attribute_length);
        }
    }
    else
    {
        printf("incorrect transaction id\n");
        return false;
    }
    close(sockfd);
    return true;
}


} // namespace

#endif // OPENER_HPP__
