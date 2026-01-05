English | [中文](README_CN.md)

# TDengine HEALPix Spatial Query Tool

## Overview

This module implements efficient spatial query tools based on HEALPix, supporting:

1. **Cone Search** - Given center coordinates and radius, search all sources within the region
2. **Time Range Query** - Query observation records for a specified source_id within a time range
3. **Batch Cone Search** - Read multiple query parameters from file and execute in batch

### HEALPix Acceleration Principle

- Uses HEALPix to divide the celestial sphere into equal-area pixels
- During query, first calculate HEALPix pixels covered by the cone region
- Only query data from relevant pixels, greatly reducing scan volume
- Finally perform precise angular distance filtering

---

## Requirements

### Must Run Inside Apptainer Container

This program uses TDengine native C interface and must run inside Apptainer container to ensure:
1. Correct TDengine configuration (`/etc/taos`)
2. Correct library paths (`libtaos.so`, `libhealpix_cxx.so`)

### Key Paths

```
Project root: /mnt/nvme/home/yxh/code/TDengine-test
├── tdengine-fs/                    # TDengine Apptainer container
├── runtime/
│   ├── taos_home/cfg/taos.cfg     # TDengine config (port 6041)
│   ├── libs/                       # HEALPix dependency libraries
│   └── deps/local/include/         # HEALPix header files
└── runtime-final/
    └── query/
        ├── optimized_query.cpp     # Source code
        ├── optimized_query         # Compiled executable
        └── README.md               # This document

Apptainer path:
APPTAINER_BIN=/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer
```

---

## Compilation

Compile on host machine (requires TDengine and HEALPix development libraries):

```bash
cd /mnt/nvme/home/yxh/code/TDengine-test/runtime-final/query

TAOS_DIR=/mnt/nvme/home/yxh/code/TDengine-test/tdengine-fs/usr/local/taos
LIBS_DIR=/mnt/nvme/home/yxh/code/TDengine-test/runtime/libs
DEPS_DIR=/mnt/nvme/home/yxh/code/TDengine-test/runtime/deps/local/include

g++ -std=c++17 -O3 -march=native optimized_query.cpp -o optimized_query \
    -I${TAOS_DIR}/include \
    -I${DEPS_DIR} \
    -L${TAOS_DIR}/driver \
    -L${LIBS_DIR} \
    -ltaos -lhealpix_cxx -lpthread \
    -Wl,-rpath,${TAOS_DIR}/driver \
    -Wl,-rpath,${LIBS_DIR}
```

---

## Running (Must Be Inside Apptainer Container)

### Basic Run Template

```bash
cd /mnt/nvme/home/yxh/code/TDengine-test

/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    --bind runtime-final:/app \
    --bind runtime/libs:/app/libs \
    --bind /usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1 \
    --env LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver \
    tdengine-fs \
    /app/query/optimized_query [parameters]
```

---

## Usage Examples

### 1. Cone Search

Search all sources within 0.1° radius centered at (RA=180°, DEC=30°):

```bash
cd /mnt/nvme/home/yxh/code/TDengine-test

/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    --bind runtime-final:/app \
    --bind runtime/libs:/app/libs \
    --bind /usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1 \
    --env LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver \
    tdengine-fs \
    /app/query/optimized_query \
    --cone --ra 180 --dec 30 --radius 0.1 \
    --db catalog_test --port 6041 \
    --output cone_results.csv
```

### 2. Time Range Query

Query all observation records for a specified source_id:

```bash
cd /mnt/nvme/home/yxh/code/TDengine-test

/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    --bind runtime-final:/app \
    --bind runtime/libs:/app/libs \
    --bind /usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1 \
    --env LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver \
    tdengine-fs \
    /app/query/optimized_query \
    --time --source_id 5870536848431465216 \
    --db catalog_test --port 6041 \
    --output time_results.csv
```

With time condition:
```bash
... --time --source_id 12345 --time_cond "ts >= '2020-01-01' AND ts <= '2020-12-31'" ...
```

### 3. Batch Cone Search

Read multiple query parameters from CSV file:

```bash
# Prepare query file queries.csv:
# ra,dec,radius
# 180.0,30.0,0.1
# 181.0,31.0,0.05
# 182.0,32.0,0.2

cd /mnt/nvme/home/yxh/code/TDengine-test

/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    --bind runtime-final:/app \
    --bind runtime/libs:/app/libs \
    --bind /usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1 \
    --env LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver \
    tdengine-fs \
    /app/query/optimized_query \
    --batch --input queries.csv \
    --db catalog_test --port 6041 \
    --output batch_results/
```

---

## Complete Parameter Reference

### Query Modes

| Parameter | Description |
|-----------|-------------|
| `--cone` | Cone search mode |
| `--time` | Time range query mode |
| `--batch` | Batch cone search mode |

### Cone Search Parameters

| Parameter | Description |
|-----------|-------------|
| `--ra <degrees>` | Center RA (0-360) |
| `--dec <degrees>` | Center DEC (-90 to 90) |
| `--radius <degrees>` | Search radius |

