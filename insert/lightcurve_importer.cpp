/*
 * TDengine 终极优化版导入器 v12 - 直接版（无队列）
 * 
 * 架构：
 * 阶段1：预先批量创建所有子表
 * 阶段2：每个线程分配一批文件，直接读+写，无生产者-消费者队列
 * 
 * 优势：
 * 1. 无队列锁开销
 * 2. 每线程独立工作，NUMA 友好
 * 3. 大 batch，充分利用内存
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

// ==================== 配置参数 ====================
constexpr int NUM_THREADS = 64;           // 线程数
constexpr int CREATE_TABLE_BATCH = 2000;  // 每批创建表数（加大）
constexpr int TAOS_PORT = 6041;

// ==================== 数据结构 ====================

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

mutex g_print_mutex;

// ==================== 工具函数 ====================

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

// ==================== 阶段1：批量创建表 ====================

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

// ==================== 阶段2：直接处理线程 ====================

void direct_worker_thread(int thread_id, 
                          const vector<SubTable>& my_tables,
                          const string& db_name,
                          PerfStats& stats) {
    if (my_tables.empty()) return;
    
    string taos_host = get_taos_host();
    TAOS* conn = taos_connect(taos_host.c_str(), "root", "taosdata", db_name.c_str(), TAOS_PORT);
    if (!conn) {
        lock_guard<mutex> lock(g_print_mutex);
        cerr << "❌ 线程 " << thread_id << " 连接失败" << endl;
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
    
    // 直接处理分配给我的所有表
    for (const auto& st : my_tables) {
        // 1. 读取文件
        vector<Record> records;
        ifstream file(st.file_path);
        if (!file.is_open()) continue;
        
        string line;
        getline(file, line); // skip header
        while (getline(file, line)) {
            if (line.empty()) continue;
            auto tokens = split(line, ',');
            if (tokens.size() >= 7) {
                try {
                    Record rec;
                    rec.ts_ms = parseTimestamp(tokens[3]);
                    strncpy(rec.band, tokens[2].c_str(), 15); rec.band[15] = 0;
                    rec.mag = stod(tokens[4]);
                    rec.flux = stod(tokens[5]);
                    rec.flux_error = stod(tokens[6]);
                    rec.mag_error = calculateMagError(rec.flux, rec.flux_error);
                    rec.jd_tcb = 2455197.5 + stod(tokens[3]);
                    records.push_back(rec);
                } catch (...) { continue; }
            }
        }
        file.close();
        
        if (records.empty()) {
            stats.processed_files++;
            continue;
        }
        
        // 2. STMT 写入
        ret = taos_stmt_set_tbname(stmt, st.table_name.c_str());
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
        }
        
        stats.processed_files++;
    }
    
    // stats.inserted_records += local_inserted;
    
    taos_stmt_close(stmt);
    taos_close(conn);
}

// ==================== 监控线程 ====================

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
        // 检查停止信号
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
        
        // 输出进度 JSON
        stringstream msg;
        msg << "Processing: " << processed << "/" << total << " files, " << speed << " rows/s";
        write_progress_json((int)pct, msg.str(), "running", processed, total, inserted, created, elapsed);
        
        string bar(30, '-');
        int filled = (int)(pct / 100.0 * 30);
        for (int i = 0; i < filled; ++i) bar[i] = '#';
        
        lock_guard<mutex> lock(g_print_mutex);
        cout << "\r🚀 [" << bar << "] " << fixed << setprecision(1) << pct << "% "
             << "| 文件:" << processed << "/" << total
             << " 行:" << inserted
             << " 速:" << speed << "/s"
             << "    " << flush;
    }
    
    // 完成时写入 100%
    int elapsed = duration_cast<seconds>(high_resolution_clock::now() - start).count();
    write_progress_json(100, "Import completed", "completed", 
                        stats.processed_files.load(), stats.total_files.load(),
                        stats.inserted_records.load(), stats.created_tables.load(), elapsed);
    cout << endl;
}

// ==================== 主函数 ====================

int main(int argc, char* argv[]) {
    setbuf(stdout, NULL); // Disable buffering
    string lc_dir, coords_file;
    string db_name = "gaiadr2_lc";
    string super_table = "sensor_data";
    bool drop_db = false;
    
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--lightcurves_dir" && i + 1 < argc) lc_dir = argv[++i];
        else if (arg == "--coords" && i + 1 < argc) coords_file = argv[++i];
        else if (arg == "--db" && i + 1 < argc) db_name = argv[++i];
        else if (arg == "--drop_db") drop_db = true;
    }
    
    if (lc_dir.empty() || coords_file.empty()) {
        cerr << "用法: " << argv[0] << " --lightcurves_dir <dir> --coords <file> [--db <name>] [--drop_db]" << endl;
        return 1;
    }
    
    // 从可执行文件路径推导配置目录（支持从任意工作目录启动）
    string exe_path = fs::canonical("/proc/self/exe").parent_path().string();
    string taos_cfg_dir = exe_path + "/../runtime/taos_home/cfg";
    if (!fs::exists(taos_cfg_dir)) {
        // 回退：尝试当前目录
        taos_cfg_dir = fs::current_path().string() + "/taos_home/cfg";
    }
    if (fs::exists(taos_cfg_dir)) {
        taos_options(TSDB_OPTION_CONFIGDIR, taos_cfg_dir.c_str());
    }
    taos_init();
    
    cout << "\n🚀 TDengine 导入器 v12 (直接版，无队列)" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << "📂 数据目录: " << lc_dir << endl;
    cout << "🧵 线程数: " << NUM_THREADS << endl;
    cout << "🔌 端口: " << TAOS_PORT << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << endl;
    
    // 初始化阶段进度
    write_progress_json(0, "Connecting to database...", "running", 0, 0, 0, 0, 0);
    
    // 准备数据库
    TAOS* conn = taos_connect(get_taos_host().c_str(), "root", "taosdata", NULL, TAOS_PORT);
    if (!conn) { 
        cerr << "❌ 连接失败" << endl;
        write_progress_json(0, "Connection failed!", "error", 0, 0, 0, 0, 0);
        return 1; 
    }
    
    write_progress_json(0, "Creating database and tables...", "running", 0, 0, 0, 0, 0);
    
    if (drop_db) {
        taos_query(conn, ("DROP DATABASE IF EXISTS " + db_name).c_str());
    }
    // 创建数据库时指定更多 vgroup，避免刷盘瓶颈
    taos_query(conn, ("CREATE DATABASE IF NOT EXISTS " + db_name + " KEEP 36500 VGROUPS 128 BUFFER 256").c_str());
    taos_query(conn, ("USE " + db_name).c_str());
    taos_query(conn, ("CREATE STABLE IF NOT EXISTS " + super_table + 
                     " (ts TIMESTAMP, band NCHAR(16), mag DOUBLE, mag_error DOUBLE, "
                     "flux DOUBLE, flux_error DOUBLE, jd_tcb DOUBLE) "
                     "TAGS (healpix_id BIGINT, source_id BIGINT, ra DOUBLE, dec DOUBLE, cls NCHAR(32))").c_str());
    cout << "✅ 数据库已就绪" << endl;
    
    // 加载元数据
    write_progress_json(0, "Loading coordinates...", "running", 0, 0, 0, 0, 0);
    cout << "📖 加载坐标数据..." << endl;
    map<int64_t, pair<double, double>> coords;
    ifstream cfile(coords_file);
    string line;
    getline(cfile, line);
    while (getline(cfile, line)) {
        auto tokens = split(line, ',');
        if (tokens.size() >= 3) {
            try { coords[stoll(tokens[0])] = {stod(tokens[1]), stod(tokens[2])}; } catch(...) {}
        }
    }
    cout << "✅ 加载 " << coords.size() << " 个坐标" << endl;
    
    write_progress_json(0, "Calculating HEALPix...", "running", 0, 0, 0, 0, 0);
    cout << "🗺️  计算 HEALPix..." << endl;
    Healpix_Base hp(64, NEST, SET_NSIDE);
    map<int64_t, int64_t> healpix_map;
    for (auto& [sid, c] : coords) {
        double theta = (90.0 - c.second) * M_PI / 180.0;
        double phi = c.first * M_PI / 180.0;
        if (theta < 0) theta = 0; 
        if (theta > M_PI) theta = M_PI;
        healpix_map[sid] = hp.ang2pix(pointing(theta, phi));
    }
    
    // ========== 收集所有表信息 ==========
    write_progress_json(0, "Scanning files...", "running", 0, 0, 0, 0, 0);
    cout << "📋 扫描文件..." << endl;
    vector<SubTable> all_tables;
    
    for (const auto& entry : fs::directory_iterator(lc_dir)) {
        string filename = entry.path().filename().string();
        size_t last_us = filename.find_last_of('_');
        size_t dot = filename.find_last_of('.');
        if (last_us == string::npos) continue;
        
        int64_t source_id = 0;
        try {
            source_id = stoll(filename.substr(last_us + 1, dot - last_us - 1));
        } catch (...) { continue; }
        
        auto it_coord = coords.find(source_id);
        auto it_hp = healpix_map.find(source_id);
        if (it_coord == coords.end() || it_hp == healpix_map.end()) continue;
        
        SubTable st;
        st.file_path = entry.path().string();
        st.source_id = source_id;
        st.healpix_id = it_hp->second;
        st.ra = it_coord->second.first;
        st.dec = it_coord->second.second;
        st.cls = "Unknown";
        st.table_name = super_table + "_" + to_string(st.healpix_id) + "_" + to_string(source_id);
        
        all_tables.push_back(st);
    }
    cout << "✅ 扫描到 " << all_tables.size() << " 个文件" << endl;
    
    PerfStats stats;
    stats.total_files = all_tables.size();
    
    // ========== 阶段1：预先创建所有子表 ==========
    cout << "\n📋 阶段1: 预先创建子表..." << endl;
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
                cout << "\r  ✅ 已创建 " << stats.created_tables << " 张表..." << flush;
                write_progress_json(0, "Phase 1/2: Creating tables (" + to_string(stats.created_tables) + ")", "running", 0, stats.total_files, 0, stats.created_tables, elapsed);
            }
        }
    }
    if (!table_batch.empty()) {
        batch_create_tables(conn, table_batch, super_table, stats);
    }
    
    auto phase1_end = high_resolution_clock::now();
    double phase1_time = duration_cast<milliseconds>(phase1_end - phase1_start).count() / 1000.0;
    cout << "\r  ✅ 阶段1完成: 创建 " << stats.created_tables << " 张表，耗时 " 
         << fixed << setprecision(2) << phase1_time << " 秒" << endl;
    
    taos_close(conn);
    
    // ========== 阶段2：直接分片处理 ==========
    cout << "\n⚡ 阶段2: 直接分片处理 (" << NUM_THREADS << " 线程)..." << endl;
    auto phase2_start = high_resolution_clock::now();
    
    // 分配文件给各线程
    vector<vector<SubTable>> thread_tasks(NUM_THREADS);
    for (size_t i = 0; i < all_tables.size(); ++i) {
        thread_tasks[i % NUM_THREADS].push_back(all_tables[i]);
    }
    
    // 启动监控
    thread monitor(monitor_thread, ref(stats));
    
    // 启动工作线程
    vector<thread> workers;
    for (int i = 0; i < NUM_THREADS; ++i) {
        workers.emplace_back(direct_worker_thread, i, ref(thread_tasks[i]), ref(db_name), ref(stats));
    }
    
    // 等待完成
    for (auto& t : workers) t.join();
    monitor.join();
    
    auto phase2_end = high_resolution_clock::now();
    double phase2_time = duration_cast<milliseconds>(phase2_end - phase2_start).count() / 1000.0;
    double total_time = phase1_time + phase2_time;
    
    cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << "✅ 导入完成!" << endl;
    cout << "⏱️  阶段1(建表): " << fixed << setprecision(2) << phase1_time << " 秒" << endl;
    cout << "⏱️  阶段2(插入): " << fixed << setprecision(2) << phase2_time << " 秒" << endl;
    cout << "⏱️  总耗时: " << fixed << setprecision(2) << total_time << " 秒" << endl;
    cout << "📊 创建表数: " << stats.created_tables << endl;
    cout << "📊 插入行数: " << stats.inserted_records << endl;
    cout << "📊 平均吞吐: " << (int64_t)(stats.inserted_records / total_time) << " 行/秒" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    
    taos_cleanup();
    return 0;
}

