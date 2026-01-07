# TDlight 测试数据集

本目录包含用于测试 TDlight 系统的 Gaia DR2 光变曲线样本数据。

## 文件说明

| 文件 | 大小 | 说明 |
|------|------|------|
| `gaia_testdata.tar.gz` | 40MB | 测试数据压缩包 |

### 压缩包内容

解压后包含：

```
catalogs/                      # 星表文件（708个）
├── catalog_001.csv            # 第1次观测
├── catalog_002.csv            # 第2次观测
├── ...
└── catalog_708.csv            # 第708次观测

lightcurves/                   # 原始光变曲线文件（3800个）
├── lightcurve_xxx.csv         # 每个天体一个文件
└── ...

source_coordinates.csv         # 天体坐标文件
```

## 数据统计

- **天体数量**: 3,800 个
- **观测记录**: 336,690 条
- **Catalog文件数**: 708 个
- **波段**: G / BP / RP
- **天区覆盖**: 全天球 (RA: 0°-360°, DEC: -87°-+87°)

## 快速开始

```bash
# 1. 解压数据
cd testdata
tar -xzf gaia_testdata.tar.gz

# 2. 进入 TDlight 环境
cd ..
./start_env.sh

# 3. 在容器内导入数据
cd /app/insert
./lightcurve_importer testdb ../testdata/catalogs/
```

## 数据格式

### Catalog 文件格式 (catalogs/*.csv)

每个 catalog 文件中，每个天体只出现一次（代表一次观测）：

```csv
source_id,ra,dec,class,band,time,flux,flux_err,mag,mag_err
1835164110276685440,300.42,26.46,Unknown,G,1763.615,24024.40,175.59,14.74,0.0079
```

### Lightcurve 文件格式 (lightcurves/*.csv)

每个天体一个文件，包含该天体的所有观测记录（已计算 mag_err）：

```csv
time,band,flux,flux_err,mag,mag_err
1710.067,G,3861.14,34.75,16.72,0.0098
```

### 坐标文件格式 (source_coordinates.csv)

```csv
source_id,ra,dec
1007596926756899072,96.68,64.12
```

| 字段 | 类型 | 说明 |
|------|------|------|
| source_id | int64 | Gaia DR2 天体ID |
| ra | double | 赤经 (度) |
| dec | double | 赤纬 (度) |

## 数据来源

数据来自 [Gaia DR2 Variable Stars](https://gea.esac.esa.int/archive/) 公开数据集。

## 许可证

测试数据遵循 Gaia DR2 数据使用条款。
