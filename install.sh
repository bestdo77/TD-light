#!/bin/bash
#=============================================================================
# TDlight One-Click Installation Script
# 
# This script sets up the complete TDlight environment including:
# - TDengine time-series database (user-mode, no sudo)
# - Python dependencies (via conda)
# - Classification models (from HuggingFace)
# - C++ compilation
#
# Usage: ./install.sh [options]
# Options:
#   --skip-conda     Skip conda environment setup
#   --skip-download  Skip downloading TDengine and models
#   --skip-build     Skip C++ compilation
#   --help           Show this help message
#
# Prerequisites:
#   - Linux x86_64
#   - conda (Miniconda/Anaconda)
#   - g++ with C++17 support
#   - wget or curl
#=============================================================================

set -e  # Exit on error

#-----------------------------------------------------------------------------
# Configuration
#-----------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

# URLs
TDENGINE_URL="https://downloads.tdengine.com/tdengine-tsdb-oss/3.4.0.0/tdengine-tsdb-oss-3.4.0.0-linux-x64.tar.gz"
TDENGINE_VERSION="3.4.0.0"
MODEL_BASE_URL="https://huggingface.co/bestdo77/Lightcurve_lgbm_111w_15_model/resolve/main"
MODEL_FILES=("lgbm_111w_model.pkl" "metadata.pkl")

# Directories
THIRD_PARTY_DIR="$PROJECT_ROOT/third_party"
TDENGINE_DIR="$THIRD_PARTY_DIR/tdengine"
MODELS_DIR="$PROJECT_ROOT/models"
RUNTIME_DIR="$PROJECT_ROOT/runtime"

# Conda environment name
CONDA_ENV_NAME="tdlight"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

#-----------------------------------------------------------------------------
# Helper Functions
#-----------------------------------------------------------------------------
print_header() {
    echo ""
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${CYAN}  $1${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}

print_step() {
    echo -e "${BLUE}[STEP]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[!]${NC} $1"
}

print_error() {
    echo -e "${RED}[✗]${NC} $1"
}

print_info() {
    echo -e "${CYAN}[i]${NC} $1"
}

check_command() {
    command -v "$1" &> /dev/null
}

download_file() {
    local url="$1"
    local output="$2"
    
    if [ -f "$output" ]; then
        print_info "Already exists: $output"
        return 0
    fi
    
    print_info "Downloading: $(basename "$output")"
    
    if check_command wget; then
        wget -q --show-progress -c -O "$output" "$url" || {
            # Try without show-progress for older wget
            wget -q -c -O "$output" "$url"
        }
    elif check_command curl; then
        curl -# -L -C - -o "$output" "$url"
    else
        print_error "Neither wget nor curl found"
        return 1
    fi
}

#-----------------------------------------------------------------------------
# Parse Arguments
#-----------------------------------------------------------------------------
SKIP_CONDA=false
SKIP_DOWNLOAD=false
SKIP_BUILD=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --skip-conda)
            SKIP_CONDA=true
            shift
            ;;
        --skip-download)
            SKIP_DOWNLOAD=true
            shift
            ;;
        --skip-build)
            SKIP_BUILD=true
            shift
            ;;
        --help|-h)
            echo "TDlight Installation Script"
            echo ""
            echo "Usage: ./install.sh [options]"
            echo ""
            echo "Options:"
            echo "  --skip-conda     Skip conda environment setup"
            echo "  --skip-download  Skip downloading TDengine and models"
            echo "  --skip-build     Skip C++ compilation"
            echo "  --help           Show this help message"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

#-----------------------------------------------------------------------------
# Main Installation
#-----------------------------------------------------------------------------
print_header "TDlight Installation Script"
echo ""
echo "Project root: $PROJECT_ROOT"
echo ""

#=============================================================================
# Step 1: Environment Detection
#=============================================================================
print_header "Step 1: Environment Detection"

