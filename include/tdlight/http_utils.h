/**
 * @file http_utils.h
 * @brief HTTP response helpers for TDlight web API.
 * 
 * Provides convenience functions for constructing HTTP responses
 * with proper headers, CORS support, and content types.
 */

#ifndef TDLIGHT_HTTP_UTILS_H
#define TDLIGHT_HTTP_UTILS_H

#include <string>
#include <sstream>
#include <vector>
#include <map>

namespace tdlight {
namespace http {

/**
 * Build an HTTP response with standard headers.
 */
inline std::string response(int status_code, const std::string& content_type,
                            const std::string& body) {
    std::string status_text;
    switch (status_code) {
        case 200: status_text = "OK"; break;
        case 400: status_text = "Bad Request"; break;
        case 404: status_text = "Not Found"; break;
        case 500: status_text = "Internal Server Error"; break;
        default:  status_text = "Unknown"; break;
    }
    
    return "HTTP/1.1 " + std::to_string(status_code) + " " + status_text + "\r\n"
           "Content-Type: " + content_type + "\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Content-Length: " + std::to_string(body.length()) + "\r\n"
           "\r\n" + body;
}

/** JSON 200 response */
inline std::string json_ok(const std::string& json_body) {
    return response(200, "application/json", json_body);
}

/** JSON error response */
inline std::string json_error(int status_code, const std::string& message) {
    std::string body = "{\"error\":\"" + message + "\"}";
    return response(status_code, "application/json", body);
}

/** HTML response (no-cache for development) */
inline std::string html_ok(const std::string& html) {
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/html\r\n"
           "Cache-Control: no-cache, no-store, must-revalidate\r\n"
           "Pragma: no-cache\r\n"
           "Expires: 0\r\n"
           "Content-Length: " + std::to_string(html.length()) + "\r\n"
           "\r\n" + html;
}

/** JavaScript response (no-cache) */
inline std::string js_ok(const std::string& js) {
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: application/javascript\r\n"
           "Cache-Control: no-cache, no-store, must-revalidate\r\n"
           "Pragma: no-cache\r\n"
           "Expires: 0\r\n"
           "Content-Length: " + std::to_string(js.length()) + "\r\n"
           "\r\n" + js;
}

/** SSE (Server-Sent Events) header */
inline std::string sse_header() {
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/event-stream\r\n"
           "Cache-Control: no-cache\r\n"
           "Connection: keep-alive\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "\r\n";
}

/** CORS preflight response */
inline std::string cors_preflight() {
    return "HTTP/1.1 200 OK\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
           "Access-Control-Allow-Headers: Content-Type\r\n"
           "Content-Length: 0\r\n"
           "\r\n";
}

/**
 * Parse HTTP query string into key-value pairs.
 */
inline std::map<std::string, std::string> parse_query_string(const std::string& query) {
    std::map<std::string, std::string> params;
    std::istringstream stream(query);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            params[pair.substr(0, eq)] = pair.substr(eq + 1);
        }
    }
    return params;
}

/**
 * Read a static file and return its contents.
 */
inline std::string read_file(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return "";
    
    std::string content;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), f) != nullptr) {
        content += buffer;
    }
    fclose(f);
    return content;
}

} // namespace http
} // namespace tdlight

#endif // TDLIGHT_HTTP_UTILS_H
