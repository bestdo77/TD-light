#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <taos.h>
#include <healpix_cxx/healpix_base.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fstream>
#include <mutex>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

using namespace std;

const string CONFIG_FILE = "../config.json";

struct Config {
    string db_host = "localhost";
    int db_port = 6030;
    string db_user = "root";
    string db_password = "taosdata";
    string db_name = "gaiadr2_lc";
    
    int web_port = 5001;
    string web_host = "0.0.0.0";
    
    string model_path = "../models/lgbm_111w_model.pkl";
    string metadata_path = "../models/metadata.pkl";
    double confidence_threshold = 0.95;
    bool update_database = true;
    
    string libs_path = "../runtime/libs";
    string taos_cfg_path = "../runtime/taos_home/cfg";
    string python_path = "python3";
    string temp_dir = "/tmp";
    
    int healpix_nside = 64;
    string healpix_scheme = "NEST";
} config;

// JSON parsing helpers
string json_get_string(const string& json, const string& key) {
    string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == string::npos) return "";
    pos = json.find(":", pos);
    if (pos == string::npos) return "";
    pos = json.find("\"", pos);
    if (pos == string::npos) return "";
    size_t end = json.find("\"", pos + 1);
    if (end == string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

int json_get_int(const string& json, const string& key, int default_val = 0) {
    string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == string::npos) return default_val;
    pos = json.find(":", pos);
    if (pos == string::npos) return default_val;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    size_t end = json.find_first_of(",}\n", pos);
    if (end == string::npos) return default_val;
    try {
        return stoi(json.substr(pos, end - pos));
    } catch (...) {
        return default_val;
    }
}

double json_get_double(const string& json, const string& key, double default_val = 0.0) {
    string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == string::npos) return default_val;
    pos = json.find(":", pos);
    if (pos == string::npos) return default_val;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    size_t end = json.find_first_of(",}\n", pos);
    if (end == string::npos) return default_val;
    try {
        return stod(json.substr(pos, end - pos));
    } catch (...) {
        return default_val;
    }
}

bool json_get_bool(const string& json, const string& key, bool default_val = false) {
    string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == string::npos) return default_val;
    pos = json.find(":", pos);
    if (pos == string::npos) return default_val;
    if (json.find("true", pos) < json.find(",", pos)) return true;
    if (json.find("false", pos) < json.find(",", pos)) return false;
    return default_val;
}

bool load_config() {
    ifstream file(CONFIG_FILE);
    if (!file.is_open()) {
        cerr << "[WARN] Config file not found: " << CONFIG_FILE << ", using defaults." << endl;
        return false;
    }
    
    stringstream buffer;
    buffer << file.rdbuf();
    string json = buffer.str();
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
    
    string libs = json_get_string(json, "libs");
    if (!libs.empty()) config.libs_path = libs;
    string taos_cfg = json_get_string(json, "taos_cfg");
    if (!taos_cfg.empty()) config.taos_cfg_path = taos_cfg;
    string python = json_get_string(json, "python");
    if (!python.empty()) config.python_path = python;
    
    // Allow environment variable override
    const char* env_python = getenv("PYTHON_EXECUTABLE");
    if (env_python && strlen(env_python) > 0) {
        config.python_path = string(env_python);
        cout << "[INFO] Using Python from env: " << config.python_path << endl;
    }
    
    string temp = json_get_string(json, "temp_dir");
    if (!temp.empty()) config.temp_dir = temp;
    
    config.healpix_nside = json_get_int(json, "nside", 64);
    config.healpix_scheme = json_get_string(json, "scheme");
    if (config.healpix_scheme.empty()) config.healpix_scheme = "NEST";
    
    cout << "[INFO] Config loaded: " << CONFIG_FILE << endl;
    cout << "[INFO] Database: " << config.db_name << "@" << config.db_host << ":" << config.db_port << endl;
    return true;
}

