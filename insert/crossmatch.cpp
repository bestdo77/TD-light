/*
 * Cross-Match Tool for TDlight
 * 
 * 功能: 将新天体目录与数据库中的天体进行交叉证认
 * - 匹配成功: 使用数据库中的 source_id
 * - 未匹配: 生成基于坐标的哈希唯一 ID
 * 
 * 编译: g++ -std=c++17 -O3 -march=native crossmatch.cpp -o crossmatch -ltaos -lhealpix_cxx -lpthread
 * 
 * 用法:
 *   ./crossmatch --catalog <new_catalog.csv> --db <database_name> [options]
 * 
 * 输出:
 *   - matched_catalog.csv: 证认后的星表（带唯一 source_id）
 *   - source_coordinates.csv: 坐标文件（包含所有天体）
 *   - crossmatch_report.txt: 证认报告
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cmath>
#include <functional>
#include <taos.h>
#include <healpix_cxx/healpix_base.h>
#include <healpix_cxx/pointing.h>

namespace fs = std::filesystem;
using namespace std;
using namespace std::chrono;

// ==================== 配置参数 ====================
int NUM_THREADS = 16;                    // 并行线程数
double MATCH_RADIUS_ARCSEC = 1.0;        // 匹配半径（角秒）
int NSIDE = 64;                          // HEALPix NSIDE
constexpr int BATCH_SIZE = 10000;        // 批处理大小

// ==================== 数据结构 ====================
struct DBObject {
    int64_t source_id;
    double ra, dec;
    long healpix_id;
};

struct CatalogObject {
    string original_id;                  // 原始ID（可能来自不同星表）
    double ra, dec;
    string cls;                          // 分类
    vector<string> extra_fields;         // 额外字段（光变曲线数据等）
};

struct MatchResult {
    int64_t unique_source_id;            // 唯一ID（数据库ID或哈希ID）
    double ra, dec;
    string cls;
    bool is_matched;                     // 是否匹配成功
    int64_t db_source_id;                // 匹配的数据库ID（-1表示未匹配）
    double separation_arcsec;            // 角距离（角秒）
    vector<string> extra_fields;
};

struct PerfStats {
    atomic<int> total_objects{0};
    atomic<int> matched_objects{0};
    atomic<int> new_objects{0};
    atomic<int> processed_objects{0};
};

mutex cout_mutex;
mutex file_write_mutex;

// ==================== 工具函数 ====================
vector<string> split(const string& line, char delim) {
    vector<string> result;
    stringstream ss(line);
    string item;
    while (getline(ss, item, delim)) result.push_back(item);
    return result;
}

string trim(const string& str) {
    size_t start = str.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    if (start == string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

// 计算角距离（Haversine公式），返回角秒
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
    
    return c * 180.0 / M_PI * 3600.0;  // 转换为角秒
}

// 基于坐标生成哈希ID（用于新天体）
int64_t generate_hash_id(double ra, double dec, int64_t salt = 20260404) {
    // 将坐标转换为整数（保留6位小数精度）
    int64_t ra_int = static_cast<int64_t>(ra * 1e6);
    int64_t dec_int = static_cast<int64_t>(dec * 1e6);
    
    // 使用简单的组合哈希
    uint64_t hash = 0xcbf29ce484222325ULL;  // FNV offset basis
    
    // 混合坐标和盐值
    auto mix = [&](uint64_t val) {
        hash ^= val;
        hash *= 0x100000001b3ULL;  // FNV prime
    };
    
    mix(static_cast<uint64_t>(ra_int));
    mix(static_cast<uint64_t>(dec_int));
    mix(static_cast<uint64_t>(salt));
    
    // 确保为正数且足够大（19位）
    uint64_t result = hash % 9000000000000000000ULL + 1000000000000000000ULL;
    return static_cast<int64_t>(result);
}

// 读取 TDengine 主机地址
string get_taos_host() {
    const char* env_host = getenv("TAOS_HOST");
    if (env_host != nullptr && strlen(env_host) > 0) {
        return string(env_host);
    }
    return "localhost";
}

// ==================== 从数据库加载天体 ====================
vector<DBObject> load_db_objects(const string& db_name, const string& super_table) {
    cout << "\n[1/4] Loading objects from database..." << endl;
    auto start = high_resolution_clock::now();
    
    string taos_host = get_taos_host();
    TAOS* conn = taos_connect(taos_host.c_str(), "root", "taosdata", db_name.c_str(), 6030);
    if (!conn) {
        cerr << "[ERROR] Failed to connect to database (host: " << taos_host << ")" << endl;
        return {};
    }
    
    // 查询所有天体的坐标
    string sql = "SELECT source_id, ra, dec, healpix_id FROM " + super_table + 
                 " GROUP BY source_id, ra, dec, healpix_id";
    
    TAOS_RES* res = taos_query(conn, sql.c_str());
    if (taos_errno(res) != 0) {
        cerr << "[ERROR] Query failed: " << taos_errstr(res) << endl;
        taos_free_result(res);
        taos_close(conn);
        return {};
    }
    
    vector<DBObject> db_objects;
    TAOS_ROW row;
    while ((row = taos_fetch_row(res)) != nullptr) {
        DBObject obj;
        obj.source_id = *(int64_t*)row[0];
        obj.ra = *(double*)row[1];
        obj.dec = *(double*)row[2];
        obj.healpix_id = *(int64_t*)row[3];
        db_objects.push_back(obj);
    }
    
    taos_free_result(res);
    taos_close(conn);
    
    auto end = high_resolution_clock::now();
    double elapsed = duration_cast<milliseconds>(end - start).count() / 1000.0;
    cout << "  [OK] Loaded " << db_objects.size() << " objects from database (" 
         << fixed << setprecision(2) << elapsed << "s)" << endl;
    
    return db_objects;
}

// ==================== 构建空间索引 ====================
struct SpatialIndex {
    // HEALPix -> 该像素内的天体列表
    unordered_map<long, vector<const DBObject*>> healpix_map;
    int nside;
    Healpix_Base hp;
    
    SpatialIndex(int nside_val) : nside(nside_val), hp(nside_val, NEST, SET_NSIDE) {}
    
    void build(const vector<DBObject>& db_objects) {
        cout << "\n[2/4] Building spatial index (HEALPix NSIDE=" << nside << ")..." << endl;
        auto start = high_resolution_clock::now();
        
        for (const auto& obj : db_objects) {
            healpix_map[obj.healpix_id].push_back(&obj);
        }
        
        auto end = high_resolution_clock::now();
        double elapsed = duration_cast<milliseconds>(end - start).count() / 1000.0;
        cout << "  [OK] Built index with " << healpix_map.size() << " HEALPix cells (" 
             << fixed << setprecision(2) << elapsed << "s)" << endl;
    }
    
    // 获取相邻的 HEALPix（包括自身）
    vector<long> get_neighboring_pixels(double ra, double dec) const {
        double theta = (90.0 - dec) * M_PI / 180.0;
        double phi = ra * M_PI / 180.0;
        if (theta < 0) theta = 0;
        if (theta > M_PI) theta = M_PI;
        pointing pt(theta, phi);
        
        long current_pix = hp.ang2pix(pt);
        vector<long> neighbors;
        neighbors.push_back(current_pix);
        
        // 获取相邻像素
        fix_arr<int, 8> pix_neighbors;
        hp.neighbors(current_pix, pix_neighbors);
        for (int i = 0; i < 8; i++) {
            if (pix_neighbors[i] >= 0) {
                neighbors.push_back(static_cast<long>(pix_neighbors[i]));
            }
        }
        
        return neighbors;
    }
    
    // 在匹配半径内查找最近的天体
    const DBObject* find_match(double ra, double dec, double max_sep_arcsec, double& out_sep) const {
        const DBObject* best_match = nullptr;
        double best_sep = max_sep_arcsec + 1.0;
        
        // 获取候选像素
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

// ==================== 并行交叉证认 ====================
void crossmatch_worker(int thread_id, 
                       const vector<CatalogObject>& catalog,
                       size_t start, size_t end,
                       const SpatialIndex& index,
                       double match_radius_arcsec,
                       vector<MatchResult>& results,
                       PerfStats& stats) {
    for (size_t i = start; i < end; ++i) {
        const auto& obj = catalog[i];
        MatchResult result;
        result.ra = obj.ra;
        result.dec = obj.dec;
        result.cls = obj.cls;
        result.extra_fields = obj.extra_fields;
        
        double sep = 0;
        const DBObject* match = index.find_match(obj.ra, obj.dec, match_radius_arcsec, sep);
        
        if (match != nullptr) {
            // 匹配成功
            result.unique_source_id = match->source_id;
            result.db_source_id = match->source_id;
            result.separation_arcsec = sep;
            result.is_matched = true;
            stats.matched_objects++;
        } else {
            // 未匹配，生成哈希ID
            result.unique_source_id = generate_hash_id(obj.ra, obj.dec, i);
            result.db_source_id = -1;
            result.separation_arcsec = -1;
            result.is_matched = false;
            stats.new_objects++;
        }
        
        results[i] = result;
        stats.processed_objects++;
    }
}

// ==================== 主函数 ====================
int main(int argc, char* argv[]) {
    string catalog_file, db_name = "gaiadr2_lc", super_table = "sensor_data";
    string output_dir = ".";
    double match_radius = MATCH_RADIUS_ARCSEC;
    int nside = NSIDE;
    
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--catalog" && i + 1 < argc) catalog_file = argv[++i];
        else if (arg == "--db" && i + 1 < argc) db_name = argv[++i];
        else if (arg == "--super_table" && i + 1 < argc) super_table = argv[++i];
        else if (arg == "--output" && i + 1 < argc) output_dir = argv[++i];
        else if (arg == "--radius" && i + 1 < argc) match_radius = stod(argv[++i]);
        else if (arg == "--nside" && i + 1 < argc) nside = stoi(argv[++i]);
        else if (arg == "--threads" && i + 1 < argc) NUM_THREADS = stoi(argv[++i]);
    }
    
    if (catalog_file.empty()) {
        cout << "Usage: " << argv[0] << " --catalog <catalog.csv> [options]" << endl;
        cout << "\nOptions:" << endl;
        cout << "  --db <name>           Database name (default: gaiadr2_lc)" << endl;
        cout << "  --super_table <name>  Super table name (default: sensor_data)" << endl;
        cout << "  --output <dir>        Output directory (default: current)" << endl;
        cout << "  --radius <arcsec>     Match radius in arcseconds (default: 1.0)" << endl;
        cout << "  --nside <N>           HEALPix NSIDE (default: 64)" << endl;
        cout << "  --threads <N>         Number of threads (default: 16)" << endl;
        return 1;
    }
    
    cout << "\n=== TDlight Cross-Match Tool ===" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << " Catalog file:      " << catalog_file << endl;
    cout << " Database:          " << db_name << endl;
    cout << " Super table:       " << super_table << endl;
    cout << " Match radius:      " << fixed << setprecision(2) << match_radius << " arcsec" << endl;
    cout << " HEALPix NSIDE:     " << nside << endl;
    cout << " Threads:           " << NUM_THREADS << endl;
    cout << " Output directory:  " << output_dir << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    
    auto total_start = high_resolution_clock::now();
    PerfStats stats;
    
    // 初始化 TDengine
    taos_init();
    
    // 1. 从数据库加载天体
    vector<DBObject> db_objects = load_db_objects(db_name, super_table);
    if (db_objects.empty()) {
        cerr << "[ERROR] No objects found in database" << endl;
        taos_cleanup();
        return 1;
    }
    
    // 2. 构建空间索引
    SpatialIndex index(nside);
    index.build(db_objects);
    
    // 3. 读取新天体目录
    cout << "\n[3/4] Reading catalog file..." << endl;
    auto catalog_start = high_resolution_clock::now();
    
    vector<CatalogObject> catalog;
    ifstream file(catalog_file);
    if (!file.is_open()) {
        cerr << "[ERROR] Cannot open catalog file: " << catalog_file << endl;
        taos_cleanup();
        return 1;
    }
    
    string line;
    getline(file, line);  // 跳过表头
    int line_num = 1;
    int skipped = 0;
    
    while (getline(file, line)) {
        line_num++;
        auto parts = split(line, ',');
        if (parts.size() < 3) { skipped++; continue; }
        
        try {
            CatalogObject obj;
            obj.original_id = trim(parts[0]);
            obj.ra = stod(trim(parts[1]));
            obj.dec = stod(trim(parts[2]));
            
            if (parts.size() > 3) obj.cls = trim(parts[3]);
            
            // 保存额外字段
            for (size_t i = 4; i < parts.size(); i++) {
                obj.extra_fields.push_back(trim(parts[i]));
            }
            
            catalog.push_back(obj);
        } catch (const exception& e) {
            skipped++;
            if (skipped <= 3) {
                cerr << "  [WARN] Skip line " << line_num << ": " << e.what() << endl;
            }
        }
    }
    file.close();
    
    auto catalog_end = high_resolution_clock::now();
    double catalog_time = duration_cast<milliseconds>(catalog_end - catalog_start).count() / 1000.0;
    cout << "  [OK] Read " << catalog.size() << " objects (" 
         << fixed << setprecision(2) << catalog_time << "s)" << endl;
    if (skipped > 0) {
        cout << "  [WARN] Skipped " << skipped << " invalid rows" << endl;
    }
    
    stats.total_objects = catalog.size();
    
    // 4. 并行交叉证认
    cout << "\n[4/4] Cross-matching (" << NUM_THREADS << " threads)..." << endl;
    auto match_start = high_resolution_clock::now();
    
    vector<MatchResult> results(catalog.size());
    vector<thread> workers;
    size_t per_thread = (catalog.size() + NUM_THREADS - 1) / NUM_THREADS;
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        size_t start = i * per_thread;
        size_t end = min(start + per_thread, catalog.size());
        if (start < catalog.size()) {
            workers.emplace_back(crossmatch_worker, i, cref(catalog), start, end,
                                cref(index), match_radius, ref(results), ref(stats));
        }
    }
    
    // 监控进度
    thread monitor([&]() {
        auto mon_start = high_resolution_clock::now();
        while (stats.processed_objects < stats.total_objects) {
            this_thread::sleep_for(milliseconds(500));
            auto now = high_resolution_clock::now();
            double elapsed = duration_cast<milliseconds>(now - mon_start).count() / 1000.0;
            double speed = stats.processed_objects / max(elapsed, 0.001);
            
            int pct = stats.total_objects > 0 ? 
                     (stats.processed_objects * 100 / stats.total_objects) : 0;
            
            cout << "\r  [PROGRESS] " << stats.processed_objects << "/" << stats.total_objects
                 << " (" << pct << "%) | Matched: " << stats.matched_objects
                 << " | New: " << stats.new_objects
                 << " | Speed: " << fixed << setprecision(0) << speed << " obj/s" << flush;
        }
    });
    
    for (auto& t : workers) t.join();
    monitor.join();
    
    auto match_end = high_resolution_clock::now();
    double match_time = duration_cast<milliseconds>(match_end - match_start).count() / 1000.0;
    cout << "\r  [OK] Cross-match complete!                              " << endl;
    
    // 5. 输出结果
    cout << "\n[OUTPUT] Writing results..." << endl;
    
    fs::create_directories(output_dir);
    
    // 5.1 输出证认后的星表
    string matched_file = output_dir + "/matched_catalog.csv";
    {
        lock_guard<mutex> lock(file_write_mutex);
        ofstream out(matched_file);
        out << "source_id,ra,dec,class,band,time,flux,flux_err,mag,mag_err" << endl;
        
        for (size_t i = 0; i < results.size(); i++) {
            const auto& r = results[i];
            out << r.unique_source_id << ","
                << fixed << setprecision(6) << r.ra << "," << r.dec << ","
                << r.cls;
            
            for (const auto& field : r.extra_fields) {
                out << "," << field;
            }
            out << endl;
        }
    }
    cout << "  [OK] " << matched_file << endl;
    
    // 5.2 输出坐标文件
    string coords_file = output_dir + "/source_coordinates.csv";
    {
        lock_guard<mutex> lock(file_write_mutex);
        ofstream out(coords_file);
        out << "source_id,ra,dec" << endl;
        
        // 使用 map 去重（同一 source_id 可能有多条记录）
        map<int64_t, pair<double, double>> coords_map;
        for (const auto& r : results) {
            if (coords_map.find(r.unique_source_id) == coords_map.end()) {
                coords_map[r.unique_source_id] = {r.ra, r.dec};
            }
        }
        
        for (const auto& [sid, coord] : coords_map) {
            out << sid << "," << fixed << setprecision(6) << coord.first << "," << coord.second << endl;
        }
    }
    cout << "  [OK] " << coords_file << endl;
    
    // 5.3 输出证认报告
    string report_file = output_dir + "/crossmatch_report.txt";
    {
        lock_guard<mutex> lock(file_write_mutex);
        ofstream report(report_file);
        
        auto total_end = high_resolution_clock::now();
        double total_time = duration_cast<milliseconds>(total_end - total_start).count() / 1000.0;
        
        report << "=== Cross-Match Report ===" << endl;
        report << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
        report << "Catalog file:      " << catalog_file << endl;
        report << "Database:          " << db_name << endl;
        report << "Match radius:      " << fixed << setprecision(2) << match_radius << " arcsec" << endl;
        report << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
        report << endl;
        report << "Results:" << endl;
        report << "  Total objects:   " << stats.total_objects << endl;
        report << "  Matched:         " << stats.matched_objects 
               << " (" << fixed << setprecision(1) 
               << (stats.total_objects > 0 ? stats.matched_objects * 100.0 / stats.total_objects : 0) << "%)" << endl;
        report << "  New objects:     " << stats.new_objects
               << " (" << fixed << setprecision(1)
               << (stats.total_objects > 0 ? stats.new_objects * 100.0 / stats.total_objects : 0) << "%)" << endl;
        report << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
        report << endl;
        report << "Performance:" << endl;
        report << "  Database load:   " << fixed << setprecision(2) 
               << duration_cast<milliseconds>(catalog_start - total_start).count() / 1000.0 << " s" << endl;
        report << "  Index build:     " << fixed << setprecision(2)
               << duration_cast<milliseconds>(catalog_start - match_start).count() / 1000.0 << " s" << endl;
        report << "  Catalog read:    " << catalog_time << " s" << endl;
        report << "  Cross-match:     " << match_time << " s" << endl;
        report << "  Total:           " << total_time << " s" << endl;
        report << "  Speed:           " << fixed << setprecision(0) 
               << (stats.total_objects / total_time) << " objects/s" << endl;
    }
    cout << "  [OK] " << report_file << endl;
    
    // 6. 打印摘要
    auto total_end = high_resolution_clock::now();
    double total_time = duration_cast<milliseconds>(total_end - total_start).count() / 1000.0;
    
    cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << "[SUMMARY] Cross-Match Complete" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << "  Total:    " << stats.total_objects << endl;
    cout << "  Matched:  " << stats.matched_objects << " (" 
         << fixed << setprecision(1) 
         << (stats.total_objects > 0 ? stats.matched_objects * 100.0 / stats.total_objects : 0) << "%)" << endl;
    cout << "  New:      " << stats.new_objects << " ("
         << fixed << setprecision(1)
         << (stats.total_objects > 0 ? stats.new_objects * 100.0 / stats.total_objects : 0) << "%)" << endl;
    cout << "  Time:     " << fixed << setprecision(2) << total_time << " s" << endl;
    cout << "  Speed:    " << fixed << setprecision(0) << (stats.total_objects / total_time) << " objects/s" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    
    taos_cleanup();
    return 0;
}
