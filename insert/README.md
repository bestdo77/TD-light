# TDlight Data Import Tools

High-performance multi-threaded tools for importing catalog and light curve data into TDengine.

## Quick Start

### 1. Build

```bash
cd insert
./build.sh
```

### 2. Import Light Curves

```bash
./lightcurve_importer \
    --lightcurves_dir /path/to/lightcurves \
    --coords /path/to/source_coordinates.csv \
    --db my_database \
    --threads 16
```

### 3. Import Catalogs (with cross-match)

```bash
./catalog_importer \
    --catalogs /path/to/catalogs \
    --coords /path/to/source_coordinates.csv \
    --db my_database \
    --crossmatch 1 \
    --radius 1.0 \
    --threads 16
```

## Parameters

### `lightcurve_importer`

| Parameter | Required | Description | Default |
|-----------|----------|-------------|---------|
| `--lightcurves_dir` | Yes | Light curve CSV directory | - |
| `--coords` | Yes | Coordinate file path | - |
| `--db` | No | Database name | `gaiadr2_lc` |
| `--threads` | No | Thread count | `16` |
| `--vgroups` | No | VGroups count | `32` |
| `--drop_db` | No | Drop existing database | `false` |
| `--crossmatch` | No | Enable cross-match (`0`/`1`) | `1` |
| `--radius` | No | Cross-match radius (arcsec) | `1.0` |

### `catalog_importer`

| Parameter | Required | Description | Default |
|-----------|----------|-------------|---------|
| `--catalogs` | Yes | Catalog CSV directory | - |
| `--coords` | Yes | Coordinate file path | - |
| `--db` | No | Database name | `gaiadr2_lc` |
| `--threads` | No | Thread count | `16` |
| `--vgroups` | No | VGroups count | `32` |
| `--drop_db` | No | Drop existing database | `false` |
| `--crossmatch` | No | Enable cross-match (`0`/`1`) | `1` |
| `--radius` | No | Cross-match radius (arcsec) | `1.0` |
| `--nside` | No | HEALPix NSIDE | `64` |

## Data Formats

### Coordinate file (`source_coordinates.csv`)

```csv
source_id,ra,dec
1007596926756899072,96.68226266572204,64.11928378951181
```

### Light curve file (`lightcurve_<source_id>.csv`)

```csv
time,band,flux,flux_err,mag,mag_err
1707.3886320197084,G,6613.973617061971,18.91623645721968,16.13720957963514,0.0031051466350914205
```

### Catalog file (`catalog_<id>.csv`)

```csv
source_id,ra,dec,class,band,time,flux,flux_err,mag,mag_err
1007596926756899072,96.68226266572204,64.11928378951181,Unknown,G,1707.3886,6613.97,18.92,16.14,0.0031
```

## Recommendations

| Parameter | Suggested Value | Note |
|-----------|-----------------|------|
| Threads | 8–16 | Adjust to CPU cores |
| VGroups | 32 | Reduces write blocking |
| Cross-match radius | 1.0 arcsec | Spatial matching radius |

## Performance

| Dataset | Tables | Rows | Import Speed |
|---------|--------|------|--------------|
| Light curves | 3,800 | 336,690 | ~280K rows/s |
| Catalogs | 3,800 | 336,690 | ~280K rows/s |

## Troubleshooting

### Connection failed

```bash
# Check if TDengine is running
ps aux | grep taosd

# Restart TDengine if needed
systemctl --user restart taosd
```

### Library not found

```bash
# Set library path
export LD_LIBRARY_PATH=../libs:$HOME/taos/driver:$LD_LIBRARY_PATH
# Or use:
source ../start_env.sh
```

### Cross-match

Cross-match matches objects by coordinates (RA/DEC) against the database:
- Match found → use existing unified ID
- No match → generate a new hash ID

To disable:
```bash
./catalog_importer --crossmatch 0 ...
```

## Database Operations

```bash
# List databases
taos -s "SHOW DATABASES;"

# Drop database
taos -s "DROP DATABASE IF EXISTS my_database;"

# Query data
taos -s "USE my_database; SELECT * FROM lightcurves LIMIT 10;"
```

---

Run `./lightcurve_importer --help` or `./catalog_importer --help` for more details.
