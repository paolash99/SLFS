#pragma once
#ifndef SLSFS_HTTP_VERB_HPP__
#define SLSFS_HTTP_VERB_HPP__

#include "scope-exit.hpp"

#include <curl/curl.h>

namespace slsfs::httpdo
{

void httpinit()
{
    static bool inited = false;
    if (not inited)
        curl_global_init(CURL_GLOBAL_ALL);
}

void httpclienup()
{
    curl_global_cleanup();
}

void put(char const* url, char const * data, std::size_t datasize)
{
    httpinit();
    struct curl_slist *headers  = nullptr;
    SCOPE_DEFER([headers]{ curl_slist_free_all(headers); });
    headers = curl_slist_append(headers, "Authorization: Basic Nzg5YzQ2YjEtNzFmNi00ZWQ1LThjNTQtODE2YWE0ZjhjNTAyOmFiY3pPM3haQ0xyTU42djJCS0sxZFhZRnBYbFBrY2NPRnFtMTJDZEFzTWdSVTRWck5aOWx5R1ZDR3VNREdJd1A=");
    headers = curl_slist_append(headers, "Content-Type: application/json");

    CURL * hnd = curl_easy_init();
    SCOPE_DEFER([hnd] { curl_easy_cleanup(hnd); });

    curl_easy_setopt(hnd, CURLOPT_BUFFERSIZE, 102400L);
    curl_easy_setopt(hnd, CURLOPT_URL, url);
    curl_easy_setopt(hnd, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(hnd, CURLOPT_POSTFIELDS, data);

    curl_easy_setopt(hnd, CURLOPT_POSTFIELDSIZE_LARGE, datasize);
    curl_easy_setopt(hnd, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(hnd, CURLOPT_USERAGENT, "curl/7.68.0");
    curl_easy_setopt(hnd, CURLOPT_MAXREDIRS, 50L);
    curl_easy_setopt(hnd, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_2TLS));
    curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(hnd, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(hnd, CURLOPT_FTP_SKIP_PASV_IP, 1L);
    curl_easy_setopt(hnd, CURLOPT_TCP_KEEPALIVE, 1L);

    // CURLcode ret =
    curl_easy_perform(hnd);
}

void put(std::string const& url, std::string const& data)
{
    put(url.c_str(), data.c_str(), data.size());
}

void post(char const* url, char const * data, std::size_t datasize)
{
    httpinit();

    struct curl_slist *headers  = nullptr;
    SCOPE_DEFER([headers]{ curl_slist_free_all(headers); });
    headers = curl_slist_append(headers, "Authorization: Basic Nzg5YzQ2YjEtNzFmNi00ZWQ1LThjNTQtODE2YWE0ZjhjNTAyOmFiY3pPM3haQ0xyTU42djJCS0sxZFhZRnBYbFBrY2NPRnFtMTJDZEFzTWdSVTRWck5aOWx5R1ZDR3VNREdJd1A=");
    headers = curl_slist_append(headers, "Content-Type: application/json");

    CURL * hnd = curl_easy_init();
    SCOPE_DEFER([hnd] { curl_easy_cleanup(hnd); });

    curl_easy_setopt(hnd, CURLOPT_BUFFERSIZE, 102400L);
    curl_easy_setopt(hnd, CURLOPT_URL, url);
    curl_easy_setopt(hnd, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(hnd, CURLOPT_POSTFIELDS, data);

    curl_easy_setopt(hnd, CURLOPT_POSTFIELDSIZE_LARGE, datasize);
    curl_easy_setopt(hnd, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(hnd, CURLOPT_USERAGENT, "curl/7.68.0");
    curl_easy_setopt(hnd, CURLOPT_MAXREDIRS, 50L);
    curl_easy_setopt(hnd, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_2TLS));
    curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(hnd, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(hnd, CURLOPT_FTP_SKIP_PASV_IP, 1L);
    curl_easy_setopt(hnd, CURLOPT_TCP_KEEPALIVE, 1L);

    curl_easy_perform(hnd);
}

void post(std::string const& url, std::string const& data)
{
    post(url.c_str(), data.c_str(), data.size());
}

void logget(char const* url, char const * data, std::size_t /*datasize*/)
{
    using namespace std::literals;
    httpinit();
    struct curl_slist *headers  = nullptr;
    SCOPE_DEFER([headers]{ curl_slist_free_all(headers); });
    std::string const d = "log: "s + data;
    headers = curl_slist_append(headers, d.c_str());

    CURL * hnd = curl_easy_init();
    SCOPE_DEFER([hnd] { curl_easy_cleanup(hnd); });

    curl_easy_setopt(hnd, CURLOPT_URL, url);
    curl_easy_setopt(hnd, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(hnd, CURLOPT_CUSTOMREQUEST, "GET");
    curl_easy_setopt(hnd, CURLOPT_TCP_KEEPALIVE, 1L);

    curl_easy_perform(hnd);
}

void logget(std::string const& url, std::string const& data)
{
    logget(url.c_str(), data.c_str(), data.size());
}


} // namespace httpdo

#endif // SLSFS_HTTP_VERB_HPP__
