English | [中文](README_CN.md)

# TDlight Web Service

A complete web service for light curve data management and classification.

## Core Features

| Feature | Description |
|---------|-------------|
| Object List | Browse objects in the database |
| Light Curves | View time-series observation data and plots |
| Spatial Search | Cone search and region search |
| Sky Map | 3D visualization of object distribution |
| Intelligent Classification | Real-time classification with LightGBM |
| Data Import | Batch CSV import via web UI |
| Database Management | Switch between multiple databases |

## Quick Start

### 1. Build & Start

```bash
cd web
./build.sh
./web_api
```

### 2. Open Browser

**http://localhost:5001**

---

## User Guide

### Browse Objects

1. The object list loads automatically when the page opens
2. Click any object to view its light curve
3. Use the search box to filter by `source_id`

### Spatial Search

**Cone Search** (centered on a point):
- Enter RA, DEC (degrees) and radius (degrees)
- Click "Search"

**Region Search** (rectangular area):
- Enter min/max values for RA and DEC
- Click "Search"

### Object Classification

1. Choose a classification mode:
   - **Random Classification**: randomly select objects from the database
   - **Visible Objects**: classify objects currently in the list
2. Set the confidence threshold (only results above the threshold are written to the database)
3. Click "Start Classification"
4. Watch real-time progress and results

### Data Import

**Before importing light curves, prepare**:
1. A directory of light curve CSV files (one file per object)
2. A coordinate file containing RA/DEC for all objects

**Steps**:
1. Switch to the "Data Import" tab
2. Enter the database name
3. Enter the CSV directory path
4. Enter the coordinate file path
5. Click "Start Import"
6. Watch real-time import progress and logs

### Configuration

In the "System Settings" tab you can:
- Modify database connection parameters
- Modify the classification model path
- Modify the Python executable path
- Click "Save Config" to save locally
- Click "Apply to Backend" to reload the configuration

---

## Classification Model

Uses a **hierarchical LightGBM predictor** (ONNX Runtime by default, auto-fallback to sklearn).

### Features Used (15)

```
PeriodLS, Mean, Rcs, Psi_eta, StetsonK_AC,
Gskew, Psi_CS, Skew, Freq1_harmonics_amplitude_1, Eta_e,
LinearTrend, Freq1_harmonics_amplitude_0, AndersonDarling, MaxSlope, StetsonK
```

---

## API Endpoints

See the main [README.md](../README.md) for the complete API reference.

Common examples:

### Cone Search

```bash
curl "http://localhost:5001/api/cone_search?ra=180&dec=30&radius=0.1"
```

### Get Light Curve

```bash
curl "http://localhost:5001/api/lightcurve/t_5870536848431465216"
```

### Start Classification

```bash
curl -X POST "http://localhost:5001/api/classify_objects" \
  -H "Content-Type: application/json" \
  -d '{"objects": [{"source_id": "5870536848431465216"}], "threshold": 0.8}'
```

---

## Build

If you modify the backend:

```bash
cd web
./build.sh
```

Requirements:
- g++ with C++17 support
- TDengine client library
- HEALPix C++ library

---

## File Structure

```
web/
├── web_api.cpp           # C++ backend source
├── web_api               # Compiled executable
├── build.sh              # Build script
├── index.html            # Frontend HTML
├── app.js                # Frontend JavaScript
├── lang.js               # i18n
├── sse_test.html         # SSE test page
└── README.md             # This document
```

---

## Troubleshooting

### 1. TDengine Connection Failed

- Confirm `taosd` is running: `systemctl --user status taosd`
- Check `config.json` for correct database settings

### 2. Classification Failed

- Confirm model files exist in `models/hierarchical_unlimited/`
- Confirm the `tdlight` conda environment is set up
- Check `paths.python` in `config.json`

### 3. Data Import Not Responding

- Confirm the path exists and is readable
- Check if the coordinate file exists
- Check error messages in the web UI logs

### 4. Port Already in Use

```bash
pkill -f web_api
# or
kill $(lsof -t -i:5001)
```

---

## Performance Reference

| Operation | Typical Time |
|-----------|--------------|
| Object list (200 items) | 50–200 ms |
| Single light curve | 10–50 ms |
| Cone search (r=0.1°) | 20–100 ms |
| Single object classification | 400–600 ms |
| Batch classification (per item) | ~50 ms |

---

## Changelog

### v2.0 (2026-01)

- SSE real-time progress updates
- Data import integration
- Dynamic configuration management
- Improved error handling
