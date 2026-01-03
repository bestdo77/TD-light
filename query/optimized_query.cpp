/*
 * 优化的 TDengine HEALPix 空间查询工具
 * 支持：
 *   1. 锥形检索（Cone Search）
 *   2. 单个 ID 的时间范围检索
 *   3. 批量查询优化
 * 
 * 编译: g++ -std=c++17 -O3 -march=native optimized_query.cpp -o optimized_query -ltaos -lhealpix_cxx -lpthread
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <map>
#include <memory>
#include <taos.h>
#include <healpix_cxx/healpix_base.h>
#include <healpix_cxx/pointing.h>

using namespace std;
using namespace std::chrono;

// 常量定义
const double PI = 3.14159265358979323846;
const double DEG2RAD = PI / 180.0;
const double RAD2DEG = 180.0 / PI;

// 查询结果结构
struct QueryResult {
    int64_t ts;
    long long source_id;
    double ra, dec;
    string band, cls;
    double mag, mag_error;
    double flux, flux_error;
    double jd_tcb;
};

// 统计信息
struct QueryStats {
    int total_results = 0;
    double query_time_ms = 0;
    double fetch_time_ms = 0;
    int healpix_pixels_searched = 0;
    string query_type;
};

class OptimizedQueryEngine {
private:
    TAOS* conn;
    string db_name;
    string super_table;
    int nside;
    unique_ptr<Healpix_Base> healpix_map;
    
public:
    OptimizedQueryEngine(const string& host = "localhost",
                        const string& user = "root",
                        const string& password = "taosdata",
                        const string& database = "test_db",
                        const string& table = "sensor_data",
                        int nside_param = 64,
                        int port = 6030)
        : db_name(database), super_table(table), nside(nside_param) {
        
        cout << "🔧 初始化 HEALPix (NSIDE=" << nside << ")..." << endl;
        healpix_map = make_unique<Healpix_Base>(nside, NEST, SET_NSIDE);
        
        cout << "🔗 连接 TDengine 数据库..." << endl;
        taos_init();
        
        conn = taos_connect(host.c_str(), user.c_str(), password.c_str(), 
                          database.c_str(), port);
        if (!conn) {
            throw runtime_error("❌ 连接失败: " + string(taos_errstr(conn)));
        }
        
        cout << "✅ 连接成功: " << database << "@" << host << ":" << port << endl;
    }
    
    ~OptimizedQueryEngine() {
        if (conn) {
            taos_close(conn);
        }
        taos_cleanup();
    }
    
    // 角距离计算（使用球面三角学）
    double calculateAngularDistance(double ra1, double dec1, double ra2, double dec2) {
        double ra1_rad = ra1 * DEG2RAD;
        double dec1_rad = dec1 * DEG2RAD;
        double ra2_rad = ra2 * DEG2RAD;
        double dec2_rad = dec2 * DEG2RAD;
        
        double dra = ra2_rad - ra1_rad;
        double cos_dist = sin(dec1_rad) * sin(dec2_rad) + 
                         cos(dec1_rad) * cos(dec2_rad) * cos(dra);
        
        // 防止数值误差
        cos_dist = max(-1.0, min(1.0, cos_dist));
        
        return acos(cos_dist) * RAD2DEG;
    }
    
    // 锥形检索 - 使用 HEALPix 加速
    QueryStats coneSearch(double center_ra, double center_dec, double radius_deg,
                         vector<QueryResult>& results, bool verbose = true,
                         const string& time_filter = "", int limit = -1) {
        
        QueryStats stats;
        stats.query_type = "cone_search";
        
        auto start_time = high_resolution_clock::now();
        
        // 参数验证
        center_ra = fmod(center_ra, 360.0);
        if (center_ra < 0) center_ra += 360.0;
        center_dec = max(-90.0, min(90.0, center_dec));
        
        if (verbose) {
            cout << "\n🎯 锥形检索" << endl;
            cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
            cout << "  中心坐标: RA=" << fixed << setprecision(6) << center_ra 
                 << "°, DEC=" << center_dec << "°" << endl;
            cout << "  搜索半径: " << radius_deg << "°" << endl;
        }
        
        // 1. 使用 HEALPix 找到锥形区域内的所有像素
        pointing center_pt(DEG2RAD * (90.0 - center_dec), DEG2RAD * center_ra);
        double radius_rad = radius_deg * DEG2RAD;
        
        vector<int> pixels;
        healpix_map->query_disc(center_pt, radius_rad, pixels);
        
        if (pixels.empty()) {
            // 如果没有找到像素，至少使用中心像素
            int center_pix = healpix_map->ang2pix(center_pt);
            pixels.push_back(center_pix);
        }
        
        stats.healpix_pixels_searched = pixels.size();
        
        if (verbose) {
            cout << "  HEALPix像素: " << pixels.size() << " 个" << endl;
        }
        
        // 2. 构建优化的 SQL 查询
        ostringstream sql;
        sql << "SELECT ts, source_id, ra, dec, band, cls, mag, mag_error, "
            << "flux, flux_error, jd_tcb FROM " << super_table 
            << " WHERE healpix_id IN (";
        
        for (size_t i = 0; i < pixels.size(); ++i) {
            if (i > 0) sql << ",";
            sql << pixels[i];
        }
        sql << ")";
        
        // 添加时间过滤条件
        if (!time_filter.empty()) {
            sql << " AND " << time_filter;
        }
        
        // 添加 LIMIT
        if (limit > 0) {
            sql << " LIMIT " << limit;
        }
        
        if (verbose) {
            cout << "  SQL查询长度: " << sql.str().length() << " 字符" << endl;
        }
        
        auto query_start = high_resolution_clock::now();
        
        // 3. 执行查询
        TAOS_RES* res = taos_query(conn, sql.str().c_str());
        if (taos_errno(res) != 0) {
            string error = "查询失败: " + string(taos_errstr(res));
            taos_free_result(res);
            throw runtime_error(error);
        }
        
        auto fetch_start = high_resolution_clock::now();
        stats.query_time_ms = duration<double, milli>(fetch_start - query_start).count();
        
        // 4. 获取结果并进行精确的角距离过滤
        TAOS_ROW row;
        int total_fetched = 0;
        int filtered_count = 0;
        
        while ((row = taos_fetch_row(res))) {
            total_fetched++;
            
            // 解析结果
            QueryResult result;
            result.ts = *(int64_t*)row[0];
            result.source_id = *(long long*)row[1];
            result.ra = *(double*)row[2];
            result.dec = *(double*)row[3];
            result.band = row[4] ? string((char*)row[4]) : "";
            result.cls = row[5] ? string((char*)row[5]) : "";
            result.mag = *(double*)row[6];
            result.mag_error = *(double*)row[7];
            result.flux = *(double*)row[8];
            result.flux_error = *(double*)row[9];
            result.jd_tcb = *(double*)row[10];
            
            // 精确角距离计算
            double dist = calculateAngularDistance(center_ra, center_dec, 
                                                   result.ra, result.dec);
            
            if (dist <= radius_deg) {
                results.push_back(result);
                filtered_count++;
            }
        }
        
        auto fetch_end = high_resolution_clock::now();
        stats.fetch_time_ms = duration<double, milli>(fetch_end - fetch_start).count();
        
        taos_free_result(res);
        
        stats.total_results = filtered_count;
        
        auto end_time = high_resolution_clock::now();
        double total_time = duration<double, milli>(end_time - start_time).count();
        
        if (verbose) {
            cout << "\n📊 查询统计" << endl;
            cout << "  HEALPix筛选: " << total_fetched << " 条记录" << endl;
            cout << "  角距离过滤: " << filtered_count << " 条记录（精确匹配）" << endl;
            cout << "  查询耗时: " << fixed << setprecision(2) << stats.query_time_ms << " ms" << endl;
            cout << "  数据获取: " << stats.fetch_time_ms << " ms" << endl;
            cout << "  总耗时: " << total_time << " ms" << endl;
            cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << endl;
        }
        
        return stats;
    }
    
    // 单个 source_id 的时间范围检索
    QueryStats timeRangeQuery(long long source_id, const string& time_condition,
                             vector<QueryResult>& results, bool verbose = true,
                             int limit = -1) {
        
        QueryStats stats;
        stats.query_type = "time_range";
        
        auto start_time = high_resolution_clock::now();
        
        if (verbose) {
            cout << "\n⏰ 时间范围查询" << endl;
            cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
            cout << "  Source ID: " << source_id << endl;
            cout << "  时间条件: " << time_condition << endl;
        }
        
        // 构建 SQL 查询（使用 TAGS 过滤优化）
        ostringstream sql;
        sql << "SELECT ts, source_id, ra, dec, band, cls, mag, mag_error, "
            << "flux, flux_error, jd_tcb FROM " << super_table 
            << " WHERE source_id = " << source_id;
        
        // 添加时间条件
        if (!time_condition.empty()) {
            sql << " AND " << time_condition;
        }
        
        // 按时间排序
        sql << " ORDER BY ts ASC";
        
        // 添加 LIMIT
        if (limit > 0) {
            sql << " LIMIT " << limit;
        }
        
        if (verbose) {
            cout << "  SQL: " << sql.str() << endl;
        }
        
        auto query_start = high_resolution_clock::now();
        
        // 执行查询
        TAOS_RES* res = taos_query(conn, sql.str().c_str());
        if (taos_errno(res) != 0) {
            string error = "查询失败: " + string(taos_errstr(res));
            taos_free_result(res);
            throw runtime_error(error);
        }
        
        auto fetch_start = high_resolution_clock::now();
        stats.query_time_ms = duration<double, milli>(fetch_start - query_start).count();
        
        // 获取结果
        TAOS_ROW row;
        while ((row = taos_fetch_row(res))) {
            QueryResult result;
            result.ts = *(int64_t*)row[0];
            result.source_id = *(long long*)row[1];
            result.ra = *(double*)row[2];
            result.dec = *(double*)row[3];
            result.band = row[4] ? string((char*)row[4]) : "";
            result.cls = row[5] ? string((char*)row[5]) : "";
            result.mag = *(double*)row[6];
            result.mag_error = *(double*)row[7];
            result.flux = *(double*)row[8];
            result.flux_error = *(double*)row[9];
            result.jd_tcb = *(double*)row[10];
            
            results.push_back(result);
        }
        
        auto fetch_end = high_resolution_clock::now();
        stats.fetch_time_ms = duration<double, milli>(fetch_end - fetch_start).count();
        
        taos_free_result(res);
        
        stats.total_results = results.size();
        
        auto end_time = high_resolution_clock::now();
        double total_time = duration<double, milli>(end_time - start_time).count();
        
        if (verbose) {
            cout << "\n📊 查询统计" << endl;
            cout << "  结果数量: " << stats.total_results << " 条记录" << endl;
            cout << "  查询耗时: " << fixed << setprecision(2) << stats.query_time_ms << " ms" << endl;
            cout << "  数据获取: " << stats.fetch_time_ms << " ms" << endl;
            cout << "  总耗时: " << total_time << " ms" << endl;
            cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << endl;
        }
        
        return stats;
    }
    
    // 批量锥形检索（多中心点优化）
    map<int, QueryStats> batchConeSearch(const vector<tuple<double, double, double>>& queries,
                                        map<int, vector<QueryResult>>& all_results,
                                        bool verbose = true) {
        map<int, QueryStats> stats_map;
        
        cout << "\n🚀 批量锥形检索" << endl;
        cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
        cout << "  查询数量: " << queries.size() << endl;
        
        auto total_start = high_resolution_clock::now();
        
        for (size_t i = 0; i < queries.size(); ++i) {
            double ra = get<0>(queries[i]);
            double dec = get<1>(queries[i]);
            double radius = get<2>(queries[i]);
            
            vector<QueryResult> results;
            QueryStats stats = coneSearch(ra, dec, radius, results, false);
            
            all_results[i] = move(results);
            stats_map[i] = stats;
            
            if (verbose && (i + 1) % 10 == 0) {
                cout << "  进度: " << (i + 1) << "/" << queries.size() << endl;
            }
        }
        
        auto total_end = high_resolution_clock::now();
        double total_time = duration<double, milli>(total_end - total_start).count();
        
        // 统计
        int total_results = 0;
        for (const auto& [idx, stats] : stats_map) {
            total_results += stats.total_results;
        }
        
        cout << "\n📊 批量查询完成" << endl;
        cout << "  总查询数: " << queries.size() << endl;
        cout << "  总结果数: " << total_results << " 条" << endl;
        cout << "  总耗时: " << fixed << setprecision(2) << total_time << " ms" << endl;
        cout << "  平均耗时: " << (total_time / queries.size()) << " ms/查询" << endl;
        cout << "  吞吐量: " << fixed << setprecision(1) 
             << (queries.size() * 1000.0 / total_time) << " 查询/秒" << endl;
        cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << endl;
        
        return stats_map;
    }
    
    // 输出结果到文件（CSV格式）
    void exportToCSV(const vector<QueryResult>& results, const string& filename) {
        ofstream file(filename);
        if (!file.is_open()) {
            throw runtime_error("无法创建输出文件: " + filename);
        }
        
        // 写入表头
        file << "ts,source_id,ra,dec,band,cls,mag,mag_error,flux,flux_error,jd_tcb\n";
        
        // 写入数据
        for (const auto& r : results) {
            file << r.ts << "," << r.source_id << ","
                 << fixed << setprecision(8) << r.ra << ","
                 << r.dec << "," << r.band << "," << r.cls << ","
                 << setprecision(6) << r.mag << "," << r.mag_error << ","
                 << r.flux << "," << r.flux_error << ","
                 << setprecision(10) << r.jd_tcb << "\n";
        }
        
        file.close();
        cout << "✅ 结果已导出到: " << filename << " (" << results.size() << " 条)" << endl;
    }
    
    // 显示前 N 条结果
    void displayResults(const vector<QueryResult>& results, int max_display = 10) {
        if (results.empty()) {
            cout << "  无结果" << endl;
            return;
        }
        
        int display_count = min(max_display, (int)results.size());
        
        cout << "\n📋 查询结果（显示前 " << display_count << " 条，共 " 
             << results.size() << " 条）" << endl;
        cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
        
        for (int i = 0; i < display_count; ++i) {
            const auto& r = results[i];
            cout << "[" << (i + 1) << "] Source " << r.source_id 
                 << " | RA=" << fixed << setprecision(6) << r.ra
                 << "° DEC=" << r.dec
                 << "° | Mag=" << setprecision(3) << r.mag
                 << " ± " << r.mag_error
                 << " | Band=" << r.band
                 << " | JD=" << setprecision(5) << r.jd_tcb << endl;
        }
        
        if (results.size() > display_count) {
            cout << "  ... 还有 " << (results.size() - display_count) << " 条结果未显示" << endl;
        }
        cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << endl;
    }
};

void printUsage(const char* program) {
    cout << "\n使用方法:\n" << endl;
    cout << "🎯 锥形检索:" << endl;
    cout << "  " << program << " --cone --ra <度> --dec <度> --radius <度> [选项]" << endl;
    cout << endl;
    cout << "⏰ 时间范围查询:" << endl;
    cout << "  " << program << " --time --source_id <ID> --time_cond \"<条件>\" [选项]" << endl;
    cout << endl;
    cout << "📦 批量锥形检索:" << endl;
    cout << "  " << program << " --batch --input <CSV文件> [选项]" << endl;
    cout << "     CSV格式: ra,dec,radius (每行一个查询)" << endl;
    cout << endl;
    cout << "通用选项:" << endl;
    cout << "  --db <名称>          数据库名 (默认: test_db)" << endl;
    cout << "  --host <地址>        服务器地址 (默认: localhost)" << endl;
    cout << "  --port <端口>        端口 (默认: 6030)" << endl;
    cout << "  --user <用户>        用户名 (默认: root)" << endl;
    cout << "  --password <密码>    密码 (默认: taosdata)" << endl;
    cout << "  --table <表名>       超级表名 (默认: sensor_data)" << endl;
    cout << "  --nside <值>         HEALPix NSIDE (默认: 64)" << endl;
    cout << "  --output <文件>      输出CSV文件" << endl;
    cout << "  --limit <数量>       限制结果数量" << endl;
    cout << "  --display <数量>     显示结果数量 (默认: 10)" << endl;
    cout << "  --quiet              静默模式（不显示详细信息）" << endl;
    cout << endl;
    cout << "示例:" << endl;
    cout << "  # 锥形检索: 中心(180°, 30°), 半径0.1°" << endl;
    cout << "  " << program << " --cone --ra 180 --dec 30 --radius 0.1 --output results.csv" << endl;
    cout << endl;
    cout << "  # 时间查询: source_id=12345, 最近30天" << endl;
    cout << "  " << program << " --time --source_id 12345 --time_cond \"ts >= NOW() - INTERVAL(30, DAY)\"" << endl;
    cout << endl;
    cout << "  # 批量查询" << endl;
    cout << "  " << program << " --batch --input queries.csv --output batch_results/" << endl;
    cout << endl;
}

int main(int argc, char* argv[]) {
    try {
        // 默认参数
        string mode;
        string db_name = "test_db";
        string host = "localhost";
        string user = "root";
        string password = "taosdata";
        string table = "sensor_data";
        int port = 6030;
        int nside = 64;
        
        // 锥形查询参数
        double ra = -999, dec = -999, radius = -1;
        
        // 时间查询参数
        long long source_id = -1;
        string time_cond;
        
        // 批量查询参数
        string input_file;
        
        // 输出参数
        string output_file;
        int limit = -1;
        int display = 10;
        bool verbose = true;
        
        // 解析参数
        if (argc < 2) {
            printUsage(argv[0]);
            return 1;
        }
        
        for (int i = 1; i < argc; ++i) {
            string arg = argv[i];
            
            if (arg == "--help" || arg == "-h") {
                printUsage(argv[0]);
                return 0;
            }
            else if (arg == "--cone") mode = "cone";
            else if (arg == "--time") mode = "time";
            else if (arg == "--batch") mode = "batch";
            else if (arg == "--ra" && i + 1 < argc) ra = stod(argv[++i]);
            else if (arg == "--dec" && i + 1 < argc) dec = stod(argv[++i]);
            else if (arg == "--radius" && i + 1 < argc) radius = stod(argv[++i]);
            else if (arg == "--source_id" && i + 1 < argc) source_id = stoll(argv[++i]);
            else if (arg == "--time_cond" && i + 1 < argc) time_cond = argv[++i];
            else if (arg == "--input" && i + 1 < argc) input_file = argv[++i];
            else if (arg == "--db" && i + 1 < argc) db_name = argv[++i];
            else if (arg == "--host" && i + 1 < argc) host = argv[++i];
            else if (arg == "--port" && i + 1 < argc) port = stoi(argv[++i]);
            else if (arg == "--user" && i + 1 < argc) user = argv[++i];
            else if (arg == "--password" && i + 1 < argc) password = argv[++i];
            else if (arg == "--table" && i + 1 < argc) table = argv[++i];
            else if (arg == "--nside" && i + 1 < argc) nside = stoi(argv[++i]);
            else if (arg == "--output" && i + 1 < argc) output_file = argv[++i];
            else if (arg == "--limit" && i + 1 < argc) limit = stoi(argv[++i]);
            else if (arg == "--display" && i + 1 < argc) display = stoi(argv[++i]);
            else if (arg == "--quiet") verbose = false;
        }
        
        // 创建查询引擎
        cout << "🚀 优化的 TDengine HEALPix 查询工具" << endl;
        cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
        
        OptimizedQueryEngine engine(host, user, password, db_name, table, nside, port);
        
        // 执行查询
        if (mode == "cone") {
            // 锥形检索
            if (ra == -999 || dec == -999 || radius == -1) {
                cerr << "❌ 锥形查询需要 --ra, --dec, --radius 参数" << endl;
                return 1;
            }
            
            vector<QueryResult> results;
            engine.coneSearch(ra, dec, radius, results, verbose);
            
            // 显示结果
            engine.displayResults(results, display);
            
            // 导出结果
            if (!output_file.empty()) {
                engine.exportToCSV(results, output_file);
            }
        }
        else if (mode == "time") {
            // 时间范围查询
            if (source_id == -1) {
                cerr << "❌ 时间查询需要 --source_id 参数" << endl;
                return 1;
            }
            
            vector<QueryResult> results;
            engine.timeRangeQuery(source_id, time_cond, results, verbose, limit);
            
            // 显示结果
            engine.displayResults(results, display);
            
            // 导出结果
            if (!output_file.empty()) {
                engine.exportToCSV(results, output_file);
            }
        }
        else if (mode == "batch") {
            // 批量锥形检索
            if (input_file.empty()) {
                cerr << "❌ 批量查询需要 --input 参数" << endl;
                return 1;
            }
            
            // 读取批量查询文件
            ifstream file(input_file);
            if (!file.is_open()) {
                cerr << "❌ 无法打开输入文件: " << input_file << endl;
                return 1;
            }
            
            vector<tuple<double, double, double>> queries;
            string line;
            getline(file, line); // 跳过表头
            
            while (getline(file, line)) {
                istringstream ss(line);
                string item;
                vector<string> fields;
                while (getline(ss, item, ',')) {
                    fields.push_back(item);
                }
                
                if (fields.size() >= 3) {
                    double q_ra = stod(fields[0]);
                    double q_dec = stod(fields[1]);
                    double q_radius = stod(fields[2]);
                    queries.push_back(make_tuple(q_ra, q_dec, q_radius));
                }
            }
            file.close();
            
            cout << "📖 读取批量查询: " << queries.size() << " 个" << endl;
            
            // 执行批量查询
            map<int, vector<QueryResult>> all_results;
            engine.batchConeSearch(queries, all_results, verbose);
            
            // 导出结果
            if (!output_file.empty()) {
                for (const auto& [idx, results] : all_results) {
                    string out = output_file + "/query_" + to_string(idx) + ".csv";
                    engine.exportToCSV(results, out);
                }
            }
        }
        else {
            cerr << "❌ 需要指定查询模式: --cone, --time, 或 --batch" << endl;
            printUsage(argv[0]);
            return 1;
        }
        
        cout << "✅ 查询完成" << endl;
        
        return 0;
        
    } catch (const exception& e) {
        cerr << "❌ 错误: " << e.what() << endl;
        return 1;
    }
}