bool save_config() {
    ofstream file(CONFIG_FILE);
    if (!file.is_open()) {
        cerr << "[ERROR] Cannot write config file: " << CONFIG_FILE << endl;
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
    cout << "[INFO] Config saved." << endl;
    return true;
}

string config_to_json() {
    stringstream ss;
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

string TO_CLASSIFY_FILE = "../data/to_classify.txt";
string TO_REVIEW_FILE = "../data/to_review.txt";
string VALUABLE_FILE = "../data/valuable.txt";

// Auto-classification related file paths
string get_auto_classify_candidate_file(const string& db_name) {
    return "../data/auto_classify_queue_" + db_name + ".csv";
}

// Run candidate detection program
int run_check_candidates(const string& db_name) {
    string cmd = "../insert/check_candidates --db " + db_name + " > /tmp/check_candidates.log 2>&1";
    return system(cmd.c_str());
}

int count_candidates(const string& candidate_file) {
    ifstream f(candidate_file);
    if (!f.is_open()) return 0;
    
    int count = 0;
    string line;
    getline(f, line); // skip header
    while (getline(f, line)) {
        if (!line.empty()) count++;
    }
    return count;
}

struct ObjectInfo {
    long long healpix_id;
    long long source_id;
    double ra;
    double dec;
    int data_count;
    string table_name;
    string object_class;
    string band;
};

struct LightcurvePoint {
    string timestamp;
    double mag;
    double mag_error;
    double flux;
    double flux_error;
    string band;
};

TAOS* conn = nullptr;
std::mutex db_mutex;
int server_socket = -1;

vector<string> split(const string& s, char delimiter) {
    vector<string> tokens;
    string token;
    istringstream tokenStream(s);
    while (getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

string trim(const string& str) {
    size_t first = str.find_first_not_of(' ');
    if (first == string::npos) return "";
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
}

double angular_distance(double ra1, double dec1, double ra2, double dec2) {
    double ra1_rad = ra1 * M_PI / 180.0;
    double dec1_rad = dec1 * M_PI / 180.0;
    double ra2_rad = ra2 * M_PI / 180.0;
    double dec2_rad = dec2 * M_PI / 180.0;
    
    double cos_d = sin(dec1_rad) * sin(dec2_rad) + 
                  cos(dec1_rad) * cos(dec2_rad) * cos(ra1_rad - ra2_rad);
    cos_d = max(-1.0, min(1.0, cos_d)); 
    return acos(cos_d) * 180.0 / M_PI;
}

string json_escape(const string& str);

string csv_to_json(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "[ERROR] Cannot open CSV file: " << filename << endl;
        return "[]";
    }

    stringstream json;
    json << "[";
    
    string line;
    vector<string> headers;
    if (getline(file, line)) {
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) {
            line = line.substr(3);
        }
        headers = split(line, ',');
        for (string& h : headers) {
            size_t first = h.find_first_not_of(" \t\r\n");
            size_t last = h.find_last_not_of(" \t\r\n");
            if (first != string::npos && last != string::npos)
                h = h.substr(first, (last - first + 1));
        }
    }

    bool first_row = true;
    while (getline(file, line)) {
        if (line.empty()) continue;
        if (!first_row) json << ",";
        first_row = false;
        
        vector<string> values = split(line, ',');
        json << "{";
        for (size_t i = 0; i < headers.size(); i++) {
            if (i > 0) json << ",";
            string val = (i < values.size()) ? values[i] : "";
            size_t first = val.find_first_not_of(" \t\r\n");
            size_t last = val.find_last_not_of(" \t\r\n");
            if (first != string::npos && last != string::npos)
                val = val.substr(first, (last - first + 1));
            
            json << "\"" << json_escape(headers[i]) << "\":\"" << json_escape(val) << "\"";
        }
        json << "}";
    }
    
    json << "]";
    return json.str();
}

bool connect_to_database() {
    if (conn != nullptr) {
        taos_close(conn);
        conn = nullptr;
    }
    
    // Try connecting to configured database
    if (!config.db_name.empty()) {
        conn = taos_connect(config.db_host.c_str(), config.db_user.c_str(), 
                            config.db_password.c_str(), config.db_name.c_str(), config.db_port);
    }
    
    if (conn == nullptr) {
        if (!config.db_name.empty()) {
            cerr << "[WARN] Failed to connect to database '" << config.db_name << "': " << taos_errstr(conn) << endl;
        }
        
        // Try connecting without specifying database
        conn = taos_connect(config.db_host.c_str(), config.db_user.c_str(), 
                            config.db_password.c_str(), NULL, config.db_port);
        
        if (conn == nullptr) {
            cerr << "[ERROR] TDengine connect failed: " << taos_errstr(conn) << endl;
            return false;
        }
        cout << "[INFO] Connected to TDengine (system/no specific database)" << endl;
    } else {
        cout << "[INFO] Connected to TDengine (" << config.db_name << ")" << endl;
    }
    return true;
}

vector<string> get_databases() {
    vector<string> databases;
    
    TAOS* temp_conn = taos_connect(config.db_host.c_str(), config.db_user.c_str(), config.db_password.c_str(), NULL, config.db_port);
    if (temp_conn == nullptr) {
        cerr << "[ERROR] TDengine connect failed" << endl;
        return databases;
    }
    
    const char* query = "SHOW DATABASES";
    TAOS_RES* res = taos_query(temp_conn, query);
    if (res == nullptr) {
        cerr << "[ERROR] Failed to query databases" << endl;
        taos_close(temp_conn);
        return databases;
    }
    
    TAOS_ROW row;
    while ((row = taos_fetch_row(res)) != nullptr) {
        if (row[0] != nullptr) {
            int* lengths = taos_fetch_lengths(res);
            if (lengths != nullptr && lengths[0] > 0) {
                string db_name(static_cast<char*>(row[0]), lengths[0]);
                databases.push_back(db_name);
            }
        }
    }
    
    taos_free_result(res);
    taos_close(temp_conn);
    return databases;
}

bool switch_database(const string& new_db_name) {
    config.db_name = new_db_name;
    return connect_to_database();
}

bool add_to_queue(const string& filename, const string& table_name, const string& source_id = "") {
    ofstream file(filename, ios::app);
    if (!file.is_open()) return false;
    time_t now = time(nullptr);
    file << now << "," << table_name << "," << source_id << "\n";
    file.close();
    return true;
}

vector<map<string, string>> read_queue(const string& filename, int limit = 100) {
    vector<map<string, string>> items;
    ifstream file(filename);
    if (!file.is_open()) return items;
    string line;
    int count = 0;
    while (getline(file, line) && count < limit) {
        map<string, string> item;
        size_t pos1 = line.find(',');
        size_t pos2 = line.find(',', pos1 + 1);
        if (pos1 != string::npos) {
            item["timestamp"] = line.substr(0, pos1);
            if (pos2 != string::npos) {
                item["table_name"] = line.substr(pos1 + 1, pos2 - pos1 - 1);
                item["source_id"] = line.substr(pos2 + 1);
            } else {
                item["table_name"] = line.substr(pos1 + 1);
                item["source_id"] = "";
            }
            items.push_back(item);
            count++;
        }
    }
    file.close();
    return items;
}

string toLower(const string& str) {
    string result = str;
    transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

vector<ObjectInfo> get_objects(int limit = 200) {
    vector<ObjectInfo> objects;
    
    string query = "SELECT healpix_id, source_id, FIRST(ra) as ra, FIRST(dec) as dec, COUNT(*) as data_count, "
                   "FIRST(cls) as cls, FIRST(band) as band FROM sensor_data "
                   "GROUP BY healpix_id, source_id LIMIT " + to_string(limit);
    
    cerr << "[DEBUG] Executing: " << query << endl;
    TAOS_RES* res = taos_query(conn, query.c_str());
    if (res == nullptr) {
        cerr << "[ERROR] Query failed: " << taos_errstr(NULL) << endl;
        return objects;
    }
    
    int error_code = taos_errno(res);
    if (error_code != 0) {
        cerr << "[ERROR] Query error: " << taos_errstr(res) << endl;
        taos_free_result(res);
        return objects;
    }
    
    TAOS_ROW row;
    while ((row = taos_fetch_row(res)) != nullptr) {
        ObjectInfo obj;
        
        if (row[0]) obj.healpix_id = *(long long*)row[0];
        else obj.healpix_id = 0;
        
        if (row[1]) obj.source_id = *(long long*)row[1];
        else obj.source_id = 0;
        
        if (row[2]) obj.ra = *(double*)row[2];
        else obj.ra = 0.0;
        
        if (row[3]) obj.dec = *(double*)row[3];
        else obj.dec = 0.0;
        
        if (row[4]) obj.data_count = *(int*)row[4];
        else obj.data_count = 0;
        
        int* lengths = taos_fetch_lengths(res);
        if (lengths != nullptr) {
            if (row[5] != nullptr && lengths[5] > 0) {
                obj.object_class = string((char*)row[5], lengths[5]);
            } else {
                obj.object_class = "unknown";
            }
            
            if (row[6] != nullptr && lengths[6] > 0) {
                obj.band = toLower(string((char*)row[6], lengths[6]));
            } else {
                obj.band = "g";
            }
        }
        
        obj.table_name = "sensor_data_" + to_string(obj.healpix_id) + "_" + to_string(obj.source_id);
        objects.push_back(obj);
    }
    
    taos_free_result(res);
    return objects;
}

vector<LightcurvePoint> get_lightcurve(const string& table_name, const string& time_start = "", const string& time_end = "") {
    vector<LightcurvePoint> points;
    
    string query = "SELECT ts, mag, mag_error, flux, flux_error, band FROM " + table_name;
    
    vector<string> conditions;
    if (!time_start.empty()) {
        conditions.push_back("ts >= '" + time_start + "'");
    }
    if (!time_end.empty()) {
        conditions.push_back("ts <= '" + time_end + "'");
    }
    
    if (!conditions.empty()) {
        query += " WHERE " + conditions[0];
        for (size_t i = 1; i < conditions.size(); i++) {
            query += " AND " + conditions[i];
        }
    }
    
    query += " ORDER BY ts";
    
    TAOS_RES* res = taos_query(conn, query.c_str());
    if (res == nullptr) {
        cerr << "[ERROR] Query failed: " << taos_errstr(res) << endl;
        return points;
    }
    
    TAOS_ROW row;
    int* lengths;
    while ((row = taos_fetch_row(res)) != nullptr) {
        lengths = taos_fetch_lengths(res);
        LightcurvePoint point;
        
        TAOS_FIELD* fields = taos_fetch_fields(res);
        if (fields[0].type == TSDB_DATA_TYPE_TIMESTAMP) {
            int64_t ts = *(int64_t*)row[0];
            time_t t = ts / 1000;
            char buffer[32];
            strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", gmtime(&t));
            point.timestamp = string(buffer);
        }
        
        point.mag = *(double*)row[1];
        point.mag_error = *(double*)row[2];
        point.flux = *(double*)row[3];
        point.flux_error = *(double*)row[4];
        
        // 获取 band 字段
        if (row[5] != nullptr && lengths[5] > 0) {
            point.band = string((char*)row[5], lengths[5]);
        } else {
            point.band = "G";  // 默认波段
        }
        
        points.push_back(point);
    }
    
    taos_free_result(res);
    return points;
}

vector<ObjectInfo> cone_search(double center_ra, double center_dec, double radius_deg) {
    vector<ObjectInfo> results;
    
    if (center_dec < -90.0 || center_dec > 90.0) {
        cerr << "[ERROR] Invalid DEC: " << center_dec << endl;
        return results;
    }
    
    if (center_ra < 0.0 || center_ra > 360.0) {
        cerr << "[ERROR] Invalid RA: " << center_ra << endl;
        return results;
    }
    
    int nside = 64;
    T_Healpix_Base<int> healpix(nside, NEST, SET_NSIDE);
    
    pointing center_point;
    center_point.theta = (90.0 - center_dec) * M_PI / 180.0;
    center_point.phi = center_ra * M_PI / 180.0;
    
    double expanded_radius_deg = radius_deg * 1.5;
    double radius_rad = expanded_radius_deg * M_PI / 180.0;
    vector<int> healpix_pixels;
    healpix.query_disc(center_point, radius_rad, healpix_pixels);
    
    cout << "[INFO] Cone search: RA=" << center_ra << ", DEC=" << center_dec 
         << ", R=" << expanded_radius_deg << " deg, Pixels=" 
         << healpix_pixels.size() << endl;
    
    if (healpix_pixels.empty()) {
        return results;
    }
    
    string healpix_ids = "(";
    for (size_t i = 0; i < healpix_pixels.size(); i++) {
        if (i > 0) healpix_ids += ",";
        healpix_ids += to_string(healpix_pixels[i]);
    }
    healpix_ids += ")";
    
    string query = "SELECT healpix_id, source_id, FIRST(ra) as ra, FIRST(dec) as dec, COUNT(*) as data_count, FIRST(cls) as cls, FIRST(band) as band "
                   "FROM sensor_data "
                   "WHERE healpix_id IN " + healpix_ids + " "
                   "GROUP BY healpix_id, source_id";
    
    TAOS_RES* res = taos_query(conn, query.c_str());
    if (res == nullptr) {
        cerr << "[ERROR] Cone search query failed: " << taos_errstr(res) << endl;
        return results;
    }
    
    TAOS_ROW row;
    while ((row = taos_fetch_row(res)) != nullptr) {
        ObjectInfo obj;
        int* lengths = taos_fetch_lengths(res);
        
        obj.healpix_id = *(long long*)row[0];
        obj.source_id = *(long long*)row[1];
        obj.ra = *(double*)row[2];
        obj.dec = *(double*)row[3];
        obj.data_count = *(int*)row[4];
        obj.table_name = "sensor_data_" + to_string(obj.healpix_id) + "_" + to_string(obj.source_id);
        
        if (row[5] && lengths[5] > 0) obj.object_class = string((char*)row[5], lengths[5]);
        else obj.object_class = "UNKNOWN";
        
        if (row[6] && lengths[6] > 0) obj.band = string((char*)row[6], lengths[6]);
        else obj.band = "Unknown";
        
        double distance = angular_distance(center_ra, center_dec, obj.ra, obj.dec);
        if (distance <= radius_deg) {
            results.push_back(obj);
        }
    }
    
    taos_free_result(res);
    cout << "[INFO] Found " << results.size() << " objects." << endl;
    return results;
}

vector<ObjectInfo> random_search(int limit) {
    return get_objects(limit);
}

vector<ObjectInfo> region_search(double ra_min, double ra_max, double dec_min, double dec_max) {
    vector<ObjectInfo> results;
    
    string query = "SELECT healpix_id, source_id, FIRST(ra) as ra, FIRST(dec) as dec, COUNT(*) as data_count, FIRST(cls) as cls, FIRST(band) as band "
                   "FROM sensor_data "
                   "WHERE ra >= " + to_string(ra_min) + " AND ra <= " + to_string(ra_max) + " "
                   "AND dec >= " + to_string(dec_min) + " AND dec <= " + to_string(dec_max) + " "
                   "GROUP BY healpix_id, source_id "
                   "ORDER BY source_id";
    
    TAOS_RES* res = taos_query(conn, query.c_str());
    if (res == nullptr) {
        cerr << "[ERROR] Query failed: " << taos_errstr(res) << endl;
        return results;
    }
    
    TAOS_ROW row;
    while ((row = taos_fetch_row(res)) != nullptr) {
        ObjectInfo obj;
        int* lengths = taos_fetch_lengths(res);
        
        obj.healpix_id = *(long long*)row[0];
        obj.source_id = *(long long*)row[1];
        obj.ra = *(double*)row[2];
        obj.dec = *(double*)row[3];
        obj.data_count = *(int*)row[4];
        obj.table_name = "sensor_data_" + to_string(obj.healpix_id) + "_" + to_string(obj.source_id);
        
        if (row[5] && lengths[5] > 0) obj.object_class = string((char*)row[5], lengths[5]);
        else obj.object_class = "UNKNOWN";
        
        if (row[6] && lengths[6] > 0) obj.band = string((char*)row[6], lengths[6]);
        else obj.band = "Unknown";
        
        results.push_back(obj);
    }
    
    taos_free_result(res);
    return results;
}

string json_escape(const string& str) {
    stringstream ss;
    for (char c : str) {
        if (c == '"') ss << "\\\"";
        else if (c == '\\') ss << "\\\\";
        else if (c == '\b') ss << "\\b";
        else if (c == '\f') ss << "\\f";
        else if (c == '\n') ss << "\\n";
        else if (c == '\r') ss << "\\r";
        else if (c == '\t') ss << "\\t";
        else if ((unsigned char)c < 0x20) {
            char buf[7];
            sprintf(buf, "\\u%04x", (unsigned char)c);
            ss << buf;
        }
        else ss << c;
    }
    return ss.str();
}

string objects_to_json(const vector<ObjectInfo>& objects) {
    stringstream json;
    json << "{\"objects\":[";
    
    for (size_t i = 0; i < objects.size(); i++) {
        if (i > 0) json << ",";
        json << "{"
             << "\"table_name\":\"" << json_escape(objects[i].table_name) << "\","
             << "\"source_id\":\"" << objects[i].source_id << "\","
             << "\"data_count\":" << objects[i].data_count << ","
             << "\"healpix_id\":\"" << objects[i].healpix_id << "\","
             << "\"ra\":" << objects[i].ra << ","
             << "\"dec\":" << objects[i].dec << ","
             << "\"object_class\":" << (objects[i].object_class.empty() ? "null" : "\"" + json_escape(objects[i].object_class) + "\"") << ","
             << "\"band\":" << (objects[i].band.empty() ? "null" : "\"" + json_escape(objects[i].band) + "\"")
             << "}";
    }
    
    json << "]}";
    return json.str();
}

string lightcurve_to_json(const vector<LightcurvePoint>& points) {
    stringstream json;
    json << "{\"metadata\":{\"healpix_id\":null,\"source_id\":null,\"ra\":null,\"dec\":null,\"object_class\":\"UNKNOWN\",\"band\":\"Unknown\"},\"data\":[";
    
    for (size_t i = 0; i < points.size(); i++) {
        if (i > 0) json << ",";
        json << "{"
             << "\"ts\":\"" << json_escape(points[i].timestamp) << "\","
             << "\"mag\":" << points[i].mag << ","
             << "\"mag_err\":" << points[i].mag_error << ","
             << "\"flux\":" << points[i].flux << ","
             << "\"flux_err\":" << points[i].flux_error << ","
             << "\"band\":\"" << json_escape(points[i].band) << "\""
             << "}";
    }
    
    json << "]}";
    return json.str();
}

string handle_request(const string& request) {
    std::lock_guard<std::mutex> lock(db_mutex);

    vector<string> lines = split(request, '\n');
    if (lines.empty()) return "";
    
    string first_line = lines[0];
    vector<string> parts = split(first_line, ' ');
    if (parts.size() < 2) return "";
    
    string method = parts[0];
    string path = parts[1];
    
    map<string, string> params;
    size_t query_pos = path.find('?');
    if (query_pos != string::npos) {
        string query_string = path.substr(query_pos + 1);
        path = path.substr(0, query_pos);
        
        vector<string> param_pairs = split(query_string, '&');
        for (const string& pair : param_pairs) {
            vector<string> key_value = split(pair, '=');
            if (key_value.size() == 2) {
                params[key_value[0]] = key_value[1];
            }
        }
    }
    
    if (path == "/api/objects") {
        int limit = 200;
        if (params.find("limit") != params.end()) {
            limit = stoi(params["limit"]);
        }
        
        vector<ObjectInfo> objects = get_objects(limit);
        string json_response = objects_to_json(objects);
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json_response.length()) + "\r\n"
               "\r\n" + json_response;
    }
    else if (path.find("/api/object/") == 0) {
        string table_name = path.substr(12);
        
        string tag_query = "SELECT healpix_id, source_id, ra, dec, cls, band FROM " + table_name + " LIMIT 1";
        TAOS_RES* tag_res = taos_query(conn, tag_query.c_str());
        
        ObjectInfo obj;
        obj.table_name = table_name;
        obj.healpix_id = 0;
        obj.source_id = 0;
        obj.ra = 0.0;
        obj.dec = 0.0;
        obj.data_count = 0;
        obj.object_class = "UNKNOWN";
        obj.band = "Unknown";
        
        if (tag_res != nullptr) {
            TAOS_ROW tag_row = taos_fetch_row(tag_res);
            if (tag_row != nullptr) {
                int* tag_lengths = taos_fetch_lengths(tag_res);
                obj.healpix_id = *(long long*)tag_row[0];
                obj.source_id = *(long long*)tag_row[1];
                obj.ra = *(double*)tag_row[2];
                obj.dec = *(double*)tag_row[3];
                obj.object_class = tag_row[4] ? string((char*)tag_row[4], tag_lengths[4]) : "UNKNOWN";
                obj.band = tag_row[5] ? string((char*)tag_row[5], tag_lengths[5]) : "Unknown";
                
                string count_query = "SELECT COUNT(*) FROM " + table_name;
                TAOS_RES* count_res = taos_query(conn, count_query.c_str());
                if (count_res != nullptr) {
                    TAOS_ROW count_row = taos_fetch_row(count_res);
                    if (count_row != nullptr) {
                        obj.data_count = *(int*)count_row[0];
                    }
                    taos_free_result(count_res);
                }
            }
            taos_free_result(tag_res);
        }
        
        string json_response = objects_to_json({obj});
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json_response.length()) + "\r\n"
               "\r\n" + json_response;
    }
    else if (path.find("/api/lightcurve/") == 0) {
        string table_name = path.substr(16);
        
        string time_start = params.find("time_start") != params.end() ? params["time_start"] : "";
        string time_end = params.find("time_end") != params.end() ? params["time_end"] : "";
        
        vector<LightcurvePoint> points = get_lightcurve(table_name, time_start, time_end);
        string json_response = lightcurve_to_json(points);
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json_response.length()) + "\r\n"
               "\r\n" + json_response;
    }
    else if (path == "/api/cone_search") {
        if (params.find("ra") == params.end() || params.find("dec") == params.end() || params.find("radius") == params.end()) {
            return "HTTP/1.1 400 Bad Request\r\n\r\nMissing parameters";
        }
        
        double ra = stod(params["ra"]);
        double dec = stod(params["dec"]);
        double radius = stod(params["radius"]);
        
        vector<ObjectInfo> results = cone_search(ra, dec, radius);
        string json_response = objects_to_json(results);
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json_response.length()) + "\r\n"
               "\r\n" + json_response;
    }
    else if (path == "/api/region_search") {
        if (params.find("ra_min") == params.end() || params.find("ra_max") == params.end() || 
            params.find("dec_min") == params.end() || params.find("dec_max") == params.end()) {
            return "HTTP/1.1 400 Bad Request\r\n\r\nMissing parameters";
        }
        
        double ra_min = stod(params["ra_min"]);
        double ra_max = stod(params["ra_max"]);
        double dec_min = stod(params["dec_min"]);
        double dec_max = stod(params["dec_max"]);
        
        vector<ObjectInfo> results = region_search(ra_min, ra_max, dec_min, dec_max);
        string json_response = objects_to_json(results);
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json_response.length()) + "\r\n"
               "\r\n" + json_response;
    }
    else if (path == "/api/sky_map") {
        int limit = 200;
        if (params.find("limit") != params.end()) {
            limit = stoi(params["limit"]);
        }
        
        vector<ObjectInfo> results = random_search(limit);
        
        string json_response = objects_to_json(results);
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json_response.length()) + "\r\n"
               "\r\n" + json_response;
    }
    else if (path == "/api/object_by_id") {
        if (params.find("id") == params.end()) {
            return "HTTP/1.1 400 Bad Request\r\n\r\nMissing id parameter";
        }
        
        string source_id = params["id"];
        
        string query = "SELECT healpix_id, source_id, FIRST(ra) as ra, FIRST(dec) as dec, COUNT(*) as data_count, "
                       "FIRST(cls) as cls, FIRST(band) as band "
                       "FROM sensor_data "
                       "WHERE source_id = " + source_id + " "
                       "GROUP BY healpix_id, source_id "
                       "LIMIT 1";
        
        TAOS_RES* res = taos_query(conn, query.c_str());
        if (res == nullptr) {
            cerr << "[ERROR] Query failed: " << taos_errstr(res) << endl;
            return "HTTP/1.1 500 Internal Server Error\r\n\r\nQuery failed";
        }
        
        vector<ObjectInfo> results;
        TAOS_ROW row = taos_fetch_row(res);
        if (row != nullptr) {
            ObjectInfo obj;
            
            obj.healpix_id = *(long long*)row[0];
            obj.source_id = *(long long*)row[1];
            obj.ra = *(double*)row[2];
            obj.dec = *(double*)row[3];
            obj.data_count = *(int*)row[4];
            
            int* lengths = taos_fetch_lengths(res);
            if (lengths != nullptr) {
                if (row[5] != nullptr && lengths[5] > 0) {
                    obj.object_class = string((char*)row[5], lengths[5]);
                } else {
                    obj.object_class = "UNKNOWN";
                }
                
                if (row[6] != nullptr && lengths[6] > 0) {
                    obj.band = string((char*)row[6], lengths[6]);
                } else {
                    obj.band = "Unknown";
                }
            } else {
                obj.object_class = "UNKNOWN";
                obj.band = "Unknown";
            }
            
            obj.table_name = "sensor_data_" + to_string(obj.healpix_id) + "_" + to_string(obj.source_id);
            
            results.push_back(obj);
        }
        
        taos_free_result(res);
        
        string json_response = objects_to_json(results);
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json_response.length()) + "\r\n"
               "\r\n" + json_response;
    }
    else if (path == "/") {
        FILE* file = fopen("index.html", "r");
        if (file == nullptr) {
            return "HTTP/1.1 500 Internal Server Error\r\n\r\nCannot read index.html";
        }
        
        string html;
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), file) != nullptr) {
            html += buffer;
        }
        fclose(file);
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: text/html\r\n"
               "Cache-Control: no-cache, no-store, must-revalidate\r\n"
               "Pragma: no-cache\r\n"
               "Expires: 0\r\n"
               "Content-Length: " + to_string(html.length()) + "\r\n"
               "\r\n" + html;
    }
    else if (path == "/sse_test.html") {
        FILE* file = fopen("sse_test.html", "r");
        if (file == nullptr) {
            return "HTTP/1.1 404 Not Found\r\n\r\nsse_test.html not found";
        }
        
        string html;
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), file) != nullptr) {
            html += buffer;
        }
        fclose(file);
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: text/html\r\n"
               "Content-Length: " + to_string(html.length()) + "\r\n"
               "\r\n" + html;
    }
    else if (path == "/app.js") {
        FILE* file = fopen("app.js", "r");
        if (file == nullptr) {
            return "HTTP/1.1 404 Not Found\r\n\r\napp.js not found";
        }
        
        string js;
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), file) != nullptr) {
            js += buffer;
        }
        fclose(file);
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/javascript\r\n"
               "Cache-Control: no-cache, no-store, must-revalidate\r\n"
               "Pragma: no-cache\r\n"
               "Expires: 0\r\n"
               "Content-Length: " + to_string(js.length()) + "\r\n"
               "\r\n" + js;
    }
    else if (path == "/api/databases") {
        vector<string> dbs = get_databases();
        
        string json = "{\"databases\":[";
        for (size_t i = 0; i < dbs.size(); i++) {
            if (i > 0) json += ",";
            json += "\"" + dbs[i] + "\"";
        }
        json += "],\"current\":\"" + config.db_name + "\"}";
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json.length()) + "\r\n"
               "\r\n" + json;
    }
    else if (path == "/api/switch_database") {
        if (params.find("database") == params.end()) {
            return "HTTP/1.1 400 Bad Request\r\n\r\nMissing database parameter";
        }
        
        string new_db = params["database"];
        bool success = switch_database(new_db);
        
        string json = "{\"success\":" + string(success ? "true" : "false") + 
                     ",\"database\":\"" + config.db_name + "\"}";
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json.length()) + "\r\n"
               "\r\n" + json;
    }
    else if (path == "/api/current_database") {
        string json = "{\"database\":\"" + config.db_name + "\"}";
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json.length()) + "\r\n"
               "\r\n" + json;
    }
    else if (path == "/api/analysis/summary") {
        string json_response = csv_to_json("../data/confidence_all_lengths_results.csv");
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json_response.length()) + "\r\n"
               "\r\n" + json_response;
    }
    else if (path == "/api/analysis/thresholds") {
        string json_response = csv_to_json("../data/threshold_analysis.csv");
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json_response.length()) + "\r\n"
               "\r\n" + json_response;
    }
    else if (path == "/api/classify_status") {
        string progress_path = "/tmp/class_progress.json";
        ifstream file(progress_path);
        string json_response = "{\"percent\":0, \"message\":\"Waiting...\", \"step\":\"\"}";
        if (file.is_open()) {
            stringstream ss;
            ss << file.rdbuf();
            json_response = ss.str();
        }
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json_response.length()) + "\r\n"
               "\r\n" + json_response;
    }
    else if (path == "/api/classify_stop") {
        ofstream stop_file("/tmp/classify_stop");
        stop_file << "stop" << endl;
        stop_file.close();
        
        usleep(500000);  // 500ms
        
        system("pkill -9 -f 'classify_pipeline.py' 2>/dev/null");
        
        remove("/tmp/classid.txt");
        remove("/tmp/class_progress.json");
        remove("/tmp/class_results.json");
        remove("/tmp/classify_stop");
        
        string json_response = "{\"success\":true, \"message\":\"Stopped\"}";
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json_response.length()) + "\r\n"
               "\r\n" + json_response;
    }
    else if (path == "/api/classify_objects" && method == "POST") {
        string body;
        size_t body_start = request.find("\r\n\r\n");
        if (body_start != string::npos) {
            body = request.substr(body_start + 4);
        }
        
        if (body.empty()) {
            string error = "{\"error\": \"Empty request body\"}";
            return "HTTP/1.1 400 Bad Request\r\n"
                   "Content-Type: application/json\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Content-Length: " + to_string(error.length()) + "\r\n"
                   "\r\n" + error;
        }
        
        ofstream outfile("/tmp/classid.txt");
        int count = 0;
        
        size_t pos = 0;
        while ((pos = body.find("\"source_id\"", pos)) != string::npos) {
            size_t val_start = body.find(":", pos) + 1;
            size_t val_end = body.find_first_of(",}", val_start);
            string source_id = body.substr(val_start, val_end - val_start);
            source_id.erase(remove(source_id.begin(), source_id.end(), ' '), source_id.end());
            source_id.erase(remove(source_id.begin(), source_id.end(), '"'), source_id.end());
            
            string healpix_id = "0", ra = "0", dec = "0";
            size_t obj_end = body.find("}", pos);
            
            size_t hp_pos = body.find("\"healpix_id\"", pos);
            if (hp_pos != string::npos && hp_pos < obj_end) {
                size_t hp_val_start = body.find(":", hp_pos) + 1;
                size_t hp_val_end = body.find_first_of(",}", hp_val_start);
                healpix_id = body.substr(hp_val_start, hp_val_end - hp_val_start);
                healpix_id.erase(remove(healpix_id.begin(), healpix_id.end(), ' '), healpix_id.end());
            }
            
            size_t ra_pos = body.find("\"ra\"", pos);
            if (ra_pos != string::npos && ra_pos < obj_end) {
                size_t ra_val_start = body.find(":", ra_pos) + 1;
                size_t ra_val_end = body.find_first_of(",}", ra_val_start);
                ra = body.substr(ra_val_start, ra_val_end - ra_val_start);
                ra.erase(remove(ra.begin(), ra.end(), ' '), ra.end());
            }
            
            size_t dec_pos = body.find("\"dec\"", pos);
            if (dec_pos != string::npos && dec_pos < obj_end) {
                size_t dec_val_start = body.find(":", dec_pos) + 1;
                size_t dec_val_end = body.find_first_of(",}", dec_val_start);
                dec = body.substr(dec_val_start, dec_val_end - dec_val_start);
                dec.erase(remove(dec.begin(), dec.end(), ' '), dec.end());
            }
            
            outfile << source_id << "," << healpix_id << "," << ra << "," << dec << "\n";
            count++;
            pos = obj_end + 1;
        }
        outfile.close();
        
        cout << "[INFO] Written " << count << " objects to /tmp/classid.txt" << endl;
        
        if (count == 0) {
            string error = "{\"error\": \"No objects found in request\"}";
            return "HTTP/1.1 400 Bad Request\r\n"
                   "Content-Type: application/json\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Content-Length: " + to_string(error.length()) + "\r\n"
                   "\r\n" + error;
        }
        
        system("pkill -9 -f 'classify_pipeline.py' 2>/dev/null");
        
        string task_id = "";
        if (params.find("task_id") != params.end()) {
            task_id = params["task_id"];
        }
        if (task_id.empty()) {
            task_id = "default_" + to_string(time(nullptr));
        }
        
        remove("/tmp/class_progress.json");
        remove("/tmp/class_results.json");
        remove("/tmp/classify_stop");
        
        {
            ofstream progress_file("/tmp/class_progress.json");
            progress_file << "{\"percent\": 1, \"message\": \"Initializing Python env...\", \"step\": \"extract\", \"task_id\": \"" << task_id << "\"}";
            progress_file.close();
            usleep(50000);
        }
        
        string cmd = "nohup bash -c '"
                     "export LD_LIBRARY_PATH=" + config.libs_path + ":$LD_LIBRARY_PATH && "
                     "export TAOS_CFG_DIR=" + config.taos_cfg_path + " && "
                     "export TAOS_LOG_DIR=/tmp/taos_log && "
                     "mkdir -p /tmp/taos_log && "
                     "" + config.python_path + " "
                     "../class/classify_pipeline.py "
                     "--input /tmp/classid.txt "
                     "--output /tmp/class_results.json "
                     "--db " + config.db_name + " "
                     "--task-id '" + task_id + "' "
                     "--threshold " + to_string(config.confidence_threshold) + " "
                     "--web-mode"
                     "' > /tmp/classify_pipeline.log 2>&1 &";
        
        system(cmd.c_str());
        
        cout << "[INFO] Started classification background task." << endl;
        
        string result = "{\"started\": true, \"count\": " + to_string(count) + ", \"message\": \"Task started\"}";
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(result.length()) + "\r\n"
               "\r\n" + result;
    }
    else if (path == "/api/classify_results") {
        string result_path = "/tmp/class_results.json";
        ifstream file(result_path);
        string json_response = "{\"results\": [], \"count\": 0}";
        if (file.is_open()) {
            stringstream ss;
            ss << file.rdbuf();
            json_response = ss.str();
        }
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json_response.length()) + "\r\n"
               "\r\n" + json_response;
    }
    else if (method == "OPTIONS") {
        return "HTTP/1.1 200 OK\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
               "Access-Control-Allow-Headers: Content-Type\r\n"
               "Content-Length: 0\r\n"
               "\r\n";
    }
    else if (path == "/api/classify_results") {
        int limit = 1000;
        if (params.find("limit") != params.end()) {
            limit = stoi(params["limit"]);
        }
        
        string result_file = "/tmp/classify_results_" + to_string(limit) + ".json";
        ifstream file(result_file);
        string json_result;
        
        if (file.is_open()) {
            stringstream buffer;
            buffer << file.rdbuf();
            json_result = buffer.str();
            file.close();
        } else {
            json_result = "{\"error\": \"No results found for limit=" + to_string(limit) + ". Run classification first.\"}";
        }
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json_result.length()) + "\r\n"
               "\r\n" + json_result;
    }
    else if (path == "/api/length_analysis") {
        string csv_file = "../data/confidence_all_lengths_results.csv";
        ifstream file(csv_file);
        string json_result = "{\"data\": [";
        
        if (file.is_open()) {
            string line;
            bool first_data = true;
            getline(file, line); 
            
            while (getline(file, line)) {
                if (line.empty()) continue;
                
                stringstream ss(line);
                string length, n_samples, accuracy, prob_correct, prob_wrong;
                string var_correct, var_wrong, md_correct, md_wrong;
                string score_correct, score_wrong, score_mean, n_correct, n_wrong;
                
                getline(ss, length, ',');
                getline(ss, n_samples, ',');
                getline(ss, accuracy, ',');
                getline(ss, prob_correct, ',');
                getline(ss, prob_wrong, ',');
                getline(ss, var_correct, ',');
                getline(ss, var_wrong, ',');
                getline(ss, md_correct, ',');
                getline(ss, md_wrong, ',');
                getline(ss, score_correct, ',');
                getline(ss, score_wrong, ',');
                getline(ss, score_mean, ',');
                getline(ss, n_correct, ',');
                getline(ss, n_wrong, ',');
                
                if (!first_data) json_result += ",";
                first_data = false;
                
                json_result += "{";
                json_result += "\"length\":" + length + ",";
                json_result += "\"n_samples\":" + n_samples + ",";
                json_result += "\"accuracy\":" + accuracy + ",";
                json_result += "\"prob_correct\":" + prob_correct + ",";
                json_result += "\"prob_wrong\":" + prob_wrong + ",";
                json_result += "\"var_correct\":" + var_correct + ",";
                json_result += "\"var_wrong\":" + var_wrong + ",";
                json_result += "\"md_correct\":" + md_correct + ",";
                json_result += "\"md_wrong\":" + md_wrong + ",";
                json_result += "\"score_correct\":" + score_correct + ",";
                json_result += "\"score_wrong\":" + score_wrong + ",";
                json_result += "\"score_mean\":" + score_mean + ",";
                json_result += "\"n_correct\":" + n_correct + ",";
                json_result += "\"n_wrong\":" + n_wrong;
                json_result += "}";
            }
            file.close();
        }
        json_result += "]}";
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json_result.length()) + "\r\n"
               "\r\n" + json_result;
    }
    
    // ========== Configuration Management API ==========
    else if (path == "/api/config" && method == "GET") {
        string json = config_to_json();
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json.length()) + "\r\n"
               "\r\n" + json;
    }
    else if (path == "/api/config" && method == "POST") {
        string body;
        size_t body_start = request.find("\r\n\r\n");
        if (body_start != string::npos) {
            body = request.substr(body_start + 4);
        }
        
        string new_db_name = json_get_string(body, "db_name");
        if (!new_db_name.empty()) config.db_name = new_db_name;
        
        string new_db_host = json_get_string(body, "db_host");
        if (!new_db_host.empty()) config.db_host = new_db_host;
        
        int new_db_port = json_get_int(body, "db_port", 0);
        if (new_db_port > 0) config.db_port = new_db_port;
        
        double new_threshold = json_get_double(body, "confidence_threshold", -1);
        if (new_threshold >= 0 && new_threshold <= 1) config.confidence_threshold = new_threshold;
        
        save_config();
        
        string result = "{\"success\":true,\"message\":\"Config updated. Restart required.\",\"config\":" + config_to_json() + "}";
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(result.length()) + "\r\n"
               "\r\n" + result;
    }
    else if (path == "/api/config/reload") {
        // Reload configuration
        load_config();
        
        // Reconnect to database
        if (conn) {
            taos_close(conn);
            conn = nullptr;
        }
        connect_to_database();
        
        string result = "{\"success\":true,\"message\":\"Config reloaded and database reconnected.\",\"config\":" + config_to_json() + "}";
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(result.length()) + "\r\n"
               "\r\n" + result;
    }
    // ==================== Database Management API ====================
    else if (path == "/api/database/drop" && method == "POST") {
        size_t body_pos = request.find("\r\n\r\n");
        string body = (body_pos != string::npos) ? request.substr(body_pos + 4) : "";
        string db_name = json_get_string(body, "db_name");
        
        if (db_name.empty()) {
            string err = "{\"success\":false,\"error\":\"Missing db_name\"}";
            return "HTTP/1.1 400 Bad Request\r\n"
                   "Content-Type: application/json\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Content-Length: " + to_string(err.length()) + "\r\n"
                   "\r\n" + err;
        }
        
        // Prevent deletion of system databases
        if (db_name == "information_schema" || db_name == "performance_schema") {
            string err = "{\"success\":false,\"error\":\"Cannot drop system database\"}";
            return "HTTP/1.1 400 Bad Request\r\n"
                   "Content-Type: application/json\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Content-Length: " + to_string(err.length()) + "\r\n"
                   "\r\n" + err;
        }
        
        string sql = "DROP DATABASE IF EXISTS " + db_name;
        TAOS_RES* res = taos_query(conn, sql.c_str());
        int code = taos_errno(res);
        string errmsg = taos_errstr(res);
        taos_free_result(res);
        
        string result;
        if (code == 0) {
            result = "{\"success\":true,\"message\":\"Database " + db_name + " dropped\"}";
        } else {
            result = "{\"success\":false,\"error\":\"" + errmsg + "\"}";
        }
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(result.length()) + "\r\n"
               "\r\n" + result;
    }
    // ==================== Data Import API ====================
    else if (path == "/api/import/start" && method == "POST") {
        size_t body_pos = request.find("\r\n\r\n");
        string body = (body_pos != string::npos) ? request.substr(body_pos + 4) : "";
        
        string type = json_get_string(body, "type");
        string data_path = json_get_string(body, "path");
        string coords_path = json_get_string(body, "coords_path");
        string db_name = json_get_string(body, "db_name");
        int nside = json_get_int(body, "nside", 64);
        
        if (data_path.empty() || coords_path.empty()) {
            string err = "{\"success\":false,\"error\":\"Missing path or coords_path\"}";
            return "HTTP/1.1 400 Bad Request\r\n"
                   "Content-Type: application/json\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Content-Length: " + to_string(err.length()) + "\r\n"
                   "\r\n" + err;
        }
        
        // Stop previous import task
        system("pkill -9 -f 'catalog_importer' 2>/dev/null");
        system("pkill -9 -f 'lightcurve_importer' 2>/dev/null");
        remove("/tmp/import_progress.json");
        remove("/tmp/import.log");
        remove("/tmp/import_stop");
        
        // Initialize progress file
        {
            ofstream progress("/tmp/import_progress.json");
            progress << "{\"percent\":0,\"message\":\"Starting import...\",\"status\":\"running\"}";
            progress.close();
        }
        
        // Set library path to ensure importer can find all dependencies
        string libs_path = "../libs";
        string env_prefix = "LD_LIBRARY_PATH=" + libs_path + ":$LD_LIBRARY_PATH ";
        
        string cmd;
        if (type == "catalog") {
            cmd = "nohup bash -c '" + env_prefix + 
                  "../insert/catalog_importer "
                  "--catalogs " + data_path + " "
                  "--coords " + coords_path + " "
                  "--db " + db_name + " "
                  "--nside " + to_string(nside) + 
                  "' > /tmp/import.log 2>&1 &";
        } else {
            cmd = "nohup bash -c '" + env_prefix + 
                  "../insert/lightcurve_importer "
                  "--lightcurves_dir " + data_path + " "
                  "--coords " + coords_path + " "
                  "--db " + db_name + 
                  "' > /tmp/import.log 2>&1 &";
        }
        
        system(cmd.c_str());
        
        string result = "{\"success\":true,\"message\":\"Import task started\"}";
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(result.length()) + "\r\n"
               "\r\n" + result;
    }
    else if (path == "/api/import/progress") {
        string json_response = "{\"percent\":0,\"message\":\"No task\",\"status\":\"idle\"}";
        ifstream file("/tmp/import_progress.json");
        if (file.is_open()) {
            stringstream ss;
            ss << file.rdbuf();
            json_response = ss.str();
            file.close();
        }
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json_response.length()) + "\r\n"
               "\r\n" + json_response;
    }
    else if (path == "/api/import/stop" && method == "POST") {
        ofstream stop_file("/tmp/import_stop");
        stop_file << "stop";
        stop_file.close();
        
        system("pkill -9 -f 'catalog_importer' 2>/dev/null");
        system("pkill -9 -f 'lightcurve_importer' 2>/dev/null");
        
        // Explicitly update progress to stopped
        {
            ofstream progress("/tmp/import_progress.json");
            progress << "{\"percent\":0,\"message\":\"Manually stopped\",\"status\":\"stopped\"}";
            progress.close();
        }
        
        string result = "{\"success\":true,\"message\":\"Import stopped\"}";
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(result.length()) + "\r\n"
               "\r\n" + result;
    }
    
    // ==================== Auto-classification API ====================
    // Trigger candidate detection
    else if (path == "/api/auto_classify/check" && method == "POST") {
        // Parse request body to get database name
        size_t body_pos = request.find("\r\n\r\n");
        string body = (body_pos != string::npos) ? request.substr(body_pos + 4) : "";
        string db_name = json_get_string(body, "db_name");
        if (db_name.empty()) db_name = config.db_name;
        
        // Run detection program
        int ret = run_check_candidates(db_name);
        
        // Read results
        string candidate_file = get_auto_classify_candidate_file(db_name);
        int count = count_candidates(candidate_file);
        
        string json;
        if (ret == 0) {
            json = "{\"success\":true,\"count\":" + to_string(count) + ",\"message\":\"Detection complete\",\"db_name\":\"" + db_name + "\"}";
        } else {
            json = "{\"success\":false,\"count\":" + to_string(count) + ",\"error\":\"Detection program failed\",\"db_name\":\"" + db_name + "\"}";
        }
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json.length()) + "\r\n"
               "\r\n" + json;
    }
    // Get current candidate count (without triggering detection)
    else if (path == "/api/auto_classify/candidates") {
        // Get database name from params
        string db_name = config.db_name;
        if (params.find("db_name") != params.end() && !params["db_name"].empty()) {
            db_name = params["db_name"];
        }
        
        string candidate_file = get_auto_classify_candidate_file(db_name);
        int count = count_candidates(candidate_file);
        
        string json = "{\"count\":" + to_string(count) + ",\"file\":\"" + candidate_file + "\",\"db_name\":\"" + db_name + "\"}";
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json.length()) + "\r\n"
               "\r\n" + json;
    }
    else if (path == "/api/auto_classify/start" && method == "POST") {
        // Parse request body
        size_t body_pos = request.find("\r\n\r\n");
        string body = (body_pos != string::npos) ? request.substr(body_pos + 4) : "";
        
        // Get database name (prefer request parameter, otherwise use config)
        string db_name = json_get_string(body, "db_name");
        if (db_name.empty()) db_name = config.db_name;
        
        string candidate_file = get_auto_classify_candidate_file(db_name);
        int count = count_candidates(candidate_file);
        
        if (count == 0) {
            string err = "{\"success\":false,\"error\":\"Queue is empty, no objects to classify\",\"db_name\":\"" + db_name + "\"}";
            return "HTTP/1.1 400 Bad Request\r\n"
                   "Content-Type: application/json\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Content-Length: " + to_string(err.length()) + "\r\n"
                   "\r\n" + err;
        }
        
        // Stop previous task
        system("pkill -9 -f 'auto_classify.py' 2>/dev/null");
        remove("/tmp/auto_classify_progress.json");
        remove("/tmp/auto_classify_stop");
        
        bool resume = json_get_bool(body, "resume", false);
        int batch_size = json_get_int(body, "batch_size", 5000);
        
        // Initialize progress
        {
            ofstream progress("/tmp/auto_classify_progress.json");
            progress << "{\"percent\":0,\"message\":\"Starting...\",\"status\":\"running\",\"db_name\":\"" << db_name << "\"}";
            progress.close();
        }
        
        // Start auto-classification task
        string cmd = "nohup bash -c '"
                     "export LD_LIBRARY_PATH=" + config.libs_path + ":$LD_LIBRARY_PATH && "
                     "export TAOS_CFG_DIR=" + config.taos_cfg_path + " && "
                     "" + config.python_path + " "
                     "../class/auto_classify.py "
                     "--candidate-file " + candidate_file + " "
                     "--db " + db_name + " "
                     "--threshold " + to_string(config.confidence_threshold) + " "
                     "--batch-size " + to_string(batch_size) +
                     (resume ? " --resume" : "") +
                     "' > /tmp/auto_classify.log 2>&1 &";
        
        system(cmd.c_str());
        
        string result = "{\"success\":true,\"count\":" + to_string(count) + ",\"message\":\"Auto-classification task started\",\"db_name\":\"" + db_name + "\"}";
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(result.length()) + "\r\n"
               "\r\n" + result;
    }
    else if (path == "/api/auto_classify/stop" && method == "POST") {
        // Write stop signal
        ofstream stop_file("/tmp/auto_classify_stop");
        stop_file << "stop" << endl;
        stop_file.close();
        
        usleep(500000);  // 500ms
        
        system("pkill -9 -f 'auto_classify.py' 2>/dev/null");
        
        string result = "{\"success\":true,\"message\":\"Stop signal sent\"}";
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(result.length()) + "\r\n"
               "\r\n" + result;
    }
    else if (path == "/api/auto_classify/status") {
        string progress_path = "/tmp/auto_classify_progress.json";
        ifstream file(progress_path);
        string json_response = "{\"percent\":0,\"message\":\"Not running\",\"status\":\"idle\"}";
        if (file.is_open()) {
            stringstream ss;
            ss << file.rdbuf();
            string content = ss.str();
            if (!content.empty()) {
                json_response = content;
            }
        }
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json_response.length()) + "\r\n"
               "\r\n" + json_response;
    }
    else if (path == "/api/auto_classify/results") {
        string candidate_file = get_auto_classify_candidate_file(config.db_name);
        string result_file = candidate_file;
        size_t pos = result_file.rfind(".csv");
        if (pos != string::npos) {
            result_file = result_file.substr(0, pos) + "_results.json";
        }
        
        ifstream file(result_file);
        string json_response = "{\"results\":[],\"count\":0}";
        if (file.is_open()) {
            stringstream ss;
            ss << file.rdbuf();
            string content = ss.str();
            if (!content.empty()) {
                json_response = content;
            }
        }
        
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Content-Length: " + to_string(json_response.length()) + "\r\n"
               "\r\n" + json_response;
    }
    
    return "HTTP/1.1 404 Not Found\r\n\r\nNot Found";
}

