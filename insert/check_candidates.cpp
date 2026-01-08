/*
 * Auto-classification Candidate Detector
 * 
 * Features:
 * 1. Query data point counts for all objects from database
 * 2. Compare with history file to find new objects or those with >20% data growth
 * 3. Output candidate list for classification
 * 4. Update history file with current data
 * 
 * Usage:
 *   ./check_candidates --db <database_name> [--threshold 0.2]
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

constexpr int TAOS_PORT = 6030;

// Data structures
struct SourceInfo {
    int64_t source_id;
    int64_t healpix_id;
    double ra;
    double dec;
    int64_t data_count;
};

// Utility functions
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

// Load history records
map<int64_t, SourceInfo> load_history(const string& history_file) {
    map<int64_t, SourceInfo> history;
    
    ifstream f(history_file);
    if (!f.is_open()) {
        cout << "[INFO] History file not found, treating as first detection" << endl;
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
    
    cout << "[INFO] Loaded " << history.size() << " history records" << endl;
    return history;
}

// Save history records
void save_history(const string& history_file, const map<int64_t, SourceInfo>& current) {
    ofstream f(history_file);
    f << "source_id,data_count,healpix_id,ra,dec\n";
    
    for (const auto& [sid, info] : current) {
        f << info.source_id << "," << info.data_count << "," << info.healpix_id << ","
          << fixed << setprecision(6) << info.ra << "," << info.dec << "\n";
    }
    
    cout << "[INFO] Saved " << current.size() << " records to history file" << endl;
}

// Save candidate list for classification
void save_candidates(const string& candidate_file, const vector<pair<SourceInfo, string>>& candidates) {
    // Append mode
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
    
    cout << "[INFO] Appended " << candidates.size() << " candidates to queue" << endl;
}

// Write progress JSON
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
    double threshold = 0.2;  // Default 20% growth threshold
    
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) db_name = argv[++i];
        else if (arg == "--threshold" && i + 1 < argc) threshold = stod(argv[++i]);
    }
    
    if (db_name.empty()) {
        cerr << "Usage: " << argv[0] << " --db <database_name> [--threshold 0.2]" << endl;
        return 1;
    }
    
    // File paths
    string exe_path = fs::canonical("/proc/self/exe").parent_path().string();
    string history_file = exe_path + "/../data/lc_counts_" + db_name + ".csv";
    string candidate_file = exe_path + "/../data/auto_classify_queue_" + db_name + ".csv";
    
    cout << "\n=== Auto-classification Candidate Detector ===" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << "[INFO] Database: " << db_name << endl;
    cout << "[INFO] Growth threshold: " << (threshold * 100) << "%" << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << endl;
    
    write_progress(0, "Connecting to database...", "running");
    
    // Connect to database
    string taos_host = get_taos_host();
    TAOS* conn = taos_connect(taos_host.c_str(), "root", "taosdata", db_name.c_str(), TAOS_PORT);
    if (!conn) {
        cerr << "[ERROR] Failed to connect to database" << endl;
        write_progress(0, "Connection failed", "error");
        return 1;
    }
    cout << "[OK] Connected to database" << endl;
    
    // Load history records
    write_progress(10, "Loading history records...", "running");
    map<int64_t, SourceInfo> history = load_history(history_file);
    
    // Query current data point counts for all objects from database
    write_progress(20, "Querying database...", "running");
    cout << "[INFO] Querying data point counts for all objects..." << endl;
    
    string sql = "SELECT source_id, healpix_id, FIRST(ra) as ra, FIRST(dec) as dec, COUNT(*) as cnt "
                 "FROM sensor_data GROUP BY source_id, healpix_id";
    
    TAOS_RES* res = taos_query(conn, sql.c_str());
    if (taos_errno(res) != 0) {
        cerr << "[ERROR] Query failed: " << taos_errstr(res) << endl;
        taos_free_result(res);
        taos_close(conn);
        write_progress(0, "Query failed", "error");
        return 1;
    }
    
    // Read and compare simultaneously
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
        
        // Compare while reading
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
        
        // Update progress every 0.2 seconds
        auto now = chrono::steady_clock::now();
        if (chrono::duration_cast<chrono::milliseconds>(now - last_update).count() >= 200) {
            string msg = "Read " + to_string(read_count) + " records, " + to_string(candidates.size()) + " candidates";
            write_progress(30, msg, "running", candidates.size());
            cout << "\r[INFO] " << msg << "    " << flush;
            last_update = now;
        }
    }
    taos_free_result(res);
    taos_close(conn);
    
    cout << "\r[OK] Read complete: " << current.size() << " objects, " << candidates.size() << " candidates    " << endl;
    
    write_progress(80, "Saving results...", "running", candidates.size());
    
    cout << "[INFO] Detection results:" << endl;
    cout << "   - New objects: " << new_count << endl;
    cout << "   - Data growth: " << growth_count << endl;
    cout << "   - Total candidates: " << candidates.size() << endl;
    
    // Save candidate list
    write_progress(80, "Saving results...", "running");
    if (!candidates.empty()) {
        save_candidates(candidate_file, candidates);
    }
    
    // Update history file
    write_progress(90, "Updating history...", "running");
    save_history(history_file, current);
    
    write_progress(100, "Complete", "completed", candidates.size());
    
    cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    cout << "[OK] Detection complete!" << endl;
    cout << "[INFO] Candidates for classification: " << candidates.size() << endl;
    cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
    
    return 0;
}

