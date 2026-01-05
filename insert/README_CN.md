[English](README.md) | 中文

# TDengine 数据导入工具

## 环境说明

本项目运行在 **Apptainer 容器** 环境中，TDengine 服务已预先启动。

### 关键路径
```
项目根目录: /mnt/nvme/home/yxh/code/TDengine-test
├── tdengine-fs/                    # TDengine 容器文件系统
├── runtime/
│   ├── taos_home/cfg/taos.cfg     # TDengine 配置（端口6041）
│   ├── libs/                       # HEALPix 等依赖库
│   ├── data/                       # 数据目录
│   │   ├── gaiadr2/               # 光变曲线数据
│   │   └── catalogs_gaiadr2/      # 星表数据
│   └── src/IO/import/             # 导入程序
```

### Apptainer 路径
```
APPTAINER_BIN=/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer
```

---

## 编译命令

```bash
cd /mnt/nvme/home/yxh/code/TDengine-test/runtime/src/IO/import

TAOS_DIR=/mnt/nvme/home/yxh/code/TDengine-test/tdengine-fs/usr/local/taos
LIBS_DIR=/mnt/nvme/home/yxh/code/TDengine-test/runtime/libs

# 编译光变曲线导入器
g++ -std=c++17 -O3 -march=native lightcurve_importer.cpp -o lightcurve_importer \
    -I${TAOS_DIR}/include -I${LIBS_DIR}/../deps/local/include \
    -L${TAOS_DIR}/driver -L${LIBS_DIR} \
    -ltaos -lhealpix_cxx -lpthread \
    -Wl,-rpath,${TAOS_DIR}/driver -Wl,-rpath,${LIBS_DIR}

# 编译星表导入器
g++ -std=c++17 -O3 -march=native catalog_importer.cpp -o catalog_importer \
    -I${TAOS_DIR}/include -I${LIBS_DIR}/../deps/local/include \
    -L${TAOS_DIR}/driver -L${LIBS_DIR} \
    -ltaos -lhealpix_cxx -lpthread \
    -Wl,-rpath,${TAOS_DIR}/driver -Wl,-rpath,${LIBS_DIR}
```

---

## 运行命令（必须在 Apptainer 容器内）

### 为什么必须用 Apptainer？
1. TDengine 服务运行在容器内
2. 需要正确的 `/etc/taos` 配置
3. 需要容器内的库路径

### 光变曲线导入
```bash
cd /mnt/nvme/home/yxh/code/TDengine-test

/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    --bind runtime:/app \
    --bind /usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1 \
    --bind runtime/libs:/app/libs \
    --env LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver \
    tdengine-fs \
    /app/src/IO/import/lightcurve_importer \
    /app/data/gaiadr2/individual_lightcurves \
    lightcurve_db
```

### 星表导入
```bash
cd /mnt/nvme/home/yxh/code/TDengine-test

/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    --bind runtime:/app \
    --bind /usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1 \
    --bind runtime/libs:/app/libs \
    --env LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver \
    tdengine-fs \
    /app/src/IO/import/catalog_importer \
    --catalogs /app/data/catalogs_gaiadr2/catalogs \
    --coords /app/data/gaiadr2/source_coordinates.csv \
    --db catalog_db
```

---

## TDengine 操作

### 启动 TDengine 服务
```bash
cd /mnt/nvme/home/yxh/code/TDengine-test

nohup /mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    tdengine-fs \
    /usr/bin/taosd > runtime/taos_home/log/stdout.log 2>&1 &
```

### 执行 SQL 命令
```bash
/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    tdengine-fs \
    taos -s "SHOW DATABASES;"
```

### 删除数据库
```bash
/mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    tdengine-fs \
    taos -s "DROP DATABASE IF EXISTS <数据库名>;"
```

---

## 最优配置参数

| 参数 | 值 | 说明 |
|------|-----|------|
| NUM_THREADS | 64 | 并行线程数（过多会争用） |
| NUM_VGROUPS | 128 | 虚拟分组数（减少刷盘阻塞） |
| BATCH_SIZE | 10000 | 每批次插入行数 |
| BUFFER | 256 | 每vgroup内存缓冲(MB) |

### TDengine 服务端配置 (taos.cfg)
```
serverPort             6041
maxConnections         500
numOfCommitThreads     8
```

---

## 常见问题

### 1. "连接失败"
- 检查 TDengine 服务是否启动
- 确认端口是 6041（不是默认的 6030）

### 2. "libsharp.so.0 not found"
- 需要绑定 libs 目录：`--bind runtime/libs:/app/libs`
- 需要设置 LD_LIBRARY_PATH：`--env LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver`

### 3. "libgomp.so.1 not found"
- 需要绑定宿主机的 libgomp：`--bind /usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1`

### 4. "Permission denied" 创建日志目录
- 必须在 Apptainer 容器内运行
- 需要绑定 taos 配置：`--bind runtime/taos_home/cfg:/etc/taos`

---

## 性能参考

| 数据集 | 表数 | 行数 | 纯插入速率 |
|--------|------|------|------------|
| 光变曲线 | 55万 | 4700万 | 2.65M 行/秒 |
| 星表 | 6593 | 170万 | 84万 行/秒 |

