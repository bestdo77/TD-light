# TDengine HEALPix 空间查询工具

## 功能说明

本模块实现基于 HEALPix 的高效空间查询工具，支持：

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

### 必须在 Apptainer 容器内运行

本程序使用 TDengine 原生 C 接口，必须在 Apptainer 容器内运行以确保：
1. 正确的 TDengine 配置（`/etc/taos`）
2. 正确的库路径（`libtaos.so`, `libhealpix_cxx.so`）

### 关键路径

```
项目根目录: /mnt/nvme/home/yxh/code/TDengine-test
├── tdengine-fs/                    # TDengine Apptainer 容器
├── runtime/
│   ├── taos_home/cfg/taos.cfg     # TDengine 配置（端口6041）
│   ├── libs/                       # HEALPix 依赖库
│   └── deps/local/include/         # HEALPix 头文件
└── runtime-final/
    └── query/
        ├── optimized_query.cpp     # 源代码
        ├── optimized_query         # 编译后的可执行文件
        └── README.md               # 本文档

Apptainer 路径:
APPTAINER_BIN=/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer
```

---

## 编译命令

在宿主机上编译（需要 TDengine 和 HEALPix 开发库）：

```bash
cd /mnt/nvme/home/yxh/code/TDengine-test/runtime-final/query

TAOS_DIR=/mnt/nvme/home/yxh/code/TDengine-test/tdengine-fs/usr/local/taos
LIBS_DIR=/mnt/nvme/home/yxh/code/TDengine-test/runtime/libs
DEPS_DIR=/mnt/nvme/home/yxh/code/TDengine-test/runtime/deps/local/include

g++ -std=c++17 -O3 -march=native optimized_query.cpp -o optimized_query \
    -I${TAOS_DIR}/include \
    -I${DEPS_DIR} \
    -L${TAOS_DIR}/driver \
    -L${LIBS_DIR} \
    -ltaos -lhealpix_cxx -lpthread \
    -Wl,-rpath,${TAOS_DIR}/driver \
    -Wl,-rpath,${LIBS_DIR}
```

---

## 运行命令（必须在 Apptainer 容器内）

### 基础运行模板

```bash
cd /mnt/nvme/home/yxh/code/TDengine-test

/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    --bind runtime-final:/app \
    --bind runtime/libs:/app/libs \
    --bind /usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1 \
    --env LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver \
    tdengine-fs \
    /app/query/optimized_query [参数]
```

---

## 使用示例

### 1. 锥形检索（Cone Search）

搜索以 (RA=180°, DEC=30°) 为中心，半径 0.1° 范围内的所有源：

```bash
cd /mnt/nvme/home/yxh/code/TDengine-test

/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    --bind runtime-final:/app \
    --bind runtime/libs:/app/libs \
    --bind /usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1 \
    --env LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver \
    tdengine-fs \
    /app/query/optimized_query \
    --cone --ra 180 --dec 30 --radius 0.1 \
    --db catalog_test --port 6041 \
    --output cone_results.csv
```

### 2. 时间范围查询

查询指定 source_id 的所有观测记录：

```bash
cd /mnt/nvme/home/yxh/code/TDengine-test

/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    --bind runtime-final:/app \
    --bind runtime/libs:/app/libs \
    --bind /usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1 \
    --env LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver \
    tdengine-fs \
    /app/query/optimized_query \
    --time --source_id 5870536848431465216 \
    --db catalog_test --port 6041 \
    --output time_results.csv
```

带时间条件：
```bash
... --time --source_id 12345 --time_cond "ts >= '2020-01-01' AND ts <= '2020-12-31'" ...
```

### 3. 批量锥形检索

从 CSV 文件读取多个查询参数：

