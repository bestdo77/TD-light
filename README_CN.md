[English](README.md) | 中文

# TDlight

基于 TDengine 时序数据库的天文光变曲线管理与分类系统。

支持大规模天文时序数据的高效存储、快速检索和智能分类。

---

## 技术栈

| 层级 | 技术 | 说明 |
|------|------|------|
| **数据库** | TDengine 3.x | 高性能时序数据库 |
| **后端** | C++17 | HTTP 服务器、HEALPix 空间索引 |
| **分类** | Python + LightGBM | feets 特征提取 + 机器学习 |
| **前端** | HTML/JS | Three.js 3D、Chart.js 图表 |
| **容器** | Apptainer | TDengine 运行环境 |

---

## 功能特性

| 功能 | 说明 |
|------|------|
|  **锥形检索** | 以天球坐标为中心，按半径搜索天体 |
|  **矩形检索** | 按 RA/DEC 范围批量查询 |
|  **光变曲线可视化** | 交互式图表展示时序测光数据 |
|  **智能分类** | 基于 LightGBM 的变星自动分类 |
| 🤖 **自动分类** | 导入时自动检测待分类天体，分批后台处理 |
|  **数据导入** | Web 界面一键导入 CSV 格式数据 |
| 🌍 **3D 天球** | WebGL 渲染的三维天球可视化 |

---

## 系统架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        浏览器 (前端)                              │
│   index.html + app.js (Three.js 3D / Chart.js / SSE 实时通信)    │
└───────────────────────────────┬─────────────────────────────────┘
                                │ HTTP/SSE
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                      web_api (C++ 后端)                          │
│                                                                 │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │  检索 API   │  │  分类 API   │  │      数据导入 API        │  │
│  │ cone_search │  │  classify   │  │  catalog_importer       │  │
│  │region_search│  │  (调用 Py)  │  │  lightcurve_importer    │  │
│  └──────┬──────┘  └──────┬──────┘  └───────────┬─────────────┘  │
│         │                │                     │                │
│         ▼                ▼                     ▼                │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    TDengine C 客户端                         ││
│  │                      (libtaos.so)                            ││
│  └──────────────────────────────┬──────────────────────────────┘│
└─────────────────────────────────┼───────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────┐
│                   TDengine 时序数据库 (Apptainer 容器)            │
│                                                                 │
│   超级表: lightcurves                                            │
│   ├── 标签: healpix_id, source_id, ra, dec, cls                 │
│   └── 数据: ts, band, mag, mag_error, flux, flux_error, jd_tcb  │
│                                                                 │
│   VGroups: 128 (支持约 2 个完整数据库)                            │
└─────────────────────────────────────────────────────────────────┘
```

### 组件连接说明

1. **前端 (index.html + app.js)**
   - 使用 Fetch API 调用后端 REST 接口
   - 使用 Server-Sent Events (SSE) 接收分类/导入进度
   - Three.js 渲染 3D 天球，Chart.js 绘制光变曲线

2. **后端 (web_api.cpp)**
   - 纯 C++ 实现的 HTTP 服务器
   - 使用 HEALPix 进行天球像素化加速检索
   - 通过 `system()` 调用 Python 分类脚本
   - 通过 `system()` 调用 C++ 导入器

3. **分类模块 (classify_pipeline.py)**
   - 由 web_api 通过子进程调用
   - 使用 feets 库提取光变曲线特征
   - 使用预训练 LightGBM 模型进行分类
   - 高置信度结果自动写回 TDengine

4. **数据导入器 (catalog_importer / lightcurve_importer)**
   - 独立 C++ 程序，64 线程并行导入
   - 通过 web_api 启动，进度通过文件通信
   - 前端通过 SSE 实时显示进度

---

## 运行环境

### 环境概述

本系统使用 **Conda 管理环境** + **Apptainer 容器运行 TDengine**：

- Apptainer 通过 Conda 安装
- TDengine 服务运行在 Apptainer 容器内
- Web 服务和分类脚本运行在容器外（Conda 环境）
- 容器通过挂载访问 Conda 环境和数据文件

```
┌──────────────────────────────────────────────────────────┐
│                     宿主机 (Linux)                        │
│                                                          │
│  ┌─────────────────────────────────────────────────────┐ │
│  │              Conda 环境 (tdlight / feets)           │ │
│  │                                                     │ │
│  │   Python 3.10 + numpy + lightgbm + feets + taospy   │ │
│  │   apptainer (通过 conda-forge 安装)                  │ │
│  │                                                     │ │
│  │   运行: web_api, classify_pipeline.py, importers    │ │
│  └─────────────────────────────────────────────────────┘ │
│                          │                               │
│                          │ 挂载                          │
│                          ▼                               │
│  ┌─────────────────────────────────────────────────────┐ │
│  │           Apptainer 容器 (tdengine-fs)              │ │
│  │                                                     │ │
│  │   TDengine 3.3.x 服务 (taosd)                       │ │
│  │   监听端口: 6030-6049                               │ │
│  │                                                     │ │
│  │   挂载:                                              │
│  │     - /app → 项目目录                                │
│  │     - Conda 环境路径                                 │
│  │     - 数据目录                                       │
│  └─────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
```

### 系统要求

| 组件 | 版本 | 说明 |
|------|------|------|
| 操作系统 | Ubuntu 20.04+ | 仅支持 Linux |
| Conda | Miniconda/Anaconda | 环境管理 |
| Apptainer | 1.1+ | 通过 conda 安装 |
| TDengine | 3.3.x | 容器内运行 |
| GCC | 7+ | C++ 编译 |

---

## 安装步骤

### 1. 克隆项目

```bash
git clone https://github.com/yourname/TDlight.git
cd TDlight
```

### 2. 创建 Conda 环境

```bash
# 创建主环境
conda create -n tdlight python=3.8 -y
conda activate tdlight

