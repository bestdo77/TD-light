[English](README.md) | 中文

# TDlight HEALPix 空间查询工具

基于 HEALPix 的高效空间查询工具。

## 功能特性

1. **锥形检索（Cone Search）** - 给定中心坐标和半径，搜索区域内的所有源
2. **时间范围查询** - 查询指定 source_id 在某时间范围内的观测记录
3. **批量锥形检索** - 从文件读取多个查询参数，批量执行

### HEALPix 加速原理

- 使用 HEALPix 将天球划分为等面积像素
- 查询时先计算锥形区域覆盖的 HEALPix 像素
- 仅查询相关像素的数据，大幅减少扫描量
- 最后进行精确的角距离过滤

---

## 环境要求

- Linux x86_64
- TDengine 原生 C 客户端（`libtaos.so`）
- HEALPix C++ 库（已放置在 `libs/` 目录）
- `start_env.sh` 会自动设置 `LD_LIBRARY_PATH`

---

## 编译

```bash
cd query

g++ -std=c++17 -O3 -march=native optimized_query.cpp -o optimized_query \
    -I../include \
    -L../libs \
    -L$HOME/taos/driver \
    -ltaos -lhealpix_cxx -lpthread \
    -Wl,-rpath,../libs \
    -Wl,-rpath,$HOME/taos/driver
```

也可以使用顶层 Makefile（如可用）：

```bash
make query
```

---

## 使用说明

确保 `taosd` 已运行，且环境变量已设置：

```bash
conda activate tdlight
source start_env.sh
```

### 1. 锥形检索

```bash
./optimized_query \
    --cone --ra 180 --dec 30 --radius 0.1 \
    --db gaiadr2_lc \
    --output cone_results.csv
```

### 2. 时间范围查询

```bash
./optimized_query \
    --time --source_id 5870536848431465216 \
    --db gaiadr2_lc \
    --output time_results.csv
```

带时间条件：

```bash
./optimized_query \
    --time --source_id 12345 \
    --time_cond "ts >= '2020-01-01' AND ts <= '2020-12-31'" \
    --db gaiadr2_lc
```

### 3. 批量锥形检索

准备 `queries.csv`：

```csv
ra,dec,radius
180.0,30.0,0.1
181.0,31.0,0.05
```

执行：

```bash
./optimized_query \
    --batch --input queries.csv \
    --db gaiadr2_lc \
    --output batch_results/
```

---

## 完整参数说明

### 查询模式

| 参数 | 说明 |
|------|------|
| `--cone` | 锥形检索模式 |
| `--time` | 时间范围查询模式 |
| `--batch` | 批量锥形检索模式 |

### 锥形检索参数

| 参数 | 说明 |
|------|------|
| `--ra <度>` | 中心赤经 (0-360) |
| `--dec <度>` | 中心赤纬 (-90 到 90) |
| `--radius <度>` | 搜索半径 |

### 时间查询参数

| 参数 | 说明 |
|------|------|
| `--source_id <ID>` | 目标源 ID |
| `--time_cond "<条件>"` | 针对 `ts` 的 SQL WHERE 条件 |

### 批量查询参数

| 参数 | 说明 |
|------|------|
| `--input <文件>` | 输入 CSV 文件（格式: ra,dec,radius） |

### 通用参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--db` | `test_db` | 数据库名 |
| `--host` | `localhost` | 服务器地址 |
| `--port` | `6030` | TDengine 原生端口 |
| `--user` | `root` | 用户名 |
| `--password` | `taosdata` | 密码 |
| `--table` | `lightcurves` | 超级表名 |
| `--nside` | `64` | HEALPix NSIDE |
| `--output` | (无) | 输出 CSV 文件/目录 |
| `--limit` | (无) | 限制结果数量 |
| `--display` | `10` | 显示结果条数 |

---

## 输出格式

### 控制台输出

```
锥形检索
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  中心坐标: RA=180.000000°, DEC=30.000000°
  搜索半径: 0.1°
  HEALPix像素: 12 个

查询统计
  HEALPix筛选: 150 条记录
  角距离过滤: 42 条记录（精确匹配）
  查询耗时: 5.23 ms
  数据获取: 2.15 ms
  总耗时: 8.56 ms
```

### CSV 输出格式

```csv
ts,source_id,ra,dec,band,cls,mag,mag_error,flux,flux_error,jd_tcb
1577836800000,5870536848431465216,180.123456,30.654321,G,DSCT,15.234,0.012,1234.56,12.34,2458849.5
```

---

## 数据库表结构

本工具查询的超级表结构：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ts` | TIMESTAMP | 观测时间戳 |
| `band` | NCHAR(16) | 波段 |
| `mag` | DOUBLE | 星等 |
| `mag_error` | DOUBLE | 星等误差 |
| `flux` | DOUBLE | 流量 |
| `flux_error` | DOUBLE | 流量误差 |
| `jd_tcb` | DOUBLE | 儒略日 |

TAG 字段：

| TAG | 类型 | 说明 |
|-----|------|------|
| `healpix_id` | BIGINT | HEALPix 像素 ID |
| `source_id` | BIGINT | 源 ID |
| `ra` | DOUBLE | 赤经 |
| `dec` | DOUBLE | 赤纬 |
| `cls` | NCHAR(32) | 分类标签 |

---

## 性能优化

### HEALPix NSIDE 选择

| NSIDE | 像素数 | 像素面积 | 适用场景 |
|-------|--------|----------|----------|
| 32 | 12,288 | 3.36 deg² | 大范围粗略搜索 |
| 64 | 49,152 | 0.84 deg² | **推荐默认值** |
| 128 | 196,608 | 0.21 deg² | 小范围精确搜索 |
| 256 | 786,432 | 0.05 deg² | 超高精度搜索 |

### 查询性能参考

| 操作 | 典型耗时 |
|------|---------|
| 小范围锥形查询 (r=0.1°) | 5-20 ms |
| 中范围锥形查询 (r=1°) | 50-200 ms |
| 单 source_id 时间查询 | 10-50 ms |
| 批量查询 (100个) | 1-5 s |

---

## 常见问题

### "连接失败"

- 检查 `taosd` 是否运行：`systemctl --user status taosd`
- 确认端口 `6030` 可访问
- 确保 `LD_LIBRARY_PATH` 包含 `../libs` 和 `$HOME/taos/driver`

```bash
source ../start_env.sh
```

### "libhealpix_cxx.so not found"

确保 `LD_LIBRARY_PATH` 指向 `libs/`：

```bash
export LD_LIBRARY_PATH=../libs:$HOME/taos/driver:$LD_LIBRARY_PATH
```

### "Database not found"

检查数据库名：

```bash
taos -s "SHOW DATABASES;"
```

### 结果为空

- 检查坐标范围是否正确（RA: 0-360, DEC: -90 到 90）
- 尝试增大搜索半径
- 确认数据库中有数据

---

## 扩展开发

### 修改默认参数

编辑 `optimized_query.cpp` 中的 `main()` 函数：

```cpp
string db_name = "gaiadr2_lc";
int port = 6030;
int nside = 64;
```

然后重新编译。
