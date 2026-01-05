English | [中文](README_CN.md)

# TDengine Light Curve Classifier

## Overview

This module implements an automated light curve classification pipeline based on LightGBM:

1. Read specified ID list
2. Fetch light curve data from TDengine
3. Extract 15 core features (using feets library)
4. Perform 10-class classification using pre-trained model
5. Output confidence scores and classification results
6. Update classification labels in TDengine when confidence >= threshold

### Supported Classes

| Code | Class | Description |
|------|-------|-------------|
| 0 | Non-var | Non-variable star |
| 1 | ROT | Rotational variable |
| 2 | EA | Algol-type eclipsing binary |
| 3 | EW | W Ursae Majoris-type eclipsing binary |
| 4 | CEP | Cepheid variable |
| 5 | DSCT | Delta Scuti variable |
| 6 | RRAB | RR Lyrae type ab |
| 7 | RRC | RR Lyrae type c |
| 8 | M | Mira variable |
| 9 | SR | Semi-regular variable |

---

## Requirements

### Python Environment

This script must run in the **feets** conda environment:

```bash
conda activate feets
```

Required dependencies:
- Python 3.9
- feets 0.4 (feature extraction library)
- lightgbm (classification model)
- pandas, numpy
- requests (REST API)
- joblib (model loading)

### TDengine Requirements

1. **taosd service** must be running (port 6041)
2. **taosAdapter** must be running (port 6044, for REST API)

### Starting taosAdapter

If taosAdapter is not running, start it with:

```bash
cd /mnt/nvme/home/yxh/code/TDengine-test

nohup /mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    --env TAOS_ADAPTER_PORT=6044 \
    tdengine-fs \
    taosadapter > runtime/taos_home/log/taosadapter.log 2>&1 &
```

Verify taosAdapter is running:
```bash
ss -tuln | grep 6044
# Should show: tcp LISTEN 0 4096 *:6044 *:*
```

---

## Key Paths

```
Project root: /mnt/nvme/home/yxh/code/TDengine-test/runtime-final/class
├── classify_pipeline.py          # Main script
└── README.md                     # This document

Model files: /mnt/nvme/home/yxh/code/leaves-retrain/results/
└── lgbm_111w_15features_tuned_20251226_114818/
    ├── lgbm_111w_model.pkl       # Trained LightGBM model
    └── metadata.pkl              # Class mapping and metadata
```

---

## Usage

### Basic Usage

```bash
cd /mnt/nvme/home/yxh/code/TDengine-test/runtime-final/class
conda activate feets

# Dry run mode (predict only, no database update)
python classify_pipeline.py \
    --input test_ids.csv \
    --output results.csv \
    --threshold 0.95 \
    --dry-run
```

### Execute Database Update

```bash
# Actually update database (samples with confidence >= 0.95)
python classify_pipeline.py \
    --input test_ids.csv \
    --output results.csv \
    --threshold 0.95 \
    --update
```

### Complete Parameter Reference

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--input` | (required) | Input ID list file (CSV, must contain ID column) |
| `--output` | (required) | Output result file (CSV) |
| `--threshold` | 0.95 | Confidence threshold for label updates |
| `--db` | catalog_test | Database name |
| `--host` | localhost | Database host |
| `--port` | 6044 | REST API port (taosAdapter) |
| `--table` | sensor_data | Super table name |
| `--model` | (see code) | Model file path |
| `--update` | False | Execute actual update (write to TDengine) |
| `--dry-run` | False | Dry run mode (output only) |
| `--verbose, -v` | False | Verbose output mode |

### Input File Format

CSV file must contain `ID` or `source_id` column:

```csv
ID,Class
5870536848431465216,Unknown
423946158683096960,Unknown
6073590601358642432,Unknown
```

### Output File Format

```csv
ID,Original_Class,Predicted_Class,Confidence,Updated_Class,Update_Status,Data_Points
5870536848431465216,Unknown,DSCT,0.9789,DSCT,db_updated,241
423946158683096960,Unknown,DSCT,0.6610,Unknown,low_confidence,253
```

Field descriptions:
- `ID`: Source ID
- `Original_Class`: Original class
- `Predicted_Class`: Model predicted class
- `Confidence`: Prediction confidence
- `Updated_Class`: Final class (after update)
- `Update_Status`: Status (db_updated/low_confidence/no_data/error)
- `Data_Points`: Number of light curve data points

---

## Performance Reference

Typical performance on current hardware:

| Operation | Average Time | Notes |
|-----------|--------------|-------|
| Model loading | ~1.2 s | One-time |
| Feature space init | ~1 ms | One-time |
| Database query | ~92 ms | Per sample |
| Feature extraction | ~343 ms | Per sample (**main bottleneck**) |
| Single sample inference | ~17 ms | Per sample |
| Batch inference | ~0.5 ms | Per sample |

**Theoretical throughput**: ~2.2 samples/second

**Main bottleneck**: Feature extraction (Lomb-Scargle period search is computationally intensive)

---

## 15 Core Features

| # | Feature Name | Description |
|---|--------------|-------------|
| 1 | PeriodLS | Lomb-Scargle period |
| 2 | Mean | Mean magnitude |
| 3 | Rcs | Cumulative sum range |
| 4 | Psi_eta | Phase-folded η statistic |
| 5 | StetsonK_AC | Autocorrelated Stetson K |
| 6 | Gskew | Light curve skewness |
| 7 | Psi_CS | Phase-folded cumulative sum |
| 8 | Skew | Skewness |
| 9 | Freq1_harmonics_amplitude_1 | First harmonic amplitude |
| 10 | Eta_e | η statistic |
| 11 | LinearTrend | Linear trend |
| 12 | Freq1_harmonics_amplitude_0 | Fundamental amplitude |
| 13 | AndersonDarling | Anderson-Darling statistic |
| 14 | MaxSlope | Maximum slope |
| 15 | StetsonK | Stetson K statistic |

---

## Troubleshooting

### 1. "Model file does not exist"

Verify model path is correct:
```bash
ls -la /mnt/nvme/home/yxh/code/leaves-retrain/results/lgbm_111w_15features_tuned_20251226_114818/
```

### 2. "Database connection failed"

Check if taosAdapter is running:
```bash
ss -tuln | grep 6044
curl -s -u root:taosdata "http://localhost:6044/rest/sql" -d "SELECT 1"
```

### 3. "Table does not exist" update failure

Sub-table naming convention is `t_{source_id}`, verify table exists:
```bash
# Execute via Apptainer
apptainer exec ... taos -s "USE catalog_test; SHOW TABLES LIKE 't_5870536848431465216';"
```

### 4. feets warning messages

feets library outputs some warnings (e.g., AndersonDarling, StetsonK value ranges), this is normal and doesn't affect results.

### 5. Proxy causing connection failure

The script internally disables proxy (`session.trust_env = False`), if issues persist:
```bash
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY
```

---

## Extension Development

### Modify Default Configuration

Edit the configuration section at the beginning of `classify_pipeline.py`:

```python
DB_HOST = "localhost"
DB_PORT = 6044  # REST API port
DB_NAME = "catalog_test"
SUPER_TABLE = "sensor_data"
MODEL_PATH = "/path/to/model.pkl"
```

### Add New Features

1. Modify `SELECTED_FEATURES` list
2. Ensure model was trained with the same features
3. Update feature list in metadata.pkl

### Use Different Model

```bash
python classify_pipeline.py --model /path/to/new_model.pkl ...
```

Ensure the new model directory contains a corresponding `metadata.pkl` file.