# 安装 Apptainer
conda install -c conda-forge apptainer -y

# 安装 Python 依赖
pip install numpy pandas scikit-learn lightgbm taospy feets taospy numpy pandas lightgbm

```

### 3. 配置 TDengine 容器

`tdengine-fs/` 容器目录需自行构建：

1. 访问 [TDengine 官网](https://docs.taosdata.com/releases/tdengine/)
2. 下载 **TDengine 3.3.2.0** 或更高版本的 Docker 镜像
3. 使用 Apptainer 构建 sandbox：

```bash
apptainer build --sandbox tdengine-fs docker://tdengine/tdengine:3.3.2.0
```

或直接拉取：

```bash
apptainer pull tdengine.sif docker://tdengine/tdengine:3.3.2.0
apptainer build --sandbox tdengine-fs tdengine.sif
```

### 4. 获取 TDengine 客户端库

`libs/` 目录下的 TDengine 库文件需从官网获取：

1. 访问 [TDengine 官网](https://docs.taosdata.com/releases/tdengine/)
2. 下载 **TDengine 3.3.7.5** 版本（Linux amd64）
3. 解压后将 `driver/libtaos.so*` 复制到 `libs/` 目录

```bash
# 示例
wget https://www.taosdata.com/assets-download/3.0/TDengine-client-3.3.7.5-Linux-x64.tar.gz
tar -xzf TDengine-client-3.3.7.5-Linux-x64.tar.gz
cp TDengine-client-3.3.7.5/driver/libtaos.so* TDlight/libs/
```

### 5. 编辑配置文件

```bash
cp config.json.example config.json
# 修改 python 路径指向你的 feets 环境
vim config.json
```

主要配置项：

```json
{
    "database": {
        "host": "localhost",
        "port": 6041,
        "name": "your_database_name"
    },
    "paths": {
        "python": "/path/to/conda/envs/feets/bin/python"
    }
}
```

### 6. 编译 C++ 组件

```bash
cd web
./build.sh

cd ../insert
./build.sh
```

### 7. 启动服务

```bash
# 终端 1: 启动 TDengine 容器
conda activate tdlight
./start_env.sh
# 容器内执行: taosd &

