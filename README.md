English | [中文](README_CN.md)

# TDlight

A light curve management and classification system for time-domain astronomy, powered by TDengine time-series database.

Supports efficient storage, fast retrieval, and intelligent classification of large-scale astronomical time-series data.

---

## Tech Stack

| Layer | Technology | Description |
|-------|------------|-------------|
| **Database** | TDengine 3.4+ | High-performance time-series database (user-mode) |
| **Backend** | C++17 | HTTP server, HEALPix spatial indexing |
| **Classification** | Python + LightGBM | feets feature extraction + machine learning |
| **Frontend** | HTML/JS | Three.js 3D, Chart.js visualization |

---

## Features

| Feature | Description |
|---------|-------------|
|  **Cone Search** | Search objects by celestial coordinates and radius |
|  **Region Search** | Batch query by RA/DEC range |
|  **Light Curve Visualization** | Interactive charts for time-series photometry |
|  **Intelligent Classification** | Automated variable star classification using LightGBM |
| 🤖 **Auto Classification** | Automatically detect and classify new objects in batches |
|  **Data Import** | One-click CSV data import via web interface |
| 🌍 **3D Celestial Sphere** | WebGL-rendered 3D visualization |

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Browser (Frontend)                        │
│   index.html + app.js (Three.js 3D / Chart.js / SSE real-time)  │
└───────────────────────────────┬─────────────────────────────────┘
                                │ HTTP/SSE
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                      web_api (C++ Backend)                       │
│                                                                 │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │ Search API  │  │Classify API │  │    Data Import API      │  │
│  │ cone_search │  │  classify   │  │  catalog_importer       │  │
│  │region_search│  │ (calls Py)  │  │  lightcurve_importer    │  │
│  └──────┬──────┘  └──────┬──────┘  └───────────┬─────────────┘  │
│         │                │                     │                │
│         ▼                ▼                     ▼                │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    TDengine C Client                         ││
│  │                      (libtaos.so)                            ││
│  └──────────────────────────────┬──────────────────────────────┘│
└─────────────────────────────────┼───────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────┐
│              TDengine Time-series Database (User Mode)            │
│                                                                 │
│   Super Table: lightcurves                                      │
│   ├── Tags: healpix_id, source_id, ra, dec, cls                 │
│   └── Columns: ts, band, mag, mag_error, flux, flux_error, jd   │
│                                                                 │
│   VGroups: 128 (supports ~2 complete databases)                 │
└─────────────────────────────────────────────────────────────────┘
```

### Component Connections

1. **Frontend (index.html + app.js)**
   - Uses Fetch API to call backend REST endpoints
   - Receives classification/import progress via Server-Sent Events (SSE)
   - Three.js renders 3D celestial sphere, Chart.js draws light curves

2. **Backend (web_api.cpp)**
   - Pure C++ HTTP server implementation
   - Uses HEALPix for celestial sphere pixelization to accelerate searches
   - Invokes Python classification scripts via `system()`
   - Invokes C++ importers via `system()`

3. **Classification Module (classify_pipeline.py)**
   - Called by web_api as a subprocess
   - Extracts light curve features using feets library
   - Classifies using pre-trained LightGBM model
   - Automatically writes high-confidence results back to TDengine

4. **Data Importers (catalog_importer / lightcurve_importer)**
   - Standalone C++ programs with multi-threaded parallel import
   - Web import defaults: 16 threads, 32 VGroups (for compatibility)
   - Command line customizable: `--threads N --vgroups N`
   - Frontend displays real-time progress via SSE

---

## Runtime Environment

### Environment Overview

This system uses **Conda for Python environment** + **TDengine user-mode installation** (no root required):

```
┌──────────────────────────────────────────────────────────────┐
│                     Host Machine (Linux)                     │
│                                                              │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │           Conda Environment (tdlight / feets)           │ │
│  │   Python 3.10 + numpy + lightgbm + feets + taospy       │ │
│  │   Runs: web_api, classify_pipeline.py, importers        │ │
│  └─────────────────────────────────────────────────────────┘ │
│                              │                               │
│                              ▼                               │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │         TDengine 3.4+ (User-mode install, ~/taos)       │ │
│  │   taosd service (systemctl --user)                      │ │
│  │   Listening port: 6030                                  │ │
│  └─────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────┘
```

### System Requirements

| Component | Version | Description |
|-----------|---------|-------------|
| OS | Ubuntu 20.04+ | Linux only |
| Conda | Miniconda/Anaconda | Python environment management |
| TDengine | 3.4.0+ | User-mode install, no sudo required |
| GCC | 7+ | C++ compilation |

---

## Installation

### 1. Clone the Repository

```bash
git clone https://github.com/yourname/TDlight.git
cd TDlight
```

### 2. Install TDengine (User Mode, No sudo Required)

```bash
# Download TDengine 3.4.0.0
wget https://downloads.tdengine.com/tdengine-tsdb-oss/3.4.0.0/tdengine-tsdb-oss-3.4.0.0-linux-x64.tar.gz

