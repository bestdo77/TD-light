/*
 * Catalog Data Importer with Integrated Cross-Match
 * Features:
 * - Automatic cross-match with existing objects in database
 * - Matched objects use database source_id
 * - New objects get hash-based unique ID
 * - Uses STMT API + Direct Assignment + Two-Phase
 * 
 * Compile: g++ -std=c++17 -O3 -march=native catalog_importer.cpp -o catalog_importer -ltaos -lhealpix_cxx -lpthread
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cmath>
#include <taos.h>
#include <healpix_cxx/healpix_base.h>
#include <healpix_cxx/pointing.h>

namespace fs = std::filesystem;
using namespace std;
using namespace std::chrono;

// ==================== Configuration Parameters ====================
int NUM_THREADS = 16;                     // Number of parallel threads (default: 16 for web)
int NUM_VGROUPS = 32;                     // Number of virtual groups (default: 32 for compatibility)
constexpr int BATCH_SIZE = 10000;         // Rows per insert batch
constexpr int BUFFER_SIZE = 256;          // Memory buffer per vgroup (MB)
double CROSSMATCH_RADIUS_ARCSEC = 1.0;    // Cross-match radius in arcseconds
bool ENABLE_CROSSMATCH = true;            // Enable automatic cross-match

// Read TDengine host address from environment variable
string get_taos_host() {
    const char* env_host = getenv("TAOS_HOST");
    if (env_host != nullptr && strlen(env_host) > 0) {
        return string(env_host);
    }
    return "localhost";
}

// ==================== Cross-Match Utilities ====================

// Calculate angular distance using Haversine formula (returns arcseconds)
double angular_distance_arcsec(double ra1, double dec1, double ra2, double dec2) {
    double ra1_rad = ra1 * M_PI / 180.0;
    double dec1_rad = dec1 * M_PI / 180.0;
    double ra2_rad = ra2 * M_PI / 180.0;
    double dec2_rad = dec2 * M_PI / 180.0;
    
    double delta_ra = ra2_rad - ra1_rad;
    double delta_dec = dec2_rad - dec1_rad;
    
    double a = sin(delta_dec / 2.0) * sin(delta_dec / 2.0) +
               cos(dec1_rad) * cos(dec2_rad) *
               sin(delta_ra / 2.0) * sin(delta_ra / 2.0);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    
    return c * 180.0 / M_PI * 3600.0;  // Convert to arcseconds
}

// Generate hash-based unique ID for new objects (always positive)
int64_t generate_hash_id(double ra, double dec, int64_t salt = 20260404) {
    uint64_t ra_int = static_cast<uint64_t>(fabs(ra) * 1e6);
    uint64_t dec_int = static_cast<uint64_t>(fabs(dec) * 1e6);
    
    uint64_t hash = 0xcbf29ce484222325ULL;  // FNV offset basis
    
    auto mix = [&](uint64_t val) {
        hash ^= val;
        hash *= 0x100000001b3ULL;  // FNV prime
    };
    
    mix(ra_int);
    mix(dec_int);
    mix(static_cast<uint64_t>(salt));
    
    // Ensure result is positive and within int64_t range
    uint64_t result = hash % 8000000000000000000ULL + 1000000000000000000ULL;
    return static_cast<int64_t>(result);
}

// Database object for cross-match
struct DBObject {
    int64_t source_id;
    double ra, dec;
    long healpix_id;
};

// Spatial index for fast cross-match
struct SpatialIndex {
    unordered_map<long, vector<const DBObject*>> healpix_map;
    int nside;
    Healpix_Base hp;
    
    SpatialIndex(int nside_val) : nside(nside_val), hp(nside_val, NEST, SET_NSIDE) {}
    
    void build(const vector<DBObject>& db_objects) {
        for (const auto& obj : db_objects) {
            healpix_map[obj.healpix_id].push_back(&obj);
        }
    }
    
    vector<long> get_neighboring_pixels(double ra, double dec) const {
        double theta = (90.0 - dec) * M_PI / 180.0;
        double phi = ra * M_PI / 180.0;
        if (theta < 0) theta = 0;
        if (theta > M_PI) theta = M_PI;
        pointing pt(theta, phi);
        
        long current_pix = hp.ang2pix(pt);
        vector<long> neighbors;
        neighbors.push_back(current_pix);
        
        fix_arr<int, 8> pix_neighbors;
        hp.neighbors(current_pix, pix_neighbors);
        for (int i = 0; i < 8; i++) {
            if (pix_neighbors[i] >= 0) {
                neighbors.push_back(static_cast<long>(pix_neighbors[i]));
            }
        }
        
        return neighbors;
    }
    
    const DBObject* find_match(double ra, double dec, double max_sep_arcsec, double& out_sep) const {
        const DBObject* best_match = nullptr;
        double best_sep = max_sep_arcsec;
        
        auto candidates = get_neighboring_pixels(ra, dec);
        
        for (long pix : candidates) {
            auto it = healpix_map.find(pix);
            if (it == healpix_map.end()) continue;
            
            for (const DBObject* obj : it->second) {
                double sep = angular_distance_arcsec(ra, dec, obj->ra, obj->dec);
                if (sep < best_sep) {
                    best_sep = sep;
                    best_match = obj;
                }
            }
        }
        
        out_sep = best_sep;
        return best_match;
    }
};

/**
 * Perform cross-match between coordinates and database objects
 * 
 * @param coords_map Input: source_id -> (ra, dec) map
 * @param db_name Database name to query
 * @param super_table Super table name
 * @param nside HEALPix NSIDE parameter
 * @param match_radius Match radius in arcseconds
 * @param enable_crossmatch Whether to enable cross-match
 * @return Map: original_source_id -> unique_source_id
 */