# Check OS
if [[ "$(uname -s)" != "Linux" ]]; then
    print_error "This script only supports Linux"
    exit 1
fi

if [[ "$(uname -m)" != "x86_64" ]]; then
    print_error "This script only supports x86_64 architecture"
    exit 1
fi

print_success "OS: Linux x86_64"

# Check conda
if check_command conda; then
    CONDA_VERSION=$(conda --version 2>&1 | awk '{print $2}')
    print_success "Conda found: $CONDA_VERSION"
    CONDA_OK=true
else
    print_error "Conda not found!"
    echo ""
    echo "    Please install Miniconda or Anaconda:"
    echo "    https://docs.conda.io/en/latest/miniconda.html"
    echo ""
    echo "    Quick install:"
    echo "    wget https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh"
    echo "    bash Miniconda3-latest-Linux-x86_64.sh"
    echo ""
    CONDA_OK=false
fi

# Check g++
if check_command g++; then
    GXX_VERSION=$(g++ --version | head -n1)
    # Check C++17 support
    if g++ -std=c++17 -x c++ -c /dev/null -o /dev/null 2>/dev/null; then
        print_success "g++ with C++17 support: $GXX_VERSION"
        GXX_OK=true
    else
        print_warning "g++ found but C++17 not supported"
        GXX_OK=false
    fi
else
    print_error "g++ not found!"
    echo ""
    echo "    Please install g++ using your package manager:"
    echo "    Ubuntu/Debian: sudo apt install build-essential"
    echo "    CentOS/RHEL:   sudo yum groupinstall 'Development Tools'"
    echo "    Fedora:        sudo dnf groupinstall 'Development Tools'"
    echo ""
    GXX_OK=false
fi

# Check make
if check_command make; then
    print_success "make found"
else
    print_warning "make not found (optional, for building C++ code)"
fi

# Check wget or curl
if check_command wget; then
    print_success "wget found"
elif check_command curl; then
    print_success "curl found"
else
    print_error "Neither wget nor curl found!"
    echo "    Please install wget or curl"
    exit 1
fi

# Stop if critical dependencies missing
if [[ "$CONDA_OK" != "true" ]]; then
    print_error "Cannot continue without conda. Please install it first."
    exit 1
fi

#=============================================================================
# Step 2: Create Directories
#=============================================================================
print_header "Step 2: Creating Directories"

mkdir -p "$THIRD_PARTY_DIR"
mkdir -p "$MODELS_DIR"
mkdir -p "$RUNTIME_DIR/taos_home/log"
mkdir -p "$RUNTIME_DIR/taos_home/data"
mkdir -p "$RUNTIME_DIR/taos_home/temp"

print_success "Created: $THIRD_PARTY_DIR"
print_success "Created: $MODELS_DIR"
print_success "Created: $RUNTIME_DIR"

#=============================================================================
# Step 3: Download Resources
#=============================================================================
if [[ "$SKIP_DOWNLOAD" != "true" ]]; then
    print_header "Step 3: Downloading Resources"
    
    # Download TDengine
    print_step "Downloading TDengine $TDENGINE_VERSION..."
    TDENGINE_TAR="$THIRD_PARTY_DIR/tdengine-tsdb-oss-$TDENGINE_VERSION-linux-x64.tar.gz"
    
    if [ ! -d "$TDENGINE_DIR" ] || [ ! -f "$TDENGINE_DIR/install.sh" ]; then
        download_file "$TDENGINE_URL" "$TDENGINE_TAR"
        
        print_info "Extracting TDengine..."
        cd "$THIRD_PARTY_DIR"
        tar -xzf "$(basename "$TDENGINE_TAR")"
        mv "tdengine-tsdb-oss-$TDENGINE_VERSION" tdengine 2>/dev/null || true
        cd "$PROJECT_ROOT"
        print_success "TDengine extracted to: $TDENGINE_DIR"
    else
        print_info "TDengine already extracted"
    fi
    
    # Download models
    print_step "Downloading classification models..."
    for model_file in "${MODEL_FILES[@]}"; do
        download_file "$MODEL_BASE_URL/$model_file?download=true" "$MODELS_DIR/$model_file"
    done
    print_success "Models downloaded to: $MODELS_DIR"