void handle_sse_stream(int client_socket, const string& request) {
    string target_task_id = "";
    size_t q_pos = request.find("?");
    if (q_pos != string::npos) {
        size_t id_pos = request.find("task_id=", q_pos);
        if (id_pos != string::npos) {
            size_t end = request.find_first_of(" \r\n&", id_pos);
            if (end == string::npos) end = request.length(); 
            target_task_id = request.substr(id_pos + 8, end - (id_pos + 8));
        }
    }

    string header = "HTTP/1.1 200 OK\r\n"
                   "Content-Type: text/event-stream\r\n"
                   "Cache-Control: no-cache\r\n"
                   "Connection: keep-alive\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "\r\n";
    send(client_socket, header.c_str(), header.length(), 0);
    
    string last_content;
    string last_sent_data = "";
    
    struct timespec start_ts;
    clock_gettime(CLOCK_REALTIME, &start_ts);
    
    time_t last_heartbeat = time(nullptr);
    
    while (true) {
        struct stat st;
        bool file_exists = (stat("/tmp/class_progress.json", &st) == 0);
        
        string json_data;
        
        if (file_exists) {
            ifstream file("/tmp/class_progress.json");
            if (file.is_open()) {
                stringstream ss;
                ss << file.rdbuf();
                string content = ss.str();
                if (!content.empty()) {
                    json_data = content;
                    last_content = content;
                } else if (!last_content.empty()) {
                    json_data = last_content;
                } else {
                    json_data = "{\"percent\":0, \"message\":\"Waiting...\", \"step\":\"\"}";
                }
            } else if (!last_content.empty()) {
                json_data = last_content;
            } else {
                json_data = "{\"percent\":0, \"message\":\"Waiting...\", \"step\":\"\"}";
            }
        } else {
            json_data = "{\"percent\":0, \"message\":\"Starting...\", \"step\":\"init\"}";
        }
        
        if (!target_task_id.empty()) {
            string file_task_id = json_get_string(json_data, "task_id");
            if (file_task_id != target_task_id) {
                usleep(50000);
                continue;
            }
        } else {
            bool is_complete = (json_data.find("\"percent\": 100") != string::npos || 
                               json_data.find("\"percent\":100") != string::npos ||
                               json_data.find("\"step\":\"done\"") != string::npos ||
                               json_data.find("\"step\": \"done\"") != string::npos);
            
            if (file_exists && is_complete) {
                bool is_old = false;
                if (st.st_mtim.tv_sec < start_ts.tv_sec) {
                    is_old = true;
                } else if (st.st_mtim.tv_sec == start_ts.tv_sec) {
                    if (st.st_mtim.tv_nsec <= start_ts.tv_nsec) {
                        is_old = true;
                    }
                }
                
                if (is_old) {
                    usleep(50000);
                    continue;
                }
            }
        }
        
        if (json_data == last_sent_data) {
            time_t now = time(nullptr);
            if (now - last_heartbeat >= 2) { 
                string heartbeat = ": keep-alive\n\n";
                ssize_t sent = send(client_socket, heartbeat.c_str(), heartbeat.length(), MSG_NOSIGNAL);
                if (sent < 0) {
                    break;
                }
                last_heartbeat = now;
            }
            usleep(50000);
            continue;
        }
        
        last_sent_data = json_data;
        last_heartbeat = time(nullptr); 
        
        string event = "data: " + json_data + "\n\n";
        ssize_t sent = send(client_socket, event.c_str(), event.length(), MSG_NOSIGNAL);
        if (sent < 0) {
            break;
        }
        
        bool is_complete_now = (json_data.find("\"percent\": 100") != string::npos || 
                               json_data.find("\"percent\":100") != string::npos ||
                               json_data.find("\"step\":\"done\"") != string::npos ||
                               json_data.find("\"step\": \"done\"") != string::npos);

        if (is_complete_now) {
            usleep(500000);
            break;
        }
        
        usleep(50000);
    }
}