unordered_map<long long, int64_t> perform_crossmatch(
    const unordered_map<long long, pair<double, double>>& coords_map,
    const string& db_name,
    const string& super_table,
    int nside,
    double match_radius,
    bool enable_crossmatch
) {
    unordered_map<long long, int64_t> crossmatch_results;
    
    if (!enable_crossmatch) {
        // If cross-match disabled, use original IDs
        for (const auto& [orig_id, coord] : coords_map) {
            crossmatch_results[orig_id] = orig_id;
        }
        return crossmatch_results;
    }
    
    cout << "\n[INFO] Performing cross-match with database..." << endl;
    auto crossmatch_start = high_resolution_clock::now();
    
    // Load existing objects from database
    string taos_host = get_taos_host();
    TAOS* conn = taos_connect(taos_host.c_str(), "root", "taosdata", db_name.c_str(), 6030);
    
    if (!conn) {
        cerr << "  [WARN] Failed to connect to database for cross-match" << endl;
        cerr << "  [WARN] Using original source_id without cross-match" << endl;
        
        // Fallback: use original IDs
        for (const auto& [orig_id, coord] : coords_map) {
            crossmatch_results[orig_id] = orig_id;
        }
        return crossmatch_results;
    }
    
    string sql = "SELECT source_id, ra, dec, healpix_id FROM " + super_table + 
                " GROUP BY source_id, ra, dec, healpix_id";
    TAOS_RES* res = taos_query(conn, sql.c_str());
    
    if (taos_errno(res) != 0) {
        cerr << "  [WARN] Cross-match query failed: " << taos_errstr(res) << endl;
        cerr << "  [WARN] Using original source_id without cross-match" << endl;
        taos_free_result(res);
        taos_close(conn);
        
        // Fallback: use original IDs
        for (const auto& [orig_id, coord] : coords_map) {
            crossmatch_results[orig_id] = orig_id;
        }
        return crossmatch_results;
    }
    
    // Load database objects
    vector<DBObject> db_objects;
    TAOS_ROW row;
    while ((row = taos_fetch_row(res)) != nullptr) {
        if (row[0] == nullptr || row[1] == nullptr || row[2] == nullptr || row[3] == nullptr) {
            continue;  // Skip invalid rows
        }
        
        DBObject obj;
        obj.source_id = *(int64_t*)row[0];
        obj.ra = *(double*)row[1];
        obj.dec = *(double*)row[2];
        obj.healpix_id = *(int64_t*)row[3];
        db_objects.push_back(obj);
    }
    taos_free_result(res);
    taos_close(conn);
    
    cout << "  [INFO] Loaded " << db_objects.size() << " objects from database" << endl;
    
    // Build spatial index
    SpatialIndex index(nside);
    index.build(db_objects);
    
    // Perform cross-match for each coordinate
    atomic<int> matched_count{0};
    atomic<int> new_count{0};
    
    for (const auto& [orig_id, coord] : coords_map) {
        double sep = 0;
        const DBObject* match = index.find_match(coord.first, coord.second, match_radius, sep);
        
        if (match != nullptr) {
            // Matched - use database source_id
            crossmatch_results[orig_id] = match->source_id;
            matched_count++;
        } else {
            // No match - generate hash ID
            crossmatch_results[orig_id] = generate_hash_id(coord.first, coord.second, orig_id);
            new_count++;
        }
    }
    
    auto crossmatch_end = high_resolution_clock::now();
    double crossmatch_time = duration_cast<milliseconds>(crossmatch_end - crossmatch_start).count() / 1000.0;
    
    cout << "  [OK] Cross-match complete (" << fixed << setprecision(2) << crossmatch_time << "s)" << endl;
    cout << "     - Matched: " << matched_count << " (" 
         << (coords_map.size() > 0 ? matched_count * 100.0 / coords_map.size() : 0) << "%)" << endl;
    cout << "     - New objects: " << new_count << " ("
         << (coords_map.size() > 0 ? new_count * 100.0 / coords_map.size() : 0) << "%)" << endl;
    
    return crossmatch_results;
}

struct Record {
    int64_t ts_ms;           // Observation time (millisecond timestamp)
    string band;             // Band
    double mag;              // Magnitude
    double flux;             // Flux
    double flux_error;       // Flux error
    double mag_error;        // Magnitude error
    double jd_tcb;           // Julian Date (TCB)
};

