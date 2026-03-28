// NetworkManager.cpp
#include "AppConfig.hpp"
#ifdef HTTP
#include "NetworkManager.hpp"
#include "Error.hpp"
#include <iostream>
#include <chrono>

// --- FIX: Include the full header for NeReLaBasic here ---
#include "NeReLaBasic.hpp" 
#include "Types.hpp" 

NetworkManager::NetworkManager(NeReLaBasic& vm) : vm_ref(vm), last_http_status_code(0) {
    // You might want to do some global initialization here if httplib needed it,
    // but for basic usage, it often doesn't.
}

NetworkManager::~NetworkManager() {
    // Clean up any persistent resources if necessary
    stopServer();
}

namespace { // Keep this helper private to this file
    std::string httpPostOrPut(const std::string& verb, const std::string& url_str, const std::string& body, const std::string& content_type, std::map<std::string, std::string>& custom_headers, int& last_http_status_code) {
        last_http_status_code = 0;

        // URL Parsing logic from your httpGet - no changes needed here
        std::regex url_regex(R"(([^:]+)://([^:/]+)(?::(\d+))?(/.*)?)");
        std::smatch matches;
        std::string protocol = "http", host, path = "/";
        int port = 0;

        if (std::regex_match(url_str, matches, url_regex)) {
            if (matches[1].matched) protocol = matches[1].str();
            if (matches[2].matched) host = matches[2].str();
            if (matches[3].matched) port = std::stoi(matches[3].str());
            if (matches[4].matched) path = matches[4].str();
        }
        else {
            host = url_str; // Fallback
        }

        if (port == 0) port = (protocol == "https") ? 443 : 80;

        // Client setup - no changes needed here
        std::unique_ptr<httplib::SSLClient> cli;
        std::unique_ptr<httplib::Client> cli_http;

        if (protocol == "https") {
            cli = std::make_unique<httplib::SSLClient>(host.c_str(), port);
        }
        else {
            cli_http = std::make_unique<httplib::Client>(host.c_str(), port);
        }

        if (cli) {
            cli->set_connection_timeout(10); // Increased timeout for potentially larger payloads
            cli->set_read_timeout(120);
            cli->set_write_timeout(10);

            httplib::Headers headers;
            for (const auto& pair : custom_headers) {
                headers.emplace(pair.first, pair.second);
            }

            // Use the verb to decide which httplib method to call
            httplib::Result res;
            if (verb == "POST") {
                res = cli->Post(path.c_str(), headers, body, content_type.c_str());
            }
            else if (verb == "PUT") {
                res = cli->Put(path.c_str(), headers, body, content_type.c_str());
            }

            if (res) {
                last_http_status_code = res->status;
                if (res->status >= 400) {
                    // Return the error body from the server for better debugging
                    return "Server error " + std::to_string(res->status) + ": " + res->body;
                }
                return res->body;
            }
            else {
                last_http_status_code = -1;
                return "HTTP request failed: " + httplib::to_string(res.error());
            }
        } else if (cli_http) {
                cli_http->set_connection_timeout(10); // Increased timeout for potentially larger payloads
                cli_http->set_read_timeout(120);
                cli_http->set_write_timeout(10);

                httplib::Headers headers;
                for (const auto& pair : custom_headers) {
                    headers.emplace(pair.first, pair.second);
                }
                // Use the verb to decide which httplib method to call
                httplib::Result res;
                if (verb == "POST") {
                    res = cli_http->Post(path.c_str(), headers, body, content_type.c_str());
                }
                else if (verb == "PUT") {
                    res = cli_http->Put(path.c_str(), headers, body, content_type.c_str());
                }

                if (res) {
                    last_http_status_code = res->status;
                    if (res->status >= 400) {
                        // Return the error body from the server for better debugging
                        return "Server error " + std::to_string(res->status) + ": " + res->body;
                    }
                    return res->body;
                }
                else {
                    last_http_status_code = -1;
                    return "HTTP request failed: " + httplib::to_string(res.error());
                }
        } else {
            last_http_status_code = -1;
            return "Error: Client creation failed.";
        }
    }
} // end anonymous namespace

std::string NetworkManager::getRouteHandler(const std::string& method, const std::string& path) {
    std::lock_guard<std::mutex> lock(route_mutex);
    const auto& route_map = (method == "GET") ? get_routes : post_routes;
    auto it = route_map.find(path);
    if (it != route_map.end()) {
        return it->second;
    }
    return "";
}