void handle_auto_classify_stream(int client_socket, const string& request) {
    string header = "HTTP/1.1 200 OK\r\n"
                   "Content-Type: text/event-stream\r\n"
                   "Cache-Control: no-cache\r\n"
                   "Connection: keep-alive\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "\r\n";
    send(client_socket, header.c_str(), header.length(), 0);
    
    string last_sent_data = "";
    time_t last_heartbeat = time(nullptr);
    
    while (true) {
        string json_data;
        
        ifstream file("/tmp/auto_classify_progress.json");
        if (file.is_open()) {
            stringstream ss;
            ss << file.rdbuf();
            json_data = ss.str();
            file.close();
        }
        
        if (json_data.empty()) {
            json_data = "{\"percent\":0,\"message\":\"Waiting...\",\"status\":\"idle\"}";
        }
        
        if (json_data != last_sent_data) {
            string event = "data: " + json_data + "\n\n";
            ssize_t sent = send(client_socket, event.c_str(), event.length(), MSG_NOSIGNAL);
            if (sent < 0) break;
            
            last_sent_data = json_data;
            last_heartbeat = time(nullptr);
            
            // Check if completed or paused
            if (json_data.find("\"status\":\"completed\"") != string::npos ||
                json_data.find("\"status\":\"paused\"") != string::npos ||
                json_data.find("\"status\":\"error\"") != string::npos) {
                usleep(500000);
                break;
            }
        } else {
            if (time(nullptr) - last_heartbeat >= 2) {
                string heartbeat = ": keep-alive\n\n";
                if (send(client_socket, heartbeat.c_str(), heartbeat.length(), MSG_NOSIGNAL) < 0) break;
                last_heartbeat = time(nullptr);
            }
        }
        usleep(100000);
    }
}

