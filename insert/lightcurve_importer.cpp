/*
 * TDengine Ultimate Optimized Importer v12 - Direct Version (No Queue)
 * 
 * Architecture:
 * Phase 1: Batch create all child tables in advance
 * Phase 2: Each thread processes assigned files directly (read+write), no producer-consumer queue
 * 
 * Advantages:
 * 1. No queue lock overhead
 * 2. Independent thread work, NUMA-friendly
 * 3. Large batch size, fully utilizing memory
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <algorithm>
#include <filesystem>

#include <taos.h>
#include <healpix_cxx/healpix_base.h>

using namespace std;
using namespace std::chrono;
namespace fs = std::filesystem;

// ==================== Configuration Parameters ====================
int NUM_THREADS = 16;                     // Number of threads (default: 16 for web)
int NUM_VGROUPS = 32;                     // Number of VGroups (default: 32 for compatibility)
constexpr int CREATE_TABLE_BATCH = 2000;  // Tables per batch (increased)
constexpr int TAOS_PORT = 6030;

// ==================== Data Structures ====================

struct Record {
    int64_t ts_ms;
    char band[16];
    double mag;
    double mag_error;
    double flux;
    double flux_error;
    double jd_tcb;
};

struct SubTable {
    string file_path;
    string table_name;
    string cls;
    int64_t healpix_id;
    int64_t source_id;
    double ra, dec;
};

struct PerfStats {
    atomic<int64_t> created_tables{0};
    atomic<int64_t> processed_files{0};
    atomic<int64_t> inserted_records{0};
    atomic<int64_t> total_files{0};
};

// Cross-match data structures
struct DBObject {
    int64_t source_id;
    double ra, dec;
    int64_t healpix_id;
};

struct FileTask {
    string file_path;
    string table_name;
    int64_t unified_sid;
};

mutex g_print_mutex;

// ==================== Utility Functions ====================

string get_taos_host() {
    const char* env_host = getenv("TAOS_HOST");
    if (env_host && strlen(env_host) > 0) return string(env_host);
    return "localhost";
}

vector<string> split(const string& line, char delim) {
    vector<string> result;
    stringstream ss(line);
    string item;
    while (getline(ss, item, delim)) result.push_back(item);
    return result;
}

int64_t parseTimestamp(const string& val) {
    try {
        return (int64_t)((2455197.5 + stod(val) - 2440587.5) * 86400.0 * 1000.0);
    } catch (...) { return 0; }
}

double calculateMagError(double flux, double flux_error) {
    if (flux <= 0) return 0.01;
    return 1.0857 * flux_error / flux;
}

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
    
    return c * 180.0 / M_PI * 3600.0;
}

// Generate hash-based unique ID for new objects (always positive)
int64_t generate_hash_id(double ra, double dec, int64_t salt) {
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

// Spatial index for fast cross-match
struct SpatialIndex {
    unordered_map<int64_t, vector<const DBObject*>> healpix_map;
    Healpix_Base hp;

    SpatialIndex(int nside) : hp(nside, NEST, SET_NSIDE) {}

    void build(const vector<DBObject>& objs) {
        for (const auto& obj : objs) {
            healpix_map[obj.healpix_id].push_back(&obj);
        }
    }

    vector<int64_t> get_neighboring_pixels(double ra, double dec) const {
        double theta = (90.0 - dec) * M_PI / 180.0;
        double phi = ra * M_PI / 180.0;
        if (theta < 0) theta = 0;
        if (theta > M_PI) theta = M_PI;

        pointing pt(theta, phi);
        int64_t current_pix = hp.ang2pix(pt);

        vector<int64_t> neighbors{current_pix};

        fix_arr<int, 8> pix_neighbors;
        hp.neighbors(current_pix, pix_neighbors);
        for (int i = 0; i < 8; ++i) {
            if (pix_neighbors[i] >= 0) {
                neighbors.push_back((int64_t)pix_neighbors[i]);
            }
        }
        return neighbors;
    }

    const DBObject* find_match(double ra, double dec, double max_sep_arcsec) const {
        const DBObject* best = nullptr;
        double best_sep = max_sep_arcsec;

        auto candidates = get_neighboring_pixels(ra, dec);
        for (auto pix : candidates) {
            auto it = healpix_map.find(pix);
            if (it == healpix_map.end()) continue;

            for (const DBObject* obj : it->second) {
                double sep = angular_distance_arcsec(ra, dec, obj->ra, obj->dec);
                if (sep < best_sep) {
                    best_sep = sep;
                    best = obj;
                }
            }
        }
        return best;
    }
};

// Perform cross-match between input coordinates and database objects
unordered_map<int64_t, int64_t> perform_crossmatch(
    const map<int64_t, pair<double,double>>& coords,
    const string& db_name,
    const string& super_table,
    int nside,
    double match_radius_arcsec
) {
    unordered_map<int64_t, int64_t> result;

    TAOS* conn = taos_connect(get_taos_host().c_str(), "root", "taosdata", db_name.c_str(), TAOS_PORT);
    if (!conn) {
        // Fallback: use original IDs
        for (const auto& [sid, _] : coords) result[sid] = sid;
        return result;
    }

    string sql = "SELECT source_id, ra, dec, healpix_id FROM " + super_table +
                 " GROUP BY source_id, ra, dec, healpix_id";
    TAOS_RES* res = taos_query(conn, sql.c_str());

    vector<DBObject> db_objects;
    if (taos_errno(res) == 0) {
        TAOS_ROW row;
        while ((row = taos_fetch_row(res)) != nullptr) {
            if (!row[0] || !row[1] || !row[2] || !row[3]) continue;
            DBObject obj;
            obj.source_id = *(int64_t*)row[0];
            obj.ra = *(double*)row[1];
            obj.dec = *(double*)row[2];
            obj.healpix_id = *(int64_t*)row[3];
            db_objects.push_back(obj);
        }
    }
    taos_free_result(res);
    taos_close(conn);

    SpatialIndex index(nside);
    index.build(db_objects);

    for (const auto& [orig_id, coord] : coords) {
        const DBObject* match = index.find_match(coord.first, coord.second, match_radius_arcsec);
        if (match) {
            result[orig_id] = match->source_id;
        } else {
            result[orig_id] = generate_hash_id(coord.first, coord.second, orig_id);
        }
    }

    return result;
}

// ==================== Phase 1: Batch Create Tables ====================

void batch_create_tables(TAOS* conn, const vector<SubTable>& tables, 
                         const string& super_table, PerfStats& stats) {
    if (tables.empty()) return;
    
    stringstream sql;
    sql << "CREATE TABLE ";
    
    for (size_t i = 0; i < tables.size(); ++i) {
        const auto& t = tables[i];
        if (i > 0) sql << " ";
        sql << "IF NOT EXISTS " << t.table_name 
            << " USING " << super_table 
            << " TAGS(" << t.healpix_id << "," << t.source_id << ","
            << fixed << setprecision(6) << t.ra << "," << t.dec 
            << ",'" << t.cls << "')";
    }
    
    TAOS_RES* res = taos_query(conn, sql.str().c_str());
    if (taos_errno(res) == 0) {
        stats.created_tables += tables.size();
    }
    taos_free_result(res);
}

// ==================== Phase 2: Direct Processing Thread ====================

void direct_worker_thread(int thread_id, 
                          const vector<FileTask>& my_tasks,
                          const string& db_name,
                          PerfStats& stats) {
    if (my_tasks.empty()) return;
    
    string taos_host = get_taos_host();
    TAOS* conn = taos_connect(taos_host.c_str(), "root", "taosdata", db_name.c_str(), TAOS_PORT);
    if (!conn) {
        lock_guard<mutex> lock(g_print_mutex);
        cerr << "[ERROR] Thread " << thread_id << " connection failed" << endl;
        return;
    }
    
    TAOS_STMT* stmt = taos_stmt_init(conn);
    if (!stmt) {
        taos_close(conn);
        return;
    }
    
    string sql = "INSERT INTO ? VALUES(?,?,?,?,?,?,?)";
    int ret = taos_stmt_prepare(stmt, sql.c_str(), sql.length());
    if (ret != 0) {
        taos_stmt_close(stmt);
        taos_close(conn);
        return;
    }
    
    int64_t local_inserted = 0;
    
    // Process all files assigned to this thread directly
    for (const auto& task : my_tasks) {
        // 1. Read file
        // Auto-detect CSV format from header
        vector<Record> records;
        ifstream file(task.file_path);
        if (!file.is_open()) {
            lock_guard<mutex> lock(g_print_mutex);
            cerr << "[WARN] Cannot open file: " << task.file_path << endl;
            continue;
        }
        
        string line;
        getline(file, line); // read header
        
        // Detect column indices from header
        auto hdr = split(line, ',');
        int col_time = -1, col_band = -1, col_flux = -1, col_flux_error = -1;
        int col_mag = -1, col_mag_error = -1;
        for (int ci = 0; ci < (int)hdr.size(); ++ci) {
            string& h = hdr[ci];
            // trim whitespace
            h.erase(0, h.find_first_not_of(" \t\r\n"));
            h.erase(h.find_last_not_of(" \t\r\n") + 1);
            if (h == "time")       col_time = ci;
            else if (h == "band")  col_band = ci;
            else if (h == "flux")  col_flux = ci;
            else if (h == "flux_error" || h == "flux_err") col_flux_error = ci;
            else if (h == "mag")   col_mag = ci;
            else if (h == "mag_error" || h == "mag_err")   col_mag_error = ci;
        }
        
        // Fallback to old format ONLY if no columns were auto-detected
        bool header_detected = (col_time >= 0 || col_band >= 0 || col_flux >= 0 || col_mag >= 0);
        if (!header_detected) {
            // Old format: time,band,flux,flux_err,mag,mag_err
            col_time = 0; col_band = 1; col_flux = 2;
            col_flux_error = 3; col_mag = 4; col_mag_error = 5;
        } else {
            // Header detected — fill missing required columns with defaults
            if (col_time < 0) col_time = 0;
            if (col_band < 0) col_band = 1;
            // col_mag_error stays -1 if not in header → will be calculated
        }
        
        int max_col = max({col_time, col_band, col_flux, col_flux_error, col_mag});
        if (col_mag_error >= 0) max_col = max(max_col, col_mag_error);
        int min_cols = max_col + 1;
        
        while (getline(file, line)) {
            if (line.empty()) continue;
            auto tokens = split(line, ',');
            if ((int)tokens.size() >= min_cols) {
                try {
                    Record rec;
                    double time_val = stod(tokens[col_time]);
                    rec.ts_ms = parseTimestamp(tokens[col_time]);
                    strncpy(rec.band, tokens[col_band].c_str(), 15); rec.band[15] = 0;
                    rec.flux = stod(tokens[col_flux]);
                    rec.flux_error = stod(tokens[col_flux_error]);
                    rec.mag = stod(tokens[col_mag]);
                    if (col_mag_error >= 0 && col_mag_error < (int)tokens.size()) {
                        rec.mag_error = stod(tokens[col_mag_error]);
                    } else {
                        // No mag_error column — calculate from flux: σ_mag = 1.0857 * flux_err / flux
                        rec.mag_error = calculateMagError(rec.flux, rec.flux_error);
                    }
                    rec.jd_tcb = 2455197.5 + time_val;
                    records.push_back(rec);
                } catch (...) { continue; }
            }
        }
        file.close();
        
        if (records.empty()) {
            lock_guard<mutex> lock(g_print_mutex);
            cerr << "[WARN] No records in file: " << task.file_path << endl;
            stats.processed_files++;
            continue;
        }
        
        // 2. STMT write - use unified table name
        ret = taos_stmt_set_tbname(stmt, task.table_name.c_str());
        if (ret != 0) {
            stats.processed_files++;
            continue;
        }
        
        size_t num_rows = records.size();
        
        vector<int64_t> ts_arr(num_rows);
        vector<char> band_arr(num_rows * 17);
        vector<int32_t> band_len(num_rows);
        vector<double> mag_arr(num_rows);
        vector<double> mag_error_arr(num_rows);
        vector<double> flux_arr(num_rows);
        vector<double> flux_error_arr(num_rows);
        vector<double> jd_tcb_arr(num_rows);
        
        for (size_t i = 0; i < num_rows; ++i) {
            const auto& r = records[i];
            ts_arr[i] = r.ts_ms;
            memset(&band_arr[i * 17], 0, 17);
            strncpy(&band_arr[i * 17], r.band, 16);
            band_len[i] = strlen(r.band);
            mag_arr[i] = r.mag;
            mag_error_arr[i] = r.mag_error;
            flux_arr[i] = r.flux;
            flux_error_arr[i] = r.flux_error;
            jd_tcb_arr[i] = r.jd_tcb;
        }
        
        TAOS_MULTI_BIND params[7];
        memset(params, 0, sizeof(params));
        
        params[0].buffer_type = TSDB_DATA_TYPE_TIMESTAMP;
        params[0].buffer = ts_arr.data();
        params[0].buffer_length = sizeof(int64_t);
        params[0].num = num_rows;
        
        params[1].buffer_type = TSDB_DATA_TYPE_NCHAR;
        params[1].buffer = band_arr.data();
        params[1].buffer_length = 17;
        params[1].length = band_len.data();
        params[1].num = num_rows;
        
        params[2].buffer_type = TSDB_DATA_TYPE_DOUBLE;
        params[2].buffer = mag_arr.data();
        params[2].buffer_length = sizeof(double);
        params[2].num = num_rows;
        
        params[3].buffer_type = TSDB_DATA_TYPE_DOUBLE;
        params[3].buffer = mag_error_arr.data();
        params[3].buffer_length = sizeof(double);
        params[3].num = num_rows;
        
        params[4].buffer_type = TSDB_DATA_TYPE_DOUBLE;
        params[4].buffer = flux_arr.data();
        params[4].buffer_length = sizeof(double);
        params[4].num = num_rows;
        
        params[5].buffer_type = TSDB_DATA_TYPE_DOUBLE;
        params[5].buffer = flux_error_arr.data();
        params[5].buffer_length = sizeof(double);
        params[5].num = num_rows;
        
        params[6].buffer_type = TSDB_DATA_TYPE_DOUBLE;
        params[6].buffer = jd_tcb_arr.data();
        params[6].buffer_length = sizeof(double);
        params[6].num = num_rows;
        
        ret = taos_stmt_bind_param_batch(stmt, params);
        if (ret != 0) {
            stats.processed_files++;
            continue;
        }
        
        ret = taos_stmt_add_batch(stmt);
        if (ret != 0) {
            stats.processed_files++;
            continue;
        }
        
        ret = taos_stmt_execute(stmt);
        if (ret == 0) {
            local_inserted += num_rows;
            stats.inserted_records += num_rows;
        } else {
            lock_guard<mutex> lock(g_print_mutex);
            cerr << "[ERROR] STMT execute failed for " << task.table_name << ": " << taos_stmt_errstr(stmt) << endl;
        }
        
        stats.processed_files++;
    }
    
    // stats.inserted_records += local_inserted;
    
    taos_stmt_close(stmt);
    taos_close(conn);
}

// ==================== Monitor Thread ====================

void write_progress_json(int percent, const string& message, const string& status,
                         int64_t processed, int64_t total, int64_t inserted, int64_t created, int elapsed) {
    ofstream f("/tmp/import_progress.json");
    f << "{\"percent\":" << percent 
      << ",\"message\":\"" << message << "\""
      << ",\"status\":\"" << status << "\""
      << ",\"stats\":{\"processed_files\":" << processed
      << ",\"total_files\":" << total
      << ",\"inserted_records\":" << inserted
      << ",\"created_tables\":" << created
      << ",\"elapsed_time\":\"" << elapsed << "s\""
      << "}}";
    f.close();
}

void monitor_thread(PerfStats& stats) {
    auto start = high_resolution_clock::now();
    int64_t last_inserted = 0;
    
    while (stats.processed_files < stats.total_files) {
        // Check for stop signal
        ifstream stop_file("/tmp/import_stop");
        if (stop_file.is_open()) {
            stop_file.close();
            write_progress_json(0, "Stopped by user", "stopped", 0, 0, 0, 0, 0);
            break;
        }
        
        this_thread::sleep_for(seconds(1));
        
        int64_t processed = stats.processed_files.load();
        int64_t total = stats.total_files.load();
        int64_t inserted = stats.inserted_records.load();
        int64_t created = stats.created_tables.load();
        
        int64_t speed = inserted - last_inserted;
        last_inserted = inserted;
        
        double pct = total > 0 ? (double)processed / total * 100.0 : 0.0;
        int elapsed = duration_cast<seconds>(high_resolution_clock::now() - start).count();
        
        // Output progress JSON
        stringstream msg;
        msg << "Processing: " << processed << "/" << total << " files, " << speed << " rows/s";
        write_progress_json((int)pct, msg.str(), "running", processed, total, inserted, created, elapsed);
        
        string bar(30, '-');
        int filled = (int)(pct / 100.0 * 30);
        for (int i = 0; i < filled; ++i) bar[i] = '#';
        
        lock_guard<mutex> lock(g_print_mutex);
        cout << "\r[PROGRESS] [" << bar << "] " << fixed << setprecision(1) << pct << "% "
             << "| Files:" << processed << "/" << total
             << " Rows:" << inserted
             << " Speed:" << speed << "/s"
             << "    " << flush;
    }
    
    // Write 100% when complete
    int elapsed = duration_cast<seconds>(high_resolution_clock::now() - start).count();
    write_progress_json(100, "Import completed", "completed", 
                        stats.processed_files.load(), stats.total_files.load(),
                        stats.inserted_records.load(), stats.created_tables.load(), elapsed);
    cout << endl;
}

// ==================== Main Function ====================

int main(int argc, char* argv[]) {
    setbuf(stdout, NULL); // Disable buffering
    string lc_dir, coords_file;
    string db_name = "gaiadr2_lc";
    string super_table = "sensor_data";
    bool drop_db = false;
    bool enable_crossmatch = true;
    double crossmatch_radius_arcsec = 1.0;
    int nside = 64;
    
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--lightcurves_dir" && i + 1 < argc) lc_dir = argv[++i];
        else if (arg == "--coords" && i + 1 < argc) coords_file = argv[++i];
        else if (arg == "--db" && i + 1 < argc) db_name = argv[++i];
        else if (arg == "--threads" && i + 1 < argc) NUM_THREADS = stoi(argv[++i]);
        else if (arg == "--vgroups" && i + 1 < argc) NUM_VGROUPS = stoi(argv[++i]);
        else if (arg == "--drop_db") drop_db = true;
        else if (arg == "--crossmatch" && i + 1 < argc) {
            string val = argv[++i];
            enable_crossmatch = (val == "true" || val == "1");
        }
        else if (arg == "--radius" && i + 1 < argc) {
            crossmatch_radius_arcsec = stod(argv[++i]);
        }
    }
    
    if (lc_dir.empty() || coords_file.empty()) {
        cerr << "Usage: " << argv[0] << " --lightcurves_dir <dir> --coords <file> [options]" << endl;
        cerr << "Options:" << endl;
        cerr << "  --db <name>       Database name (default: gaiadr2_lc)" << endl;
        cerr << "  --threads <N>     Number of threads (default: 16)" << endl;
        cerr << "  --vgroups <N>     Number of VGroups (default: 32)" << endl;
        cerr << "  --drop_db         Drop existing database" << endl;
        return 1;
    }
    
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
    taos_init();
    
    cout << "\n=== TDengine Importer v12 (Direct, No Queue) ===" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << "[INFO] Data directory: " << lc_dir << endl;
    cout << "[INFO] Threads: " << NUM_THREADS << endl;
    cout << "[INFO] VGroups: " << NUM_VGROUPS << endl;
    cout << "[INFO] Port: " << TAOS_PORT << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << endl;
    
    // Initialize phase progress
    write_progress_json(0, "Connecting to database...", "running", 0, 0, 0, 0, 0);
    
    // Prepare database
    TAOS* conn = taos_connect(get_taos_host().c_str(), "root", "taosdata", NULL, TAOS_PORT);
    if (!conn) { 
        cerr << "[ERROR] Connection failed" << endl;
        write_progress_json(0, "Connection failed!", "error", 0, 0, 0, 0, 0);
        return 1; 
    }
    
    write_progress_json(0, "Creating database and tables...", "running", 0, 0, 0, 0, 0);
    
    if (drop_db) {
        taos_query(conn, ("DROP DATABASE IF EXISTS " + db_name).c_str());
    }
    // Specify more vgroups when creating database to avoid disk flush bottleneck
    taos_query(conn, ("CREATE DATABASE IF NOT EXISTS " + db_name + " KEEP 36500 VGROUPS " + to_string(NUM_VGROUPS) + " BUFFER 256").c_str());
    taos_query(conn, ("USE " + db_name).c_str());
    taos_query(conn, ("CREATE STABLE IF NOT EXISTS " + super_table + 
                     " (ts TIMESTAMP, band NCHAR(16), mag DOUBLE, mag_error DOUBLE, "
                     "flux DOUBLE, flux_error DOUBLE, jd_tcb DOUBLE) "
                     "TAGS (healpix_id BIGINT, source_id BIGINT, ra DOUBLE, dec DOUBLE, cls NCHAR(32))").c_str());
    cout << "[OK] Database ready" << endl;
    
    // Load metadata
    write_progress_json(0, "Loading coordinates...", "running", 0, 0, 0, 0, 0);
    cout << "[INFO] Loading coordinate data..." << endl;
    map<int64_t, pair<double, double>> coords;
    ifstream cfile(coords_file);
    string line;
    
    // Check if first line is header
    if (getline(cfile, line)) {
        auto tokens = split(line, ',');
        bool is_header = (tokens.size() >= 3 && tokens[0].find("source_id") != string::npos);
        if (!is_header) {
            // First line is data, parse it
            if (tokens.size() >= 3) {
                try { coords[stoll(tokens[0])] = {stod(tokens[1]), stod(tokens[2])}; } catch(...) {}
            }
        }
        // Continue reading rest of file
        while (getline(cfile, line)) {
            auto tokens = split(line, ',');
            if (tokens.size() >= 3) {
                try { coords[stoll(tokens[0])] = {stod(tokens[1]), stod(tokens[2])}; } catch(...) {}
            }
        }
    }
    cout << "[OK] Loaded " << coords.size() << " coordinates" << endl;
    
    // ==================== Cross-Match ====================
    write_progress_json(0, "Performing cross-match...", "running", 0, 0, 0, 0, 0);
    cout << "[INFO] Performing cross-match..." << endl;
    
    unordered_map<int64_t, int64_t> crossmatch_map;
    if (enable_crossmatch) {
        crossmatch_map = perform_crossmatch(coords, db_name, super_table, nside, crossmatch_radius_arcsec);
    } else {
        // Disable cross-match: use original IDs
        for (const auto& [sid, _] : coords) {
            crossmatch_map[sid] = sid;
        }
    }
    cout << "[OK] Cross-match finished for " << crossmatch_map.size() << " sources" << endl;
    
    // Build unified coordinates and HEALPix maps
    write_progress_json(0, "Calculating HEALPix...", "running", 0, 0, 0, 0, 0);
    cout << "[INFO] Calculating HEALPix for unified sources..." << endl;
    
    Healpix_Base hp(nside, NEST, SET_NSIDE);
    unordered_map<int64_t, int64_t> matched_healpix;
    unordered_map<int64_t, pair<double,double>> matched_coords;
    
    for (const auto& [orig_sid, coord] : coords) {
        int64_t unified_sid = crossmatch_map[orig_sid];
        
        if (matched_coords.find(unified_sid) == matched_coords.end()) {
            matched_coords[unified_sid] = coord;
            
            double theta = (90.0 - coord.second) * M_PI / 180.0;
            double phi = coord.first * M_PI / 180.0;
            if (theta < 0) theta = 0; 
            if (theta > M_PI) theta = M_PI;
            
            matched_healpix[unified_sid] = hp.ang2pix(pointing(theta, phi));
            
            // Debug output for first few sources
            if (matched_coords.size() <= 3) {
                lock_guard<mutex> lock(g_print_mutex);
                cout << "  [DEBUG] unified_sid=" << unified_sid << " orig=" << orig_sid 
                     << " ra=" << coord.first << " dec=" << coord.second 
                     << " healpix=" << matched_healpix[unified_sid] << endl;
            }
        }
    }
    cout << "[OK] Computed " << matched_coords.size() << " unique sources" << endl;
    
    // ========== Collect All Table Information with Cross-Match ==========
    write_progress_json(0, "Scanning files...", "running", 0, 0, 0, 0, 0);
    cout << "[INFO] Scanning files and mapping to unified sources..." << endl;
    
    // Map: unified_sid -> SubTable (for table creation)
    unordered_map<int64_t, SubTable> unique_tables;
    // Map: file_path -> unified_sid (for file processing)
    vector<pair<string, int64_t>> file_to_unified_sid;
    
    for (const auto& entry : fs::directory_iterator(lc_dir)) {
        string filename = entry.path().filename().string();
        size_t last_us = filename.find_last_of('_');
        size_t dot = filename.find_last_of('.');
        if (last_us == string::npos || dot == string::npos || dot <= last_us) continue;
        
        int64_t orig_sid = 0;
        try {
            orig_sid = stoll(filename.substr(last_us + 1, dot - last_us - 1));
        } catch (...) { continue; }
        
        auto it_coord = coords.find(orig_sid);
        auto it_match = crossmatch_map.find(orig_sid);
        if (it_coord == coords.end() || it_match == crossmatch_map.end()) continue;
        
        int64_t unified_sid = it_match->second;
        
        // Record file -> unified_sid mapping
        file_to_unified_sid.push_back({entry.path().string(), unified_sid});
        
        // Create unique table entry if not exists
        if (unique_tables.find(unified_sid) == unique_tables.end()) {
            // Check if we have matched coordinates for this unified_sid
            if (matched_healpix.find(unified_sid) == matched_healpix.end()) {
                lock_guard<mutex> lock(g_print_mutex);
                cerr << "[WARN] No matched HEALPix for unified_sid: " << unified_sid 
                     << " (orig: " << orig_sid << ")" << endl;
                continue;
            }
            
            SubTable st;
            st.file_path = "";  // Not used for table creation phase
            st.source_id = unified_sid;
            st.healpix_id = matched_healpix[unified_sid];
            st.ra = matched_coords[unified_sid].first;
            st.dec = matched_coords[unified_sid].second;
            st.cls = "Unknown";
            st.table_name = super_table + "_" + to_string(st.healpix_id) + "_" + to_string(unified_sid);
            unique_tables[unified_sid] = st;
        }
    }
    
    // Convert to vector for batch processing
    vector<SubTable> all_tables;
    for (const auto& [sid, st] : unique_tables) {
        all_tables.push_back(st);
    }
    
    cout << "[OK] Found " << file_to_unified_sid.size() << " files, mapped to " 
         << all_tables.size() << " unified sources" << endl;
    
    PerfStats stats;
    stats.total_files = all_tables.size();
    
    // ========== Phase 1: Pre-create All Child Tables ==========
    cout << "\n[PHASE 1] Pre-creating child tables..." << endl;
    auto phase1_start = high_resolution_clock::now();
    
    vector<SubTable> table_batch;
    for (size_t i = 0; i < all_tables.size(); ++i) {
        table_batch.push_back(all_tables[i]);
        
        if (table_batch.size() >= CREATE_TABLE_BATCH) {
            batch_create_tables(conn, table_batch, super_table, stats);
            table_batch.clear();
            
            if (stats.created_tables % 200 == 0) {
                auto now = high_resolution_clock::now();
                int elapsed = duration_cast<seconds>(now - phase1_start).count();
                cout << "\r  [OK] Created " << stats.created_tables << " tables..." << flush;
                write_progress_json(0, "Phase 1/2: Creating tables (" + to_string(stats.created_tables) + ")", "running", 0, stats.total_files, 0, stats.created_tables, elapsed);
            }
        }
    }
    if (!table_batch.empty()) {
        batch_create_tables(conn, table_batch, super_table, stats);
    }
    
    auto phase1_end = high_resolution_clock::now();
    double phase1_time = duration_cast<milliseconds>(phase1_end - phase1_start).count() / 1000.0;
    cout << "\r  [OK] Phase 1 complete: Created " << stats.created_tables << " tables in " 
         << fixed << setprecision(2) << phase1_time << " seconds" << endl;
    
    taos_close(conn);
    
    // ========== Phase 2: Direct Sharded Processing ==========
    cout << "\n[PHASE 2] Direct sharded processing (" << NUM_THREADS << " threads)..." << endl;
    auto phase2_start = high_resolution_clock::now();
    
    // Build unified_sid -> table_name map
    unordered_map<int64_t, string> sid_to_table;
    for (const auto& st : all_tables) {
        sid_to_table[st.source_id] = st.table_name;
    }
    
    // Create FileTask for each file
    vector<FileTask> all_tasks;
    for (const auto& [file_path, unified_sid] : file_to_unified_sid) {
        FileTask task;
        task.file_path = file_path;
        task.unified_sid = unified_sid;
        task.table_name = sid_to_table[unified_sid];
        all_tasks.push_back(task);
    }
    
    // Distribute tasks to threads
    vector<vector<FileTask>> thread_tasks(NUM_THREADS);
    for (size_t i = 0; i < all_tasks.size(); ++i) {
        thread_tasks[i % NUM_THREADS].push_back(all_tasks[i]);
    }
    
    // Start monitor
    thread monitor(monitor_thread, ref(stats));
    
    // Start worker threads
    vector<thread> workers;
    for (int i = 0; i < NUM_THREADS; ++i) {
        workers.emplace_back(direct_worker_thread, i, ref(thread_tasks[i]), ref(db_name), ref(stats));
    }
    
    // Wait for completion
    for (auto& t : workers) t.join();
    monitor.join();
    
    auto phase2_end = high_resolution_clock::now();
    double phase2_time = duration_cast<milliseconds>(phase2_end - phase2_start).count() / 1000.0;
    double total_time = phase1_time + phase2_time;
    
    cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << "[OK] Import complete!" << endl;
    cout << "[TIME] Phase 1 (create tables): " << fixed << setprecision(2) << phase1_time << " s" << endl;
    cout << "[TIME] Phase 2 (insert data): " << fixed << setprecision(2) << phase2_time << " s" << endl;
    cout << "[TIME] Total: " << fixed << setprecision(2) << total_time << " s" << endl;
    cout << "[STATS] Tables created: " << stats.created_tables << endl;
    cout << "[STATS] Rows inserted: " << stats.inserted_records << endl;
    cout << "[STATS] Avg throughput: " << (int64_t)(stats.inserted_records / total_time) << " rows/s" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    
    taos_cleanup();
    return 0;
}
