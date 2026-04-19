[English](README.md) | 中文

# TDlight

> **基于 TDengine 时序数据库的天文光变曲线管理与分类系统。**
>
> 支持大规模天文时序数据的高效存储、快速检索和智能分类。
>
> **论文亮点：** 本仓库中的 `cross_validate_560_results.csv` 记录了分类器识别出的 **560 个高置信度星表不一致候选体**，可供后续验证跟进。
>
> **预训练模型：** [HuggingFace — bestdo77/Lightcurve_lgbm_111w_15_model](https://huggingface.co/bestdo77/Lightcurve_lgbm_111w_15_model)

---

## 快速开始

```bash
git clone https://github.com/bestdo77/TD-light.git
cd TD-light
./install.sh
```

然后打开浏览器访问 [http://localhost:5001](http://localhost:5001)。

详细的安装选项和手动步骤请参见 [INSTALL.md](INSTALL.md)。

---

## Web 界面

![TDlight Web 界面](assets/tdlight_web_interface.png)

*TDlight Web 界面。左侧面板支持在三维天球上进行交互式锥形检索，高亮显示搜索边界；右侧面板展示多波段（G/BP/RP）光变曲线及选中目标的分类元数据和统计信息。*

---

## 技术栈

| 层级 | 技术 | 说明 |
|------|------|------|
| **数据库** | TDengine 3.4+ | 高性能时序数据库（用户模式安装，无需 sudo） |
| **后端** | C++17 | 原生 HTTP 服务器、HEALPix 空间索引 |
| **机器学习** | Python + LightGBM + ONNX Runtime | `feets` 特征提取 + 加速推理 |
| **增量训练** | Flask (Python) | 训练服务器，SSE 实时推送进度 |
| **前端** | HTML/JS | Three.js 3D 天球、Chart.js 交互式图表 |

---

## 功能特性

| 功能 | 说明 |
|------|------|
| **锥形检索** | 以天球坐标为中心、按半径搜索天体，HEALPix 索引加速 |
| **矩形检索** | 按 RA/DEC 范围批量查询 |
| **光变曲线可视化** | 交互式图表展示时序测光数据 |
| **智能分类** | 基于分层 LightGBM 预测器的变星自动分类 |
| **自动分类** | 自动检测新导入或数据增长的天体，分批后台处理 |
| **星表不一致检测** | 识别出 560 个高置信度星表不一致候选体，供后续验证 |
| **数据导入** | Web 界面一键导入，或命令行多线程高性能导入 |
| **增量训练** | 上传带 `class` 列的光变曲线，自动识别标签并增量更新 7 个子模型 |
| **3D 天球** | WebGL 渲染的交互式三维天球 |

---

## 系统架构

![TDlight 系统架构](assets/tdlight_figure1.png)

```
┌─────────────────────────────────────────────────────────────────┐
│                        浏览器 (前端)                              │
│   index.html + app.js (Three.js 3D / Chart.js / SSE 实时通信)    │
└───────────────────────────────┬─────────────────────────────────┘
                                │ HTTP/SSE
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                      web_api (C++ 后端)                          │
│                      train_server (Flask, :5002)                │
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
│                   TDengine 时序数据库 (用户模式)                  │
│                                                                 │
│   超级表: lightcurves                                            │
│   ├── 标签: healpix_id, source_id, ra, dec, cls                 │
│   └── 数据: ts, band, mag, mag_error, flux, flux_error, jd_tcb  │
│                                                                 │
│   VGroups: 128 (支持约 2 个完整数据库)                            │
└─────────────────────────────────────────────────────────────────┘
```

### 组件说明

1. **前端 (`index.html` + `app.js`)**
   - 使用 Fetch API 调用后端 REST 接口
   - 通过 Server-Sent Events (SSE) 接收分类/导入进度
   - Three.js 渲染 3D 天球，Chart.js 绘制交互式光变曲线

2. **后端 (`web_api.cpp`)**
   - 纯 C++ 实现的 HTTP 服务器
   - 使用 HEALPix 进行天球像素化，加速空间检索
   - 通过子进程调用 Python 分类脚本和 C++ 导入器

3. **分类模块 (`classify_pipeline.py` / `auto_classify.py`)**
   - 分层 LightGBM 预测器（4 层树结构，7 个子模型）
   - 使用 `feets` 库提取天文特征
   - 默认使用 ONNX Runtime（比 sklearn 快约 3.7 倍）；未安装时自动回退到 sklearn
   - 高置信度结果自动写回 TDengine

4. **增量训练 (`class/incremental_train.py` + `train_server.py`)**
   - Web 界面上传 CSV/ZIP 光变曲线
   - 自动识别 `class` / `label` / `type` 列中的类别标签
   - 使用 `init_model=old_model` 增量训练全部 7 个子模型
   - 后台自动导出 ONNX + 手动重导出端点
   - 训练失败自动回滚到备份模型

5. **数据导入器 (`catalog_importer` / `lightcurve_importer`)
   - 独立的 C++ 多线程并行导入程序
   - Web 导入默认：16 线程、32 VGroups
   - 命令行支持自定义 `--threads` 和 `--vgroups`

---

## 安装步骤

### 一键安装（推荐）

```bash
git clone https://github.com/bestdo77/TD-light.git
cd TD-light
./install.sh
```

`install.sh` 会依次完成：
1. 检查依赖（conda、g++、wget/curl）
2. 下载并用户模式安装 TDengine 3.4+（`~/taos`，无需 sudo）
3. 从 HuggingFace 下载预训练模型
4. 创建 `tdlight` conda 环境并安装 Python 依赖
5. 编译所有 C++ 组件（`web_api`、导入器、查询工具）
6. 配置 TDengine 并创建默认数据库（`gaiadr2_lc`）
7. 解压测试数据（如有）

### 启动服务

```bash
# 激活环境
conda activate tdlight
source start_env.sh

# 启动 TDengine（如未运行）
systemctl --user start taosd

# 启动服务
source start_env.sh        # 同时启动 web_api (5001) + train_server (5002) + TDengine
```

打开浏览器访问 [http://localhost:5001](http://localhost:5001)。

### 手动安装

如需分步手动安装，请参阅 [INSTALL.md](INSTALL.md)。

---

## 数据导入

### Web 界面导入

点击网页上的 **数据导入** 标签页（默认 16 线程、32 VGroups）。

### 命令行导入

```bash
# 星表导入
./insert/catalog_importer \
    --catalogs /path/to/catalogs \
    --coords /path/to/coordinates.csv \
    --db gaiadr2_lc \
    --threads 64 \
    --vgroups 128

# 光变曲线导入
./insert/lightcurve_importer \
    --lightcurves_dir /path/to/lightcurves \
    --coords /path/to/coordinates.csv \
    --db gaiadr2_lc \
    --threads 64 \
    --vgroups 128
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--threads` | 16 | 并行线程数 |
| `--vgroups` | 32 | 数据库 VGroups 数 |
| `--nside` | 64 | HEALPix NSIDE（仅 catalog） |
| `--drop_db` | - | 删除已存在的数据库 |

### 必需文件

导入光变曲线时，**必须同时提供坐标文件**：

| 文件 | 说明 |
|------|------|
| 光变曲线目录 | 每个天体一个 CSV 文件（文件名需包含 `source_id`） |
| 坐标文件 | 一个 CSV 文件，包含所有天体的 `source_id,ra,dec` |

坐标文件用于计算 HEALPix 索引和创建子表时设置 TAGS。

### 数据格式

**光变曲线 CSV**（每个天体一个文件）：
```csv
source_id,band,time,mag,mag_error,flux,flux_error,class
12345678,G,2015.5,15.234,0.002,1234.5,2.5,ROT
```

> **提示：** 在 CSV 中添加 `class`（或 `label` / `type`）列，可在训练上传时启用**自动识别模式**。支持的标签：`Non-var`、`ROT`、`EA`、`EW`、`CEP`、`DSCT`、`RRAB`、`RRC`、`M`、`SR`、`EB`。

**坐标文件 CSV**（所有天体一个文件）：
```csv
source_id,ra,dec
12345678,180.123,-45.678
```

### 数据库行为

| 场景 | 行为 |
|------|------|
| 数据库不存在 | 自动创建，128 VGroups |
| 数据库已存在 | 继续使用 |
| 表已存在 | 跳过创建 |
| 插入新数据 | **追加**到表中 |
| 时间戳冲突 | **覆盖**旧记录 |

> **VGroups 限制**：配置文件 `supportVnodes=256`，每个数据库使用 128 VGroups，系统同时最多支持约 **2 个完整数据库**。导入前请删除不需要的数据库以释放资源。

---

## 检索功能

### 锥形检索 (Cone Search)

以指定天球坐标为圆心、按半径搜索：
- 输入：RA (度), DEC (度), 半径 (角分)
- 使用 HEALPix 索引加速

### 矩形检索 (Region Search)

按 RA/DEC 范围批量查询，适合获取区域内的所有天体。

---

## 分类功能

### 推理加速

| 推理后端 | 5,000 样本耗时 | 吞吐量 | 说明 |
|---------|--------------|--------|------|
| sklearn（单线程） | ~8,300 ms | ~600 样本/s | 基线 |
| sklearn（8 线程） | ~2,000 ms | ~2,500 样本/s | 多线程 |
| **ONNX Runtime（8 线程）** | **~400 ms** | **~12,500 样本/s** | **默认后端，快约 3.7 倍** |

未安装 `onnxruntime` 时自动回退到 sklearn，无需修改代码。

### 手动分类流程

1. 在 Web 界面选择要分类的天体
2. 点击 **开始分类**
3. 系统自动：
   - 从数据库提取光变曲线
   - 使用 `feets` 提取 15 个天文特征
   - 分层 LightGBM 预测器推断变星类型（ONNX Runtime 加速）
   - 高置信度结果写回数据库

### 自动分类功能

自动分类模块与导入器完全解耦：

| 检测条件 | 说明 |
|----------|------|
| **首次出现** | 历史记录文件中没有该 `source_id` |
| **数据增长 >20%** | 数据点数比历史记录增长超过 20% |

**工作流程：**

1. 通过任意导入器导入数据
2. 点击 **查询** 按钮运行 `check_candidates`：
   - 查询所有天体的数据点数
   - 与历史文件 `data/lc_counts_<db>.csv` 对比
   - 新增或增长 >20% 的天体写入 `data/auto_classify_queue_<db>.csv`
   - 更新历史记录文件
3. 点击 **开始** 启动 `auto_classify.py`：
   - 分批处理（默认 5000 条/批）
   - `feets` 特征提取 + LightGBM 预测
   - 高置信度结果自动写回数据库

**特性：**
- 解耦设计：检测程序独立于导入器，可随时手动触发
- 可中断、可恢复：点击 **继续** 从中断处恢复
- 实时进度：SSE 推送批次进度
- 多数据库支持：每个数据库独立的队列和历史文件

### 置信度阈值

在 **系统设置** 中可调整：
- 高于阈值：自动写回数据库
- 低于阈值：仅显示，不保存

---

## 目录结构

```
TDlight/
├── config.json              # 主配置文件
├── start_env.sh             # 环境激活脚本
├── install.sh               # 一键安装脚本
├── INSTALL.md               # 详细安装指南
├── requirements.txt         # Python 依赖
├── Makefile                 # C++ 编译（install.sh 调用）
│
├── web/                     # Web 服务
│   ├── web_api.cpp          # C++ HTTP 后端
│   ├── build.sh
│   └── static/              # 前端资源
│
├── class/                   # 分类与训练模块
│   ├── classify_pipeline.py
│   ├── auto_classify.py
│   ├── hierarchical_predictor.py
│   ├── feature_extractor.py       # feets 特征提取
│   ├── incremental_train.py       # 增量训练流水线
│   ├── train_server.py            # Flask HTTP 服务器 (端口 5002)
│   └── training_data_manager.py   # 训练数据持久化
│
├── insert/                  # 数据导入器
│   ├── catalog_importer.cpp
│   ├── lightcurve_importer.cpp
│   ├── check_candidates.cpp
│   ├── crossmatch.cpp
│   └── build.sh
│
├── query/                   # 查询工具
│   └── optimized_query.cpp
│
├── models/                  # 预训练模型（自动下载）
│   ├── hierarchical_unlimited/   # 7 个子模型（pkl + onnx）
│   └── lgbm_111w_model.*         # 旧版扁平模型
│
├── testdata/                # 测试数据（install.sh 自动解压）
├── libs/                    # C++ 运行时库
├── include/                 # C++ 头文件
├── config/                  # TDengine 客户端配置
├── data/                    # 运行时数据文件（点数记录、队列）
└── runtime/                 # 运行时日志
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
| `/api/auto_classify/check` | POST | 触发候选检测 |
| `/api/auto_classify/candidates` | GET | 获取待分类天体数量 |
| `/api/auto_classify/start` | POST | 启动自动分类任务 |
| `/api/auto_classify/stop` | POST | 停止自动分类 |
| `/api/auto_classify/stream` | GET (SSE) | 自动分类进度流 |
| `/api/auto_classify/results` | GET | 获取自动分类结果 |
| `/api/config` | GET/POST | 获取/修改配置 |
| `/api/config/reload` | GET | 重载配置到后端 |
| `/api/databases` | GET | 列出数据库 |
| `/api/database/drop` | POST | 删除数据库 |
| `/api/train/upload` | POST | 上传训练 CSV/ZIP |
| `/api/train/start` | POST | 启动特征提取 / 训练 |
| `/api/train/stream` | GET (SSE) | 训练进度流 |
| `/api/train/stop` | POST | 停止训练 |
| `/api/train/summary` | GET | 训练数据概览 |
| `/api/train/export_onnx` | POST | 手动导出 ONNX |
| `/api/train/clear` | POST | 清空所有训练数据 |

---

## 常见问题

### 编译报错：找不到头文件

确保在对应目录执行编译：
```bash
cd web && ./build.sh
cd ../insert && ./build.sh
```

### 运行时找不到 .so 文件

```bash
source start_env.sh
# 或手动设置：
export LD_LIBRARY_PATH=/path/to/TDlight/libs:$LD_LIBRARY_PATH
```

### 无法连接 TDengine

1. 确认 `taosd` 已运行：`systemctl --user status taosd`
2. 检查 `config.json` 中的数据库配置
3. 检查端口 6030 是否可访问

### VNodes exhausted 错误

数据库 VGroups 资源耗尽。解决方案：
1. 通过 Web 界面删除不需要的数据库
2. 或增加 `config/taos_cfg/taos.cfg` 中的 `supportVnodes` 值

### 分类无结果

1. 确认 `config.json` 中 `python` 路径正确
2. 确认 `tdlight` conda 环境中依赖完整
3. 查看 `class/` 目录下的日志

### 终端崩溃 / 命令无法执行

如果执行 `source start_env.sh` 后终端崩溃：
```bash
# 检查 libs/ 目录是否包含不兼容的系统库
ls libs/ | grep -E "libstdc|libgcc|libgomp"

# 如果存在，删除它们（应使用系统版本）
rm -f libs/libstdc++.so* libs/libgcc_s.so* libs/libgomp.so*
```

### 端口 5001 被占用

```bash
lsof -i :5001
# 或在 web/web_api.cpp 中修改 PORT 常量
```

---

## 大文件获取

以下文件因体积较大未包含在仓库中，`install.sh` 会自动下载：

| 文件 | 大小 | 获取方式 |
|------|------|----------|
| `models/hierarchical_unlimited/*.pkl` | ~350 MB 总计 | `install.sh` 自动下载 |
| `models/hierarchical_unlimited/*.onnx` | ~280 MB 总计 | `install.sh` 自动下载 |
| `data/` | - | 用户自备天文数据 |
| TDengine | ~500 MB | `install.sh` 自动下载 |

**联系方式**：如需预训练模型或部署相关问题，请联系 3023244355@tju.edu.cn。

---

## License

本项目基于 **MIT License** 开源，详见 [LICENSE](LICENSE) 文件。

---

## 第三方库

本项目在 `libs/` 目录中包含预编译的库文件：

| 库 | 许可证 | 来源 |
|----|--------|------|
| CFITSIO | NASA/GSFC (类 BSD) | https://heasarc.gsfc.nasa.gov/fitsio/ |
| HEALPix C++ | GPL v2 | http://healpix.sourceforge.net |
| libsharp | GPL v2 | http://healpix.sourceforge.net |

### HEALPix 引用

如果您使用本软件，请引用 HEALPix：

> K.M. Górski, E. Hivon, A.J. Banday, B.D. Wandelt, F.K. Hansen, M. Reinecke, M. Bartelmann (2005),  
> *HEALPix: A Framework for High-Resolution Discretization and Fast Analysis of Data Distributed on the Sphere*,  
> ApJ, 622, p.759-771  
> http://healpix.sourceforge.net

---

## 致谢

- [TDengine](https://www.taosdata.com/) - 高性能时序数据库
- [HEALPix](https://healpix.sourceforge.net/) - 天球像素化方案
- [feets](https://feets.readthedocs.io/) - 天文特征提取
- [LightGBM](https://lightgbm.readthedocs.io/) - 梯度提升框架
- [ONNX Runtime](https://onnxruntime.ai/) - 高性能推理引擎
- [Three.js](https://threejs.org/) - WebGL 3D 渲染
- [Chart.js](https://www.chartjs.org/) - 图表可视化
