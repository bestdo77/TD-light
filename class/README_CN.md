[English](README.md) | 中文

# TDengine 光变曲线分类器

## 功能说明

本模块实现基于 LightGBM 的光变曲线自动分类流水线：

1. 读取指定的 ID 列表
2. 从 TDengine 读取光变曲线数据
3. 提取 15 个核心特征（使用 feets 库）
4. 使用预训练模型进行 10 类分类
5. 输出置信度和分类结果
6. 当置信度 >= 阈值时，更新 TDengine 中的分类标签

### 支持的类别

| 编号 | 类别名 | 说明 |
|------|--------|------|
| 0 | Non-var | 非变星 |
| 1 | ROT | 旋转变星 |
| 2 | EA | Algol 型食双星 |
| 3 | EW | 大熊座 W 型食双星 |
| 4 | CEP | 造父变星 |
| 5 | DSCT | 盾牌座 δ 型变星 |
| 6 | RRAB | 天琴座 RR 型 (ab亚型) |
| 7 | RRC | 天琴座 RR 型 (c亚型) |
| 8 | M | 米拉型变星 |
| 9 | SR | 半规则变星 |

---

## 环境要求

### Python 环境

本脚本必须在 **feets** conda 环境中运行：

```bash
conda activate feets
```

该环境包含以下关键依赖：
- Python 3.9
- feets 0.4 (特征提取库)
- lightgbm (分类模型)
- pandas, numpy
- requests (REST API)
- joblib (模型加载)

### TDengine 要求

1. **taosd 服务** 必须运行（端口 6041）
2. **taosAdapter** 必须运行（端口 6044，用于 REST API）

### 启动 taosAdapter

如果 taosAdapter 未运行，使用以下命令启动：

```bash
cd /mnt/nvme/home/yxh/code/TDengine-test

nohup /mnt/nvme/home/yxh/anaconda3/envs/singularity/bin/apptainer exec \
    --bind runtime/taos_home/cfg:/etc/taos \
    --env TAOS_ADAPTER_PORT=6044 \
    tdengine-fs \
    taosadapter > runtime/taos_home/log/taosadapter.log 2>&1 &
```

验证 taosAdapter 运行状态：
```bash
ss -tuln | grep 6044
# 应显示: tcp LISTEN 0 4096 *:6044 *:*
```

---

## 关键路径

```
项目根目录: /mnt/nvme/home/yxh/code/TDengine-test/runtime-final/class
├── classify_pipeline.py          # 主脚本
└── README.md                     # 本文档

模型文件: /mnt/nvme/home/yxh/code/leaves-retrain/results/
└── lgbm_111w_15features_tuned_20251226_114818/
    ├── lgbm_111w_model.pkl       # 训练好的 LightGBM 模型
    └── metadata.pkl              # 类别映射等元数据
```

---

## 使用方法

### 基本用法

```bash
cd /mnt/nvme/home/yxh/code/TDengine-test/runtime-final/class
conda activate feets

# 干跑模式（只预测，不更新数据库）
python classify_pipeline.py \
    --input test_ids.csv \
    --output results.csv \
    --threshold 0.95 \
    --dry-run
```

### 执行数据库更新

```bash
# 实际更新数据库（置信度 >= 0.95 的样本）
python classify_pipeline.py \
    --input test_ids.csv \
    --output results.csv \
    --threshold 0.95 \
    --update
```

