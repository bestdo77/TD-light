#!/usr/bin/env python3
"""
自动分类流水线 - 分批处理导入时检测到的候选光变曲线
支持：
- 分批处理（每批5000条）
- 断点续传
- 后台运行
- 进度报告
"""

import os
import sys
import pandas as pd
import numpy as np
import feets
import joblib
import pickle
import taos
import json
import time
import argparse
import warnings
from datetime import datetime
from pathlib import Path

warnings.filterwarnings('ignore')

# ==================== 配置 ====================
CONFIG_FILE = "../config.json"
PROGRESS_FILE = "/tmp/auto_classify_progress.json"
STATE_FILE = "/tmp/auto_classify_state.json"
STOP_FILE = "/tmp/auto_classify_stop"

# 默认配置
DB_HOST = "localhost"
DB_PORT = 6041
DB_NAME = "gaiadr2_lc"
MODEL_PATH = "../classifier/lgbm_111w_model.pkl"
METADATA_PATH = "../classifier/metadata.pkl"
CONFIDENCE_THRESHOLD = 0.95
UPDATE_DATABASE = True
BATCH_SIZE = 5000

# 特征列表
SELECTED_FEATURES = [
    'PeriodLS', 'Mean', 'Rcs', 'Psi_eta', 'StetsonK_AC',
    'Gskew', 'Psi_CS', 'Skew', 'Freq1_harmonics_amplitude_1', 'Eta_e',
    'LinearTrend', 'Freq1_harmonics_amplitude_0', 'AndersonDarling', 'MaxSlope', 'StetsonK'
]

ALL_CLASSES = ['Non-var', 'ROT', 'EA', 'EW', 'CEP', 'DSCT', 'RRAB', 'RRC', 'M', 'SR']


def load_config():
    """加载配置文件"""
    global DB_HOST, DB_PORT, DB_NAME, MODEL_PATH, METADATA_PATH, CONFIDENCE_THRESHOLD, UPDATE_DATABASE
    
    if not os.path.exists(CONFIG_FILE):
        return
    
    try:
        with open(CONFIG_FILE, 'r') as f:
            config = json.load(f)
        
        if 'database' in config:
            db = config['database']
            DB_HOST = db.get('host', DB_HOST)
            DB_PORT = db.get('port', DB_PORT)
            DB_NAME = db.get('name', DB_NAME)
        
        if 'classification' in config:
            cls = config['classification']
            MODEL_PATH = cls.get('model_path', MODEL_PATH)
            METADATA_PATH = cls.get('metadata_path', METADATA_PATH)
            CONFIDENCE_THRESHOLD = cls.get('confidence_threshold', CONFIDENCE_THRESHOLD)
            UPDATE_DATABASE = cls.get('update_database', UPDATE_DATABASE)
            
    except Exception as e:
        print(f"[ERROR] Failed to load config: {e}")


load_config()


def update_progress(percent, message, status="running", batch_info=None):
    """更新进度文件"""
    try:
        data = {
            "percent": percent,
            "message": message,
            "status": status,
            "timestamp": int(time.time())
        }
        if batch_info:
            data.update(batch_info)
        
        tmp_file = PROGRESS_FILE + ".tmp"
        with open(tmp_file, "w") as f:
            json.dump(data, f)
            f.flush()
            os.fsync(f.fileno())
        os.rename(tmp_file, PROGRESS_FILE)
    except:
        pass


def save_state(state):
    """保存状态（用于断点续传）"""
    try:
        with open(STATE_FILE, "w") as f:
            json.dump(state, f)
    except:
        pass


def load_state():
    """加载上次的状态"""
    try:
        if os.path.exists(STATE_FILE):
            with open(STATE_FILE, "r") as f:
                return json.load(f)
    except:
        pass
    return None


def clear_state():
    """清除状态文件"""
    try:
        if os.path.exists(STATE_FILE):
            os.remove(STATE_FILE)
    except:
        pass


def check_stop():
    """检查是否需要停止"""
    if os.path.exists(STOP_FILE):
        return True
    return False


class TDengineClient:
    """TDengine 原生客户端"""
    def __init__(self, host, port, user="root", password="taosdata", db_name=None):
        self.host = host
        self.port = port
        self.user = user
        self.password = password
        self.db_name = db_name
        self.conn = None
        self.cursor = None
        
    def connect(self):
        try:
            self.conn = taos.connect(
                host=self.host, 
                user=self.user, 
                password=self.password, 
                database=self.db_name, 
                port=self.port
            )
            self.cursor = self.conn.cursor()
            return True
        except Exception as e:
            print(f"[ERROR] Connect failed: {e}")
            return False
    
    def close(self):
        if self.cursor:
            self.cursor.close()
        if self.conn:
            self.conn.close()

    def query(self, sql):
        try:
            self.cursor.execute(sql)
            return self.cursor.fetchall()
        except Exception as e:
            print(f"[ERROR] Query failed: {e}")
            return None

    def execute(self, sql):
        try:
            self.cursor.execute(sql)
            return True
        except Exception as e:
            print(f"[ERROR] Execute failed: {e}")
            return False


