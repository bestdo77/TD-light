/*
 * Optimized TDengine HEALPix Spatial Query Tool
 * Supports:
 *   1. Cone Search
 *   2. Time Range Query for Single ID
 *   3. Batch Query Optimization
 * 
 * Compile: g++ -std=c++17 -O3 -march=native optimized_query.cpp -o optimized_query -ltaos -lhealpix_cxx -lpthread
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

// Constants
const double PI = 3.14159265358979323846;
const double DEG2RAD = PI / 180.0;
const double RAD2DEG = 180.0 / PI;

// Query result structure
struct QueryResult {
    int64_t ts;
    long long source_id;
    double ra, dec;
    string band, cls;
    double mag, mag_error;
    double flux, flux_error;
    double jd_tcb;
};

// Statistics
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
        
        cout << "[INFO] Initializing HEALPix (NSIDE=" << nside << ")..." << endl;
        healpix_map = make_unique<Healpix_Base>(nside, NEST, SET_NSIDE);
        
        cout << "[INFO] Connecting to TDengine database..." << endl;
        taos_init();
        
        conn = taos_connect(host.c_str(), user.c_str(), password.c_str(), 
                          database.c_str(), port);
        if (!conn) {
            throw runtime_error("Connection failed: " + string(taos_errstr(conn)));
        }
        
        cout << "[OK] Connected: " << database << "@" << host << ":" << port << endl;
    }
    
    ~OptimizedQueryEngine() {
        if (conn) {
            taos_close(conn);
        }
        taos_cleanup();
    }
    
    // Angular distance calculation (using spherical trigonometry)
    double calculateAngularDistance(double ra1, double dec1, double ra2, double dec2) {
        double ra1_rad = ra1 * DEG2RAD;
        double dec1_rad = dec1 * DEG2RAD;
        double ra2_rad = ra2 * DEG2RAD;
        double dec2_rad = dec2 * DEG2RAD;
        
        double dra = ra2_rad - ra1_rad;
        double cos_dist = sin(dec1_rad) * sin(dec2_rad) + 
                         cos(dec1_rad) * cos(dec2_rad) * cos(dra);
        
        // Prevent numerical errors
        cos_dist = max(-1.0, min(1.0, cos_dist));
        
        return acos(cos_dist) * RAD2DEG;
    }
    
    // Cone search - HEALPix accelerated
    QueryStats coneSearch(double center_ra, double center_dec, double radius_deg,
                         vector<QueryResult>& results, bool verbose = true,
                         const string& time_filter = "", int limit = -1) {
        
        QueryStats stats;
        stats.query_type = "cone_search";
        
        auto start_time = high_resolution_clock::now();
        
        // Parameter validation
        center_ra = fmod(center_ra, 360.0);
        if (center_ra < 0) center_ra += 360.0;
        center_dec = max(-90.0, min(90.0, center_dec));
        
        if (verbose) {
            cout << "\n=== Cone Search ===" << endl;
            cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
            cout << "  Center: RA=" << fixed << setprecision(6) << center_ra 
                 << " deg, DEC=" << center_dec << " deg" << endl;
            cout << "  Radius: " << radius_deg << " deg" << endl;
        }
        
        // 1. Use HEALPix to find all pixels in the cone region
        pointing center_pt(DEG2RAD * (90.0 - center_dec), DEG2RAD * center_ra);
        double radius_rad = radius_deg * DEG2RAD;
        
        vector<int> pixels;
        healpix_map->query_disc(center_pt, radius_rad, pixels);
        
        if (pixels.empty()) {
            // If no pixels found, use at least the center pixel
            int center_pix = healpix_map->ang2pix(center_pt);
            pixels.push_back(center_pix);
        }
        
        stats.healpix_pixels_searched = pixels.size();
        
        if (verbose) {
            cout << "  HEALPix pixels: " << pixels.size() << endl;
        }
        
        // 2. Build optimized SQL query
        ostringstream sql;
        sql << "SELECT ts, source_id, ra, dec, band, cls, mag, mag_error, "
            << "flux, flux_error, jd_tcb FROM " << super_table 
            << " WHERE healpix_id IN (";
        
        for (size_t i = 0; i < pixels.size(); ++i) {
            if (i > 0) sql << ",";
            sql << pixels[i];
        }
        sql << ")";
        
        // Add time filter condition
        if (!time_filter.empty()) {
            sql << " AND " << time_filter;
        }
        
        // Add LIMIT
        if (limit > 0) {
            sql << " LIMIT " << limit;
        }
        
        if (verbose) {
            cout << "  SQL query length: " << sql.str().length() << " chars" << endl;
        }
        
        auto query_start = high_resolution_clock::now();
        
        // 3. Execute query
        TAOS_RES* res = taos_query(conn, sql.str().c_str());
        if (taos_errno(res) != 0) {
            string error = "Query failed: " + string(taos_errstr(res));
            taos_free_result(res);
            throw runtime_error(error);
        }
        
        auto fetch_start = high_resolution_clock::now();
        stats.query_time_ms = duration<double, milli>(fetch_start - query_start).count();
        
        // 4. Fetch results and perform precise angular distance filtering
        TAOS_ROW row;
        int total_fetched = 0;
        int filtered_count = 0;
        
        while ((row = taos_fetch_row(res))) {
            total_fetched++;
            
            // Parse results
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
            
            // Precise angular distance calculation
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
            cout << "\n[STATS] Query Statistics" << endl;
            cout << "  HEALPix filtered: " << total_fetched << " records" << endl;
            cout << "  Angular distance filtered: " << filtered_count << " records (exact match)" << endl;
            cout << "  Query time: " << fixed << setprecision(2) << stats.query_time_ms << " ms" << endl;
            cout << "  Fetch time: " << stats.fetch_time_ms << " ms" << endl;
            cout << "  Total time: " << total_time << " ms" << endl;
            cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << endl;
        }
        
        return stats;
    }
    
    // Time range query for single source_id
    QueryStats timeRangeQuery(long long source_id, const string& time_condition,
                             vector<QueryResult>& results, bool verbose = true,
                             int limit = -1) {
        
        QueryStats stats;
        stats.query_type = "time_range";
        
        auto start_time = high_resolution_clock::now();
        
        if (verbose) {
            cout << "\n=== Time Range Query ===" << endl;
            cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
            cout << "  Source ID: " << source_id << endl;
            cout << "  Time condition: " << time_condition << endl;
        }
        
        // Build SQL query (optimized with TAGS filtering)
        ostringstream sql;
        sql << "SELECT ts, source_id, ra, dec, band, cls, mag, mag_error, "
            << "flux, flux_error, jd_tcb FROM " << super_table 
            << " WHERE source_id = " << source_id;
        
        // Add time condition
        if (!time_condition.empty()) {
            sql << " AND " << time_condition;
        }
        
        // Order by time
        sql << " ORDER BY ts ASC";
        
        // Add LIMIT
        if (limit > 0) {
            sql << " LIMIT " << limit;
        }
        
        if (verbose) {
            cout << "  SQL: " << sql.str() << endl;
        }
        
        auto query_start = high_resolution_clock::now();
        
        // Execute query
        TAOS_RES* res = taos_query(conn, sql.str().c_str());
        if (taos_errno(res) != 0) {
            string error = "Query failed: " + string(taos_errstr(res));
            taos_free_result(res);
            throw runtime_error(error);
        }
        
        auto fetch_start = high_resolution_clock::now();
        stats.query_time_ms = duration<double, milli>(fetch_start - query_start).count();
        
        // Fetch results
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
            cout << "\n[STATS] Query Statistics" << endl;
            cout << "  Result count: " << stats.total_results << " records" << endl;
            cout << "  Query time: " << fixed << setprecision(2) << stats.query_time_ms << " ms" << endl;
            cout << "  Fetch time: " << stats.fetch_time_ms << " ms" << endl;
            cout << "  Total time: " << total_time << " ms" << endl;
            cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << endl;
        }
        
        return stats;
    }
    
    // Batch cone search (multi-center optimization)
    map<int, QueryStats> batchConeSearch(const vector<tuple<double, double, double>>& queries,
                                        map<int, vector<QueryResult>>& all_results,
                                        bool verbose = true) {
        map<int, QueryStats> stats_map;
        
        cout << "\n=== Batch Cone Search ===" << endl;
        cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
        cout << "  Query count: " << queries.size() << endl;
        
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
                cout << "  Progress: " << (i + 1) << "/" << queries.size() << endl;
            }
        }
        
        auto total_end = high_resolution_clock::now();
        double total_time = duration<double, milli>(total_end - total_start).count();
        
        // Statistics
        int total_results = 0;
        for (const auto& [idx, stats] : stats_map) {
            total_results += stats.total_results;
        }
        
        cout << "\n[STATS] Batch Query Complete" << endl;
        cout << "  Total queries: " << queries.size() << endl;
        cout << "  Total results: " << total_results << endl;
        cout << "  Total time: " << fixed << setprecision(2) << total_time << " ms" << endl;
        cout << "  Avg time: " << (total_time / queries.size()) << " ms/query" << endl;
        cout << "  Throughput: " << fixed << setprecision(1) 
             << (queries.size() * 1000.0 / total_time) << " queries/s" << endl;
        cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << endl;
        
        return stats_map;
    }
    
    // Export results to file (CSV format)
    void exportToCSV(const vector<QueryResult>& results, const string& filename) {
        ofstream file(filename);
        if (!file.is_open()) {
            throw runtime_error("Cannot create output file: " + filename);
        }
        
        // Write header
        file << "ts,source_id,ra,dec,band,cls,mag,mag_error,flux,flux_error,jd_tcb\n";
        
        // Write data
        for (const auto& r : results) {
            file << r.ts << "," << r.source_id << ","
                 << fixed << setprecision(8) << r.ra << ","
                 << r.dec << "," << r.band << "," << r.cls << ","
                 << setprecision(6) << r.mag << "," << r.mag_error << ","
                 << r.flux << "," << r.flux_error << ","
                 << setprecision(10) << r.jd_tcb << "\n";
        }
        
        file.close();
        cout << "[OK] Results exported to: " << filename << " (" << results.size() << " records)" << endl;
    }
    
    // Display first N results
    void displayResults(const vector<QueryResult>& results, int max_display = 10) {
        if (results.empty()) {
            cout << "  No results" << endl;
            return;
        }
        
        int display_count = min(max_display, (int)results.size());
        
        cout << "\n[RESULTS] Query results (showing " << display_count << " of " 
             << results.size() << " records)" << endl;
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
            cout << "  ... " << (results.size() - display_count) << " more results not shown" << endl;
        }
        cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << endl;
    }
};

void printUsage(const char* program) {
    cout << "\nUsage:\n" << endl;
    cout << "Cone Search:" << endl;
    cout << "  " << program << " --cone --ra <deg> --dec <deg> --radius <deg> [options]" << endl;
    cout << endl;
    cout << "Time Range Query:" << endl;
    cout << "  " << program << " --time --source_id <ID> --time_cond \"<condition>\" [options]" << endl;
    cout << endl;
    cout << "Batch Cone Search:" << endl;
    cout << "  " << program << " --batch --input <CSV_file> [options]" << endl;
    cout << "     CSV format: ra,dec,radius (one query per line)" << endl;
    cout << endl;
    cout << "Common Options:" << endl;
    cout << "  --db <name>          Database name (default: test_db)" << endl;
    cout << "  --host <address>     Server address (default: localhost)" << endl;
    cout << "  --port <port>        Port (default: 6030)" << endl;
    cout << "  --user <user>        Username (default: root)" << endl;
    cout << "  --password <pass>    Password (default: taosdata)" << endl;
    cout << "  --table <name>       Super table name (default: sensor_data)" << endl;
    cout << "  --nside <value>      HEALPix NSIDE (default: 64)" << endl;
    cout << "  --output <file>      Output CSV file" << endl;
    cout << "  --limit <count>      Limit result count" << endl;
    cout << "  --display <count>    Display result count (default: 10)" << endl;
    cout << "  --quiet              Quiet mode (no verbose output)" << endl;
    cout << endl;
    cout << "Examples:" << endl;
    cout << "  # Cone search: center(180 deg, 30 deg), radius 0.1 deg" << endl;
    cout << "  " << program << " --cone --ra 180 --dec 30 --radius 0.1 --output results.csv" << endl;
    cout << endl;
    cout << "  # Time query: source_id=12345, last 30 days" << endl;
    cout << "  " << program << " --time --source_id 12345 --time_cond \"ts >= NOW() - INTERVAL(30, DAY)\"" << endl;
    cout << endl;
    cout << "  # Batch query" << endl;
    cout << "  " << program << " --batch --input queries.csv --output batch_results/" << endl;
    cout << endl;
}

int main(int argc, char* argv[]) {
    try {
        // Default parameters
        string mode;
        string db_name = "test_db";
        string host = "localhost";
        string user = "root";
        string password = "taosdata";
        string table = "sensor_data";
        int port = 6030;
        int nside = 64;
        
        // Cone search parameters
        double ra = -999, dec = -999, radius = -1;
        
        // Time query parameters
        long long source_id = -1;
        string time_cond;
        
        // Batch query parameters
        string input_file;
        
        // Output parameters
        string output_file;
        int limit = -1;
        int display = 10;
        bool verbose = true;
        
        // Parse arguments
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
        
        // Create query engine
        cout << "=== Optimized TDengine HEALPix Query Tool ===" << endl;
        cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << endl;
        
        OptimizedQueryEngine engine(host, user, password, db_name, table, nside, port);
        
        // Execute query
        if (mode == "cone") {
            // Cone search
            if (ra == -999 || dec == -999 || radius == -1) {
                cerr << "[ERROR] Cone search requires --ra, --dec, --radius parameters" << endl;
                return 1;
            }
            
            vector<QueryResult> results;
            engine.coneSearch(ra, dec, radius, results, verbose);
            
            // Display results
            engine.displayResults(results, display);
            
            // Export results
            if (!output_file.empty()) {
                engine.exportToCSV(results, output_file);
            }
        }
        else if (mode == "time") {
            // Time range query
            if (source_id == -1) {
                cerr << "[ERROR] Time query requires --source_id parameter" << endl;
                return 1;
            }
            
            vector<QueryResult> results;
            engine.timeRangeQuery(source_id, time_cond, results, verbose, limit);
            
            // Display results
            engine.displayResults(results, display);
            
            // Export results
            if (!output_file.empty()) {
                engine.exportToCSV(results, output_file);
            }
        }
        else if (mode == "batch") {
            // Batch cone search
            if (input_file.empty()) {
                cerr << "[ERROR] Batch query requires --input parameter" << endl;
                return 1;
            }
            
            // Read batch query file
            ifstream file(input_file);
            if (!file.is_open()) {
                cerr << "[ERROR] Cannot open input file: " << input_file << endl;
                return 1;
            }
            
            vector<tuple<double, double, double>> queries;
            string line;
            getline(file, line); // Skip header
            
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
            
            cout << "[INFO] Loaded batch queries: " << queries.size() << endl;
            
            // Execute batch query
            map<int, vector<QueryResult>> all_results;
            engine.batchConeSearch(queries, all_results, verbose);
            
            // Export results
            if (!output_file.empty()) {
                for (const auto& [idx, results] : all_results) {
                    string out = output_file + "/query_" + to_string(idx) + ".csv";
                    engine.exportToCSV(results, out);
                }
            }
        }
        else {
            cerr << "[ERROR] Query mode required: --cone, --time, or --batch" << endl;
            printUsage(argv[0]);
            return 1;
        }
        
        cout << "[OK] Query complete" << endl;
        
        return 0;
        
    } catch (const exception& e) {
        cerr << "[ERROR] " << e.what() << endl;
        return 1;
    }
}