# 终端 2: 启动 Web 服务
conda activate tdlight
cd web
export LD_LIBRARY_PATH=../libs:$LD_LIBRARY_PATH
./web_api
```

### 8. 访问

打开浏览器访问：**http://localhost:5001**

---

## 数据导入

### 导入方式

通过 Web 界面 → "数据导入" 标签页操作。

### 必需文件

导入光变曲线数据时，**必须同时提供坐标文件**：

| 文件 | 说明 |
|------|------|
| 光变曲线目录 | 包含每个天体的 CSV 文件 |
| 坐标文件 | 包含所有天体的 RA/DEC 坐标 |

坐标文件用于：
- 计算 HEALPix 索引（加速空间检索）
- 为每个天体创建子表时设置 TAGS

### 数据格式

**光变曲线 CSV**（每个天体一个文件，文件名包含 source_id）：

```csv
source_id,band,time,mag,mag_error,flux,flux_error
12345678,G,2015.5,15.234,0.002,1234.5,2.5
12345678,G,2015.6,15.238,0.003,1230.2,3.1
...
```

**坐标文件 CSV**（一个文件，包含所有天体）：

```csv
source_id,ra,dec
12345678,180.123,-45.678
12345679,181.456,-44.321
...
```

### 数据库行为

| 场景 | 行为 |
|------|------|
| 数据库不存在 | 自动创建，128 VGroups |
| 数据库已存在 | 继续使用 |
| 表已存在 | 跳过创建 |
| 插入新数据 | **追加**到表中 |
| 时间戳冲突 | **覆盖**旧记录 |

>  **VGroups 限制**：默认配置 `supportVnodes=256`，每个数据库使用 128 VGroups。
> 这意味着系统同时最多支持约 **2 个完整数据库**。
> 导入前请通过"数据库管理"删除不需要的数据库释放资源。

---

## 检索功能

### 锥形检索 (Cone Search)

以指定天球坐标为圆心，按半径搜索：

- 输入：RA (度), DEC (度), 半径 (角分)
- 使用 HEALPix 索引加速

### 矩形检索 (Region Search)

按 RA/DEC 范围批量查询：

- 输入：RA 范围, DEC 范围
- 适合批量获取区域内天体

---

## 分类功能

### 手动分类流程

1. 选择要分类的天体
2. 点击"开始分类"
3. 系统自动：
   - 从数据库提取光变曲线
   - 使用 feets 提取 15个 天文特征
   - LightGBM 模型预测变星类型
   - 高于阈值的结果写回数据库

### 自动分类功能

系统支持自动检测需要分类的光变曲线，与导入器完全解耦：

| 检测条件 | 说明 |
|----------|------|
| **首次出现** | 历史记录文件中没有该 source_id |
| **数据增长 >20%** | 数据点数比历史记录增长超过 20% |

**工作流程**：

```
┌─────────────────────────────────────────────────────────────────┐
│  1. 数据导入（任意导入器）                                        │
│     catalog_importer / lightcurve_importer                      │
└───────────────────────────────┬─────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│  2. 点击"查询"按钮，触发 check_candidates 程序                    │
│     - 查询数据库中所有天体的数据点数                               │
│     - 与历史文件 lc_counts_<db>.csv 对比                         │
│     - 新增或增长>20% 的天体写入 auto_classify_queue_<db>.csv      │
│     - 新数据替换历史文件                                          │
└───────────────────────────────┬─────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│  3. 点击"开始"，启动 auto_classify.py 后台任务                    │
│     - 分批处理（默认 5000 条/批）                                  │
│     - 使用 feets 提取特征 + LightGBM 预测                         │
│     - 高置信度结果自动写回数据库                                   │
└─────────────────────────────────────────────────────────────────┘
```

**生成的文件**：

| 文件 | 说明 |
|------|------|
| `data/lc_counts_<db>.csv` | 历史点数记录，用于下次比对 |
| `data/auto_classify_queue_<db>.csv` | 待分类队列 |

**支持特性**：

- 🔌 **解耦设计**：检测程序独立于导入器，可随时手动触发
- ⏸️ **可中断**：随时停止，保存当前进度
- 🔄 **断点续传**：点击"继续"从中断处恢复
- 📊 **实时进度**：SSE 推送批次进度
- 🔧 **可配置批次大小**：默认 5000，可调整
- 🗄️ **多数据库支持**：每个数据库独立的队列和历史文件

### 置信度阈值

在"系统设置"中可调整：

- 高于阈值：自动写回数据库
- 低于阈值：仅显示，不保存

---

## 目录结构

```
TDlight/
├── config.json          # 主配置文件
├── start_env.sh         # 容器启动脚本
│
├── web/                 # Web 服务
│   ├── web_api.cpp      # C++ HTTP 后端
│   ├── index.html       # 前端页面
│   ├── app.js           # 前端交互逻辑
│   └── build.sh         # 编译脚本
│
├── class/               # 分类模块
│   ├── classify_pipeline.py  # 手动分类流水线
│   └── auto_classify.py      # 自动分类脚本
│
├── insert/              # 数据导入与检测
│   ├── catalog_importer.cpp      # 星表导入
│   ├── lightcurve_importer.cpp   # 光变曲线导入
│   ├── check_candidates.cpp      # 自动分类候选检测
│   └── build.sh
│
├── classifier/          # 预训练模型
│   ├── lgbm_111w_model.pkl
│   └── metadata.pkl
│
├── libs/                # C++ 运行时库
├── include/             # C++ 头文件
├── config/              # TDengine 客户端配置
├── data/                # 数据文件目录
│   ├── lc_counts_<db>.csv           # 历史点数记录
│   └── auto_classify_queue_<db>.csv # 待分类队列
└── tdengine-fs/         # Apptainer 容器
```

---

## API 参考

| 端点 | 方法 | 说明 |
|------|------|------|
| `/api/cone_search` | GET | 锥形检索 |
| `/api/region_search` | GET | 矩形检索 |
| `/api/lightcurve/{table}` | GET | 获取光变曲线 |
| `/api/classify_objects` | POST | 启动分类任务 |
| `/api/classify_stream` | GET (SSE) | 分类进度流 |
| `/api/import/start` | POST | 启动数据导入 |
| `/api/import/stream` | GET (SSE) | 导入进度流 |
| `/api/import/stop` | POST | 停止导入 |
| `/api/auto_classify/check` | POST | 触发候选检测（对比历史，生成队列）|
| `/api/auto_classify/candidates` | GET | 获取待分类天体数量 |
| `/api/auto_classify/start` | POST | 启动自动分类任务 |
| `/api/auto_classify/stop` | POST | 停止自动分类 |
| `/api/auto_classify/stream` | GET (SSE) | 自动分类进度流 |
| `/api/auto_classify/results` | GET | 获取自动分类结果 |
| `/api/config` | GET/POST | 获取/修改配置 |
| `/api/config/reload` | GET | 重载配置到后端 |
| `/api/databases` | GET | 列出数据库 |
| `/api/database/drop` | POST | 删除数据库 |

---

## 常见问题

### 编译报错：找不到头文件

确保在对应目录执行编译：

```bash
cd web && ./build.sh
cd insert && ./build.sh
```

### 运行时找不到 .so 文件

设置库路径：

```bash
export LD_LIBRARY_PATH=/path/to/TDlight/libs:$LD_LIBRARY_PATH
```

### 无法连接 TDengine

1. 确认容器内 taosd 已启动
2. 检查 `config.json` 中的数据库配置
3. 检查端口 6041 是否可访问

### VNodes exhausted 错误

数据库 VGroups 资源耗尽。解决方案：

1. 通过 Web 界面删除不需要的数据库
2. 或增加 `config/taos_cfg/taos.cfg` 中的 `supportVnodes` 值

### 分类无结果

1. 确认 `config.json` 中 `python` 路径正确
2. 确认 feets 环境中依赖完整
3. 查看 `class/` 目录下的日志

---

## 大文件获取

以下文件因体积过大未包含在仓库中，如有需要请联系作者获取：

| 文件 | 大小 | 获取方式 |
|------|------|----------|
| `classifier/lgbm_111w_model.pkl` | ~250MB | 联系作者获取预训练模型 |
| `data/` | - | 用户自备天文数据 |
| `tdengine-fs/` | ~2GB | 使用 `apptainer build` 构建容器 |

**联系方式**：如需预训练模型/部署相关问题，请联系 3023244355@tju.edu.cn。

---

## License

This project is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for details.

---

## 致谢

- [TDengine](https://www.taosdata.com/) - 高性能时序数据库
- [HEALPix](https://healpix.jpl.nasa.gov/) - 天球像素化方案
- [feets](https://feets.readthedocs.io/) - 天文特征提取
- [LightGBM](https://lightgbm.readthedocs.io/) - 梯度提升框架
- [Three.js](https://threejs.org/) - WebGL 3D 渲染
- [Chart.js](https://www.chartjs.org/) - 图表可视化