def fetch_lightcurve(client, source_id):
    """获取光变曲线数据"""
    sql = f"SELECT ts, mag, mag_error FROM {client.db_name}.sensor_data WHERE source_id = {source_id} ORDER BY ts ASC"
    rows = client.query(sql)
    
    if not rows or len(rows) < 5:
        return None
        
    df = pd.DataFrame(rows, columns=['ts', 'mag', 'mag_error'])
    
    try:
        ts_vals = pd.to_datetime(df['ts'])
        t0 = ts_vals.min()
        t = ((ts_vals - t0).dt.total_seconds() / 86400.0).values
    except:
        return None

    mag = df['mag'].values.astype(float)
    err = df['mag_error'].values.astype(float)
    
    mask = np.isfinite(t) & np.isfinite(mag) & np.isfinite(err)
    if mask.sum() < 5:
        return None
    return t[mask], mag[mask], err[mask]


def extract_features(fs, t, mag, err):
    """提取特征"""
    try:
        if len(t) < 5:
            return None
            
        feat_names, feat_values = fs.extract(time=t, magnitude=mag, error=err)
        feat_dict = dict(zip(feat_names, feat_values))
        
        results = []
        for feat in SELECTED_FEATURES:
            val = feat_dict.get(feat, 0.0)
            if hasattr(val, '__len__') and not isinstance(val, str):
                val = val[0] if len(val) > 0 else 0.0
            results.append(float(val) if np.isfinite(val) else 0.0)
            
        return results
    except Exception:
        return None


def update_class_in_db(client, source_id, new_class, healpix_id=None):
    """更新数据库中的分类结果"""
    table_name = ""
    if healpix_id is not None and int(healpix_id) != 0:
        table_name = f"sensor_data_{healpix_id}_{source_id}"
    else:
        try:
            sql = f"SELECT tbname FROM {client.db_name}.sensor_data WHERE source_id={source_id} LIMIT 1"
            rows = client.query(sql)
            if rows and len(rows) > 0:
                table_name = rows[0][0]
                if "." in table_name:
                    table_name = table_name.split(".")[-1]
            else:
                return False
        except:
            return False
            
    sql = f"ALTER TABLE {client.db_name}.{table_name} SET TAG cls = '{new_class}'"
    return client.execute(sql)


def process_batch(client, fs, model, idx_to_class, candidates, batch_idx, total_batches, threshold):
    """处理一批候选"""
    results = []
    total = len(candidates)
    updated_count = 0
    
    for i, cand in enumerate(candidates):
        if check_stop():
            return results, updated_count, True
        
        source_id = cand['source_id']
        healpix_id = cand.get('healpix_id', 0)
        ra = cand.get('ra', 0)
        dec = cand.get('dec', 0)
        
        # 获取光变曲线
        lc_data = fetch_lightcurve(client, source_id)
        if lc_data is None:
            continue
        
        t, mag, err = lc_data
        
        # 提取特征
        feats = extract_features(fs, t, mag, err)
        if feats is None:
            continue
        
        # 分类
        feats_arr = np.array(feats).reshape(1, -1)
        probs = model.predict_proba(feats_arr)[0]
        max_idx = np.argmax(probs)
        
        pred_idx = model.classes_[max_idx] if hasattr(model, 'classes_') else max_idx
        pred_class = idx_to_class.get(pred_idx, str(pred_idx))
        confidence = float(probs[max_idx])
        
        result = {
            'source_id': str(source_id),
            'healpix_id': healpix_id,
            'ra': ra,
            'dec': dec,
            'prediction': pred_class,
            'confidence': round(confidence, 4),
            'data_points': len(t),
            'reason': cand.get('reason', 'unknown'),
            'updated': False
        }
        
        # 高置信度时更新数据库
        if confidence >= threshold and UPDATE_DATABASE:
            if update_class_in_db(client, source_id, pred_class, healpix_id):
                result['updated'] = True
                updated_count += 1
        
        results.append(result)
        
        # 更新进度
        batch_progress = (i + 1) / total
        overall_progress = ((batch_idx - 1) + batch_progress) / total_batches * 100
        update_progress(
            int(overall_progress),
            f"批次 {batch_idx}/{total_batches}: 处理 {i+1}/{total}",
            "running",
            {
                "current_batch": batch_idx,
                "total_batches": total_batches,
                "batch_progress": int(batch_progress * 100),
                "processed": i + 1,
                "batch_total": total,
                "updated": updated_count
            }
        )
    
    return results, updated_count, False