std::string NetworkManager::httpGet(const std::string& url_str) {
    last_http_status_code = 0; // Reset status

    // --- More Robust URL Parsing ---
    // Use std::regex to properly separate protocol, host, port, and path.
    // Example URL: https://www.example.com:8443/path/to/resource?query=1
    // Group 1: Protocol (e.g., "https")
    // Group 2: Host (e.g., "www.example.com")
    // Group 3: Port (e.g., ":8443" or empty if default)
    // Group 4: Path (e.g., "/path/to/resource?query=1" or empty if "/")
    std::regex url_regex(R"(([^:]+)://([^:/]+)(?::(\d+))?(/.*)?)");
    std::smatch matches;

    std::string protocol = "http"; // Default protocol
    std::string host;
    int port = 0; // 0 means default port
    std::string path = "/";

    if (std::regex_match(url_str, matches, url_regex)) {
        if (matches[1].matched) { // Protocol
            protocol = matches[1].str();
        }
        if (matches[2].matched) { // Host
            host = matches[2].str();
        }
        if (matches[3].matched) { // Port
            try {
                port = std::stoi(matches[3].str());
            }
            catch (const std::exception& e) {
                std::cerr << "NetworkManager Warning: Could not parse port '" << matches[3].str() << "' from URL: " << e.what() << std::endl;
            }
        }
        if (matches[4].matched) { // Path
            path = matches[4].str();
        }
    }
    else {
        // If regex doesn't match, try to treat the whole string as a host (less robust)
        host = url_str;
        path = "/";
        // Attempt to infer protocol if it starts with "https" for direct host names
        if (host.rfind("https://", 0) == 0) {
            protocol = "https";
            host = host.substr(8); // Remove "https://"
        }
        else if (host.rfind("http://", 0) == 0) {
            protocol = "http";
            host = host.substr(7); // Remove "http://"
        }
    }

    // Handle default ports if not explicitly specified
    if (port == 0) {
        if (protocol == "https") {
            port = 443;
        }
        else {
            port = 80;
        }
    }

    //std::cout << "NetworkManager Debug: Parsed URL - Protocol: " << protocol << ", Host: " << host << ", Port: " << port << ", Path: " << path << std::endl;

    // Correct way to create and assign to std::unique_ptr<httplib::Client>
    std::unique_ptr<httplib::SSLClient> cli; // This declaration is fine as a local variable.
    std::unique_ptr<httplib::Client> cli_http;

    if (protocol == "https") {
        // Create the SSLClient unique_ptr first, then move it into the base unique_ptr.
        // This leverages the implicit conversion during construction/move.
        cli = std::make_unique<httplib::SSLClient>(host.c_str(), port);
        // Only for testing, if you face SSL handshake errors
        // ssl_cli->set_verify_ssl(false);
    }
    else {
        //std::cout << "NetworkManager Debug: HTTP not supported "  << std::endl;
        //return "";
        cli_http = std::make_unique<httplib::Client>(host.c_str(), port);
    }

    // Now, set timeouts and headers on the *base* client pointer
    if (cli) { // Always check if unique_ptr holds a valid object before dereferencing
        cli->set_connection_timeout(5);
        cli->set_read_timeout(5);
        cli->set_write_timeout(5);

        httplib::Headers headers;
        for (const auto& pair : custom_headers) {
            headers.emplace(pair.first, pair.second);
        }

        auto res = cli->Get(path.c_str(), headers); // Call Get on the base class pointer
        // ... (rest of success/error handling) ...
        if (res) {
            last_http_status_code = res->status;
            //std::cout << "NetworkManager Debug: Request successful. Status: " << res->status << ", Body size: " << res->body.length() << std::endl;
            return res->body;
        }
        else {
            last_http_status_code = -1;
            std::cerr << "NetworkManager Error: HTTP GET failed for URL: " << url_str
                << ". httplib error code: " << httplib::to_string(res.error())
                << std::endl;
            return "";
        }
    }
    else if (cli_http) {
        cli_http->set_connection_timeout(5);
        cli_http->set_read_timeout(5);
        cli_http->set_write_timeout(5);

        httplib::Headers headers;
        for (const auto& pair : custom_headers) {
            headers.emplace(pair.first, pair.second);
        }

        auto res = cli_http->Get(path.c_str(), headers); // Call Get on the base class pointer
        // ... (rest of success/error handling) ...
        if (res) {
            last_http_status_code = res->status;
            //std::cout << "NetworkManager Debug: Request successful. Status: " << res->status << ", Body size: " << res->body.length() << std::endl;
            return res->body;
        }
        else {
            last_http_status_code = -1;
            std::cerr << "NetworkManager Error: HTTP GET failed for URL: " << url_str
                << ". httplib error code: " << httplib::to_string(res.error())
                << std::endl;
            return "";
        }
    }
    else {
        last_http_status_code = -1; // Indicate client creation failed
        std::cerr << "NetworkManager Error: Failed to create HTTP client for URL: " << url_str << std::endl;
        return "";
    }

}