### 完整参数说明

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--input` | (必填) | 输入 ID 列表文件 (CSV，需包含 ID 列) |
| `--output` | (必填) | 输出结果文件 (CSV) |
| `--threshold` | 0.95 | 置信度阈值，超过此值才更新标签 |
| `--db` | catalog_test | 数据库名 |
| `--host` | localhost | 数据库主机 |
| `--port` | 6044 | REST API 端口 (taosAdapter) |
| `--table` | sensor_data | 超级表名 |
| `--model` | (见代码) | 模型文件路径 |
| `--update` | False | 执行实际更新（写入 TDengine） |
| `--dry-run` | False | 干跑模式（只输出结果） |
| `--verbose, -v` | False | 详细输出模式 |

### 输入文件格式

CSV 文件，必须包含 `ID` 或 `source_id` 列：

```csv
ID,Class
5870536848431465216,Unknown
423946158683096960,Unknown
6073590601358642432,Unknown
```

### 输出文件格式

```csv
ID,Original_Class,Predicted_Class,Confidence,Updated_Class,Update_Status,Data_Points
5870536848431465216,Unknown,DSCT,0.9789,DSCT,db_updated,241
423946158683096960,Unknown,DSCT,0.6610,Unknown,low_confidence,253
```

字段说明：
- `ID`: 源ID
- `Original_Class`: 原始类别
- `Predicted_Class`: 模型预测类别
- `Confidence`: 预测置信度
- `Updated_Class`: 最终类别（更新后）
- `Update_Status`: 状态 (db_updated/low_confidence/no_data/error)
- `Data_Points`: 光变曲线数据点数

---

## 性能参考

在当前硬件环境下的典型性能：

| 操作 | 平均时间 | 说明 |
|------|---------|------|
| 模型加载 | ~1.2 秒 | 一次性 |
| 特征空间初始化 | ~1 毫秒 | 一次性 |
| 数据库查询 | ~92 毫秒 | 每样本 |
| 特征提取 | ~343 毫秒 | 每样本（**主要瓶颈**） |
| 单样本推理 | ~17 毫秒 | 每样本 |
| 批量推理 | ~0.5 毫秒 | 每样本 |

**理论吞吐量**: ~2.2 样本/秒

**主要瓶颈**: 特征提取（Lomb-Scargle 周期搜索计算密集）

---

## 15 个核心特征

| 序号 | 特征名 | 说明 |
|------|--------|------|
| 1 | PeriodLS | Lomb-Scargle 周期 |
| 2 | Mean | 平均星等 |
| 3 | Rcs | 累积和距离 |
| 4 | Psi_eta | 相位折叠 η 统计 |
| 5 | StetsonK_AC | 自相关 Stetson K |
| 6 | Gskew | 光变曲线偏度 |
| 7 | Psi_CS | 相位折叠累积和 |
| 8 | Skew | 偏度 |
| 9 | Freq1_harmonics_amplitude_1 | 第一谐波振幅 |
| 10 | Eta_e | η 统计量 |
| 11 | LinearTrend | 线性趋势 |
| 12 | Freq1_harmonics_amplitude_0 | 基波振幅 |
| 13 | AndersonDarling | Anderson-Darling 统计量 |
| 14 | MaxSlope | 最大斜率 |
| 15 | StetsonK | Stetson K 统计量 |

---

## 常见问题

### 1. "模型文件不存在"

确认模型路径正确：
```bash
ls -la /mnt/nvme/home/yxh/code/leaves-retrain/results/lgbm_111w_15features_tuned_20251226_114818/
```

### 2. "数据库连接失败"

检查 taosAdapter 是否运行：
```bash
ss -tuln | grep 6044
curl -s -u root:taosdata "http://localhost:6044/rest/sql" -d "SELECT 1"
```

### 3. "Table does not exist" 更新失败

子表命名规则是 `t_{source_id}`，确认表存在：
```bash
# 通过 Apptainer 执行
apptainer exec ... taos -s "USE catalog_test; SHOW TABLES LIKE 't_5870536848431465216';"
```

### 4. feets 警告信息

feets 库会输出一些警告（如 AndersonDarling、StetsonK 的值范围），这是正常的，不影响结果。

### 5. 代理导致连接失败

脚本内部已禁用代理（`session.trust_env = False`），如果仍有问题：
```bash
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY
```

---

## 扩展开发

### 修改默认配置

编辑 `classify_pipeline.py` 开头的配置区域：

```python
DB_HOST = "localhost"
DB_PORT = 6044  # REST API 端口
DB_NAME = "catalog_test"
SUPER_TABLE = "sensor_data"
MODEL_PATH = "/path/to/model.pkl"
```

### 添加新特征

1. 修改 `SELECTED_FEATURES` 列表
2. 确保模型是用相同特征训练的
3. 更新 metadata.pkl 中的特征列表

### 使用不同模型

```bash
python classify_pipeline.py --model /path/to/new_model.pkl ...
```

确保新模型目录下有对应的 `metadata.pkl` 文件。