struct SubTable {
    string table_name;       // Child table name t_<source_id>
    long healpix_id;         // HEALPix ID
    long long source_id;     // Source ID
    string cls;              // Classification label (as TAG)
    double ra, dec;          // Coordinates
    vector<Record> records;  // Observation records
};

struct PerfStats {
    atomic<long long> total_records{0};
    atomic<long long> inserted_records{0};
    atomic<int> table_count{0};
    atomic<int> tables_created{0};
};

// Global stop flag for graceful shutdown
atomic<bool> stop_requested{false};

mutex cout_mutex;

vector<string> split(const string& line, char delim) {
    vector<string> result;
    stringstream ss(line);
    string item;
    while (getline(ss, item, delim)) result.push_back(item);
    return result;
}

// SQL string escape (prevent SQL injection)
string sql_escape(const string& str) {
    string result;
    result.reserve(str.size() + 10);
    for (char c : str) {
        if (c == '\'') {
            result += "''";  // SQL escape: ' -> ''
        } else if (c == '\\') {
            result += "\\\\";
        } else {
            result += c;
        }
    }
    return result;
}

// Note: calculateMagError removed - not used in current implementation

// ==================== Phase 1: Parallel Table Creation ====================
void create_tables_worker(int thread_id, const vector<SubTable*>& tables, 
                          size_t start, size_t end, 
                          const string& db_name, const string& super_table,
                          PerfStats& stats) {
    string taos_host = get_taos_host();
    TAOS* conn = taos_connect(taos_host.c_str(), "root", "taosdata", db_name.c_str(), 6030);
    if (!conn) {
        lock_guard<mutex> lock(cout_mutex);
        cerr << "[ERROR] Thread " << thread_id << " connection failed" << endl;
        return;
    }
    
    for (size_t i = start; i < end; ++i) {
        // Check stop flag
        if (stop_requested.load()) {
            break;
        }
        
        const SubTable* st = tables[i];
        
        // FIX: Escape cls to prevent SQL injection
        string escaped_cls = sql_escape(st->cls);
        
        stringstream sql;
        sql << "CREATE TABLE IF NOT EXISTS " << st->table_name 
            << " USING " << super_table 
            << " TAGS(" << st->healpix_id << "," << st->source_id << "," 
            << fixed << setprecision(6) << st->ra << "," << st->dec << ",'" << escaped_cls << "')";
        
        TAOS_RES* res = taos_query(conn, sql.str().c_str());
        if (taos_errno(res) != 0) {
            lock_guard<mutex> lock(cout_mutex);
            cerr << "[ERROR] Table creation failed " << st->table_name << ": " << taos_errstr(res) << endl;
        } else {
            stats.tables_created++;
        }
        taos_free_result(res);
    }
    
    taos_close(conn);
}

