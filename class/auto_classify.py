#!/usr/bin/env python3
"""
Auto-classification Pipeline - Batch process candidate light curves detected during import
Features:
- Batch processing (5000 per batch)
- Checkpoint resume
- Background execution
- Progress reporting
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

# ==================== Configuration ====================
CONFIG_FILE = "../config.json"
PROGRESS_FILE = "/tmp/auto_classify_progress.json"
STATE_FILE = "/tmp/auto_classify_state.json"
STOP_FILE = "/tmp/auto_classify_stop"

# Default configuration
DB_HOST = "localhost"
DB_PORT = 6041
DB_NAME = "gaiadr2_lc"
MODEL_PATH = "../classifier/lgbm_111w_model.pkl"
METADATA_PATH = "../classifier/metadata.pkl"
CONFIDENCE_THRESHOLD = 0.95
UPDATE_DATABASE = True
BATCH_SIZE = 5000

# Feature list
SELECTED_FEATURES = [
    'PeriodLS', 'Mean', 'Rcs', 'Psi_eta', 'StetsonK_AC',
    'Gskew', 'Psi_CS', 'Skew', 'Freq1_harmonics_amplitude_1', 'Eta_e',
    'LinearTrend', 'Freq1_harmonics_amplitude_0', 'AndersonDarling', 'MaxSlope', 'StetsonK'
]

ALL_CLASSES = ['Non-var', 'ROT', 'EA', 'EW', 'CEP', 'DSCT', 'RRAB', 'RRC', 'M', 'SR']


def load_config():
    """Load configuration file"""
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
    """Update progress file"""
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
    """Save state (for checkpoint resume)"""
    try:
        with open(STATE_FILE, "w") as f:
            json.dump(state, f)
    except:
        pass


def load_state():
    """Load previous state"""
    try:
        if os.path.exists(STATE_FILE):
            with open(STATE_FILE, "r") as f:
                return json.load(f)
    except:
        pass
    return None


def clear_state():
    """Clear state file"""
    try:
        if os.path.exists(STATE_FILE):
            os.remove(STATE_FILE)
    except:
        pass


def check_stop():
    """Check if stop requested"""
    if os.path.exists(STOP_FILE):
        return True
    return False


class TDengineClient:
    """TDengine native client"""
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
    """Fetch light curve data"""
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
    """Extract features"""
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
    """Update classification result in database"""
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
    """Process a batch of candidates"""
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
        
        # Fetch light curve
        lc_data = fetch_lightcurve(client, source_id)
        if lc_data is None:
            continue
        
        t, mag, err = lc_data
        
        # Extract features
        feats = extract_features(fs, t, mag, err)
        if feats is None:
            continue
        
        # Classify
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
        
        # Update database for high confidence predictions
        if confidence >= threshold and UPDATE_DATABASE:
            if update_class_in_db(client, source_id, pred_class, healpix_id):
                result['updated'] = True
                updated_count += 1
        
        results.append(result)
        
        # Update progress
        batch_progress = (i + 1) / total
        overall_progress = ((batch_idx - 1) + batch_progress) / total_batches * 100
        update_progress(
            int(overall_progress),
            f"Batch {batch_idx}/{total_batches}: Processing {i+1}/{total}",
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
    """Run auto-classification"""
    update_progress(0, "Initializing...", "running")
    
    # Check candidate file
    if not os.path.exists(candidate_file):
        update_progress(0, "Candidate file not found", "error")
        return 1
    
    # Load model
    update_progress(1, "Loading model...", "running")
    if not os.path.exists(MODEL_PATH):
        update_progress(0, "Model file not found", "error")
        return 1
    
    model = joblib.load(MODEL_PATH)
    
    # Load class mapping
    model_dir = os.path.dirname(MODEL_PATH)
    metadata_path = os.path.join(model_dir, 'metadata.pkl')
    if os.path.exists(metadata_path):
        with open(metadata_path, 'rb') as f:
            metadata = pickle.load(f)
        class_map = metadata.get('class_map', {c: i for i, c in enumerate(ALL_CLASSES)})
        idx_to_class = {v: k for k, v in class_map.items()}
    else:
        idx_to_class = {i: c for i, c in enumerate(ALL_CLASSES)}
    
    # Initialize feature extractor
    update_progress(2, "Initializing feature extractor...", "running")
    fs = feets.FeatureSpace(data=['time', 'magnitude', 'error'], only=SELECTED_FEATURES)
    
    # Read candidate list
    update_progress(3, "Reading candidate list...", "running")
    try:
        df = pd.read_csv(candidate_file)
        candidates = df.to_dict('records')
    except Exception as e:
        update_progress(0, f"Failed to read candidate file: {e}", "error")
        return 1
    
    if not candidates:
        update_progress(100, "No candidates to process", "completed")
        return 0
    
    total_candidates = len(candidates)
    print(f"[INFO] Total {total_candidates} candidates to classify")
    
    # Split into batches
    batches = [candidates[i:i+BATCH_SIZE] for i in range(0, total_candidates, BATCH_SIZE)]
    total_batches = len(batches)
    print(f"[INFO] Split into {total_batches} batches, max {BATCH_SIZE} per batch")
    
    # Check if resume needed
    start_batch = 0
    all_results = []
    
    if resume:
        state = load_state()
        if state and state.get('candidate_file') == candidate_file:
            start_batch = state.get('completed_batch', 0)
            all_results = state.get('results', [])
            print(f"[INFO] Resuming from batch {start_batch + 1}")
    
    # Connect to database
    update_progress(5, "Connecting to database...", "running")
    client = TDengineClient(DB_HOST, DB_PORT, db_name=db_name)
    if not client.connect():
        update_progress(0, "Database connection failed", "error")
        return 1
    
    # Process each batch
    total_updated = 0
    stopped = False
    
    for batch_idx in range(start_batch, total_batches):
        if check_stop():
            stopped = True
            break
        
        batch = batches[batch_idx]
        print(f"[INFO] Processing batch {batch_idx + 1}/{total_batches} ({len(batch)} items)")
        
        results, updated, should_stop = process_batch(
            client, fs, model, idx_to_class, batch,
            batch_idx + 1, total_batches, threshold
        )
        
        all_results.extend(results)
        total_updated += updated
        
        # Save state (for checkpoint resume)
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
    
    # Save results
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
    
    # If completed, clear candidate file and remove state
    if not stopped:
        clear_state()
        # Clear candidate file (keep header)
        try:
            with open(candidate_file, 'r') as f:
                header = f.readline()
            with open(candidate_file, 'w') as f:
                f.write(header)
        except:
            pass
        
        update_progress(100, f"Done: {len(all_results)} classified, {total_updated} updated", "completed")
        print(f"[INFO] Done: {len(all_results)} classified, {total_updated} updated")
    else:
        update_progress(
            int((batch_idx + 1) / total_batches * 100),
            f"Paused: Completed {batch_idx + 1}/{total_batches} batches",
            "paused"
        )
        print(f"[INFO] Paused: Completed {batch_idx + 1}/{total_batches} batches")
    
    return 0


def main():
    global BATCH_SIZE
    
    parser = argparse.ArgumentParser(description='Auto-classification Pipeline')
    parser.add_argument('--candidate-file', required=True, help='Candidate file path')
    parser.add_argument('--db', default=DB_NAME, help='Database name')
    parser.add_argument('--threshold', type=float, default=CONFIDENCE_THRESHOLD, help='Confidence threshold')
    parser.add_argument('--resume', action='store_true', help='Resume from last checkpoint')
    parser.add_argument('--batch-size', type=int, default=BATCH_SIZE, help='Batch size')
    
    args = parser.parse_args()
    
    BATCH_SIZE = args.batch_size
    
    # Clear stop signal
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