# Extract and install
tar -xzf tdengine-tsdb-oss-3.4.0.0-linux-x64.tar.gz
cd tdengine-tsdb-oss-3.4.0.0
./install.sh -e no

# After installation, TDengine is located at ~/taos
```

### 3. Create Conda Environment

```bash
# Create Python environment
conda create -n tdlight python=3.10 -y
conda activate tdlight

# Install Python dependencies
pip install numpy pandas scikit-learn lightgbm taospy feets
```

### 4. Edit Configuration File

```bash
cp config.json.example config.json
# Modify python path to point to your feets environment
vim config.json
```

Main configuration options:

```json
{
    "database": {
        "host": "localhost",
        "port": 6030,
        "name": "your_database_name"
    },
    "paths": {
        "python": "/path/to/conda/envs/feets/bin/python"
    }
}
```

### 5. Compile C++ Components

```bash
cd web
./build.sh

cd ../insert
./build.sh
```

### 6. Start Services

```bash
# Start TDengine service
systemctl --user start taosd

# Start Web service
conda activate tdlight
source start_env.sh  # Set environment variables
cd web
./web_api
```

### 7. Access

Open browser and visit: **http://localhost:5001**

---

## Data Import

### Import Method

**Web Interface**: Click "Data Import" tab (defaults: 16 threads, 32 VGroups)

**Command Line** (customizable parameters):

```bash
# Catalog import
./insert/catalog_importer \
    --catalogs /path/to/catalogs \
    --coords /path/to/coordinates.csv \
    --db gaiadr2_lc \
    --threads 64 \      # Thread count (default: 16)
    --vgroups 128       # VGroups count (default: 32)

# Light curve import
./insert/lightcurve_importer \
    --lightcurves_dir /path/to/lightcurves \
    --coords /path/to/coordinates.csv \
    --db gaiadr2_lc \
    --threads 64 \
    --vgroups 128
```

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--threads` | 16 | Parallel thread count |
| `--vgroups` | 32 | Database VGroups count |
| `--nside` | 64 | HEALPix NSIDE (catalog only) |
| `--drop_db` | - | Drop existing database |

### Required Files

When importing light curve data, **coordinate file must be provided**:

| File | Description |
|------|-------------|
| Light curve directory | Contains CSV file for each object |
| Coordinate file | Contains RA/DEC coordinates for all objects |

Coordinate file is used for:
- Computing HEALPix indices (accelerates spatial search)
- Setting TAGS when creating sub-tables for each object

### Data Format

**Light Curve CSV** (one file per object, filename contains source_id):

```csv
source_id,band,time,mag,mag_error,flux,flux_error
12345678,G,2015.5,15.234,0.002,1234.5,2.5
12345678,G,2015.6,15.238,0.003,1230.2,3.1
...
```

**Coordinate File CSV** (one file containing all objects):

```csv
source_id,ra,dec
12345678,180.123,-45.678
12345679,181.456,-44.321
...
```

### Database Behavior

| Scenario | Behavior |
|----------|----------|
| Database doesn't exist | Auto-create with 128 VGroups |
| Database exists | Continue using |
| Table exists | Skip creation |
| Insert new data | **Append** to table |
| Timestamp conflict | **Overwrite** old record |

>  **VGroups Limit**: Config file sets `supportVnodes=256`, each database uses 128 VGroups, allowing ~2 databases simultaneously.
> This means the system supports approximately **2 complete databases** simultaneously.
> Delete unnecessary databases via "Database Management" before importing to free resources.

---

## Search Functions

### Cone Search

Search by celestial coordinates as center with radius:

- Input: RA (degrees), DEC (degrees), Radius (arcmin)
- Uses HEALPix index for acceleration

### Region Search

Batch query by RA/DEC range:

- Input: RA range, DEC range
- Suitable for bulk retrieval of objects in a region

---

## Classification Functions

### Manual Classification Workflow