void handle_import_stream(int client_socket, const string& request) {
    string header = "HTTP/1.1 200 OK\r\n"
                   "Content-Type: text/event-stream\r\n"
                   "Cache-Control: no-cache\r\n"
                   "Connection: keep-alive\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "\r\n";
    send(client_socket, header.c_str(), header.length(), 0);
    
    string last_sent_data = "";
    time_t last_heartbeat = time(nullptr);
    time_t stream_start_time = time(nullptr);
    
    while (true) {
        string json_data;
        
        // Check if progress file exists and is recent (within last 60 seconds)
        struct stat file_stat;
        bool file_is_recent = false;
        if (stat("/tmp/import_progress.json", &file_stat) == 0) {
            file_is_recent = (difftime(time(nullptr), file_stat.st_mtime) < 60);
        }
        
        ifstream file("/tmp/import_progress.json");
        if (file.is_open()) {
            stringstream ss;
            ss << file.rdbuf();
            json_data = ss.str();
            file.close();
            
            // If file is old and shows completed, treat as idle (waiting for new import)
            if (!file_is_recent && 
                (json_data.find("\"status\":\"completed\"") != string::npos ||
                 json_data.find("\"status\":\"stopped\"") != string::npos)) {
                json_data = "{\"percent\":0,\"message\":\"Ready to import...\",\"status\":\"idle\"}";
            }
        }
        
        if (json_data.empty()) {
            json_data = "{\"percent\":0,\"message\":\"Waiting...\",\"status\":\"idle\"}";
        }
        
        // Read Log Tail (only if log file is recent - within 60 seconds)
        string log_tail = "";
        struct stat log_stat;
        bool log_is_recent = false;
        if (stat("/tmp/import.log", &log_stat) == 0) {
            log_is_recent = (difftime(time(nullptr), log_stat.st_mtime) < 60);
        }
        
        if (log_is_recent) {
            // Read last 4KB bytes to ensure we get the latest progress
            FILE* pipe = popen("tail -c 4096 /tmp/import.log 2>/dev/null", "r");
            if (pipe) {
                char buffer[8192];
                size_t n = fread(buffer, 1, sizeof(buffer)-1, pipe);
                if (n > 0) {
                    buffer[n] = '\0';
                    log_tail = string(buffer);
                }
                pclose(pipe);
            }
        }
        
        if (!log_tail.empty() && json_data.length() > 0 && json_data.back() == '}') {
            json_data.pop_back();
            json_data += ",\"log\":\"" + json_escape(log_tail) + "\"}";
        }
        
        if (json_data != last_sent_data) {
            string event = "data: " + json_data + "\n\n";
            ssize_t sent = send(client_socket, event.c_str(), event.length(), MSG_NOSIGNAL);
            if (sent < 0) break;
            
            last_sent_data = json_data;
            last_heartbeat = time(nullptr);
            
            if (json_data.find("\"status\":\"completed\"") != string::npos || 
                json_data.find("\"status\": \"completed\"") != string::npos ||
                json_data.find("\"status\":\"stopped\"") != string::npos) {
                usleep(500000);
                break;
            }
        } else {
            if (time(nullptr) - last_heartbeat >= 2) {
                string heartbeat = ": keep-alive\n\n";
                if (send(client_socket, heartbeat.c_str(), heartbeat.length(), MSG_NOSIGNAL) < 0) break;
                last_heartbeat = time(nullptr);
            }
        }
        usleep(100000);
    }
}