// ==================== Phase 2: STMT API Insertion ====================
void insert_worker(int thread_id, const vector<SubTable*>& tables,
                   size_t start, size_t end,
                   const string& db_name, PerfStats& stats) {
    string taos_host = get_taos_host();
    TAOS* conn = taos_connect(taos_host.c_str(), "root", "taosdata", db_name.c_str(), 6030);
    if (!conn) {
        lock_guard<mutex> lock(cout_mutex);
        cerr << "[ERROR] Thread " << thread_id << " connection failed" << endl;
        return;
    }
    
    TAOS_STMT* stmt = taos_stmt_init(conn);
    if (!stmt) {
        lock_guard<mutex> lock(cout_mutex);
        cerr << "[ERROR] Thread " << thread_id << " STMT initialization failed" << endl;
        taos_close(conn);
        return;
    }
    
    // Prepare STMT
    const char* sql = "INSERT INTO ? VALUES(?,?,?,?,?,?,?)";
    if (taos_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        lock_guard<mutex> lock(cout_mutex);
        cerr << "[ERROR] STMT prepare failed: " << taos_stmt_errstr(stmt) << endl;
        taos_stmt_close(stmt);
        taos_close(conn);
        return;
    }
    
    // Allocate buffers
    vector<int64_t> ts_buf(BATCH_SIZE);
    vector<char> band_buf(BATCH_SIZE * 17);  // NCHAR(16) + null
    vector<int32_t> band_len(BATCH_SIZE);
    vector<double> mag_buf(BATCH_SIZE);
    vector<double> mag_error_buf(BATCH_SIZE);
    vector<double> flux_buf(BATCH_SIZE);
    vector<double> flux_error_buf(BATCH_SIZE);
    vector<double> jd_buf(BATCH_SIZE);
    
    for (size_t i = start; i < end; ++i) {
        // Check stop flag
        if (stop_requested.load()) {
            break;
        }
        
        const SubTable* st = tables[i];
        if (st->records.empty()) continue;
        
        // Set table name
        if (taos_stmt_set_tbname(stmt, st->table_name.c_str()) != 0) {
            lock_guard<mutex> lock(cout_mutex);
            cerr << "[ERROR] Set table name failed " << st->table_name << ": " << taos_stmt_errstr(stmt) << endl;
            continue;
        }
        
        size_t total = st->records.size();
        for (size_t batch_start = 0; batch_start < total; batch_start += BATCH_SIZE) {
            // Check stop flag in inner loop
            if (stop_requested.load()) {
                break;
            }
            
            size_t batch_end = min(batch_start + BATCH_SIZE, total);
            int batch_count = batch_end - batch_start;
            
            // Fill buffers
            for (int j = 0; j < batch_count; ++j) {
                const Record& r = st->records[batch_start + j];
                ts_buf[j] = r.ts_ms;
                
                // Process band string
                memset(&band_buf[j * 17], 0, 17);
                strncpy(&band_buf[j * 17], r.band.c_str(), 16);
                band_len[j] = r.band.length();
                
                mag_buf[j] = r.mag;
                mag_error_buf[j] = r.mag_error;
                flux_buf[j] = r.flux;
                flux_error_buf[j] = r.flux_error;
                jd_buf[j] = r.jd_tcb;
            }
            
            // Bind parameters
            TAOS_MULTI_BIND binds[7];
            memset(binds, 0, sizeof(binds));
            
            // ts (TIMESTAMP)
            binds[0].buffer_type = TSDB_DATA_TYPE_TIMESTAMP;
            binds[0].buffer = ts_buf.data();
            binds[0].buffer_length = sizeof(int64_t);
            binds[0].length = nullptr;
            binds[0].is_null = nullptr;
            binds[0].num = batch_count;
            
            // band (NCHAR)
            binds[1].buffer_type = TSDB_DATA_TYPE_NCHAR;
            binds[1].buffer = band_buf.data();
            binds[1].buffer_length = 17;
            binds[1].length = band_len.data();
            binds[1].is_null = nullptr;
            binds[1].num = batch_count;
            
            // mag (DOUBLE)
            binds[2].buffer_type = TSDB_DATA_TYPE_DOUBLE;
            binds[2].buffer = mag_buf.data();
            binds[2].buffer_length = sizeof(double);
            binds[2].length = nullptr;
            binds[2].is_null = nullptr;
            binds[2].num = batch_count;
            
            // mag_error (DOUBLE)
            binds[3].buffer_type = TSDB_DATA_TYPE_DOUBLE;
            binds[3].buffer = mag_error_buf.data();
            binds[3].buffer_length = sizeof(double);
            binds[3].length = nullptr;
            binds[3].is_null = nullptr;
            binds[3].num = batch_count;
            
            // flux (DOUBLE)
            binds[4].buffer_type = TSDB_DATA_TYPE_DOUBLE;
            binds[4].buffer = flux_buf.data();
            binds[4].buffer_length = sizeof(double);
            binds[4].length = nullptr;
            binds[4].is_null = nullptr;
            binds[4].num = batch_count;
            
            // flux_error (DOUBLE)
            binds[5].buffer_type = TSDB_DATA_TYPE_DOUBLE;
            binds[5].buffer = flux_error_buf.data();
            binds[5].buffer_length = sizeof(double);
            binds[5].length = nullptr;
            binds[5].is_null = nullptr;
            binds[5].num = batch_count;
            
            // jd_tcb (DOUBLE)
            binds[6].buffer_type = TSDB_DATA_TYPE_DOUBLE;
            binds[6].buffer = jd_buf.data();
            binds[6].buffer_length = sizeof(double);
            binds[6].length = nullptr;
            binds[6].is_null = nullptr;
            binds[6].num = batch_count;
            
            if (taos_stmt_bind_param_batch(stmt, binds) != 0) {
                lock_guard<mutex> lock(cout_mutex);
                cerr << "[ERROR] Bind parameters failed: " << taos_stmt_errstr(stmt) << endl;
                continue;
            }
            
            if (taos_stmt_add_batch(stmt) != 0) {
                lock_guard<mutex> lock(cout_mutex);
                cerr << "[ERROR] Add batch failed: " << taos_stmt_errstr(stmt) << endl;
                continue;
            }
            
            if (taos_stmt_execute(stmt) != 0) {
                lock_guard<mutex> lock(cout_mutex);
                cerr << "[ERROR] Execute failed: " << taos_stmt_errstr(stmt) << endl;
                continue;
            }
            
            stats.inserted_records += batch_count;
        }
        
        stats.table_count++;
    }
    
    taos_stmt_close(stmt);
    taos_close(conn);
}

