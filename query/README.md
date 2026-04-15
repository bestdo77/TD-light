English | [中文](README_CN.md)

# TDlight HEALPix Spatial Query Tool

Efficient spatial query utilities based on HEALPix.

## Features

1. **Cone Search** – search all sources within a circular region
2. **Time Range Query** – query observation records for a given `source_id`
3. **Batch Cone Search** – run multiple cone searches from a CSV file

### HEALPix Acceleration

- The celestial sphere is divided into equal-area pixels
- For a query, the tool first computes which HEALPix pixels overlap the search region
- Only relevant pixels are scanned, drastically reducing I/O
- Final precise angular-distance filtering is applied

---

## Requirements

- Linux x86_64
- TDengine native C client (`libtaos.so`)
- HEALPix C++ library (provided in `libs/`)
- `start_env.sh` sets `LD_LIBRARY_PATH` automatically

---

## Build

```bash
cd query

g++ -std=c++17 -O3 -march=native optimized_query.cpp -o optimized_query \
    -I../include \
    -L../libs \
    -L$HOME/taos/driver \
    -ltaos -lhealpix_cxx -lpthread \
    -Wl,-rpath,../libs \
    -Wl,-rpath,$HOME/taos/driver
```

Or use the top-level Makefile if available:

```bash
make query
```

---

## Usage

Make sure `taosd` is running and environment variables are set:

```bash
conda activate tdlight
source start_env.sh
```

### 1. Cone Search

```bash
./optimized_query \
    --cone --ra 180 --dec 30 --radius 0.1 \
    --db gaiadr2_lc \
    --output cone_results.csv
```

### 2. Time Range Query

```bash
./optimized_query \
    --time --source_id 5870536848431465216 \
    --db gaiadr2_lc \
    --output time_results.csv
```

With a time condition:

```bash
./optimized_query \
    --time --source_id 12345 \
    --time_cond "ts >= '2020-01-01' AND ts <= '2020-12-31'" \
    --db gaiadr2_lc
```

### 3. Batch Cone Search

Prepare `queries.csv`:

```csv
ra,dec,radius
180.0,30.0,0.1
181.0,31.0,0.05
```

Run:

```bash
./optimized_query \
    --batch --input queries.csv \
    --db gaiadr2_lc \
    --output batch_results/
```

---

## Parameters

### Query Modes

| Parameter | Description |
|-----------|-------------|
| `--cone` | Cone search mode |
| `--time` | Time range query mode |
| `--batch` | Batch cone search mode |

### Cone Search

| Parameter | Description |
|-----------|-------------|
| `--ra <degrees>` | Center RA (0–360) |
| `--dec <degrees>` | Center DEC (–90 to 90) |
| `--radius <degrees>` | Search radius |

### Time Query

| Parameter | Description |
|-----------|-------------|
| `--source_id <ID>` | Target source ID |
| `--time_cond "<condition>"` | SQL WHERE condition on `ts` |

### Batch Query

| Parameter | Description |
|-----------|-------------|
| `--input <file>` | Input CSV (`ra,dec,radius`) |

### Common

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--db` | `test_db` | Database name |
| `--host` | `localhost` | Server address |
| `--port` | `6030` | TDengine native port |
| `--user` | `root` | Username |
| `--password` | `taosdata` | Password |
| `--table` | `lightcurves` | Super table name |
| `--nside` | `64` | HEALPix NSIDE |
| `--output` | (none) | Output CSV file / directory |
| `--limit` | (none) | Limit result count |
| `--display` | `10` | Number of results to display |

---

## Output

### Console

```
Cone Search
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Center: RA=180.000000°, DEC=30.000000°
  Radius: 0.1°
  HEALPix pixels: 12

Query Statistics
  HEALPix filter: 150 records
  Angular distance filter: 42 records (exact match)
  Query time: 5.23 ms
  Data fetch: 2.15 ms
  Total time: 8.56 ms
```

### CSV

```csv
ts,source_id,ra,dec,band,cls,mag,mag_error,flux,flux_error,jd_tcb
1577836800000,5870536848431465216,180.123456,30.654321,G,DSCT,15.234,0.012,1234.56,12.34,2458849.5
```

---

## Database Schema

The super table queried by this tool:

| Field | Type | Description |
|-------|------|-------------|
| `ts` | TIMESTAMP | Observation timestamp |
| `band` | NCHAR(16) | Band |
| `mag` | DOUBLE | Magnitude |
| `mag_error` | DOUBLE | Magnitude error |
| `flux` | DOUBLE | Flux |
| `flux_error` | DOUBLE | Flux error |
| `jd_tcb` | DOUBLE | Julian date |

TAGs:

| TAG | Type | Description |
|-----|------|-------------|
| `healpix_id` | BIGINT | HEALPix pixel ID |
| `source_id` | BIGINT | Source ID |
| `ra` | DOUBLE | Right Ascension |
| `dec` | DOUBLE | Declination |
| `cls` | NCHAR(32) | Classification label |

---

## Performance

### NSIDE Selection

| NSIDE | Pixels | Pixel Area | Use Case |
|-------|--------|------------|----------|
| 32 | 12,288 | 3.36 deg² | Large-range rough search |
| 64 | 49,152 | 0.84 deg² | **Recommended default** |
| 128 | 196,608 | 0.21 deg² | Small-range precise search |
| 256 | 786,432 | 0.05 deg² | Ultra-high precision search |

### Typical Query Times

| Operation | Typical Time |
|-----------|--------------|
| Small cone query (r=0.1°) | 5–20 ms |
| Medium cone query (r=1°) | 50–200 ms |
| Single source time query | 10–50 ms |
| Batch query (100 items) | 1–5 s |

---

## Troubleshooting

### "Connection failed"

- Check that `taosd` is running: `systemctl --user status taosd`
- Verify port `6030` is accessible
- Ensure `LD_LIBRARY_PATH` includes `../libs` and `$HOME/taos/driver`

```bash
source ../start_env.sh
```

### "libhealpix_cxx.so not found"

Make sure `LD_LIBRARY_PATH` points to `libs/`:

```bash
export LD_LIBRARY_PATH=../libs:$HOME/taos/driver:$LD_LIBRARY_PATH
```

### "Database not found"

Check database name:

```bash
taos -s "SHOW DATABASES;"
```

### Empty results

- Verify coordinate ranges (RA: 0–360, DEC: –90 to 90)
- Try increasing the search radius
- Confirm the database contains data

---

## Extension

### Change Defaults

Edit the `main()` function in `optimized_query.cpp`:

```cpp
string db_name = "gaiadr2_lc";
int port = 6030;
int nside = 64;
```

Then recompile.
