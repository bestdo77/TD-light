/*
 * 星表数据导入器 - 优化版本
 * 采用 STMT API + 直接分配 + 两阶段（先建表后插入）
 * 编译: g++ -std=c++17 -O3 -march=native catalog_importer.cpp -o catalog_importer -ltaos -lhealpix_cxx -lpthread
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

// ==================== 配置参数 ====================
constexpr int NUM_THREADS = 64;           // 并行线程数
constexpr int NUM_VGROUPS = 128;          // 虚拟分组数
constexpr int BATCH_SIZE = 10000;         // 每批次插入行数
constexpr int BUFFER_SIZE = 256;          // 每vgroup内存缓冲(MB)

// 从环境变量读取 TDengine 主机地址
string get_taos_host() {
    const char* env_host = getenv("TAOS_HOST");
    if (env_host != nullptr && strlen(env_host) > 0) {
        return string(env_host);
    }
    return "localhost";
}

struct Record {
    int64_t ts_ms;           // 观测时间（毫秒时间戳）
    string band;             // 波段
    double mag;              // 星等
    double flux;             // 流量
    double flux_error;       // 流量误差
    double mag_error;        // 星等误差
    double jd_tcb;           // 儒略日
};

struct SubTable {
    string table_name;       // 子表名 t_<source_id>
    long healpix_id;         // HEALPix ID
    long long source_id;     // 源ID
    string cls;              // 分类标签（作为TAG）
    double ra, dec;          // 坐标
    vector<Record> records;  // 观测记录
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

// 计算星等误差
double calculateMagError(double flux, double flux_error) {
    if (flux <= 0) return 0.01;
    return 1.0857 * flux_error / flux;
}

// ==================== 阶段1：并行建表 ====================
void create_tables_worker(int thread_id, const vector<SubTable*>& tables, 
                          size_t start, size_t end, 
                          const string& db_name, const string& super_table,
                          PerfStats& stats) {
    string taos_host = get_taos_host();
    TAOS* conn = taos_connect(taos_host.c_str(), "root", "taosdata", db_name.c_str(), 6041);
    if (!conn) {
        lock_guard<mutex> lock(cout_mutex);
        cerr << "❌ 线程 " << thread_id << " 连接失败" << endl;
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
            cerr << "❌ 建表失败 " << st->table_name << ": " << taos_errstr(res) << endl;
        }
        taos_free_result(res);
        stats.tables_created++;
    }
    
    taos_close(conn);
}

// ==================== 阶段2：STMT API 插入 ====================
void insert_worker(int thread_id, const vector<SubTable*>& tables,
                   size_t start, size_t end,
                   const string& db_name, PerfStats& stats) {
    string taos_host = get_taos_host();
    TAOS* conn = taos_connect(taos_host.c_str(), "root", "taosdata", db_name.c_str(), 6041);
    if (!conn) {
        lock_guard<mutex> lock(cout_mutex);
        cerr << "❌ 线程 " << thread_id << " 连接失败" << endl;
        return;
    }
    
    TAOS_STMT* stmt = taos_stmt_init(conn);
    if (!stmt) {
        lock_guard<mutex> lock(cout_mutex);
        cerr << "❌ 线程 " << thread_id << " STMT初始化失败" << endl;
        taos_close(conn);
        return;
    }
    
    // 准备 STMT
    const char* sql = "INSERT INTO ? VALUES(?,?,?,?,?,?,?)";
    if (taos_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        lock_guard<mutex> lock(cout_mutex);
        cerr << "❌ STMT prepare 失败: " << taos_stmt_errstr(stmt) << endl;
        taos_stmt_close(stmt);
        taos_close(conn);
        return;
    }
    
    // 分配缓冲区
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
        
        // 设置表名
        if (taos_stmt_set_tbname(stmt, st->table_name.c_str()) != 0) {
            lock_guard<mutex> lock(cout_mutex);
            cerr << "❌ 设置表名失败 " << st->table_name << ": " << taos_stmt_errstr(stmt) << endl;
            continue;
        }
        
        size_t total = st->records.size();
        for (size_t batch_start = 0; batch_start < total; batch_start += BATCH_SIZE) {
            size_t batch_end = min(batch_start + BATCH_SIZE, total);
            int batch_count = batch_end - batch_start;
            
            // 填充缓冲区
            for (int j = 0; j < batch_count; ++j) {
                const Record& r = st->records[batch_start + j];
                ts_buf[j] = r.ts_ms;
                
                // 处理 band 字符串
                memset(&band_buf[j * 17], 0, 17);
                strncpy(&band_buf[j * 17], r.band.c_str(), 16);
                band_len[j] = r.band.length();
                
                mag_buf[j] = r.mag;
                mag_error_buf[j] = r.mag_error;
                flux_buf[j] = r.flux;
                flux_error_buf[j] = r.flux_error;
                jd_buf[j] = r.jd_tcb;
            }
            
            // 绑定参数
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
                cerr << "❌ 绑定参数失败: " << taos_stmt_errstr(stmt) << endl;
                continue;
            }
            
            if (taos_stmt_add_batch(stmt) != 0) {
                lock_guard<mutex> lock(cout_mutex);
                cerr << "❌ 添加批次失败: " << taos_stmt_errstr(stmt) << endl;
                continue;
            }
            
            if (taos_stmt_execute(stmt) != 0) {
                lock_guard<mutex> lock(cout_mutex);
                cerr << "❌ 执行失败: " << taos_stmt_errstr(stmt) << endl;
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
    
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--catalogs" && i + 1 < argc) catalog_dir = argv[++i];
        else if (arg == "--coords" && i + 1 < argc) coords_file = argv[++i];
        else if (arg == "--db" && i + 1 < argc) db_name = argv[++i];
        else if (arg == "--nside" && i + 1 < argc) nside = stoi(argv[++i]);
    }
    
    if (catalog_dir.empty() || coords_file.empty()) {
        cout << "用法: " << argv[0] << " --catalogs <dir> --coords <file> [选项]" << endl;
        cout << "\n选项:" << endl;
        cout << "  --db <name>         数据库名称 (默认: catalog_database)" << endl;
        cout << "  --nside <N>         HEALPix NSIDE (默认: 64)" << endl;
        return 1;
    }
    
    cout << "\n🚀 星表数据导入器 (优化版本)" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << " 星表目录: " << catalog_dir << endl;
    cout << " 坐标文件: " << coords_file << endl;
    cout << " 数据库: " << db_name << endl;
    cout << " 线程数: " << NUM_THREADS << endl;
    cout << " vgroups: " << NUM_VGROUPS << endl;
    cout << " 批量大小: " << BATCH_SIZE << " 条/批" << endl;
    cout << " HEALPix NSIDE: " << nside << endl;
    cout << " 策略: STMT API + 直接分配 + 两阶段" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << endl;
    
    PerfStats stats;
    auto total_start = high_resolution_clock::now();
    
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
    
    // 初始化 TDengine
    taos_init();
    
    // 连接数据库
    string taos_host = get_taos_host();
    TAOS* conn = taos_connect(taos_host.c_str(), "root", "taosdata", NULL, 6041);
    if (!conn) {
        cerr << "❌ 连接失败 (host: " << taos_host << ")" << endl;
        taos_cleanup();
        return 1;
    }
    
    // 创建数据库（指定 vgroups）
    stringstream create_db_sql;
    create_db_sql << "CREATE DATABASE IF NOT EXISTS " << db_name 
                  << " VGROUPS " << NUM_VGROUPS 
                  << " BUFFER " << BUFFER_SIZE 
                  << " KEEP 36500";
    TAOS_RES* res = taos_query(conn, create_db_sql.str().c_str());
    if (taos_errno(res) != 0) {
        cerr << "❌ 创建数据库失败: " << taos_errstr(res) << endl;
        taos_free_result(res);
        taos_close(conn);
        taos_cleanup();
        return 1;
    }
    taos_free_result(res);
    
    string use_db = "USE " + db_name + ";";
    taos_query(conn, use_db.c_str());
    
    // 创建超级表
    string create_stable = "CREATE STABLE IF NOT EXISTS " + super_table + 
        " (ts TIMESTAMP, band NCHAR(16), "
        "mag DOUBLE, mag_error DOUBLE, flux DOUBLE, flux_error DOUBLE, jd_tcb DOUBLE) "
        "TAGS (healpix_id BIGINT, source_id BIGINT, ra DOUBLE, dec DOUBLE, cls NCHAR(32));";
    res = taos_query(conn, create_stable.c_str());
    if (taos_errno(res) != 0 && taos_errno(res) != 0x80002603) {
        cerr << "❌ 创建超级表失败: " << taos_errstr(res) << endl;
        taos_free_result(res);
        taos_close(conn);
        taos_cleanup();
        return 1;
    }
    taos_free_result(res);
    
    cout << "✅ 数据库和超级表已就绪 (vgroups=" << NUM_VGROUPS << ")" << endl;
    taos_close(conn);
    
    // 初始化 HEALPix
    Healpix_Base hp(nside, NEST, SET_NSIDE);
    
    // ==================== 读取坐标文件 ====================
    cout << "\n📖 读取坐标文件..." << endl;
    auto coord_start = high_resolution_clock::now();
    
    unordered_map<long long, pair<double, double>> coords_map;
    ifstream coord_file(coords_file);
    if (!coord_file.is_open()) {
        cerr << "❌ 无法打开坐标文件: " << coords_file << endl;
        taos_cleanup();
        return 1;
    }
    
    string line;
    getline(coord_file, line);  // 跳过表头
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
    cout << "  ✅ 读取 " << coords_map.size() << " 个源的坐标 (" << fixed << setprecision(2) << coord_time << "s)" << endl;
    
    // ==================== 读取星表文件 ====================
    cout << "\n📖 读取星表文件..." << endl;
    auto catalog_start = high_resolution_clock::now();
    
    vector<string> catalog_files;
    for (const auto& entry : fs::directory_iterator(catalog_dir)) {
        string filename = entry.path().filename().string();
        if (filename.find("catalog_") == 0 && filename.find(".csv") != string::npos) {
            catalog_files.push_back(entry.path().string());
        }
    }
    sort(catalog_files.begin(), catalog_files.end());
    
    cout << "  📁 找到 " << catalog_files.size() << " 个星表文件" << endl;
    
    // 收集每个源的数据
    map<long long, SubTable*> source_data;
    
    for (const auto& catalog_file : catalog_files) {
        ifstream file(catalog_file);
        if (!file.is_open()) continue;
        
        getline(file, line);  // 跳过表头
        
        while (getline(file, line)) {
            auto parts = split(line, ',');
            if (parts.size() < 7) continue;
            
            long long source_id = stoll(parts[0]);
            if (coords_map.find(source_id) == coords_map.end()) continue;
            
            if (source_data.find(source_id) == source_data.end()) {
                SubTable* st = new SubTable();
                st->source_id = source_id;
                st->table_name = "t_" + to_string(source_id);
                st->cls = "unknown";
                st->ra = coords_map[source_id].first;
                st->dec = coords_map[source_id].second;
                
                // 计算 HEALPix ID
                double theta = (90.0 - st->dec) * M_PI / 180.0;
                double phi = st->ra * M_PI / 180.0;
                pointing pt(theta, phi);
                st->healpix_id = hp.ang2pix(pt);
                
                source_data[source_id] = st;
            }
            
            Record rec;
            rec.band = parts[2];
            double time_days = stod(parts[3]);
            // Gaia DR2 reference epoch is J2015.5 (TCB) ~ JD 2457206.375 ?? 
            // Actually Gaia time is relative to J2010.0 TCB (JD 2455197.5)
            // Unix Epoch is JD 2440587.5
            // So we need: (time_days + 2455197.5 - 2440587.5) * 86400000
            rec.ts_ms = static_cast<int64_t>((time_days + 2455197.5 - 2451545.0) * 86400000);
            rec.mag = stod(parts[4]);
            rec.flux = stod(parts[5]);
            rec.flux_error = stod(parts[6]);
            rec.mag_error = calculateMagError(rec.flux, rec.flux_error);
            rec.jd_tcb = rec.ts_ms / 86400000.0 + 2451545.0;
            
            source_data[source_id]->records.push_back(rec);
            stats.total_records++;
        }
        file.close();
    }
    
    auto catalog_end = high_resolution_clock::now();
    double catalog_time = duration_cast<milliseconds>(catalog_end - catalog_start).count() / 1000.0;
    cout << "  ✅ 读取 " << source_data.size() << " 个源，共 " 
         << stats.total_records << " 条记录 (" << catalog_time << "s)" << endl;
    
    // 转换为 vector 便于分配
    vector<SubTable*> tables;
    tables.reserve(source_data.size());
    for (auto& pair : source_data) {
        tables.push_back(pair.second);
    }
    
    // ==================== 阶段1：并行建表 ====================
    cout << "\n🏗️  [阶段1] 并行建表 (" << NUM_THREADS << " 线程)..." << endl;
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
    cout << "  ✅ 创建 " << stats.tables_created << " 张表 (" << create_time << "s)" << endl;
    
    // ==================== 阶段2：STMT API 插入 ====================
    cout << "\n⚡ [阶段2] STMT API 插入 (" << NUM_THREADS << " 线程)..." << endl;
    auto insert_start = high_resolution_clock::now();
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        size_t start = i * tables_per_thread;
        size_t end = min(start + tables_per_thread, tables.size());
        if (start < tables.size()) {
            workers.emplace_back(insert_worker, i, ref(tables), start, end, 
                                ref(db_name), ref(stats));
        }
    }
    
    // 监控进度
    thread monitor([&]() {
        auto monitor_start = high_resolution_clock::now();
        while (stats.table_count < (int)tables.size()) {
            // 检查停止信号
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
            
            // 输出进度 JSON
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
            
            cout << "\r  📊 进度: " << stats.table_count << "/" << tables.size() 
                 << " 表 | 行: " << stats.inserted_records 
                 << " | 速度: " << fixed << setprecision(0) << speed << " 行/秒" << flush;
        }
        
        // 完成时写入 100%
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
    
    // ==================== 性能报告 ====================
    cout << "\n\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << "📊 星表导入性能报告 (优化版本)" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << fixed << setprecision(2);
    cout << "📖 数据读取:         " << (coord_time + catalog_time) << " 秒" << endl;
    cout << "🏗️  建表耗时:         " << create_time << " 秒" << endl;
    cout << "💾 插入耗时:         " << insert_time << " 秒" << endl;
    cout << "⏱️  总耗时:           " << total_time << " 秒" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << "📊 数据统计:" << endl;
    cout << "  • 表数量:           " << stats.table_count << endl;
    cout << "  • 总记录数:         " << stats.total_records << endl;
    cout << "  • 成功插入:         " << stats.inserted_records << endl;
    cout << "  • 总速率:           " << setprecision(0) << (stats.inserted_records / total_time) << " 行/秒" << endl;
    cout << "  • 纯插入速率:       " << setprecision(0) << (stats.inserted_records / insert_time) << " 行/秒" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    
    // 清理
    for (auto* st : tables) delete st;
    taos_cleanup();
    
    cout << "\n✅ 星表导入完成！" << endl;
    return 0;
}
