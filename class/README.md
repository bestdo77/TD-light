English | [中文](README_CN.md)

# TDlight Classification Module

Automated light curve classification pipeline powered by LightGBM and `feets`.

## Overview

1. Read a list of source IDs
2. Fetch light curves from TDengine
3. Extract astronomical features using `feets`
4. Run inference with a hierarchical LightGBM predictor
5. Output predictions with confidence scores
6. Write high-confidence results back to TDengine

## Requirements

- **Conda environment**: `tdlight`
- **TDengine**: `taosd` running on port `6030` (native C client)
- **Models**: pre-trained files in `models/hierarchical_unlimited/` (downloaded by `install.sh`)

## Key Paths

```
TDlight/class/
├── classify_pipeline.py         # Manual classification entry
├── auto_classify.py             # Automatic batch classification
├── hierarchical_predictor.py    # Hierarchical predictor (ONNX / sklearn)
└── README.md                    # This document
```

## Usage

Activate the environment before running any classification script:

```bash
conda activate tdlight
```

### Manual Classification

Dry-run mode (predict only, no DB update):

```bash
cd class
python classify_pipeline.py \
    --input test_ids.csv \
    --output results.csv \
    --threshold 0.95 \
    --dry-run
```

Update database for high-confidence samples:

```bash
python classify_pipeline.py \
    --input test_ids.csv \
    --output results.csv \
    --threshold 0.95 \
    --update
```

### Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--input` | (required) | Input ID list (CSV with `ID` or `source_id` column) |
| `--output` | (required) | Output result CSV |
| `--threshold` | `0.95` | Confidence threshold for writing labels |
| `--db` | `gaiadr2_lc` | Database name |
| `--host` | `localhost` | Database host |
| `--port` | `6030` | TDengine native port |
| `--update` | `False` | Actually write results to TDengine |
| `--dry-run` | `False` | Predict without writing |

### Input Format

```csv
ID,Class
5870536848431465216,Unknown
423946158683096960,Unknown
```

### Output Format

```csv
ID,Original_Class,Predicted_Class,Confidence,Updated_Class,Update_Status,Data_Points
5870536848431465216,Unknown,DSCT,0.9789,DSCT,db_updated,241
423946158683096960,Unknown,DSCT,0.6610,Unknown,low_confidence,253
```

## Auto Classification

See the main [README.md](../README.md) for the automatic classification workflow. In short:

```bash
cd class
python auto_classify.py \
    --db gaiadr2_lc \
    --batch-size 5000
```

## 15 Core Features

| # | Feature | Description |
|---|---------|-------------|
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

## Troubleshooting

### "Model file does not exist"

Make sure `install.sh` has downloaded the models:

```bash
ls models/hierarchical_unlimited/
```

### "Database connection failed"

Check if TDengine is running:

```bash
systemctl --user status taosd
# or
ps aux | grep taosd
```

### "Table does not exist"

Sub-tables are named `t_{source_id}`. Verify with:

```bash
taos -s "USE gaiadr2_lc; SHOW TABLES LIKE 't_5870536848431465216';"
```

### Proxy issues

The script disables proxies internally (`session.trust_env = False`). If problems persist:

```bash
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY
```