else
    print_header "Step 3: Skipping Downloads (--skip-download)"
fi

#=============================================================================
# Step 4: Install TDengine (User Mode)
#=============================================================================
print_header "Step 4: Installing TDengine (User Mode)"

export TDENGINE_HOME="$HOME/taos"

if [ -d "$TDENGINE_HOME" ] && [ -f "$TDENGINE_HOME/bin/taos" ]; then
    print_info "TDengine already installed at: $TDENGINE_HOME"
    
    # Check version
    if [ -f "$TDENGINE_HOME/bin/taos" ]; then
        export LD_LIBRARY_PATH="$TDENGINE_HOME/driver:$LD_LIBRARY_PATH"
        export PATH="$TDENGINE_HOME/bin:$PATH"
        INSTALLED_VERSION=$("$TDENGINE_HOME/bin/taos" -V 2>/dev/null | head -n1 || echo "unknown")
        print_info "Installed version: $INSTALLED_VERSION"
    fi
else
    if [ -f "$TDENGINE_DIR/install.sh" ]; then
        print_step "Running TDengine user-mode installation..."
        cd "$TDENGINE_DIR"
        
        # Run install script with no-email option (non-interactive)
        ./install.sh -e no
        
        cd "$PROJECT_ROOT"
        print_success "TDengine installed to: $TDENGINE_HOME"
    else
        print_error "TDengine installation files not found"
        print_info "Run without --skip-download to download TDengine"
        exit 1
    fi
fi

# Create symlink for libtaos.so if missing
if [ -f "$TDENGINE_HOME/driver/libtaos.so.$TDENGINE_VERSION" ] && [ ! -f "$TDENGINE_HOME/driver/libtaos.so" ]; then
    print_info "Creating libtaos.so symlink..."
    ln -sf "libtaos.so.$TDENGINE_VERSION" "$TDENGINE_HOME/driver/libtaos.so"
fi

# Set up environment
export LD_LIBRARY_PATH="$TDENGINE_HOME/driver:$PROJECT_ROOT/libs:$LD_LIBRARY_PATH"
export PATH="$TDENGINE_HOME/bin:$PATH"
export TAOS_CONFIG_DIR="$PROJECT_ROOT/config/taos_cfg"

print_success "TDengine environment configured"

#=============================================================================
# Step 5: Setup Conda Environment
#=============================================================================
if [[ "$SKIP_CONDA" != "true" ]]; then
    print_header "Step 5: Setting Up Python Environment"
    
    # Initialize conda for script
    eval "$(conda shell.bash hook)"
    
    # Check if environment exists
    if conda env list | grep -q "^$CONDA_ENV_NAME "; then
        print_info "Conda environment '$CONDA_ENV_NAME' already exists"
        conda activate "$CONDA_ENV_NAME"
    else
        print_step "Creating conda environment: $CONDA_ENV_NAME"
        conda create -n "$CONDA_ENV_NAME" python=3.9 -y
        conda activate "$CONDA_ENV_NAME"
        print_success "Created conda environment: $CONDA_ENV_NAME"
    fi
    
    # Install Python dependencies
    print_step "Installing Python dependencies..."
    if [ -f "$PROJECT_ROOT/requirements.txt" ]; then
        pip install -r "$PROJECT_ROOT/requirements.txt" -q
        print_success "Python dependencies installed"
    else
        print_warning "requirements.txt not found"
    fi
    
    # Install healpy via conda (for Python-side HEALPix if needed)
    print_step "Installing healpy (optional, for Python HEALPix support)..."
    conda install -c conda-forge healpy -y 2>/dev/null || print_warning "healpy installation skipped"
    