int main(int argc, char* argv[]) {
    string catalog_dir, coords_file;
    string db_name = "gaiadr2_lc";
    string super_table = "sensor_data";
    int nside = 64;
    bool drop_db = false;
    bool enable_crossmatch = ENABLE_CROSSMATCH;
    double crossmatch_radius = CROSSMATCH_RADIUS_ARCSEC;
    
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--catalogs" && i + 1 < argc) catalog_dir = argv[++i];
        else if (arg == "--coords" && i + 1 < argc) coords_file = argv[++i];
        else if (arg == "--db" && i + 1 < argc) db_name = argv[++i];
        else if (arg == "--nside" && i + 1 < argc) nside = stoi(argv[++i]);
        else if (arg == "--threads" && i + 1 < argc) NUM_THREADS = stoi(argv[++i]);
        else if (arg == "--vgroups" && i + 1 < argc) NUM_VGROUPS = stoi(argv[++i]);
        else if (arg == "--drop_db") drop_db = true;
        else if (arg == "--crossmatch" && i + 1 < argc) {
            string val = argv[++i];
            enable_crossmatch = (val == "true" || val == "1");
        }
        else if (arg == "--radius" && i + 1 < argc) crossmatch_radius = stod(argv[++i]);
    }
    
    if (catalog_dir.empty() || coords_file.empty()) {
        cout << "Usage: " << argv[0] << " --catalogs <dir> --coords <file> [options]" << endl;
        cout << "\nOptions:" << endl;
        cout << "  --db <name>              Database name (default: gaiadr2_lc)" << endl;
        cout << "  --nside <N>              HEALPix NSIDE (default: 64)" << endl;
        cout << "  --threads <N>            Number of threads (default: 16)" << endl;
        cout << "  --vgroups <N>            Number of VGroups (default: 32)" << endl;
        cout << "  --drop_db                Drop existing database" << endl;
        cout << "  --crossmatch <0|1>       Enable cross-match (default: 1)" << endl;
        cout << "  --radius <arcsec>        Cross-match radius (default: 1.0)" << endl;
        return 1;
    }
    
    cout << "\n=== Catalog Data Importer with Cross-Match ===" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << " Catalog directory: " << catalog_dir << endl;
    cout << " Coordinates file: " << coords_file << endl;
    cout << " Database: " << db_name << endl;
    cout << " Threads: " << NUM_THREADS << endl;
    cout << " vgroups: " << NUM_VGROUPS << endl;
    cout << " Cross-match: " << (enable_crossmatch ? "enabled" : "disabled") << endl;
    if (enable_crossmatch) {
        cout << " Match radius: " << fixed << setprecision(2) << crossmatch_radius << " arcsec" << endl;
    }
    cout << " Batch size: " << BATCH_SIZE << " rows/batch" << endl;
    cout << " HEALPix NSIDE: " << nside << endl;
    cout << " Format: source_id,ra,dec,class,band,time,flux,flux_err,mag,mag_err" << endl;
    cout << " Strategy: STMT API + Direct Assignment + Two-Phase" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << endl;
    
    // ==================== Pre-processing: Cross-Match (NOT included in timing) ====================
    unordered_map<long long, int64_t> crossmatch_results;
    string taos_host = get_taos_host();  // Get host early for cross-match

    // Derive config directory: prefer env var, then project paths
    string taos_cfg_dir;
    const char* env_cfg = getenv("TAOS_CFG_DIR");
    if (env_cfg && strlen(env_cfg) > 0 && fs::exists(env_cfg)) {
        taos_cfg_dir = env_cfg;
    } else {
        // Try paths relative to executable
        string exe_path = fs::canonical("/proc/self/exe").parent_path().string();
        vector<string> candidates = {
            exe_path + "/../config/taos_cfg",
            exe_path + "/../runtime/taos_home/cfg",
            fs::current_path().string() + "/config/taos_cfg",
            fs::current_path().string() + "/../config/taos_cfg"
        };
        for (const auto& path : candidates) {
            if (fs::exists(path)) {
                taos_cfg_dir = path;
                break;
            }
        }
    }

    if (!taos_cfg_dir.empty()) {
        taos_options(TSDB_OPTION_CONFIGDIR, taos_cfg_dir.c_str());
        cout << "[INFO] TDengine config: " << taos_cfg_dir << endl;
    } else {
        cerr << "[WARN] No TDengine config found. Set TAOS_CFG_DIR or run from project root." << endl;
    }

    // Initialize TDengine before any taos_connect call
    taos_init();

    // Read coordinates first (needed for cross-match)
    cout << "\n[PRE-PROCESS] Reading coordinates file..." << endl;
    auto coord_start = high_resolution_clock::now();
    
    unordered_map<long long, pair<double, double>> coords_map;
    ifstream coord_file(coords_file);
    if (!coord_file.is_open()) {
        cerr << "[ERROR] Cannot open coordinates file: " << coords_file << endl;
        taos_cleanup();
        return 1;
    }
    
    string line;
    getline(coord_file, line);  // Skip header
    long long coord_skipped = 0;
    while (getline(coord_file, line)) {
        auto parts = split(line, ',');
        if (parts.size() >= 3) {
            try {
                string& sid = parts[0];
                sid.erase(0, sid.find_first_not_of(" \t\r\n\xEF\xBB\xBF"));
                sid.erase(sid.find_last_not_of(" \t\r\n") + 1);
                if (sid.empty()) { coord_skipped++; continue; }
                long long source_id = stoll(sid);
                double ra = stod(parts[1]);
                double dec = stod(parts[2]);
                coords_map[source_id] = {ra, dec};
            } catch (const exception& e) {
                coord_skipped++;
                if (coord_skipped <= 3) {
                    cerr << "  [WARN] Skip bad coord line: " << line.substr(0, 60) 
                         << "... (" << e.what() << ")" << endl;
                }
            }
        }
    }
    coord_file.close();
    if (coord_skipped > 0) {
        cout << "  [WARN] Skipped " << coord_skipped << " invalid coordinate rows" << endl;
    }
    
    auto coord_end = high_resolution_clock::now();
    double coord_time = duration_cast<milliseconds>(coord_end - coord_start).count() / 1000.0;
    cout << "  [OK] Read " << coords_map.size() << " source coordinates (" 
         << fixed << setprecision(2) << coord_time << "s)" << endl;
    
    // Perform cross-match (excluded from main timing)
    crossmatch_results = perform_crossmatch(
        coords_map, db_name, super_table, nside, crossmatch_radius, enable_crossmatch
    );
    
    // ==================== Start Main Import Timing ====================
    cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << "[INFO] Starting main import process..." << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << endl;
    
    PerfStats stats;
    auto total_start = high_resolution_clock::now();
    
    // Connect to database (reuse taos_host from perform_crossmatch)
    TAOS* conn = taos_connect(taos_host.c_str(), "root", "taosdata", NULL, 6030);
    if (!conn) {
        cerr << "[ERROR] Connection failed (host: " << taos_host << ")" << endl;
        taos_cleanup();
        return 1;
    }
    
    // Drop database if requested
    if (drop_db) {
        string drop_sql = "DROP DATABASE IF EXISTS " + db_name;
        TAOS_RES* drop_res = taos_query(conn, drop_sql.c_str());
        if (taos_errno(drop_res) != 0) {
            cerr << "[ERROR] Drop database failed: " << taos_errstr(drop_res) << endl;
            taos_free_result(drop_res);
            taos_close(conn);
            taos_cleanup();
            return 1;
        }
        taos_free_result(drop_res);
        cout << "[INFO] Dropped existing database: " << db_name << endl;
    }
    
    // Create database (specify vgroups)
    stringstream create_db_sql;
    create_db_sql << "CREATE DATABASE IF NOT EXISTS " << db_name 
                  << " VGROUPS " << NUM_VGROUPS 
                  << " BUFFER " << BUFFER_SIZE 
                  << " KEEP 36500";
    TAOS_RES* res = taos_query(conn, create_db_sql.str().c_str());
    if (taos_errno(res) != 0) {
        cerr << "[ERROR] Create database failed: " << taos_errstr(res) << endl;
        taos_free_result(res);
        taos_close(conn);
        taos_cleanup();
        return 1;
    }
    taos_free_result(res);
    
    // Use database - with error checking
    string use_db = "USE " + db_name + ";";
    res = taos_query(conn, use_db.c_str());
    if (taos_errno(res) != 0) {
        cerr << "[ERROR] Use database failed: " << taos_errstr(res) << endl;
        taos_free_result(res);
        taos_close(conn);
        taos_cleanup();
        return 1;
    }
    taos_free_result(res);
    
    // Create super table
    string create_stable = "CREATE STABLE IF NOT EXISTS " + super_table + 
        " (ts TIMESTAMP, band NCHAR(16), "
        "mag DOUBLE, mag_error DOUBLE, flux DOUBLE, flux_error DOUBLE, jd_tcb DOUBLE) "
        "TAGS (healpix_id BIGINT, source_id BIGINT, ra DOUBLE, dec DOUBLE, cls NCHAR(32));";
    res = taos_query(conn, create_stable.c_str());
    if (taos_errno(res) != 0 && taos_errno(res) != 0x80002603) {
        cerr << "[ERROR] Create super table failed: " << taos_errstr(res) << endl;
        taos_free_result(res);
        taos_close(conn);
        taos_cleanup();
        return 1;
    }
    taos_free_result(res);
    
    cout << "[OK] Database and super table ready (vgroups=" << NUM_VGROUPS << ")" << endl;
    taos_close(conn);
    
    // Initialize HEALPix
    Healpix_Base hp(nside, NEST, SET_NSIDE);
    
    // ==================== Read Catalog Files ====================
    // Format: source_id,ra,dec,class,band,time,flux,flux_err,mag,mag_err
    cout << "\n[INFO] Reading catalog files..." << endl;
    auto catalog_start = high_resolution_clock::now();
    
    vector<string> catalog_files;
    for (const auto& entry : fs::directory_iterator(catalog_dir)) {
        string filename = entry.path().filename().string();
        if (filename.find("catalog_") == 0 && filename.find(".csv") != string::npos) {
            catalog_files.push_back(entry.path().string());
        }
    }
    sort(catalog_files.begin(), catalog_files.end());
    
    cout << "  [INFO] Found " << catalog_files.size() << " catalog files" << endl;
    
    // Collect data for each source
    // FIX: Use unordered_map for better performance (O(1) vs O(log n))
    unordered_map<long long, SubTable*> source_data;
    source_data.reserve(coords_map.size());  // Pre-allocate to avoid rehashing
    long long skipped_rows = 0;
    
    for (const auto& catalog_file : catalog_files) {
        ifstream file(catalog_file);
        if (!file.is_open()) continue;
        
        getline(file, line);  // Skip header
        int line_num = 1;
        
        while (getline(file, line)) {
            line_num++;
            auto parts = split(line, ',');
            if (parts.size() < 10) { skipped_rows++; continue; }
            
            // Format: source_id,ra,dec,class,band,time,flux,flux_err,mag,mag_err
            long long source_id;
            try {
                // Trim whitespace/BOM from source_id field
                string& sid = parts[0];
                sid.erase(0, sid.find_first_not_of(" \t\r\n\xEF\xBB\xBF"));
                sid.erase(sid.find_last_not_of(" \t\r\n") + 1);
                if (sid.empty()) { skipped_rows++; continue; }
                source_id = stoll(sid);
            } catch (const exception& e) {
                skipped_rows++;
                if (skipped_rows <= 5) {
                    lock_guard<mutex> lock(cout_mutex);
                    cerr << "  [WARN] Skip bad source_id at " << catalog_file << ":" << line_num 
                         << " value=\"" << parts[0] << "\" (" << e.what() << ")" << endl;
                }
                continue;
            }
            
            // Use coordinates from coords file
            if (coords_map.find(source_id) == coords_map.end()) continue;
            
            // Get unique source_id from cross-match results
            int64_t unique_source_id = source_id;
            if (enable_crossmatch && crossmatch_results.find(source_id) != crossmatch_results.end()) {
                unique_source_id = crossmatch_results[source_id];
            }
            
            if (source_data.find(unique_source_id) == source_data.end()) {
                SubTable* st = new SubTable();
                st->source_id = unique_source_id;
                st->ra = coords_map[source_id].first;
                st->dec = coords_map[source_id].second;
                st->cls = parts[3];  // class from catalog
                
                // Calculate HEALPix ID
                double theta = (90.0 - st->dec) * M_PI / 180.0;
                double phi = st->ra * M_PI / 180.0;
                if (theta < 0) theta = 0;
                if (theta > M_PI) theta = M_PI;
                pointing pt(theta, phi);
                st->healpix_id = hp.ang2pix(pt);
                
                // Set table name after healpix_id is calculated
                // Use hash-based short name to avoid collisions while staying within TDengine 64-char limit
                // Format: t_<healpix>_<abs(hash(source_id)) mod 10^9>
                // This ensures uniqueness, positive numbers, and short table names
                long long source_hash = std::abs(unique_source_id % 1000000000LL);
                st->table_name = "t_" + to_string(st->healpix_id) + "_" + to_string(source_hash);
                
                source_data[unique_source_id] = st;
            }
            
            try {
                Record rec;
                rec.band = parts[4];  // band
                double time_days = stod(parts[5]);  // time
                // Gaia time is relative to J2010.0 TCB (JD 2455197.5)
                // Convert to Unix timestamp: subtract Unix Epoch JD (2440587.5), not J2000 (2451545.0)
                rec.ts_ms = static_cast<int64_t>((time_days + 2455197.5 - 2440587.5) * 86400000);
                rec.flux = stod(parts[6]);  // flux
                rec.flux_error = stod(parts[7]);  // flux_err
                rec.mag = stod(parts[8]);  // mag
                rec.mag_error = stod(parts[9]);  // mag_err
                rec.jd_tcb = 2455197.5 + time_days;
                
                // Use unique_source_id instead of original source_id
                source_data[unique_source_id]->records.push_back(rec);
                stats.total_records++;
            } catch (const exception& e) {
                skipped_rows++;
                if (skipped_rows <= 5) {
                    lock_guard<mutex> lock(cout_mutex);
                    cerr << "  [WARN] Skip bad numeric field at " << catalog_file << ":" << line_num 
                         << " (" << e.what() << ")" << endl;
                }
                continue;
            }
        }
        file.close();
    }
    
    if (skipped_rows > 0) {
        cout << "  [WARN] Skipped " << skipped_rows << " rows with invalid data" << endl;
    }
    
    auto catalog_end = high_resolution_clock::now();
    double catalog_time = duration_cast<milliseconds>(catalog_end - catalog_start).count() / 1000.0;
    cout << "  [OK] Read " << source_data.size() << " sources, " 
         << stats.total_records << " records total (" << catalog_time << "s)" << endl;
    
    // Convert to vector for distribution
    vector<SubTable*> tables;
    tables.reserve(source_data.size());
    for (auto& pair : source_data) {
        tables.push_back(pair.second);
    }
    
    // ==================== Phase 1: Parallel Table Creation ====================
    cout << "\n[PHASE 1] Parallel table creation (" << NUM_THREADS << " threads)..." << endl;
    auto create_start = high_resolution_clock::now();
    
    vector<thread> workers;
    size_t tables_per_thread = (tables.size() + NUM_THREADS - 1) / NUM_THREADS;
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        size_t start = i * tables_per_thread;
        size_t end = min(start + tables_per_thread, tables.size());
        if (start < tables.size()) {
            workers.emplace_back(create_tables_worker, i, ref(tables), start, end, 
                                ref(db_name), ref(super_table), ref(stats));
        }
    }
    
    for (auto& t : workers) t.join();
    workers.clear();
    
    auto create_end = high_resolution_clock::now();
    double create_time = duration_cast<milliseconds>(create_end - create_start).count() / 1000.0;
    cout << "  [OK] Created " << stats.tables_created << " tables (" << create_time << "s)" << endl;
    
    // ==================== Phase 2: STMT API Insert ====================
    cout << "\n[PHASE 2] STMT API insert (" << NUM_THREADS << " threads)..." << endl;
    auto insert_start = high_resolution_clock::now();
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        size_t start = i * tables_per_thread;
        size_t end = min(start + tables_per_thread, tables.size());
        if (start < tables.size()) {
            workers.emplace_back(insert_worker, i, ref(tables), start, end, 
                                ref(db_name), ref(stats));
        }
    }
    
    // Monitor progress
    thread monitor([&]() {
        auto monitor_start = high_resolution_clock::now();
        while (stats.table_count < (int)tables.size()) {
            // Check for stop signal
            ifstream stop_file("/tmp/import_stop");
            if (stop_file.is_open()) {
                stop_file.close();
                ofstream f("/tmp/import_progress.json");
                f << "{\"percent\":0,\"message\":\"Stopped by user\",\"status\":\"stopped\",\"stats\":{}}";
                f.close();
                break;
            }
            
            this_thread::sleep_for(milliseconds(500));
            auto now = high_resolution_clock::now();
            double elapsed = duration_cast<milliseconds>(now - monitor_start).count() / 1000.0;
            double speed = stats.inserted_records / max(elapsed, 0.001);
            
            double pct = (double)stats.table_count / tables.size() * 100.0;
            
            // Output progress JSON
            {
                ofstream f("/tmp/import_progress.json");
                f << "{\"percent\":" << (int)pct
                  << ",\"message\":\"Processing: " << stats.table_count << "/" << tables.size() << " tables\""
                  << ",\"status\":\"running\""
                  << ",\"stats\":{\"processed_files\":" << stats.table_count
                  << ",\"total_files\":" << tables.size()
                  << ",\"inserted_records\":" << stats.inserted_records.load()
                  << ",\"created_tables\":" << stats.tables_created.load()
                  << ",\"elapsed_time\":\"" << (int)elapsed << "s\""
                  << "}}";
                f.close();
            }
            
            cout << "\r  [PROGRESS] " << stats.table_count << "/" << tables.size() 
                 << " tables | Rows: " << stats.inserted_records 
                 << " | Speed: " << fixed << setprecision(0) << speed << " rows/s" << flush;
        }
        
        // Write 100% when complete
        auto now = high_resolution_clock::now();
        double elapsed = duration_cast<milliseconds>(now - monitor_start).count() / 1000.0;
        ofstream f("/tmp/import_progress.json");
        f << "{\"percent\":100,\"message\":\"Import completed\",\"status\":\"completed\""
          << ",\"stats\":{\"processed_files\":" << stats.table_count.load()
          << ",\"total_files\":" << tables.size()
          << ",\"inserted_records\":" << stats.inserted_records.load()
          << ",\"created_tables\":" << stats.tables_created.load()
          << ",\"elapsed_time\":\"" << (int)elapsed << "s\""
          << "}}";
        f.close();
    });
    
    for (auto& t : workers) t.join();
    monitor.join();
    
    auto insert_end = high_resolution_clock::now();
    double insert_time = duration_cast<milliseconds>(insert_end - insert_start).count() / 1000.0;
    
    auto total_end = high_resolution_clock::now();
    double total_time = duration_cast<milliseconds>(total_end - total_start).count() / 1000.0;
    
    // ==================== Performance Report ====================
    cout << "\n\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << "[REPORT] Catalog Import Performance (Optimized)" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << fixed << setprecision(2);
    cout << "[TIME] Data reading:    " << (coord_time + catalog_time) << " s" << endl;
    cout << "[TIME] Table creation:  " << create_time << " s" << endl;
    cout << "[TIME] Data insertion:  " << insert_time << " s" << endl;
    cout << "[TIME] Total:           " << total_time << " s" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << "[STATS] Data statistics:" << endl;
    cout << "  - Table count:       " << stats.table_count << endl;
    cout << "  - Total records:     " << stats.total_records << endl;
    cout << "  - Successfully inserted: " << stats.inserted_records << endl;
    cout << "  - Overall rate:      " << setprecision(0) << (stats.inserted_records / total_time) << " rows/s" << endl;
    cout << "  - Insert rate:       " << setprecision(0) << (stats.inserted_records / insert_time) << " rows/s" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    
    // Cleanup
    for (auto* st : tables) delete st;
    taos_cleanup();
    
    cout << "\n[OK] Catalog import complete!" << endl;
    return 0;
}
