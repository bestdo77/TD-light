/**
 * @file config.h
 * @brief Configuration management for TDlight.
 * 
 * Handles loading, saving, and serialization of the application
 * configuration (config.json).
 */

#ifndef TDLIGHT_CONFIG_H
#define TDLIGHT_CONFIG_H

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>

namespace tdlight {

/**
 * Application configuration.
 * Loaded from config.json at startup, modifiable via REST API.
 */
struct Config {
    // Database
    std::string db_host = "localhost";
    int db_port = 6030;
    std::string db_user = "root";
    std::string db_password = "taosdata";
    std::string db_name = "gaiadr2_lc";
    
    // Web server
    int web_port = 5001;
    std::string web_host = "0.0.0.0";
    
    // Classification
    std::string model_path = "../models/lgbm_111w_model.pkl";
    std::string metadata_path = "../models/metadata.pkl";
    double confidence_threshold = 0.95;
    bool update_database = true;
    
    // Paths
    std::string libs_path = "../runtime/libs";
    std::string taos_cfg_path = "../runtime/taos_home/cfg";
    std::string python_path = "python3";
    std::string temp_dir = "/tmp";
    
    // HEALPix
    int healpix_nside = 64;
    std::string healpix_scheme = "NEST";
};

// ==================== Simple JSON helpers ====================
// NOTE: These are intentionally simple parsers for flat JSON config.
// They work for TDlight's config.json structure.
// For complex JSON, consider nlohmann/json.

inline std::string json_get_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(":", pos);
    if (pos == std::string::npos) return "";
    pos = json.find("\"", pos);
    if (pos == std::string::npos) return "";
    size_t end = json.find("\"", pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

inline int json_get_int(const std::string& json, const std::string& key, int default_val = 0) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return default_val;
    pos = json.find(":", pos);
    if (pos == std::string::npos) return default_val;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    size_t end = json.find_first_of(",}\n", pos);
    if (end == std::string::npos) return default_val;
    try { return std::stoi(json.substr(pos, end - pos)); }
    catch (...) { return default_val; }
}

inline double json_get_double(const std::string& json, const std::string& key, double default_val = 0.0) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return default_val;
    pos = json.find(":", pos);
    if (pos == std::string::npos) return default_val;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    size_t end = json.find_first_of(",}\n", pos);
    if (end == std::string::npos) return default_val;
    try { return std::stod(json.substr(pos, end - pos)); }
    catch (...) { return default_val; }
}

inline bool json_get_bool(const std::string& json, const std::string& key, bool default_val = false) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return default_val;
    pos = json.find(":", pos);
    if (pos == std::string::npos) return default_val;
    if (json.find("true", pos) < json.find(",", pos)) return true;
    if (json.find("false", pos) < json.find(",", pos)) return false;
    return default_val;
}

/**
 * Load configuration from JSON file.
 */
inline bool load_config(Config& config, const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[WARN] Config file not found: " << path << ", using defaults." << std::endl;
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();
    file.close();
    
    config.db_host = json_get_string(json, "host");
    if (config.db_host.empty()) config.db_host = "localhost";
    config.db_port = json_get_int(json, "port", 6030);
    config.db_user = json_get_string(json, "user");
    if (config.db_user.empty()) config.db_user = "root";
    config.db_password = json_get_string(json, "password");
    if (config.db_password.empty()) config.db_password = "taosdata";
    config.db_name = json_get_string(json, "name");
    if (config.db_name.empty()) config.db_name = "gaiadr2_lc";
    
    config.model_path = json_get_string(json, "model_path");
    config.metadata_path = json_get_string(json, "metadata_path");
    config.confidence_threshold = json_get_double(json, "confidence_threshold", 0.95);
    config.update_database = json_get_bool(json, "update_database", true);
    
    std::string libs = json_get_string(json, "libs");
    if (!libs.empty()) config.libs_path = libs;
    std::string taos_cfg = json_get_string(json, "taos_cfg");
    if (!taos_cfg.empty()) config.taos_cfg_path = taos_cfg;
    std::string python = json_get_string(json, "python");
    if (!python.empty()) config.python_path = python;
    
    const char* env_python = getenv("PYTHON_EXECUTABLE");
    if (env_python && strlen(env_python) > 0) {
        config.python_path = std::string(env_python);
    }
    
    std::string temp = json_get_string(json, "temp_dir");
    if (!temp.empty()) config.temp_dir = temp;
    
    config.healpix_nside = json_get_int(json, "nside", 64);
    config.healpix_scheme = json_get_string(json, "scheme");
    if (config.healpix_scheme.empty()) config.healpix_scheme = "NEST";
    
    std::cout << "[INFO] Config loaded: " << path << std::endl;
    std::cout << "[INFO] Database: " << config.db_name << "@" 
              << config.db_host << ":" << config.db_port << std::endl;
    return true;
}