void handle_client(int client_socket) {
    try {
        string request;
        char buffer[8192];
        
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            close(client_socket);
            return;
        }
        buffer[bytes_received] = '\0';
        request = string(buffer);
        
        cout << "[INFO] Received request: " << request.substr(0, request.find('\n')) << endl;
        
        if (request.find("GET /api/classify_stream") != string::npos) {
            handle_sse_stream(client_socket, request);
            close(client_socket);
            return;
        }
        
        if (request.find("GET /api/import/stream") != string::npos) {
            handle_import_stream(client_socket, request);
            close(client_socket);
            return;
        }
        
        if (request.find("GET /api/auto_classify/stream") != string::npos) {
            handle_auto_classify_stream(client_socket, request);
            close(client_socket);
            return;
        }
        
        if (request.find("POST") == 0 || request.find("post") == 0) {
            size_t cl_pos = request.find("Content-Length:");
            if (cl_pos == string::npos) cl_pos = request.find("content-length:");
            
            int content_length = 0;
            if (cl_pos != string::npos) {
                size_t cl_end = request.find("\r\n", cl_pos);
                string cl_str = request.substr(cl_pos + 15, cl_end - cl_pos - 15);
                content_length = stoi(cl_str);
            }
            
            size_t body_start = request.find("\r\n\r\n");
            if (body_start != string::npos) {
                body_start += 4;
                int body_received = request.length() - body_start;
                
                while (body_received < content_length) {
                    bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
                    if (bytes_received <= 0) break;
                    buffer[bytes_received] = '\0';
                    request += string(buffer);
                    body_received += bytes_received;
                }
            }
        }
        
        string response = handle_request(request);
        
        const char* data = response.c_str();
        size_t total_sent = 0;
        size_t total_size = response.length();
        
        while (total_sent < total_size) {
            ssize_t sent = send(client_socket, data + total_sent, total_size - total_sent, 0);
            if (sent < 0) {
                cerr << "[ERROR] Failed to send response" << endl;
                break;
            }
            total_sent += sent;
        }
    } catch (const std::exception& e) {
        cerr << "[ERROR] Exception handling client: " << e.what() << endl;
    } catch (...) {
        cerr << "[ERROR] Unknown exception handling client" << endl;
    }
    
    close(client_socket);
}

