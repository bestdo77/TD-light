/*
 * Catalog Data Importer - Optimized Version
 * Uses STMT API + Direct Assignment + Two-Phase (Create Tables First, Then Insert)
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
constexpr int NUM_THREADS = 64;           // Number of parallel threads
constexpr int NUM_VGROUPS = 128;          // Number of virtual groups
constexpr int BATCH_SIZE = 10000;         // Rows per insert batch
constexpr int BUFFER_SIZE = 256;          // Memory buffer per vgroup (MB)

// Read TDengine host address from environment variable
string get_taos_host() {
    const char* env_host = getenv("TAOS_HOST");
    if (env_host != nullptr && strlen(env_host) > 0) {
        return string(env_host);
    }
    return "localhost";
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

mutex cout_mutex;

vector<string> split(const string& line, char delim) {
    vector<string> result;
    stringstream ss(line);
    string item;
    while (getline(ss, item, delim)) result.push_back(item);
    return result;
}

// Calculate magnitude error
double calculateMagError(double flux, double flux_error) {
    if (flux <= 0) return 0.01;
    return 1.0857 * flux_error / flux;
}

// ==================== Phase 1: Parallel Table Creation ====================
void create_tables_worker(int thread_id, const vector<SubTable*>& tables, 
                          size_t start, size_t end, 
                          const string& db_name, const string& super_table,
                          PerfStats& stats) {
    string taos_host = get_taos_host();
    TAOS* conn = taos_connect(taos_host.c_str(), "root", "taosdata", db_name.c_str(), 6030);
    if (!conn) {
        lock_guard<mutex> lock(cout_mutex);
        cerr << "❌ Thread " << thread_id << " connection failed" << endl;
        return;
    }
    
    for (size_t i = start; i < end; ++i) {
        const SubTable* st = tables[i];
        
        stringstream sql;
        sql << "CREATE TABLE IF NOT EXISTS " << st->table_name 
            << " USING " << super_table 
            << " TAGS(" << st->healpix_id << "," << st->source_id << "," 
            << fixed << setprecision(6) << st->ra << "," << st->dec << ",'" << st->cls << "')";
        
        TAOS_RES* res = taos_query(conn, sql.str().c_str());
        if (taos_errno(res) != 0) {
            lock_guard<mutex> lock(cout_mutex);
            cerr << "[ERROR] Table creation failed " << st->table_name << ": " << taos_errstr(res) << endl;
        }
        taos_free_result(res);
        stats.tables_created++;
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
        cerr << "❌ Thread " << thread_id << " connection failed" << endl;
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
    
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--catalogs" && i + 1 < argc) catalog_dir = argv[++i];
        else if (arg == "--coords" && i + 1 < argc) coords_file = argv[++i];
        else if (arg == "--db" && i + 1 < argc) db_name = argv[++i];
        else if (arg == "--nside" && i + 1 < argc) nside = stoi(argv[++i]);
        else if (arg == "--drop_db") drop_db = true;
    }
    
    if (catalog_dir.empty() || coords_file.empty()) {
        cout << "Usage: " << argv[0] << " --catalogs <dir> --coords <file> [options]" << endl;
        cout << "\nOptions:" << endl;
        cout << "  --db <name>         Database name (default: gaiadr2_lc)" << endl;
        cout << "  --nside <N>         HEALPix NSIDE (default: 64)" << endl;
        cout << "  --drop_db           Drop existing database" << endl;
        return 1;
    }
    
    cout << "\n=== Catalog Data Importer (Optimized) ===" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << " Catalog directory: " << catalog_dir << endl;
    cout << " Coordinates file: " << coords_file << endl;
    cout << " Database: " << db_name << endl;
    cout << " Threads: " << NUM_THREADS << endl;
    cout << " vgroups: " << NUM_VGROUPS << endl;
    cout << " Batch size: " << BATCH_SIZE << " rows/batch" << endl;
    cout << " HEALPix NSIDE: " << nside << endl;
    cout << " Format: source_id,ra,dec,class,band,time,flux,flux_err,mag,mag_err" << endl;
    cout << " Strategy: STMT API + Direct Assignment + Two-Phase" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << endl;
    
    PerfStats stats;
    auto total_start = high_resolution_clock::now();
    
    // Derive config directory from executable path (supports launching from any working directory)
    string exe_path = fs::canonical("/proc/self/exe").parent_path().string();
    string taos_cfg_dir = exe_path + "/../runtime/taos_home/cfg";
    if (!fs::exists(taos_cfg_dir)) {
        // Fallback: try current directory
        taos_cfg_dir = fs::current_path().string() + "/taos_home/cfg";
    }
    if (fs::exists(taos_cfg_dir)) {
        taos_options(TSDB_OPTION_CONFIGDIR, taos_cfg_dir.c_str());
    }
    
    // Initialize TDengine
    taos_init();
    
    // Connect to database
    string taos_host = get_taos_host();
    TAOS* conn = taos_connect(taos_host.c_str(), "root", "taosdata", NULL, 6030);
    if (!conn) {
        cerr << "[ERROR] Connection failed (host: " << taos_host << ")" << endl;
        taos_cleanup();
        return 1;
    }
    
    // Drop database if requested
    if (drop_db) {
        string drop_sql = "DROP DATABASE IF EXISTS " + db_name;
        taos_query(conn, drop_sql.c_str());
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
    
    string use_db = "USE " + db_name + ";";
    taos_query(conn, use_db.c_str());
    
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
    
    // ==================== Read Coordinates File ====================
    cout << "\n[INFO] Reading coordinates file..." << endl;
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
    while (getline(coord_file, line)) {
        auto parts = split(line, ',');
        if (parts.size() >= 3) {
            long long source_id = stoll(parts[0]);
            double ra = stod(parts[1]);
            double dec = stod(parts[2]);
            coords_map[source_id] = {ra, dec};
        }
    }
    coord_file.close();
    
    auto coord_end = high_resolution_clock::now();
    double coord_time = duration_cast<milliseconds>(coord_end - coord_start).count() / 1000.0;
    cout << "  [OK] Read " << coords_map.size() << " source coordinates (" << fixed << setprecision(2) << coord_time << "s)" << endl;
    
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
    map<long long, SubTable*> source_data;
    
    for (const auto& catalog_file : catalog_files) {
        ifstream file(catalog_file);
        if (!file.is_open()) continue;
        
        getline(file, line);  // Skip header
        
        while (getline(file, line)) {
            auto parts = split(line, ',');
            if (parts.size() < 10) continue;
            
            // Format: source_id,ra,dec,class,band,time,flux,flux_err,mag,mag_err
            long long source_id = stoll(parts[0]);
            
            // Use coordinates from coords file
            if (coords_map.find(source_id) == coords_map.end()) continue;
            
            if (source_data.find(source_id) == source_data.end()) {
                SubTable* st = new SubTable();
                st->source_id = source_id;
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
                st->table_name = "sensor_data_" + to_string(st->healpix_id) + "_" + to_string(source_id);
                
                source_data[source_id] = st;
            }
            
            Record rec;
            rec.band = parts[4];  // band
            double time_days = stod(parts[5]);  // time
            // Gaia time is relative to J2010.0 TCB (JD 2455197.5)
            rec.ts_ms = static_cast<int64_t>((time_days + 2455197.5 - 2451545.0) * 86400000);
            rec.flux = stod(parts[6]);  // flux
            rec.flux_error = stod(parts[7]);  // flux_err
            rec.mag = stod(parts[8]);  // mag
            rec.mag_error = stod(parts[9]);  // mag_err
            rec.jd_tcb = 2455197.5 + time_days;
            
            source_data[source_id]->records.push_back(rec);
            stats.total_records++;
        }
        file.close();
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
