/*
 * 自动分类候选检测器
 * 
 * 功能：
 * 1. 从数据库查询所有天体的数据点数
 * 2. 与历史记录文件比较，找出新增或增长>20%的天体
 * 3. 输出待分类列表
 * 4. 用新数据更新历史文件
 * 
 * 使用：
 *   ./check_candidates --db <数据库名> [--threshold 0.2]
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <cstring>

#include <taos.h>

using namespace std;
namespace fs = std::filesystem;

constexpr int TAOS_PORT = 6041;

// 数据结构
struct SourceInfo {
    int64_t source_id;
    int64_t healpix_id;
    double ra;
    double dec;
    int64_t data_count;
};

// 工具函数
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

// 加载历史记录
map<int64_t, SourceInfo> load_history(const string& history_file) {
    map<int64_t, SourceInfo> history;
    
    ifstream f(history_file);
    if (!f.is_open()) {
        cout << "📋 历史记录文件不存在，将视为首次检测" << endl;
        return history;
    }
    
    string line;
    getline(f, line); // skip header
    
    while (getline(f, line)) {
        if (line.empty()) continue;
        auto tokens = split(line, ',');
        if (tokens.size() >= 5) {
            try {
                SourceInfo info;
                info.source_id = stoll(tokens[0]);
                info.data_count = stoll(tokens[1]);
                info.healpix_id = stoll(tokens[2]);
                info.ra = stod(tokens[3]);
                info.dec = stod(tokens[4]);
                history[info.source_id] = info;
            } catch (...) {}
        }
    }
    
    cout << "📋 加载 " << history.size() << " 条历史记录" << endl;
    return history;
}

// 保存历史记录
void save_history(const string& history_file, const map<int64_t, SourceInfo>& current) {
    ofstream f(history_file);
    f << "source_id,data_count,healpix_id,ra,dec\n";
    
    for (const auto& [sid, info] : current) {
        f << info.source_id << "," << info.data_count << "," << info.healpix_id << ","
          << fixed << setprecision(6) << info.ra << "," << info.dec << "\n";
    }
    
    cout << "📋 保存 " << current.size() << " 条记录到历史文件" << endl;
}

// 保存待分类列表
void save_candidates(const string& candidate_file, const vector<pair<SourceInfo, string>>& candidates) {
    // 追加模式
    bool file_exists = fs::exists(candidate_file);
    ofstream f(candidate_file, ios::app);
    
    if (!file_exists || fs::file_size(candidate_file) == 0) {
        f << "source_id,data_count,healpix_id,ra,dec,reason,timestamp\n";
    }
    
    time_t now = time(nullptr);
    for (const auto& [info, reason] : candidates) {
        f << info.source_id << "," << info.data_count << "," << info.healpix_id << ","
          << fixed << setprecision(6) << info.ra << "," << info.dec << ","
          << reason << "," << now << "\n";
    }
    
    cout << "📋 追加 " << candidates.size() << " 条待分类记录" << endl;
}

// 写入进度 JSON
void write_progress(int percent, const string& message, const string& status, int candidates_count = 0) {
    ofstream f("/tmp/check_candidates_progress.json");
    f << "{\"percent\":" << percent 
      << ",\"message\":\"" << message << "\""
      << ",\"status\":\"" << status << "\""
      << ",\"candidates\":" << candidates_count << "}";
    f.close();
}

int main(int argc, char* argv[]) {
    setbuf(stdout, NULL);
    
    string db_name = "";
    double threshold = 0.2;  // 默认 20% 增长阈值
    
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) db_name = argv[++i];
        else if (arg == "--threshold" && i + 1 < argc) threshold = stod(argv[++i]);
    }
    
    if (db_name.empty()) {
        cerr << "用法: " << argv[0] << " --db <数据库名> [--threshold 0.2]" << endl;
        return 1;
    }
    
    // 文件路径
    string exe_path = fs::canonical("/proc/self/exe").parent_path().string();
    string history_file = exe_path + "/../data/lc_counts_" + db_name + ".csv";
    string candidate_file = exe_path + "/../data/auto_classify_queue_" + db_name + ".csv";
    
    cout << "\n🔍 自动分类候选检测器" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << "📂 数据库: " << db_name << endl;
    cout << "📊 增长阈值: " << (threshold * 100) << "%" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << endl;
    
    write_progress(0, "连接数据库...", "running");
    
    // 连接数据库
    string taos_host = get_taos_host();
    TAOS* conn = taos_connect(taos_host.c_str(), "root", "taosdata", db_name.c_str(), TAOS_PORT);
    if (!conn) {
        cerr << "❌ 连接数据库失败" << endl;
        write_progress(0, "连接失败", "error");
        return 1;
    }
    cout << "✅ 已连接数据库" << endl;
    
    // 加载历史记录
    write_progress(10, "加载历史记录...", "running");
    map<int64_t, SourceInfo> history = load_history(history_file);
    
    // 从数据库查询当前所有天体的数据点数
    write_progress(20, "查询数据库...", "running");
    cout << "📊 查询数据库中所有天体的数据点数..." << endl;
    
    string sql = "SELECT source_id, healpix_id, FIRST(ra) as ra, FIRST(dec) as dec, COUNT(*) as cnt "
                 "FROM sensor_data GROUP BY source_id, healpix_id";
    
    TAOS_RES* res = taos_query(conn, sql.c_str());
    if (taos_errno(res) != 0) {
        cerr << "❌ 查询失败: " << taos_errstr(res) << endl;
        taos_free_result(res);
        taos_close(conn);
        write_progress(0, "查询失败", "error");
        return 1;
    }
    
    // 边读取边比较
    map<int64_t, SourceInfo> current;
    vector<pair<SourceInfo, string>> candidates;
    int new_count = 0;
    int growth_count = 0;
    int64_t read_count = 0;
    auto last_update = chrono::steady_clock::now();
    
    TAOS_ROW row;
    while ((row = taos_fetch_row(res)) != nullptr) {
        SourceInfo info;
        info.source_id = *(int64_t*)row[0];
        info.healpix_id = *(int64_t*)row[1];
        info.ra = *(double*)row[2];
        info.dec = *(double*)row[3];
        info.data_count = *(int64_t*)row[4];
        current[info.source_id] = info;
        read_count++;
        
        // 边读取边比较
        auto it = history.find(info.source_id);
        if (it == history.end()) {
            candidates.push_back({info, "new"});
            new_count++;
        } else {
            int64_t old_count = it->second.data_count;
            if (old_count > 0 && info.data_count > old_count) {
                double growth = (double)(info.data_count - old_count) / old_count;
                if (growth >= threshold) {
                    string reason = "growth_" + to_string((int)(growth * 100)) + "%";
                    candidates.push_back({info, reason});
                    growth_count++;
                }
            }
        }
        
        // 每 0.2 秒更新一次进度
        auto now = chrono::steady_clock::now();
        if (chrono::duration_cast<chrono::milliseconds>(now - last_update).count() >= 200) {
            string msg = "已读取 " + to_string(read_count) + " 条，待分类 " + to_string(candidates.size());
            write_progress(30, msg, "running", candidates.size());
            cout << "\r📊 " << msg << "    " << flush;
            last_update = now;
        }
    }
    taos_free_result(res);
    taos_close(conn);
    
    cout << "\r✅ 读取完成: " << current.size() << " 个天体，待分类 " << candidates.size() << "    " << endl;
    
    write_progress(80, "保存结果...", "running", candidates.size());
    
    cout << "📊 检测结果：" << endl;
    cout << "   • 新增天体: " << new_count << endl;
    cout << "   • 数据增长: " << growth_count << endl;
    cout << "   • 总计待分类: " << candidates.size() << endl;
    
    // 保存候选列表
    write_progress(80, "保存结果...", "running");
    if (!candidates.empty()) {
        save_candidates(candidate_file, candidates);
    }
    
    // 更新历史文件
    write_progress(90, "更新历史...", "running");
    save_history(history_file, current);
    
    write_progress(100, "完成", "completed", candidates.size());
    
    cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << "✅ 检测完成！" << endl;
    cout << "📋 待分类天体: " << candidates.size() << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    
    return 0;
}

