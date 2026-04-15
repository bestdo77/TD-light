# TDlight Test Data

This directory contains a sample of Gaia DR2 variable star data for testing the TDlight system.

## Files

| File | Size | Description |
|------|------|-------------|
| `gaia_testdata.tar.gz` | ~40 MB | Compressed test dataset |

### Contents after extraction

```
catalogs/                  # Catalog files (708 files)
├── catalog_001.csv        # Observation 1
├── catalog_002.csv        # Observation 2
├── ...
└── catalog_708.csv        # Observation 708

lightcurves/               # Light curve files (3,800 files)
├── lightcurve_xxx.csv     # One file per object
└── ...

source_coordinates.csv     # Object coordinates
```

## Statistics

- **Objects**: 3,800
- **Observations**: 336,690
- **Catalog files**: 708
- **Bands**: G / BP / RP
- **Sky coverage**: Full sky (RA: 0°–360°, DEC: –87°–+87°)

## Quick Start

```bash
# 1. Extract data
cd testdata
tar -xzf gaia_testdata.tar.gz

# 2. Activate environment
cd ..
conda activate tdlight
source start_env.sh

# 3. Import via command line
cd insert
./lightcurve_importer \
    --lightcurves_dir ../testdata/lightcurves \
    --coords ../testdata/source_coordinates.csv \
    --db gaiadr2_lc \
    --threads 16
```

## Data Formats

### Catalog file (`catalogs/*.csv`)

Each catalog contains one row per object per observation:

```csv
source_id,ra,dec,class,band,time,flux,flux_err,mag,mag_err
1835164110276685440,300.42,26.46,Unknown,G,1763.615,24024.40,175.59,14.74,0.0079
```

### Light curve file (`lightcurves/*.csv`)

One file per object, containing all observations:

```csv
time,band,flux,flux_err,mag,mag_err
1710.067,G,3861.14,34.75,16.72,0.0098
```

### Coordinate file (`source_coordinates.csv`)

```csv
source_id,ra,dec
1007596926756899072,96.68,64.12
```

| Field | Type | Description |
|-------|------|-------------|
| `source_id` | int64 | Gaia DR2 source ID |
| `ra` | double | Right Ascension (degrees) |
| `dec` | double | Declination (degrees) |

## Data Source

Data from the [Gaia DR2 Variable Stars](https://gea.esac.esa.int/archive/) public dataset.

## License

Test data follows Gaia DR2 data usage terms.
