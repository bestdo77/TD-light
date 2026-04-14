# TDlight 数据导入工具

快速将光变曲线和星表数据导入 TDengine 数据库。

## 🚀 快速开始

### 1. 启动环境

```bash
cd /mnt/nvme/home/yxh/code/TDlight
./start_env.sh
```

### 2. 编译

```bash
cd /mnt/nvme/home/yxh/code/TDlight/insert

# 编译光变曲线导入器
g++ -std=c++17 -O3 -march=native lightcurve_importer.cpp -o lightcurve_importer \
    -I../include -L../libs -L$HOME/taos/driver -ltaos -lhealpix_cxx -lpthread \
    -Wl,-rpath,../libs -Wl,-rpath,$HOME/taos/driver

# 编译星表导入器
g++ -std=c++17 -O3 -march=native catalog_importer.cpp -o catalog_importer \
    -I../include -L../libs -L$HOME/taos/driver -ltaos -lhealpix_cxx -lpthread \
    -Wl,-rpath,../libs -Wl,-rpath,$HOME/taos/driver
```

### 3. 导入数据

#### 导入光变曲线

```bash
./lightcurve_importer \
    --lightcurves_dir /path/to/lightcurves \
    --coords /path/to/source_coordinates.csv \
    --db my_database \
    --threads 8
```

#### 导入星表（带交叉证认）

```bash
./catalog_importer \
    --catalogs /path/to/catalogs \
    --coords /path/to/source_coordinates.csv \
    --db my_database \
    --crossmatch 1 \
    --radius 1.0 \
    --threads 8
```

## 📋 参数说明

### 光变曲线导入器参数

| 参数 | 必需 | 说明 | 默认值 |
|------|------|------|--------|
| `--lightcurves_dir` | ✅ | 光变曲线文件目录 | - |
| `--coords` | ✅ | 坐标文件路径 | - |
| `--db` | ❌ | 数据库名 | `gaiadr2_lc` |
| `--threads` | ❌ | 线程数 | `16` |
| `--vgroups` | ❌ | VGroups 数量 | `32` |
| `--drop_db` | ❌ | 删除已有数据库 | `false` |
| `--crossmatch` | ❌ | 启用交叉证认 (0/1) | `1` |
| `--radius` | ❌ | 证认半径 (角秒) | `1.0` |

### 星表导入器参数

| 参数 | 必需 | 说明 | 默认值 |
|------|------|------|--------|
| `--catalogs` | ✅ | 星表文件目录 | - |
| `--coords` | ✅ | 坐标文件路径 | - |
| `--db` | ❌ | 数据库名 | `gaiadr2_lc` |
| `--threads` | ❌ | 线程数 | `16` |
| `--vgroups` | ❌ | VGroups 数量 | `32` |
| `--drop_db` | ❌ | 删除已有数据库 | `false` |
| `--crossmatch` | ❌ | 启用交叉证认 (0/1) | `1` |
| `--radius` | ❌ | 证认半径 (角秒) | `1.0` |

## 📁 数据格式

### 坐标文件格式 (`source_coordinates.csv`)

```csv
source_id,ra,dec
1007596926756899072,96.68226266572204,64.11928378951181
1016642299680425600,136.14613167127357,52.120411687974375
```

### 光变曲线文件格式 (`lightcurve_<source_id>.csv`)

```csv
time,band,flux,flux_err,mag,mag_err
1707.3886320197084,G,6613.973617061971,18.91623645721968,16.13720957963514,0.0031051466350914205
```

### 星表文件格式 (`catalog_<id>.csv`)

```csv
source_id,ra,dec,class,band,time,flux,flux_err,mag,mag_err
1007596926756899072,96.68226266572204,64.11928378951181,Unknown,G,1707.3886,6613.97,18.92,16.14,0.0031
```

## ⚙️ 配置建议

| 参数 | 推荐值 | 说明 |
|------|--------|------|
| 线程数 | 8-16 | 根据 CPU 核心数调整 |
| VGroups | 32 | 减少写入阻塞 |
| Batch Size | 10000 | 每批插入行数 |
| 证认半径 | 1.0 角秒 | 空间匹配半径 |

## 🎯 性能参考

| 数据集 | 表数量 | 数据行数 | 导入速度 |
|--------|--------|----------|----------|
| 光变曲线 | 3,800 | 336,690 | ~280K rows/s |
| 星表 | 3,800 | 336,690 | ~280K rows/s |

## 🔧 常见问题

### 1. 连接失败

```bash
# 检查 TDengine 是否运行
ps aux | grep taosd

# 重启 TDengine
./stop_env.sh
./start_env.sh
```

### 2. 库文件未找到

```bash
# 设置库路径
export LD_LIBRARY_PATH=../libs:$HOME/taos/driver:$LD_LIBRARY_PATH
```

### 3. 表名过长

自动处理，无需担心。表名格式：`t_<healpix>_<9 位哈希>`

## 📊 交叉证认说明

**什么是交叉证认？**
- 根据天体坐标（RA/DEC）匹配数据库中的现有天体
- 匹配成功：使用数据库中的统一 ID
- 匹配失败：生成新的哈希 ID

**何时使用？**
- ✅ 导入新数据时避免重复天体
- ✅ 合并多个观测数据源
- ✅ 保持天体 ID 一致性

**如何关闭？**
```bash
./catalog_importer --crossmatch 0 ...
```

## 🛠️ 数据库操作

### 查看数据库

```bash
taos -s "SHOW DATABASES;"
```

### 删除数据库

```bash
taos -s "DROP DATABASE IF EXISTS my_database;"
```

### 查询数据

```bash
taos -s "USE my_database; SELECT * FROM sensor_data LIMIT 10;"
```

---

**更多帮助**: 运行 `./lightcurve_importer --help` 或 `./catalog_importer --help`