### Time Query Parameters

| Parameter | Description |
|-----------|-------------|
| `--source_id <ID>` | Target source ID |
| `--time_cond "<condition>"` | Time condition (SQL WHERE syntax) |

### Batch Query Parameters

| Parameter | Description |
|-----------|-------------|
| `--input <file>` | Input CSV file (format: ra,dec,radius) |

### Common Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--db` | test_db | Database name |
| `--host` | localhost | Server address |
| `--port` | 6030 | TDengine native port |
| `--user` | root | Username |
| `--password` | taosdata | Password |
| `--table` | sensor_data | Super table name |
| `--nside` | 64 | HEALPix NSIDE parameter |
| `--output` | (none) | Output CSV file/directory |
| `--limit` | (none) | Limit result count |
| `--display` | 10 | Number of results to display |
| `--quiet` | false | Quiet mode |

---

## Output Format

### Console Output

```
🎯 Cone Search
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Center: RA=180.000000°, DEC=30.000000°
  Radius: 0.1°
  HEALPix pixels: 12

📊 Query Statistics
  HEALPix filter: 150 records
  Angular distance filter: 42 records (exact match)
  Query time: 5.23 ms
  Data fetch: 2.15 ms
  Total time: 8.56 ms
```

### CSV Output Format

```csv
ts,source_id,ra,dec,band,cls,mag,mag_error,flux,flux_error,jd_tcb
1577836800000,5870536848431465216,180.123456,30.654321,G,DSCT,15.234,0.012,1234.56,12.34,2458849.5
```

---

## Database Table Structure

Super table structure queried by this tool:

| Field | Type | Description |
|-------|------|-------------|
| ts | TIMESTAMP | Observation timestamp |
| band | NCHAR(16) | Band |
| mag | DOUBLE | Magnitude |
| mag_error | DOUBLE | Magnitude error |
| flux | DOUBLE | Flux |
| flux_error | DOUBLE | Flux error |
| jd_tcb | DOUBLE | Julian date |

TAG fields:
| TAG | Type | Description |
|-----|------|-------------|
| healpix_id | BIGINT | HEALPix pixel ID |
| source_id | BIGINT | Source ID |
| ra | DOUBLE | Right Ascension |
| dec | DOUBLE | Declination |
| cls | NCHAR(32) | Classification label |

---

## Performance Optimization

### HEALPix NSIDE Selection

| NSIDE | Pixels | Pixel Area | Use Case |
|-------|--------|------------|----------|
| 32 | 12,288 | 3.36 deg² | Large range rough search |
| 64 | 49,152 | 0.84 deg² | **Recommended default** |
| 128 | 196,608 | 0.21 deg² | Small range precise search |
| 256 | 786,432 | 0.05 deg² | Ultra-high precision search |

### Query Performance Reference

| Operation | Typical Time |
|-----------|--------------|
| Small range cone query (r=0.1°) | 5-20 ms |
| Medium range cone query (r=1°) | 50-200 ms |
| Single source_id time query | 10-50 ms |
| Batch query (100 items) | 1-5 s |

---

## Troubleshooting

### 1. "Connection failed"

- Check if TDengine service is running
- Confirm port is 6041 (not default 6030)
- Must run inside Apptainer container

```bash
# Check taosd running status
ps aux | grep taosd
```

### 2. "libhealpix_cxx.so not found"

Ensure libs directory is bound and LD_LIBRARY_PATH is set:
```bash
--bind runtime/libs:/app/libs \
--env LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver
```

### 3. "libgomp.so.1 not found"

Bind host machine's libgomp:
```bash
--bind /usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1
```

### 4. "Database not found"

Check if database name is correct:
```bash
apptainer exec ... taos -s "SHOW DATABASES;"
```

### 5. Empty Results

- Check if coordinate range is correct (RA: 0-360, DEC: -90 to 90)
- Try increasing search radius
- Confirm database has data

---

## Quick Test

Verify installation and connection:

```bash
cd /mnt/nvme/home/yxh/code/TDengine-test

# View help
/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    --bind runtime-final:/app \
    --bind runtime/libs:/app/libs \
    --bind /usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1 \
    --env LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver \
    tdengine-fs \
    /app/query/optimized_query --help

# Simple cone query test
/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    --bind runtime-final:/app \
    --bind runtime/libs:/app/libs \
    --bind /usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1 \
    --env LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver \
    tdengine-fs \
    /app/query/optimized_query \
    --cone --ra 180 --dec 0 --radius 1 \
    --db catalog_test --port 6041 --display 5
```

---

## Extension Development

### Modify Default Parameters

Edit default values in `main()` function of `optimized_query.cpp`:

```cpp
string db_name = "catalog_test";  // Modify default database
int port = 6041;                   // Modify default port
int nside = 64;                    // Modify HEALPix precision
```

### Add New Query Modes

1. Add new method in `OptimizedQueryEngine` class
2. Add command line argument parsing in `main()`
3. Recompile

### Extend Output Format

Modify `exportToCSV()` and `displayResults()` methods.
