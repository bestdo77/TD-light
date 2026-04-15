[English](README.md) | 中文

# TDlight 数据导入工具

高性能多线程光变曲线与星表数据导入 TDengine 工具。

## 快速开始

### 1. 编译

```bash
cd insert
./build.sh
```

### 2. 导入光变曲线

```bash
./lightcurve_importer \
    --lightcurves_dir /path/to/lightcurves \
    --coords /path/to/source_coordinates.csv \
    --db my_database \
    --threads 16
```

### 3. 导入星表（带交叉证认）

```bash
./catalog_importer \
    --catalogs /path/to/catalogs \
    --coords /path/to/source_coordinates.csv \
    --db my_database \
    --crossmatch 1 \
    --radius 1.0 \
    --threads 16
```

## 参数说明

### `lightcurve_importer`

| 参数 | 必需 | 说明 | 默认值 |
|------|------|------|--------|
| `--lightcurves_dir` | 是 | 光变曲线文件目录 | - |
| `--coords` | 是 | 坐标文件路径 | - |
| `--db` | 否 | 数据库名 | `gaiadr2_lc` |
| `--threads` | 否 | 线程数 | `16` |
| `--vgroups` | 否 | VGroups 数量 | `32` |
| `--drop_db` | 否 | 删除已有数据库 | `false` |
| `--crossmatch` | 否 | 启用交叉证认 (0/1) | `1` |
| `--radius` | 否 | 证认半径 (角秒) | `1.0` |

### `catalog_importer`

| 参数 | 必需 | 说明 | 默认值 |
|------|------|------|--------|
| `--catalogs` | 是 | 星表文件目录 | - |
| `--coords` | 是 | 坐标文件路径 | - |
| `--db` | 否 | 数据库名 | `gaiadr2_lc` |
| `--threads` | 否 | 线程数 | `16` |
| `--vgroups` | 否 | VGroups 数量 | `32` |
| `--drop_db` | 否 | 删除已有数据库 | `false` |
| `--crossmatch` | 否 | 启用交叉证认 (0/1) | `1` |
| `--radius` | 否 | 证认半径 (角秒) | `1.0` |
| `--nside` | 否 | HEALPix NSIDE | `64` |

## 数据格式

### 坐标文件 (`source_coordinates.csv`)

```csv
source_id,ra,dec
1007596926756899072,96.68226266572204,64.11928378951181
```

### 光变曲线文件 (`lightcurve_<source_id>.csv`)

```csv
time,band,flux,flux_err,mag,mag_err
1707.3886320197084,G,6613.973617061971,18.91623645721968,16.13720957963514,0.0031051466350914205
```

### 星表文件 (`catalog_<id>.csv`)

```csv
source_id,ra,dec,class,band,time,flux,flux_err,mag,mag_err
1007596926756899072,96.68226266572204,64.11928378951181,Unknown,G,1707.3886,6613.97,18.92,16.14,0.0031
```

## 配置建议

| 参数 | 推荐值 | 说明 |
|------|--------|------|
| 线程数 | 8-16 | 根据 CPU 核心数调整 |
| VGroups | 32 | 减少写入阻塞 |
| 证认半径 | 1.0 角秒 | 空间匹配半径 |

## 性能参考

| 数据集 | 表数量 | 数据行数 | 导入速度 |
|--------|--------|----------|----------|
| 光变曲线 | 3,800 | 336,690 | ~280K rows/s |
| 星表 | 3,800 | 336,690 | ~280K rows/s |

## 常见问题

### 1. 连接失败

```bash
# 检查 TDengine 是否运行
ps aux | grep taosd

# 重启 TDengine
systemctl --user restart taosd
```

### 2. 库文件未找到

```bash
# 设置库路径
export LD_LIBRARY_PATH=../libs:$HOME/taos/driver:$LD_LIBRARY_PATH
# 或使用
source ../start_env.sh
```

### 3. 交叉证认

**交叉证认**根据天体坐标（RA/DEC）匹配数据库中的现有天体：
- 匹配成功 → 使用数据库中的统一 ID
- 匹配失败 → 生成新的哈希 ID

关闭证认：
```bash
./catalog_importer --crossmatch 0 ...
```

## 数据库操作

```bash
# 查看数据库
taos -s "SHOW DATABASES;"

# 删除数据库
taos -s "DROP DATABASE IF EXISTS my_database;"

# 查询数据
taos -s "USE my_database; SELECT * FROM lightcurves LIMIT 10;"
```

---

更多帮助请运行 `./lightcurve_importer --help` 或 `./catalog_importer --help`。