else
    print_header "Step 5: Skipping Conda Setup (--skip-conda)"
fi

#=============================================================================
# Step 6: Build C++ Components
#=============================================================================
if [[ "$SKIP_BUILD" != "true" ]] && [[ "$GXX_OK" == "true" ]]; then
    print_header "Step 6: Building C++ Components"
    
    cd "$PROJECT_ROOT"
    
    # Set TDENGINE_HOME for Makefile
    export TDENGINE_HOME="$HOME/taos"
    
    if [ -f "Makefile" ]; then
        print_step "Compiling C++ binaries..."
        make clean 2>/dev/null || true
        
        if make; then
            print_success "C++ components built successfully"
            echo ""
            print_info "Built binaries:"
            for target in web/web_api insert/catalog_importer insert/lightcurve_importer query/optimized_query; do
                if [ -f "$target" ]; then
                    echo "    ✓ $target"
                fi
            done
        else
            print_warning "C++ build failed, but continuing..."
            print_info "You can manually build later with: make"
        fi
    else
        print_warning "Makefile not found"
    fi
else
    print_header "Step 6: Skipping C++ Build"
    if [[ "$GXX_OK" != "true" ]]; then
        print_info "g++ not available"
    fi
fi

#=============================================================================
# Step 7: Configure TDengine & Project
#=============================================================================
print_header "Step 7: Configuring TDengine & Project"

TAOS_CFG_DIR="$PROJECT_ROOT/config/taos_cfg"
TAOS_CFG_FILE="$TAOS_CFG_DIR/taos.cfg"

mkdir -p "$TAOS_CFG_DIR"

if [ ! -f "$TAOS_CFG_FILE" ]; then
    print_step "Creating TDengine configuration..."
    cat > "$TAOS_CFG_FILE" << EOF
# TDlight TDengine Configuration
fqdn               localhost
serverPort         6030
logDir             $RUNTIME_DIR/taos_home/log
dataDir            $RUNTIME_DIR/taos_home/data
tempDir            $RUNTIME_DIR/taos_home/temp
firstEp            localhost:6030
EOF
    print_success "Created: $TAOS_CFG_FILE"
else
    print_info "TDengine config already exists: $TAOS_CFG_FILE"
fi

# Copy project config if not exists
if [ ! -f "$PROJECT_ROOT/config.json" ]; then
    if [ -f "$PROJECT_ROOT/config/config.example.json" ]; then
        print_step "Creating project configuration..."
        cp "$PROJECT_ROOT/config/config.example.json" "$PROJECT_ROOT/config.json"
        print_success "Created: config.json (from template)"
    fi
else
    print_info "Project config already exists: config.json"
fi

#=============================================================================
# Step 8: Start TDengine & Initialize Database
#=============================================================================
print_header "Step 8: Starting TDengine & Initializing Database"

# Start TDengine service
print_step "Starting TDengine service..."

# Check if systemd user service exists
if [ -f "$HOME/.config/systemd/user/taosd.service" ]; then
    systemctl --user daemon-reload 2>/dev/null
    systemctl --user start taosd 2>/dev/null
    sleep 3
    
    if systemctl --user is-active taosd &>/dev/null; then
        print_success "TDengine service started"
    else
        print_warning "TDengine service failed to start via systemd"
        print_info "Trying manual start..."
        nohup "$TDENGINE_HOME/bin/taosd" -c "$TAOS_CONFIG_DIR" > "$RUNTIME_DIR/taos_home/log/taosd_manual.log" 2>&1 &
        sleep 3
    fi
else
    # No systemd service, start manually
    print_info "Starting TDengine manually..."
    nohup "$TDENGINE_HOME/bin/taosd" -c "$TAOS_CONFIG_DIR" > "$RUNTIME_DIR/taos_home/log/taosd_manual.log" 2>&1 &
    sleep 3
fi

