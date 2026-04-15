[English](README.md) | 中文

# TDlight 分类模块

基于 LightGBM 和 `feets` 的自动光变曲线分类流水线。

## 功能说明

1. 读取源 ID 列表
2. 从 TDengine 获取光变曲线
3. 使用 `feets` 提取天文特征
4. 使用分层 LightGBM 预测器进行推理
5. 输出预测结果与置信度
6. 将高置信度结果写回 TDengine

## 环境要求

- **Conda 环境**: `tdlight`
- **TDengine**: `taosd` 运行于端口 `6030`（原生 C 客户端）
- **模型文件**: `models/hierarchical_unlimited/`（由 `install.sh` 自动下载）

## 关键路径

```
TDlight/class/
├── classify_pipeline.py         # 手动分类入口
├── auto_classify.py             # 自动批量分类
├── hierarchical_predictor.py    # 分层预测器（ONNX / sklearn）
└── README_CN.md                 # 本文档
```

## 使用方法

运行分类脚本前请先激活环境：

```bash
conda activate tdlight
```

### 手动分类

干跑模式（仅预测，不更新数据库）：

```bash
cd class
python classify_pipeline.py \
    --input test_ids.csv \
    --output results.csv \
    --threshold 0.95 \
    --dry-run
```

实际更新数据库（置信度 >= 0.95）：

```bash
python classify_pipeline.py \
    --input test_ids.csv \
    --output results.csv \
    --threshold 0.95 \
    --update
```

### 参数说明

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--input` | (必填) | 输入 ID 列表（CSV，需含 `ID` 或 `source_id` 列） |
| `--output` | (必填) | 输出结果 CSV |
| `--threshold` | `0.95` | 置信度阈值，超过此值才更新标签 |
| `--db` | `gaiadr2_lc` | 数据库名 |
| `--host` | `localhost` | 数据库主机 |
| `--port` | `6030` | TDengine 原生端口 |
| `--update` | `False` | 执行实际更新（写入 TDengine） |
| `--dry-run` | `False` | 干跑模式（只输出结果） |

### 输入格式

```csv
ID,Class
5870536848431465216,Unknown
423946158683096960,Unknown
```

### 输出格式

```csv
ID,Original_Class,Predicted_Class,Confidence,Updated_Class,Update_Status,Data_Points
5870536848431465216,Unknown,DSCT,0.9789,DSCT,db_updated,241
423946158683096960,Unknown,DSCT,0.6610,Unknown,low_confidence,253
```

## 自动分类

自动分类工作流请参见主 [README_CN.md](../README_CN.md)。简要命令：

```bash
cd class
python auto_classify.py \
    --db gaiadr2_lc \
    --batch-size 5000
```

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

## 常见问题

### "模型文件不存在"

确认 `install.sh` 已下载模型：

```bash
ls models/hierarchical_unlimited/
```

### "数据库连接失败"

检查 TDengine 是否运行：

```bash
systemctl --user status taosd
# 或
ps aux | grep taosd
```

### "表不存在"

子表命名规则为 `t_{source_id}`，可用以下命令确认：

```bash
taos -s "USE gaiadr2_lc; SHOW TABLES LIKE 't_5870536848431465216';"
```

### 代理问题

脚本内部已禁用代理（`session.trust_env = False`）。如仍有问题：

```bash
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY
```