int main() {
    // Note: Relative paths assume CWD is TDlight/web/
    setenv("TAOS_CFG_DIR", "../runtime/taos_home/cfg", 0);
    setenv("TAOS_LOG_DIR", "../runtime/taos_home/log", 0);
    
    cout << "=== TD-light Web API Service ===" << endl;
    
    load_config();
    
    if (!connect_to_database()) {
        return 1;
    }
    
    int     server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        cerr << "[ERROR] Failed to create socket" << endl;
        return 1;
    }
    
    // Prevent child processes (like importers) from inheriting the socket
    int flags = fcntl(server_socket, F_GETFD);
    if (flags != -1) {
        fcntl(server_socket, F_SETFD, flags | FD_CLOEXEC);
    }
    
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    setsockopt(server_socket, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(config.web_port);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "[ERROR] Failed to bind port " << config.web_port << ": " << strerror(errno) << endl;
        return 1;
    }
    
    if (listen(server_socket, 128) < 0) {
        cerr << "[ERROR] Failed to listen" << endl;
        return 1;
    }
    
    cout << "[INFO] Web API listening on port " << config.web_port << endl;
    
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            cerr << "[ERROR] Failed to accept connection" << endl;
            continue;
        }
        
        thread client_thread(handle_client, client_socket);
        client_thread.detach();
    }
    
    close(server_socket);
    taos_close(conn);
    return 0;
}