/**
 * Save configuration to JSON file.
 */
inline bool save_config(const Config& config, const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "[ERROR] Cannot write config file: " << path << std::endl;
        return false;
    }
    
    file << "{\n";
    file << "    \"database\": {\n";
    file << "        \"host\": \"" << config.db_host << "\",\n";
    file << "        \"port\": " << config.db_port << ",\n";
    file << "        \"user\": \"" << config.db_user << "\",\n";
    file << "        \"password\": \"" << config.db_password << "\",\n";
    file << "        \"name\": \"" << config.db_name << "\"\n";
    file << "    },\n";
    file << "    \"web\": {\n";
    file << "        \"port\": " << config.web_port << ",\n";
    file << "        \"host\": \"" << config.web_host << "\"\n";
    file << "    },\n";
    file << "    \"classification\": {\n";
    file << "        \"model_path\": \"" << config.model_path << "\",\n";
    file << "        \"metadata_path\": \"" << config.metadata_path << "\",\n";
    file << "        \"confidence_threshold\": " << config.confidence_threshold << ",\n";
    file << "        \"update_database\": " << (config.update_database ? "true" : "false") << "\n";
    file << "    },\n";
    file << "    \"paths\": {\n";
    file << "        \"libs\": \"" << config.libs_path << "\",\n";
    file << "        \"taos_cfg\": \"" << config.taos_cfg_path << "\",\n";
    file << "        \"python\": \"" << config.python_path << "\",\n";
    file << "        \"temp_dir\": \"" << config.temp_dir << "\"\n";
    file << "    },\n";
    file << "    \"healpix\": {\n";
    file << "        \"nside\": " << config.healpix_nside << ",\n";
    file << "        \"scheme\": \"" << config.healpix_scheme << "\"\n";
    file << "    }\n";
    file << "}\n";
    
    file.close();
    std::cout << "[INFO] Config saved." << std::endl;
    return true;
}

/**
 * Serialize config to JSON string (for REST API response).
 */
inline std::string config_to_json(const Config& config) {
    std::stringstream ss;
    ss << "{";
    ss << "\"database\":{";
    ss << "\"host\":\"" << config.db_host << "\",";
    ss << "\"port\":" << config.db_port << ",";
    ss << "\"user\":\"" << config.db_user << "\",";
    ss << "\"name\":\"" << config.db_name << "\"";
    ss << "},";
    ss << "\"web\":{";
    ss << "\"port\":" << config.web_port;
    ss << "},";
    ss << "\"classification\":{";
    ss << "\"model_path\":\"" << config.model_path << "\",";
    ss << "\"confidence_threshold\":" << config.confidence_threshold << ",";
    ss << "\"update_database\":" << (config.update_database ? "true" : "false");
    ss << "},";
    ss << "\"healpix\":{";
    ss << "\"nside\":" << config.healpix_nside << ",";
    ss << "\"scheme\":\"" << config.healpix_scheme << "\"";
    ss << "}";
    ss << "}";
    return ss.str();
}

} // namespace tdlight

#endif // TDLIGHT_CONFIG_H
