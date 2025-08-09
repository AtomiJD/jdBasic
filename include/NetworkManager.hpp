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
struct ServerRequestEvent;

// Represents a request received by the server, to be processed by the BASIC VM
struct ServerRequestEvent {
    // Request data
    std::string method;
    std::string path;
    std::string body;
    httplib::Headers headers;

    // Response data (to be filled in by the main BASIC thread)
    int status_code = 500; // Default to Internal Server Error
    std::string response_body;
    std::string content_type = "text/plain";

    // Synchronization primitives
    std::shared_ptr<std::condition_variable> cv;
    std::shared_ptr<std::mutex> mtx;
    bool handled = false;
};

class NetworkManager {
public:
    NetworkManager(NeReLaBasic& vm); // Constructor
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

    // --- HTTP Server Functions ---
    bool startServer(int port);
    void stopServer();
    void registerServerRoute(const std::string& method, const std::string& path, const std::string& function_name);
    std::string getRouteHandler(const std::string& method, const std::string& path);
    bool isServerRunning() const;


    NeReLaBasic& vm_ref;
    std::unique_ptr<httplib::Server> http_server;
    std::unique_ptr<std::thread> server_thread;

    // Maps a server path (e.g., "/api/users") to a BASIC function name
    std::map<std::string, std::string> get_routes;
    std::map<std::string, std::string> post_routes;
    std::mutex route_mutex; // To protect access to route maps

    void setup_server_routes();
    void queue_request_for_vm(const httplib::Request& req, httplib::Response& res);

};
#endif