1. Select objects to classify
2. Click "Start Classification"
3. System automatically:
   - Extracts light curves from database
   - Uses feets to extract 15 astronomical features
   - LightGBM model predicts variable star type
   - Results above threshold are written back to database

### Automatic Classification

System supports automatic detection of light curves requiring classification, fully decoupled from importers:

| Detection Condition | Description |
|---------------------|-------------|
| **First Appearance** | source_id not in history file |
| **Data Growth >20%** | Data points increased by more than 20% compared to history |

**Workflow**:

```
┌─────────────────────────────────────────────────────────────────┐
│  1. Data Import (any importer)                                   │
│     catalog_importer / lightcurve_importer                      │
└───────────────────────────────┬─────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│  2. Click "Query" button, triggers check_candidates program      │
│     - Query data point count for all objects in database        │
│     - Compare with history file lc_counts_<db>.csv              │
│     - Write new/grown >20% objects to auto_classify_queue.csv   │
│     - Replace history file with new data                        │
└───────────────────────────────┬─────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│  3. Click "Start", launches auto_classify.py background task    │
│     - Batch processing (default 5000 per batch)                 │
│     - Feature extraction with feets + LightGBM prediction       │
│     - High-confidence results auto-written to database          │
└─────────────────────────────────────────────────────────────────┘
```

**Generated Files**:

| File | Description |
|------|-------------|
| `data/lc_counts_<db>.csv` | Historical count records for next comparison |
| `data/auto_classify_queue_<db>.csv` | Classification queue |

**Supported Features**:

- 🔌 **Decoupled Design**: Detection program independent from importer, can be triggered manually anytime
- ⏸️ **Interruptible**: Stop anytime, saves current progress
- 🔄 **Resume Support**: Click "Continue" to resume from interruption point
- 📊 **Real-time Progress**: SSE pushes batch progress
- 🔧 **Configurable Batch Size**: Default 5000, adjustable
- 🗄️ **Multi-database Support**: Independent queue and history files per database

### Confidence Threshold

Adjustable in "System Settings":

- Above threshold: Automatically written to database
- Below threshold: Display only, not saved

---

## Directory Structure

```
TDlight/
├── config.json          # Main configuration file
├── start_env.sh         # Container startup script
│
├── web/                 # Web service
│   ├── web_api.cpp      # C++ HTTP backend
│   ├── index.html       # Frontend page
│   ├── app.js           # Frontend interaction logic
│   └── build.sh         # Build script
│
├── class/               # Classification module
│   ├── classify_pipeline.py  # Manual classification pipeline
│   └── auto_classify.py      # Automatic classification script
│
├── insert/              # Data import and detection
│   ├── catalog_importer.cpp      # Catalog import
│   ├── lightcurve_importer.cpp   # Light curve import
│   ├── check_candidates.cpp      # Auto-classify candidate detection
│   └── build.sh
│
├── models/              # Pre-trained models (auto-downloaded)
│   ├── lgbm_111w_model.pkl
│   └── metadata.pkl
│
├── libs/                # C++ runtime libraries
├── include/             # C++ header files
├── config/              # TDengine client configuration
├── data/                # Data file directory
│   ├── lc_counts_<db>.csv           # Historical count records
│   └── auto_classify_queue_<db>.csv # Classification queue
└── runtime/             # Runtime logs
```

---

## API Reference

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/cone_search` | GET | Cone search |
| `/api/region_search` | GET | Region search |
| `/api/lightcurve/{table}` | GET | Get light curve |
| `/api/classify_objects` | POST | Start classification task |
| `/api/classify_stream` | GET (SSE) | Classification progress stream |
| `/api/import/start` | POST | Start data import |
| `/api/import/stream` | GET (SSE) | Import progress stream |
| `/api/import/stop` | POST | Stop import |
| `/api/auto_classify/check` | POST | Trigger candidate detection (compare history, generate queue) |
| `/api/auto_classify/candidates` | GET | Get number of objects to classify |
| `/api/auto_classify/start` | POST | Start auto-classification task |
| `/api/auto_classify/stop` | POST | Stop auto-classification |
| `/api/auto_classify/stream` | GET (SSE) | Auto-classification progress stream |
| `/api/auto_classify/results` | GET | Get auto-classification results |
| `/api/config` | GET/POST | Get/modify configuration |
| `/api/config/reload` | GET | Reload configuration to backend |
| `/api/databases` | GET | List databases |
| `/api/database/drop` | POST | Delete database |

---

## Troubleshooting

### Compilation Error: Header Files Not Found

Ensure you compile in the correct directory:

```bash
cd web && ./build.sh
cd insert && ./build.sh
```

### Runtime Error: .so Files Not Found

Set library path:

```bash
export LD_LIBRARY_PATH=/path/to/TDlight/libs:$LD_LIBRARY_PATH
```

### Cannot Connect to TDengine

1. Confirm taosd is running inside container
2. Check database configuration in `config.json`
3. Check if port 6041 is accessible

### VNodes Exhausted Error

Database VGroups resources exhausted. Solutions:

1. Delete unnecessary databases via Web interface
2. Or increase `supportVnodes` value in `config/taos_cfg/taos.cfg`

### No Classification Results

1. Confirm `python` path in `config.json` is correct
2. Confirm feets environment has complete dependencies
3. Check logs in `class/` directory

### Terminal Crash / Commands Not Working

If terminal crashes or commands fail after running `source start_env.sh`:

```bash
# Check if libs/ contains incompatible system libraries
ls libs/ | grep -E "libstdc|libgcc|libgomp"

