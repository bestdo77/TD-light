# TDlight Installation Guide

## Quick Install (One Command)

```bash
git clone https://github.com/bestdo77/TD-light.git
cd TD-light
./install.sh
```

## Prerequisites

| Dependency | Required | Installation |
|------------|----------|--------------|
| Linux x86_64 | ✓ | - |
| conda | ✓ | [Miniconda](https://docs.conda.io/en/latest/miniconda.html) |
| g++ (C++17) | ✓ | `sudo apt install build-essential` |
| wget/curl | ✓ | Usually pre-installed |

## Recommended Project Structure

After installation, your project should look like:

```
TD-light/
├── install.sh              # One-click installation script
├── start_env.sh            # Environment activation script
├── config.json             # Main configuration
├── requirements.txt        # Python dependencies
├── Makefile                # C++ build system
│
├── config/
│   └── taos_cfg/
│       └── taos.cfg        # TDengine configuration
│
├── third_party/
│   └── tdengine/           # TDengine installation files
│       ├── install.sh
│       ├── bin/
│       └── driver/
│
├── models/                  # Classification models (downloaded)
│   ├── lgbm_111w_model.pkl
│   └── metadata.pkl
│
├── libs/                    # Shared libraries (HEALPix, etc.)
│   ├── libhealpix_cxx.so
│   ├── libsharp.so
│   ├── libcfitsio.so
│   └── ...
│
├── include/                 # C++ headers
│   ├── healpix_cxx/
│   ├── taos.h
│   ├── sharp.h
│   └── ...
│
├── web/                     # Web interface
│   ├── web_api.cpp          # C++ backend
│   ├── web_api              # Compiled binary
│   └── static/              # Frontend files
│
├── insert/                  # Data importers
│   ├── catalog_importer.cpp
│   └── lightcurve_importer.cpp
│
├── class/                   # Classification scripts
│   ├── auto_classify.py
│   └── classify_pipeline.py
│
├── query/                   # Query tools
│   └── optimized_query.cpp
│
└── runtime/                 # Runtime data
    └── taos_home/
        ├── log/
        ├── data/
        └── temp/
```

## Installation Options

```bash
# Full installation (default)
./install.sh

# Skip conda environment setup (if you have your own)
./install.sh --skip-conda

# Skip downloading resources (if already downloaded)
./install.sh --skip-download

# Skip C++ build (if you don't have g++)
./install.sh --skip-build

# Show help
./install.sh --help
```

## What the Installer Does

1. **Environment Detection**
   - Checks for conda, g++, wget/curl
   - Reports missing dependencies with installation instructions

2. **Downloads Resources**
   - TDengine 3.4.0.0 → `third_party/tdengine/`
   - Classification models → `models/`

3. **Installs TDengine**
   - User-mode installation (no sudo required)
   - Installed to `$HOME/taos/`

4. **Sets Up Python Environment**
   - Creates `tdlight` conda environment
   - Installs all Python dependencies

5. **Builds C++ Components**
   - Compiles web_api, importers, query tools
   - Links against TDengine and HEALPix libraries

6. **Verifies Installation**
   - Checks all components are working
   - Provides next-step instructions

## Post-Installation

### Activate Environment

```bash
conda activate tdlight
source start_env.sh
```

### Start TDengine

```bash
# Using systemd (recommended)
systemctl --user start taosd

# Or manually
taosd -c $TAOS_CONFIG_DIR &
```

### Verify TDengine

```bash
taos -c $TAOS_CONFIG_DIR
# Should open TDengine CLI
```

### Start Web Interface

```bash
cd web
./web_api
# Open http://localhost:5001
```

## Troubleshooting

### "libtaos.so not found"

```bash
export LD_LIBRARY_PATH=$HOME/taos/driver:$LD_LIBRARY_PATH
# Or use: source start_env.sh
```

### "Connection failed"

Make sure TDengine is running:
```bash
systemctl --user status taosd
# Or check: ps aux | grep taosd
```

### "Database not found"

Create the database first:
```bash
taos -c $TAOS_CONFIG_DIR
> CREATE DATABASE IF NOT EXISTS gaiadr2_lc;
```

### C++ build fails

Ensure you have g++ with C++17 support:
```bash
g++ --version
# Should be g++ 7+ or higher
```

## Manual Installation

If the automated script doesn't work, follow these steps:

1. **Download TDengine**
```bash
cd third_party
wget https://downloads.tdengine.com/tdengine-tsdb-oss/3.4.0.0/tdengine-tsdb-oss-3.4.0.0-linux-x64.tar.gz
tar -xzf tdengine-tsdb-oss-3.4.0.0-linux-x64.tar.gz
mv tdengine-tsdb-oss-3.4.0.0 tdengine
cd tdengine && ./install.sh -e no
```

2. **Download Models**
```bash
cd models
wget -O lgbm_111w_model.pkl "https://huggingface.co/bestdo77/Lightcurve_lgbm_111w_15_model/resolve/main/lgbm_111w_model.pkl?download=true"
wget -O metadata.pkl "https://huggingface.co/bestdo77/Lightcurve_lgbm_111w_15_model/resolve/main/metadata.pkl?download=true"
```

3. **Setup Python**
```bash
conda create -n tdlight python=3.9
conda activate tdlight
pip install -r requirements.txt
```

4. **Build C++**
```bash
export TDENGINE_HOME=$HOME/taos
make
```

