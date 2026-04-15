[English](README.md) | 中文

# TDlight Web 服务

提供完整的光变曲线数据管理和分类 Web 服务。

## 核心功能

| 功能 | 说明 |
|------|------|
| 天体列表 | 浏览数据库中的天体 |
| 光变曲线 | 查看时序观测数据和可视化 |
| 空间检索 | 锥形检索和区域检索 |
| 天图显示 | 3D 可视化天体分布 |
| 智能分类 | LightGBM 模型实时分类 |
| 数据导入 | Web 界面批量 CSV 导入 |
| 数据库管理 | 多数据库切换 |

## 快速开始

### 1. 编译并启动

```bash
cd web
./build.sh
./web_api
```

### 2. 打开浏览器

**http://localhost:5001**

---

## 使用说明

### 天体浏览

1. 打开页面后自动加载天体列表
2. 点击任意天体查看光变曲线
3. 使用搜索框按 `source_id` 过滤

### 空间检索

**锥形检索**（以某点为圆心）：
- 输入 RA、DEC（度）和半径（度）
- 点击"搜索"

**区域检索**（矩形区域）：
- 输入 RA/DEC 的最小值和最大值
- 点击"搜索"

### 天体分类

1. 选择分类方式：
   - **随机分类**：从数据库随机选取天体
   - **可见天体**：分类当前列表中的天体
2. 设置置信度阈值（仅高于阈值的结果写入数据库）
3. 点击"开始分类"
4. 实时查看进度和结果

### 数据导入

**导入光变曲线前必须准备**：
1. 光变曲线 CSV 目录（每个天体一个文件）
2. 坐标文件（包含所有天体的 RA/DEC）

**操作步骤**：
1. 切换到"数据导入"标签
2. 填写数据库名称
3. 填写 CSV 目录路径
4. 填写坐标文件路径
5. 点击"开始导入"
6. 实时查看导入进度和日志

### 配置管理

在"系统设置"标签可以：
- 修改数据库连接参数
- 修改分类模型路径
- 修改 Python 可执行路径
- 点击"保存配置"保存到本地
- 点击"应用到后端"重载配置

---

## 分类模型

使用 **分层 LightGBM 预测器**（默认 ONNX Runtime，自动回退到 sklearn）。

### 使用的特征（15个）

```
PeriodLS, Mean, Rcs, Psi_eta, StetsonK_AC,
Gskew, Psi_CS, Skew, Freq1_harmonics_amplitude_1, Eta_e,
LinearTrend, Freq1_harmonics_amplitude_0, AndersonDarling, MaxSlope, StetsonK
```

---

## API 接口

完整的 API 列表请参见主 [README_CN.md](../README_CN.md)。

常用示例：

### 锥形检索

```bash
curl "http://localhost:5001/api/cone_search?ra=180&dec=30&radius=0.1"
```

### 获取光变曲线

```bash
curl "http://localhost:5001/api/lightcurve/t_5870536848431465216"
```

### 启动分类

```bash
curl -X POST "http://localhost:5001/api/classify_objects" \
  -H "Content-Type: application/json" \
  -d '{"objects": [{"source_id": "5870536848431465216"}], "threshold": 0.8}'
```

---

## 编译说明

如需修改后端代码：

```bash
cd web
./build.sh
```

编译需要：
- g++（支持 C++17）
- TDengine 客户端库
- HEALPix C++ 库

---

## 文件结构

```
web/
├── web_api.cpp           # C++ 后端源码
├── web_api               # 编译后的可执行文件
├── build.sh              # 编译脚本
├── index.html            # 前端 HTML
├── app.js                # 前端 JavaScript
├── lang.js               # 国际化
├── sse_test.html         # SSE 测试页面
└── README_CN.md          # 本文档
```

---

## 常见问题

### 1. 连接 TDengine 失败

- 确认 `taosd` 正在运行：`systemctl --user status taosd`
- 检查 `config.json` 中的数据库配置

### 2. 分类失败

- 确认模型文件存在于 `models/hierarchical_unlimited/`
- 确认 `tdlight` conda 环境已配置
- 检查 `config.json` 中的 `paths.python`

### 3. 数据导入无响应

- 确认路径存在且可读
- 检查坐标文件是否存在
- 查看 Web UI 中的日志错误信息

### 4. 端口被占用

```bash
pkill -f web_api
# 或
kill $(lsof -t -i:5001)
```

---

## 性能参考

| 操作 | 典型耗时 |
|------|---------|
| 天体列表 (200条) | 50-200 ms |
| 单条光变曲线 | 10-50 ms |
| 锥形检索 (r=0.1°) | 20-100 ms |
| 单天体分类 | 400-600 ms |
| 批量分类 (每个) | ~50 ms |

---

## 更新日志

### v2.0 (2026-01)

- SSE 实时进度更新
- 数据导入功能集成
- 动态配置管理
- 改进的错误处理
