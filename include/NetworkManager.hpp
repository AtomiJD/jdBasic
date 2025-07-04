// NetworkManager.hpp
#pragma once
#ifdef HTTP
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib.h> // Include httplib here

#include <string>
#include <vector>
#include <map>

// Forward declare NeReLaBasic so we can pass it to methods if needed
class NeReLaBasic;

class NetworkManager {
public:
    NetworkManager(); // Constructor
    ~NetworkManager(); // Destructor to clean up

    //std::unique_ptr<httplib::Client> active_http_client;

    // Stores the HTTP status code of the last request
    int last_http_status_code;

    // Stores custom headers to be sent with subsequent requests
    std::map<std::string, std::string> custom_headers;

    // --- HTTP Client Functions ---
    // Performs an HTTP GET request
    std::string httpGet(const std::string& url);

    // Performs an HTTP POST request
    std::string httpPost(const std::string& url, const std::string& body, const std::string& content_type);

    // Performs an HTTP PUT request
    std::string httpPut(const std::string& url, const std::string& body, const std::string& content_type);

    // Sets a custom header for subsequent requests
    void setHeader(const std::string& name, const std::string& value);

    // Clears all custom headers
    void clearHeaders();

    // --- TODO: Add HTTP Server functions here later ---
};
#endif