# If found, delete them (should use system versions)
rm -f libs/libstdc++.so* libs/libgcc_s.so* libs/libgomp.so*
```

### Port 5001 Already in Use

```bash
# Find the process using the port
lsof -i :5001

# Or change port (edit PORT constant in web/web_api.cpp)
```

---

## Large File Acquisition

The following files are not included in the repository due to their large size. Contact the author if needed:

| File | Size | How to Obtain |
|------|------|---------------|
| `models/lgbm_111w_model.pkl` | ~250MB | Auto-downloaded during installation |
| `data/` | - | Users provide their own astronomical data |
| TDengine | ~500MB | Auto-downloaded during installation |

**Contact**: For pre-trained models or deployment issues, please contact 3023244355@tju.edu.cn.

---

## License

This project is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for details.

---

## Third-Party Libraries

This project includes pre-compiled libraries in `libs/` for convenience:

| Library | License | Source |
|---------|---------|--------|
| CFITSIO | NASA/GSFC (BSD-like) | https://heasarc.gsfc.nasa.gov/fitsio/ |
| HEALPix C++ | GPL v2 | http://healpix.sourceforge.net |
| libsharp | GPL v2 | http://healpix.sourceforge.net |

### HEALPix Citation

If you use this software, please cite HEALPix:

> K.M. Górski, E. Hivon, A.J. Banday, B.D. Wandelt, F.K. Hansen, M. Reinecke, M. Bartelmann (2005),  
> *HEALPix: A Framework for High-Resolution Discretization and Fast Analysis of Data Distributed on the Sphere*,  
> ApJ, 622, p.759-771  
> http://healpix.sourceforge.net

---

## Acknowledgments

- [TDengine](https://www.taosdata.com/) - High-performance time-series database
- [HEALPix](https://healpix.sourceforge.net/) - Hierarchical Equal Area isoLatitude Pixelization
- [feets](https://feets.readthedocs.io/) - Feature Extraction for Time Series
- [LightGBM](https://lightgbm.readthedocs.io/) - Gradient Boosting Framework
- [Three.js](https://threejs.org/) - WebGL 3D Rendering
- [Chart.js](https://www.chartjs.org/) - Chart Visualization


| Library | License | Source |
|---------|---------|--------|
| CFITSIO | NASA/GSFC (BSD-like) | https://heasarc.gsfc.nasa.gov/fitsio/ |
| HEALPix C++ | GPL v2 | http://healpix.sourceforge.net |
| libsharp | GPL v2 | http://healpix.sourceforge.net |

### HEALPix Citation

If you use this software, please cite HEALPix:

> K.M. Górski, E. Hivon, A.J. Banday, B.D. Wandelt, F.K. Hansen, M. Reinecke, M. Bartelmann (2005),  
> *HEALPix: A Framework for High-Resolution Discretization and Fast Analysis of Data Distributed on the Sphere*,  
> ApJ, 622, p.759-771  
> http://healpix.sourceforge.net

---

## Acknowledgments

- [TDengine](https://www.taosdata.com/) - High-performance time-series database
- [HEALPix](https://healpix.sourceforge.net/) - Hierarchical Equal Area isoLatitude Pixelization
- [feets](https://feets.readthedocs.io/) - Feature Extraction for Time Series
- [LightGBM](https://lightgbm.readthedocs.io/) - Gradient Boosting Framework
- [Three.js](https://threejs.org/) - WebGL 3D Rendering
- [Chart.js](https://www.chartjs.org/) - Chart Visualization