void NetworkManager::setHeader(const std::string& name, const std::string& value) {
    custom_headers[name] = value;
}

void NetworkManager::clearHeaders() {
    custom_headers.clear();
}

std::string NetworkManager::httpPost(const std::string& url, const std::string& body, const std::string& content_type) {
    return httpPostOrPut("POST", url, body, content_type, custom_headers, last_http_status_code);
}

std::string NetworkManager::httpPut(const std::string& url, const std::string& body, const std::string& content_type) {
    return httpPostOrPut("PUT", url, body, content_type, custom_headers, last_http_status_code);
}

// --- Generic Request Handler ---
// This function is called by the httplib server thread.
// It creates an event and pushes it onto the VM's queue.
void NetworkManager::queue_request_for_vm(const httplib::Request& req, httplib::Response& res) {
    std::string lookupPath = req.path;
    if (lookupPath.empty()) lookupPath = "/";
    //std::string handler_function_name = getRouteHandler(req.method, req.path);
    std::string handler_function_name = getRouteHandler(req.method, lookupPath);
    if (handler_function_name.empty()) {
        std::cout << "HTTP Error: No handler for [" << req.method << "] " << lookupPath << std::endl;
        res.status = 404;
        //res.set_content("Not Found: No handler registered for " + req.path, "text/plain");
        res.set_content("Not Found: No handler registered for " + lookupPath, "text/plain");
        return;
    }

    // Create the simplified event and synchronization tools
    auto event = std::make_shared<ServerRequestEvent>();
    event->method = req.method;
    event->path = req.path;
    event->body = req.body;
    event->headers = req.headers;
    event->cv = std::make_shared<std::condition_variable>();
    event->mtx = std::make_shared<std::mutex>();

    // Queue the event for the main VM thread to process
    vm_ref.queue_http_request(event);

    // Wait for the main thread to handle the request and fill in the response fields
    std::unique_lock<std::mutex> lock(*event->mtx);
    if (!event->cv->wait_for(lock, std::chrono::seconds(10), [&event] { return event->handled; })) {
        // If the handler in BASIC takes too long, send a timeout response
        res.status = 504; // Gateway Timeout
        res.set_content("Request timed out in jdBasic script.", "text/plain");
        return;
    }

    // --- Build the final response from the data the main thread provided ---
    res.status = event->status_code;
    res.set_content(event->response_body, event->content_type.c_str());
}

// --- Server Management Methods ---
bool NetworkManager::startServer(int port) {
    if (http_server) {
        std::cerr << "Server is already running." << std::endl;
        return false;
    }
    try {
        http_server = std::make_unique<httplib::Server>();
        setup_server_routes(); // Setup generic handlers

        server_thread = std::make_unique<std::thread>([this, port]() {
            std::cout << "HTTP Server starting on port " << port << "..." << std::endl;
            if (!http_server->listen("0.0.0.0", port)) {
                std::cerr << "Failed to start HTTP server." << std::endl;
                // Consider a mechanism to report this failure back to the main thread
            }
            });
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception while starting server: " << e.what() << std::endl;
        return false;
    }
}

void NetworkManager::stopServer() {
    if (http_server) {
        http_server->stop();
        if (server_thread && server_thread->joinable()) {
            server_thread->join();
        }
        http_server.reset();
        server_thread.reset();
        std::cout << "HTTP Server stopped." << std::endl;
    }
}

void NetworkManager::registerServerRoute(const std::string& method, const std::string& path, const std::string& function_name) {
    std::lock_guard<std::mutex> lock(route_mutex);
    std::cout << "Registering: " << method << " " << path << " -> " << function_name << std::endl;
    if (method == "GET") {
        get_routes[path] = function_name;
    }
    else if (method == "POST") {
        post_routes[path] = function_name;
    }
}

bool NetworkManager::isServerRunning() const {
    return http_server && http_server->is_running();
}


void NetworkManager::setup_server_routes() {
    if (!http_server) return;

    // Generic handler for all GET requests
    http_server->Get(R"(/.*)", [this](const httplib::Request& req, httplib::Response& res) {
        this->queue_request_for_vm(req, res);
        });

    // Generic handler for all POST requests
    http_server->Post(R"(/.*)", [this](const httplib::Request& req, httplib::Response& res) {
        this->queue_request_for_vm(req, res);
        });
}

#endif