# Check if TDengine is running
if pgrep -x taosd > /dev/null; then
    print_success "TDengine is running"
    
    # Create database
    print_step "Creating database 'gaiadr2_lc'..."
    
    # Use taos CLI to create database
    if "$TDENGINE_HOME/bin/taos" -c "$TAOS_CONFIG_DIR" -s "CREATE DATABASE IF NOT EXISTS gaiadr2_lc VGROUPS 128 BUFFER 256 KEEP 365d;" 2>/dev/null; then
        print_success "Database 'gaiadr2_lc' ready"
    else
        print_warning "Could not create database automatically"
        print_info "You can create it manually: taos -c \$TAOS_CONFIG_DIR"
        print_info "  > CREATE DATABASE IF NOT EXISTS gaiadr2_lc;"
    fi
else
    print_warning "TDengine is not running"
    print_info "Start it manually after installation:"
    print_info "  systemctl --user start taosd"
    print_info "  OR: taosd -c \$TAOS_CONFIG_DIR &"
fi

#=============================================================================
# Step 9: Verification
#=============================================================================
print_header "Step 9: Installation Verification"

VERIFY_OK=true

# Check TDengine
print_step "Checking TDengine..."
if [ -f "$TDENGINE_HOME/bin/taos" ]; then
    print_success "TDengine executable found"
else
    print_error "TDengine executable not found"
    VERIFY_OK=false
fi

# Check models
print_step "Checking models..."
for model_file in "${MODEL_FILES[@]}"; do
    if [ -f "$MODELS_DIR/$model_file" ]; then
        print_success "Model: $model_file"
    else
        print_error "Model missing: $model_file"
        VERIFY_OK=false
    fi
done

# Check Python dependencies
print_step "Checking Python dependencies..."
if [[ "$SKIP_CONDA" != "true" ]]; then
    eval "$(conda shell.bash hook)"
    conda activate "$CONDA_ENV_NAME" 2>/dev/null
    
    PYTHON_DEPS=("numpy" "pandas" "taos" "lightgbm" "sklearn" "joblib")
    for dep in "${PYTHON_DEPS[@]}"; do
        if python -c "import $dep" 2>/dev/null; then
            print_success "Python: $dep"
        else
            print_warning "Python: $dep (not found)"
        fi
    done
fi

# Check C++ binaries
print_step "Checking C++ binaries..."
BINARIES=("web/web_api" "insert/catalog_importer" "insert/lightcurve_importer")
for bin in "${BINARIES[@]}"; do
    if [ -f "$PROJECT_ROOT/$bin" ] && [ -x "$PROJECT_ROOT/$bin" ]; then
        print_success "Binary: $bin"
    else
        print_warning "Binary: $bin (not found)"
    fi
done

#=============================================================================
# Summary
#=============================================================================
print_header "Installation Complete!"

if [[ "$VERIFY_OK" == "true" ]]; then
    echo -e "${GREEN}"
    echo "  ╔═══════════════════════════════════════════════════════════╗"
    echo "  ║           TDlight Installation Successful!                ║"
    echo "  ╚═══════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
else
    echo -e "${YELLOW}"
    echo "  ╔═══════════════════════════════════════════════════════════╗"
    echo "  ║     Installation completed with some warnings             ║"
    echo "  ╚═══════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
fi

echo ""
echo "Quick Start Guide:"
echo "─────────────────────────────────────────────────────────────────"
echo ""
echo "  1. Activate environment:"
echo "     ${CYAN}conda activate tdlight${NC}"
echo "     ${CYAN}source start_env.sh${NC}"
echo ""
echo "  2. Start TDengine (if not already running):"
echo "     ${CYAN}systemctl --user start taosd${NC}"
echo ""
echo "  3. Start web interface:"
echo "     ${CYAN}cd web && ./web_api${NC}"
echo ""
echo "  4. Open browser:"
echo "     ${CYAN}http://localhost:5001${NC}"
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo ""
echo "For more information, see: ${CYAN}README.md${NC}"
echo ""

