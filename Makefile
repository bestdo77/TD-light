# TDlight Makefile
# Prerequisites: g++ (C++17), TDengine installed

CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall
INCLUDES = -I./include -I$(TDENGINE_HOME)/include
LIBS = -L./libs -L$(TDENGINE_HOME)/driver -ltaos -lhealpix_cxx -lsharp -lcfitsio -lpthread
RPATH = -Wl,-rpath,'$$ORIGIN/../libs'

# Default TDengine path (user-mode installation)
TDENGINE_HOME ?= $(HOME)/taos

# Targets
TARGETS = web/web_api insert/catalog_importer insert/lightcurve_importer insert/check_candidates query/optimized_query

.PHONY: all clean check-env

all: check-env $(TARGETS)

check-env:
	@if [ ! -d "$(TDENGINE_HOME)" ]; then \
		echo "Error: TDengine not found at $(TDENGINE_HOME)"; \
		echo "Install TDengine or set TDENGINE_HOME environment variable"; \
		exit 1; \
	fi

web/web_api: web/web_api.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(RPATH)
	@echo "Built: $@"

insert/catalog_importer: insert/catalog_importer.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(RPATH)
	@echo "Built: $@"

insert/lightcurve_importer: insert/lightcurve_importer.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(RPATH)
	@echo "Built: $@"

insert/check_candidates: insert/check_candidates.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(RPATH)
	@echo "Built: $@"

query/optimized_query: query/optimized_query.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(LIBS) $(RPATH)
	@echo "Built: $@"

clean:
	rm -f $(TARGETS)

# Help
help:
	@echo "TDlight Build System"
	@echo ""
	@echo "Usage:"
	@echo "  make          - Build all targets"
	@echo "  make clean    - Remove built binaries"
	@echo ""
	@echo "Environment:"
	@echo "  TDENGINE_HOME - TDengine installation path (default: ~/taos)"
	@echo ""
	@echo "Prerequisites:"
	@echo "  - g++ with C++17 support"
	@echo "  - TDengine 3.4.0+ installed"

