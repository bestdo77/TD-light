English | [中文](README_CN.md)

# TDlight Web Service

## Overview

Provides a complete web service for light curve data management and classification.

### Core Features

| Feature | Description |
|---------|-------------|
| 📋 Object List | Browse objects in database |
| 📈 Light Curves | View time-series observation data and visualization |
| 🔍 Spatial Search | Cone search and region search |
| 🌌 Sky Map | 3D visualization of object distribution |
| 🤖 Intelligent Classification | Real-time classification using LightGBM model |
| 📥 Data Import | Batch import of CSV data |
| ⚙️ Database Management | Multi-database switching |

---

## Quick Start

### 1. Start Service

```bash
cd TDlight
./start_env.sh
# After entering container
cd /app/web
./web_api
```

### 2. Access Interface

Open browser: **http://localhost:5001**

---

## User Guide

### Object Browsing

1. Object list loads automatically when page opens
2. Click any object to view light curve
3. Use search box to search by source_id

### Spatial Search

**Cone Search** (centered on a point):
- Enter RA, DEC (degrees) and radius (degrees)
- Click "Search"

**Region Search** (rectangular area):
- Enter min/max values for RA/DEC
- Click "Search"

### Object Classification

1. Select classification method:
   - **Random Classification**: Randomly select objects from database
   - **Visible Objects**: Classify objects in current list
2. Set confidence threshold (only results above threshold are written to database)
3. Click "Start Classification"
4. View real-time progress and results

### Data Import

⚠️ **Before importing light curves, prepare**:
1. Light curve CSV directory (one file per object)
2. Coordinate file (containing RA/DEC for all objects)

**Steps**:
1. Switch to "Data Import" tab
2. Enter database name
3. Enter CSV directory path (container path)
4. Enter coordinate file path (container path)
5. Click "Start Import"
6. View real-time import progress and logs

**Path Notes**:
- If data is on host machine, mount to container via `--bind`
- e.g., host `/data/gaia` mounted as `/app/data/gaia`

### Configuration Management

In "System Settings" tab you can:
- Modify database connection parameters
- Modify classification model path
- Modify Python environment path
- Click "Save Config" to save locally
- Click "Apply to Backend" to make configuration effective

---

## Classification Model

Uses **LightGBM model**, supports 10-class variable star classification:

| Code | Type | Description |
|------|------|-------------|
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

### Features Used (15)

```
PeriodLS, Mean, Rcs, Psi_eta, StetsonK_AC,
Gskew, Psi_CS, Skew, Freq1_harmonics_amplitude_1, Eta_e,
LinearTrend, Freq1_harmonics_amplitude_0, AndersonDarling, MaxSlope, StetsonK
```

---

## API Endpoints

### Object Query

| Endpoint | Method | Parameters | Description |
|----------|--------|------------|-------------|
| `/api/objects` | GET | `limit` | Get object list |
| `/api/object/{table_name}` | GET | - | Get object details |
| `/api/object_by_id` | GET | `id` | Query by source_id |

### Light Curve

| Endpoint | Method | Parameters | Description |
|----------|--------|------------|-------------|
| `/api/lightcurve/{table_name}` | GET | `time_start`, `time_end` | Get observation data |

### Spatial Search

| Endpoint | Method | Parameters | Description |
|----------|--------|------------|-------------|
| `/api/cone_search` | GET | `ra`, `dec`, `radius` | Cone search |
| `/api/region_search` | GET | `ra_min/max`, `dec_min/max` | Region search |
| `/api/sky_map` | GET | `limit` | Sky map data |

### Classification

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/classify` | POST | Classify specified objects (SSE stream) |
| `/api/classify/stop` | POST | Stop classification task |

### Data Import

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/import/start` | POST | Start import task |
| `/api/import/stop` | POST | Stop import task |
| `/api/import/stream` | GET | Import progress (SSE stream) |

### Database Management

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/databases` | GET | Get database list |
| `/api/databases/drop` | POST | Delete database |
| `/api/config` | GET | Get current configuration |
| `/api/config/reload` | GET | Reload configuration |

---

## API Examples

### Cone Search

```bash
curl "http://localhost:5001/api/cone_search?ra=180&dec=30&radius=0.1"
```

### Get Light Curve

```bash
curl "http://localhost:5001/api/lightcurve/t_5870536848431465216"
```

### Classify Object

```bash
curl -X POST "http://localhost:5001/api/classify" \
  -H "Content-Type: application/json" \
  -d '{"objects": [{"source_id": "5870536848431465216"}], "threshold": 0.8}'
```

---

## Compilation

If you need to modify backend code:

```bash
cd TDlight/web
./build.sh
```

Compilation requires:
- g++ (C++17 support)
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
├── classify_pipeline.py  # Python classification script
└── README.md             # This document
```

---

## Troubleshooting

### 1. TDengine Connection Failed

- Confirm taosd service is running
- Confirm running inside Apptainer container
- Check database configuration in config.json

### 2. Classification Failed

- Confirm model files exist (models/*.pkl)
- Confirm Python environment path is correct
- Check paths.python in config.json

### 3. Data Import Not Responding

- Confirm path is container path
- Check if coordinate file exists
- Check error messages in /tmp/import.log

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
| Object list (200 items) | 50-200 ms |
| Single light curve | 10-50 ms |
| Cone search (r=0.1°) | 20-100 ms |
| Single object classification | 400-600 ms |
| Batch classification (per item) | ~50 ms |

---

## Changelog

### v2.0 (2026-01)

- SSE real-time progress updates
- Data import integration
- Dynamic configuration management
- Improved error handling
