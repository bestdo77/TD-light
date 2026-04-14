/**
 * @file sanitize.h
 * @brief Input sanitization and validation utilities for TDlight.
 * 
 * Provides functions to prevent SQL injection, command injection,
 * and path traversal attacks in the web API.
 */

#ifndef TDLIGHT_SANITIZE_H
#define TDLIGHT_SANITIZE_H

#include <string>
#include <cstring>
#include <cstdio>
#include <cctype>

namespace tdlight {

/**
 * Validate a SQL identifier (table name, database name, column name).
 * Only allows alphanumeric characters and underscores.
 * Must start with a letter or underscore.
 */
inline bool is_valid_sql_identifier(const std::string& str) {
    if (str.empty() || str.size() > 256) return false;
    for (char c : str) {
        if (!isalnum(c) && c != '_' && c != '.') return false;
    }
    if (!isalpha(str[0]) && str[0] != '_') return false;
    return true;
}

/**
 * Remove non-alphanumeric/underscore characters from a string.
 */
inline std::string sanitize_sql_identifier(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        if (isalnum(c) || c == '_') result += c;
    }
    return result;
}

/**
 * Validate that a string contains only numeric characters (digits, sign, dot).
 */
inline bool is_valid_numeric(const std::string& str) {
    if (str.empty()) return false;
    for (size_t i = 0; i < str.size(); i++) {
        if (i == 0 && str[i] == '-') continue;
        if (!isdigit(str[i]) && str[i] != '.') return false;
    }
    return true;
}

/**
 * Sanitize a shell argument by wrapping in single quotes.
 * Internal single quotes are escaped as: '\\''
 * This prevents command injection when using system().
 */
inline std::string sanitize_shell_arg(const std::string& str) {
    std::string result = "'";
    for (char c : str) {
        if (c == '\'') {
            result += "'\\''";
        } else {
            result += c;
        }
    }
    result += "'";
    return result;
}

/**
 * Validate a file path - rejects dangerous characters that could
 * allow command injection or unexpected behavior.
 */
inline bool is_valid_path(const std::string& str) {
    if (str.empty() || str.size() > 4096) return false;
    if (str.find(';') != std::string::npos || str.find('|') != std::string::npos ||
        str.find('&') != std::string::npos || str.find('`') != std::string::npos ||
        str.find('$') != std::string::npos || str.find('\n') != std::string::npos ||
        str.find('\r') != std::string::npos) {
        return false;
    }
    return true;
}

/**
 * URL-decode a percent-encoded string.
 */
inline std::string url_decode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.size(); i++) {
        if (str[i] == '%' && i + 2 < str.size()) {
            int hex = 0;
            sscanf(str.substr(i + 1, 2).c_str(), "%x", &hex);
            result += static_cast<char>(hex);
            i += 2;
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

/**
 * Escape a string for safe inclusion in JSON output.
 */
inline std::string json_escape(const std::string& str) {
    std::string result;
    result.reserve(str.size() + 16);
    for (char c : str) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b";  break;
            case '\f': result += "\\f";  break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[7];
                    sprintf(buf, "\\u%04x", (unsigned char)c);
                    result += buf;
                } else {
                    result += c;
                }
        }
    }
    return result;
}

} // namespace tdlight

#endif // TDLIGHT_SANITIZE_H
