#!/usr/bin/env python3
"""
TDengine + LightGBM Classification Pipeline
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

warnings.filterwarnings('ignore')

CONFIG_FILE = "../config.json"
PROGRESS_FILE = "/tmp/class_progress.json"

# Defaults
DB_HOST = "localhost"
DB_PORT = 6030
DB_NAME = "gaiadr2_lc"
SUPER_TABLE = "sensor_data"
MODEL_PATH = "../models/lgbm_111w_model.pkl"
METADATA_PATH = "../models/metadata.pkl"
CONFIDENCE_THRESHOLD = 0.95
UPDATE_DATABASE = True

def load_config():
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

# Rate limiting for progress updates
LAST_UPDATE_TIME = 0
TASK_ID = ""

def update_progress(percent, message, step=""):
    global LAST_UPDATE_TIME
    current_time = time.time()
    
    # Rate limit: max 1 update per 100ms unless complete/error
    if percent != 100 and percent != 0 and (current_time - LAST_UPDATE_TIME < 0.1):
        return

    try:
        data = {"percent": percent, "message": message, "step": step, "task_id": TASK_ID}
        tmp_file = PROGRESS_FILE + ".tmp"
        with open(tmp_file, "w") as f:
            json.dump(data, f)
            f.flush()
            os.fsync(f.fileno())
        os.rename(tmp_file, PROGRESS_FILE)
        LAST_UPDATE_TIME = current_time
    except:
        pass

STOP_FILE = "/tmp/classify_stop"

def check_stop():
    if os.path.exists(STOP_FILE):
        update_progress(0, "Stopped", "stopped")
        return True
    return False

SELECTED_FEATURES = [
    'PeriodLS', 'Mean', 'Rcs', 'Psi_eta', 'StetsonK_AC',
    'Gskew', 'Psi_CS', 'Skew', 'Freq1_harmonics_amplitude_1', 'Eta_e',
    'LinearTrend', 'Freq1_harmonics_amplitude_0', 'AndersonDarling', 'MaxSlope', 'StetsonK'
]

ALL_CLASSES = ['Non-var', 'ROT', 'EA', 'EW', 'CEP', 'DSCT', 'RRAB', 'RRC', 'M', 'SR']


class TDengineNativeClient:
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


def update_tdengine_class(client, db_name, super_table, source_id, new_class, healpix_id=None):
    table_name = ""
    if healpix_id is not None and int(healpix_id) != 0:
        table_name = f"sensor_data_{healpix_id}_{source_id}"
    else:
        # Try to find table name from super table
        if super_table is None:
            super_table = "sensor_data"
        try:
            sql = f"SELECT tbname FROM {db_name}.{super_table} WHERE source_id={source_id} LIMIT 1"
            rows = client.query(sql)
            if rows and len(rows) > 0:
                table_name = rows[0][0]
                # tbname returned by taos might be fully qualified or just table name
                if "." in table_name:
                    table_name = table_name.split(".")[-1]
            else:
                print(f"[WARN] Could not find table for source_id={source_id}")
                return False
        except Exception as e:
            print(f"[ERROR] Failed to query table name: {e}")
            return False
            
    sql = f"ALTER TABLE {db_name}.{table_name} SET TAG cls = '{new_class}'"
    result = client.execute(sql)
    return result is not None


def run_web_mode(input_file, output_file, db_name, host, port, threshold):
    update_progress(2, "Loading model...", "extract")
    
    if not os.path.exists(MODEL_PATH):
        update_progress(0, "Model file not found", "error")
        return 1
    model = joblib.load(MODEL_PATH)
    
    model_dir = os.path.dirname(MODEL_PATH)
    metadata_path = os.path.join(model_dir, 'metadata.pkl')
    
    if os.path.exists(metadata_path):
        with open(metadata_path, 'rb') as f:
            metadata = pickle.load(f)
        class_map = metadata.get('class_map', {c: i for i, c in enumerate(ALL_CLASSES)})
        idx_to_class = {v: k for k, v in class_map.items()}
    else:
        idx_to_class = {i: c for i, c in enumerate(ALL_CLASSES)}

    update_progress(4, "Initializing feature extractor...", "extract")
    fs = feets.FeatureSpace(data=['time', 'magnitude', 'error'], only=SELECTED_FEATURES)

    update_progress(5, "Reading input list...", "extract")
    
    try:
        samples = []
        with open(input_file, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                parts = [p.strip().strip('"') for p in line.split(',')]
                if len(parts) >= 2:
                    samples.append({
                        'source_id': int(parts[0]),
                        'healpix_id': int(parts[1]) if len(parts) > 1 and parts[1] else 0,
                        'ra': float(parts[2]) if len(parts) > 2 and parts[2] else 0,
                        'dec': float(parts[3]) if len(parts) > 3 and parts[3] else 0
                    })
    except Exception as e:
        update_progress(0, f"Failed to read input: {e}", "error")
        return 1
    
    if not samples:
        update_progress(0, "No objects to classify", "error")
        return 1

    total = len(samples)
    update_progress(6, f"Connecting to DB ({total} objects)...", "extract")
    
    client = TDengineNativeClient(host, port, db_name=db_name)
    if not client.connect():
        update_progress(0, "Database connection failed", "error")
        return 1

    # Stage 1: Fetch lightcurves (6% - 33%)
    lightcurves = {}
    lc_count = 0
    
    for i, sample in enumerate(samples):
        if check_stop():
            client.close()
            return 0
        
        source_id = sample['source_id']
        lc_data = fetch_lightcurve(client, source_id)
        
        if lc_data is not None:
            lightcurves[source_id] = lc_data
            lc_count += 1
        
        pct = 6 + int(27 * (i + 1) / total)
        update_progress(pct, f"Fetching lightcurves: {i+1}/{total}, Loaded {lc_count}", "extract")
    
    client.close()
    
    if not lightcurves:
        update_progress(0, "No lightcurve data fetched", "error")
        return 1
    
    # Stage 2: Extract features (33% - 66%)
    features_data = {}
    feat_count = 0
    lc_ids = list(lightcurves.keys())
    lc_total = len(lc_ids)
    
    for i, source_id in enumerate(lc_ids):
        if check_stop():
            return 0
        
        t, mag, err = lightcurves[source_id]
        feats = extract_features(fs, t, mag, err)
        
        if feats is not None:
            features_data[source_id] = feats
            feat_count += 1
        
        pct = 33 + int(33 * (i + 1) / lc_total)
        update_progress(pct, f"Extracting features: {i+1}/{lc_total}, Success {feat_count}", "feature")
    
    if not features_data:
        update_progress(0, "No features extracted", "error")
        return 1
    
    # Stage 3: Classify (66% - 95%)
    results = []
    feat_ids = list(features_data.keys())
    feat_total = len(feat_ids)
    updated_count = 0
    
    client = TDengineNativeClient(host, port, db_name=db_name)
    if not client.connect():
        update_progress(0, "DB Reconnect failed", "error")
        return 1
    
    for i, source_id in enumerate(feat_ids):
        if check_stop():
            client.close()
            return 0
        
        sample = next((s for s in samples if s['source_id'] == source_id), {})
        
        feats = features_data[source_id]
        feats_arr = np.array(feats).reshape(1, -1)
        probs = model.predict_proba(feats_arr)[0]
        max_idx = np.argmax(probs)
        
        pred_idx = model.classes_[max_idx] if hasattr(model, 'classes_') else max_idx
        pred_class = idx_to_class.get(pred_idx, str(pred_idx))
        confidence = float(probs[max_idx])
        
        result = {
            'source_id': str(source_id),
            'ra': sample.get('ra'),
            'dec': sample.get('dec'),
            'healpix_id': sample.get('healpix_id'),
            'prediction': pred_class,
            'confidence': round(confidence, 4),
            'data_points': len(lightcurves[source_id][0]),
            'status': 'high_confidence' if confidence >= threshold else 'low_confidence',
            'updated': False
        }
        
        if confidence >= threshold and UPDATE_DATABASE:
            try:
                healpix_id = sample.get('healpix_id')
                update_tdengine_class(client, db_name, SUPER_TABLE, source_id, pred_class, healpix_id=healpix_id)
                result['updated'] = True
                updated_count += 1
            except Exception as ue:
                result['update_error'] = str(ue)
        
        results.append(result)
        
        pct = 66 + int(29 * (i + 1) / feat_total)
        update_progress(pct, f"Classifying: {i+1}/{feat_total}, Updated {updated_count}", "classify")
    
    client.close()
    
    high_conf_count = sum(1 for r in results if r.get('status') == 'high_confidence')
    
    update_progress(97, f"Saving results ({len(results)} items)...", "classify")
    
    output_data = {
        "results": results,
        "count": len(results),
        "lightcurves_fetched": lc_count,
        "features_extracted": feat_count,
        "high_confidence_count": high_conf_count,
        "updated_count": updated_count,
        "threshold": threshold,
        "model": "lgbm_111w_15features_tuned"
    }
    with open(output_file, 'w') as f:
        json.dump(output_data, f, indent=2)
    
    update_progress(100, f"Done: {len(results)} classified, {updated_count} updated", "done")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description='TDengine + LightGBM Classification Pipeline',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    
    parser.add_argument('--input', required=True, help='Input ID list file')
    parser.add_argument('--output', required=True, help='Output result file')
    parser.add_argument('--threshold', type=float, default=0.95, help='Confidence threshold')
    parser.add_argument('--db', default=DB_NAME, help='Database name')
    parser.add_argument('--host', default=DB_HOST, help='Database host')
    parser.add_argument('--port', type=int, default=DB_PORT, help='Database port')
    parser.add_argument('--web-mode', action='store_true', help='Web mode (background run)')
    parser.add_argument('--task-id', default="", help='Task ID')
    
    args = parser.parse_args()
    
    global TASK_ID
    TASK_ID = args.task_id
    
    if args.web_mode:
        return run_web_mode(
            args.input, args.output, 
            args.db, args.host, args.port, 
            args.threshold
        )
    else:
        print("Please run with --web-mode")
        return 1


if __name__ == "__main__":
    sys.exit(main())