def run_auto_classify(candidate_file, db_name, threshold, resume=False):
    """运行自动分类"""
    update_progress(0, "初始化中...", "running")
    
    # 检查候选文件
    if not os.path.exists(candidate_file):
        update_progress(0, "候选文件不存在", "error")
        return 1
    
    # 加载模型
    update_progress(1, "加载模型...", "running")
    if not os.path.exists(MODEL_PATH):
        update_progress(0, "模型文件不存在", "error")
        return 1
    
    model = joblib.load(MODEL_PATH)
    
    # 加载类别映射
    model_dir = os.path.dirname(MODEL_PATH)
    metadata_path = os.path.join(model_dir, 'metadata.pkl')
    if os.path.exists(metadata_path):
        with open(metadata_path, 'rb') as f:
            metadata = pickle.load(f)
        class_map = metadata.get('class_map', {c: i for i, c in enumerate(ALL_CLASSES)})
        idx_to_class = {v: k for k, v in class_map.items()}
    else:
        idx_to_class = {i: c for i, c in enumerate(ALL_CLASSES)}
    
    # 初始化特征提取器
    update_progress(2, "初始化特征提取器...", "running")
    fs = feets.FeatureSpace(data=['time', 'magnitude', 'error'], only=SELECTED_FEATURES)
    
    # 读取候选列表
    update_progress(3, "读取候选列表...", "running")
    try:
        df = pd.read_csv(candidate_file)
        candidates = df.to_dict('records')
    except Exception as e:
        update_progress(0, f"读取候选文件失败: {e}", "error")
        return 1
    
    if not candidates:
        update_progress(100, "没有候选需要处理", "completed")
        return 0
    
    total_candidates = len(candidates)
    print(f"[INFO] 共 {total_candidates} 个候选待分类")
    
    # 分批
    batches = [candidates[i:i+BATCH_SIZE] for i in range(0, total_candidates, BATCH_SIZE)]
    total_batches = len(batches)
    print(f"[INFO] 分为 {total_batches} 批，每批最多 {BATCH_SIZE} 个")
    
    # 检查是否需要恢复
    start_batch = 0
    all_results = []
    
    if resume:
        state = load_state()
        if state and state.get('candidate_file') == candidate_file:
            start_batch = state.get('completed_batch', 0)
            all_results = state.get('results', [])
            print(f"[INFO] 从批次 {start_batch + 1} 恢复")
    
    # 连接数据库
    update_progress(5, "连接数据库...", "running")
    client = TDengineClient(DB_HOST, DB_PORT, db_name=db_name)
    if not client.connect():
        update_progress(0, "数据库连接失败", "error")
        return 1
    
    # 处理每一批
    total_updated = 0
    stopped = False
    
    for batch_idx in range(start_batch, total_batches):
        if check_stop():
            stopped = True
            break
        
        batch = batches[batch_idx]
        print(f"[INFO] 处理批次 {batch_idx + 1}/{total_batches} ({len(batch)} 个)")
        
        results, updated, should_stop = process_batch(
            client, fs, model, idx_to_class, batch,
            batch_idx + 1, total_batches, threshold
        )
        
        all_results.extend(results)
        total_updated += updated
        
        # 保存状态（断点续传）
        save_state({
            'candidate_file': candidate_file,
            'completed_batch': batch_idx + 1,
            'results': all_results,
            'total_updated': total_updated
        })
        
        if should_stop:
            stopped = True
            break
    
    client.close()
    
    # 保存结果
    result_file = candidate_file.replace('.csv', '_results.json')
    output_data = {
        "results": all_results,
        "count": len(all_results),
        "total_candidates": total_candidates,
        "updated_count": total_updated,
        "threshold": threshold,
        "completed": not stopped,
        "timestamp": int(time.time())
    }
    
    with open(result_file, 'w') as f:
        json.dump(output_data, f, indent=2)
    
    # 如果完成，清空候选文件并删除状态
    if not stopped:
        clear_state()
        # 清空候选文件（保留表头）
        try:
            with open(candidate_file, 'r') as f:
                header = f.readline()
            with open(candidate_file, 'w') as f:
                f.write(header)
        except:
            pass
        
        update_progress(100, f"完成: {len(all_results)} 个分类, {total_updated} 个更新", "completed")
        print(f"[INFO] 完成: {len(all_results)} 个分类, {total_updated} 个更新")
    else:
        update_progress(
            int((batch_idx + 1) / total_batches * 100),
            f"已暂停: 完成 {batch_idx + 1}/{total_batches} 批",
            "paused"
        )
        print(f"[INFO] 已暂停: 完成 {batch_idx + 1}/{total_batches} 批")
    
    return 0


def main():
    global BATCH_SIZE
    
    parser = argparse.ArgumentParser(description='自动分类流水线')
    parser.add_argument('--candidate-file', required=True, help='候选文件路径')
    parser.add_argument('--db', default=DB_NAME, help='数据库名')
    parser.add_argument('--threshold', type=float, default=CONFIDENCE_THRESHOLD, help='置信度阈值')
    parser.add_argument('--resume', action='store_true', help='从上次中断处恢复')
    parser.add_argument('--batch-size', type=int, default=BATCH_SIZE, help='每批处理数量')
    
    args = parser.parse_args()
    
    BATCH_SIZE = args.batch_size
    
    # 清除停止信号
    if os.path.exists(STOP_FILE):
        os.remove(STOP_FILE)
    
    return run_auto_classify(
        args.candidate_file,
        args.db,
        args.threshold,
        args.resume
    )


if __name__ == "__main__":
    sys.exit(main())