```bash
# 准备查询文件 queries.csv:
# ra,dec,radius
# 180.0,30.0,0.1
# 181.0,31.0,0.05
# 182.0,32.0,0.2

cd /mnt/nvme/home/yxh/code/TDengine-test

/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    --bind runtime-final:/app \
    --bind runtime/libs:/app/libs \
    --bind /usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1 \
    --env LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver \
    tdengine-fs \
    /app/query/optimized_query \
    --batch --input queries.csv \
    --db catalog_test --port 6041 \
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
| `--time_cond "<条件>"` | 时间条件（SQL WHERE 语法） |

### 批量查询参数

| 参数 | 说明 |
|------|------|
| `--input <文件>` | 输入 CSV 文件（格式: ra,dec,radius） |

### 通用参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--db` | test_db | 数据库名 |
| `--host` | localhost | 服务器地址 |
| `--port` | 6030 | TDengine 原生端口 |
| `--user` | root | 用户名 |
| `--password` | taosdata | 密码 |
| `--table` | sensor_data | 超级表名 |
| `--nside` | 64 | HEALPix NSIDE 参数 |
| `--output` | (无) | 输出 CSV 文件/目录 |
| `--limit` | (无) | 限制结果数量 |
| `--display` | 10 | 显示结果条数 |
| `--quiet` | false | 静默模式 |

---

## 输出格式

### 控制台输出

```
🎯 锥形检索
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  中心坐标: RA=180.000000°, DEC=30.000000°
  搜索半径: 0.1°
  HEALPix像素: 12 个

📊 查询统计
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
| ts | TIMESTAMP | 观测时间戳 |
| band | NCHAR(16) | 波段 |
| mag | DOUBLE | 星等 |
| mag_error | DOUBLE | 星等误差 |
| flux | DOUBLE | 流量 |
| flux_error | DOUBLE | 流量误差 |
| jd_tcb | DOUBLE | 儒略日 |

TAG 字段：
| TAG | 类型 | 说明 |
|-----|------|------|
| healpix_id | BIGINT | HEALPix 像素 ID |
| source_id | BIGINT | 源 ID |
| ra | DOUBLE | 赤经 |
| dec | DOUBLE | 赤纬 |
| cls | NCHAR(32) | 分类标签 |

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

### 1. "连接失败"

- 检查 TDengine 服务是否启动
- 确认端口是 6041（不是默认的 6030）
- 必须在 Apptainer 容器内运行

```bash
# 检查 taosd 运行状态
ps aux | grep taosd
```

### 2. "libhealpix_cxx.so not found"

确保绑定了 libs 目录并设置了 LD_LIBRARY_PATH：
```bash
--bind runtime/libs:/app/libs \
--env LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver
```

### 3. "libgomp.so.1 not found"

绑定宿主机的 libgomp：
```bash
--bind /usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1
```

### 4. "Database not found"

检查数据库名是否正确：
```bash
apptainer exec ... taos -s "SHOW DATABASES;"
```

### 5. 结果为空

- 检查坐标范围是否正确（RA: 0-360, DEC: -90 到 90）
- 尝试增大搜索半径
- 确认数据库中有数据

---

## 快速测试

验证安装和连接是否正常：

```bash
cd /mnt/nvme/home/yxh/code/TDengine-test

# 查看帮助
/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    --bind runtime-final:/app \
    --bind runtime/libs:/app/libs \
    --bind /usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1 \
    --env LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver \
    tdengine-fs \
    /app/query/optimized_query --help

# 简单锥形查询测试
/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    --bind runtime-final:/app \
    --bind runtime/libs:/app/libs \
    --bind /usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1 \
    --env LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver \
    tdengine-fs \
    /app/query/optimized_query \
    --cone --ra 180 --dec 0 --radius 1 \
    --db catalog_test --port 6041 --display 5
```

---

## 扩展开发

### 修改默认参数

编辑 `optimized_query.cpp` 中的 `main()` 函数默认值：

```cpp
string db_name = "catalog_test";  // 修改默认数据库
int port = 6041;                   // 修改默认端口
int nside = 64;                    // 修改 HEALPix 精度
```

### 添加新的查询模式

1. 在 `OptimizedQueryEngine` 类中添加新方法
2. 在 `main()` 中添加命令行参数解析
3. 重新编译

### 输出格式扩展

修改 `exportToCSV()` 和 `displayResults()` 方法。

