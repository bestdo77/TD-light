#!/usr/bin/env python3
"""
TDengine On-Demand Classification Script
"""

import sys
import os
import json
import io
import warnings
import traceback

# Suppress warnings
warnings.filterwarnings('ignore')

# Redirect stdout to capture JSON output cleanly
original_stdout = sys.stdout
sys.stdout = io.StringIO()

try:
    import numpy as np
    import pandas as pd
    import taos
    import pickle
    import joblib
    import feets
except ImportError as e:
    sys.stdout = original_stdout
    print(json.dumps({"error": f"Import failed: {str(e)}"}))
    sys.exit(1)

# Configuration
PROGRESS_FILE = "/tmp/classify_progress.json"
STOP_FILE = "/tmp/classify_stop"

# Model Configuration
# Note: Paths should ideally come from a config file, keeping hardcoded for compatibility with existing deployment
MODEL_DIR = "../classifier"
MODEL_PATH = os.path.join(MODEL_DIR, "lgbm_111w_model.pkl")
METADATA_PATH = os.path.join(MODEL_DIR, "metadata.pkl")

SELECTED_FEATURES = [
    'PeriodLS', 'Mean', 'Rcs', 'Psi_eta', 'StetsonK_AC',
    'Gskew', 'Psi_CS', 'Skew', 'Freq1_harmonics_amplitude_1', 'Eta_e',
    'LinearTrend', 'Freq1_harmonics_amplitude_0', 'AndersonDarling', 'MaxSlope', 'StetsonK'
]

ALL_CLASSES = ['Non-var', 'ROT', 'EA', 'EW', 'CEP', 'DSCT', 'RRAB', 'RRC', 'M', 'SR']

def update_progress(percent, message):
    try:
        with open(PROGRESS_FILE, "w") as f:
            json.dump({"percent": percent, "message": message}, f)
    except:
        pass

def check_stop():
    if os.path.exists(STOP_FILE):
        os.remove(STOP_FILE)
        return True
    return False

def clear_stop():
    if os.path.exists(STOP_FILE):
        os.remove(STOP_FILE)

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

def run_classification():
    if len(sys.argv) < 3:
        return {"error": "Missing arguments. Usage: on_demand_classify.py <limit> <database> [objects_json_file]"}

    db_name = sys.argv[2]
    taos_host = os.environ.get('TAOS_HOST', 'localhost')
    
    # Input handling
    ids_json = None
    if len(sys.argv) > 3:
        arg3 = sys.argv[3]
        if arg3.startswith('@'):
            filepath = arg3[1:]
            try:
                with open(filepath, 'r') as f:
                    ids_json = f.read()
            except Exception as e:
                return {"error": f"Failed to read IDs file: {str(e)}"}
        else:
            ids_json = arg3

    update_progress(5, "Connecting to database...")
    
    try:
        conn = taos.connect(host=taos_host, user='root', password='taosdata', database=db_name, port=6030)
        cursor = conn.cursor()
    except Exception as e:
        return {"error": f"Database connection failed: {str(e)}"}

    # Fetch objects
    try:
        batch = []
        
        if ids_json:
            update_progress(10, "Retrieving objects...")
            try:
                objects = json.loads(ids_json)
                for obj in objects:
                    batch.append({
                        'healpix_id': int(obj['healpix_id']),
                        'source_id': int(obj['source_id']),
                        'ra': obj.get('ra'),
                        'dec': obj.get('dec')
                    })
            except Exception as e:
                return {"error": f"JSON parse error: {str(e)}"}
        else:
            limit = int(sys.argv[1])
            update_progress(10, "Random sampling...")
            
            query = f"""SELECT healpix_id, source_id, FIRST(ra) as ra, FIRST(dec) as dec 
                       FROM sensor_data 
                       GROUP BY healpix_id, source_id 
                       LIMIT {limit}"""
            cursor.execute(query)
            rows = cursor.fetchall()
            
            if not rows:
                return {"error": "No objects found"}

            for row in rows:
                batch.append({
                    'healpix_id': int(row[0]),
                    'source_id': int(row[1]),
                    'ra': row[2],
                    'dec': row[3]
                })
        
        update_progress(15, f"Retrieved {len(batch)} objects")
        
        if not batch:
            return {"error": "No valid objects"}

        # Load Model
        update_progress(20, "Loading model...")
        
        if not os.path.exists(MODEL_PATH):
            return {"error": f"Model not found: {MODEL_PATH}"}
        
        model = joblib.load(MODEL_PATH)
        
        # Load metadata
        if os.path.exists(METADATA_PATH):
            with open(METADATA_PATH, 'rb') as f:
                metadata = pickle.load(f)
            class_map = metadata.get('class_map', {c: i for i, c in enumerate(ALL_CLASSES)})
            idx_to_class = {v: k for k, v in class_map.items()}
        else:
            idx_to_class = {i: c for i, c in enumerate(ALL_CLASSES)}
        
        update_progress(25, "Model loaded")
        
        # Feature Extraction & Classification
        fs = feets.FeatureSpace(data=['time', 'magnitude', 'error'], only=SELECTED_FEATURES)
        
        total_objects = len(batch)
        results = []
        clear_stop()
        
        for i, item in enumerate(batch):
            if check_stop():
                update_progress(0, "Stopped")
                cursor.close()
                conn.close()
                return {"stopped": True, "message": "User stopped classification", "processed": i}
            
            pct = 28 + int(52 * (i + 1) / total_objects)
            update_progress(pct, f"Processing: {i+1}/{total_objects}")
            
            source_id = item['source_id']
            healpix_id = item['healpix_id']
            
            query = f"""SELECT ts, mag, mag_error 
                       FROM sensor_data 
                       WHERE healpix_id = {healpix_id} AND source_id = {source_id} 
                       ORDER BY ts"""
            cursor.execute(query)
            rows = cursor.fetchall()
            
            if not rows or len(rows) < 10:
                continue
            
            df = pd.DataFrame(rows, columns=['ts', 'mag', 'mag_error'])
            df['ts'] = pd.to_datetime(df['ts'])
            t0 = df['ts'].min()
            time_days = ((df['ts'] - t0).dt.total_seconds() / 86400.0).values
            mag = df['mag'].values.astype(float)
            err = df['mag_error'].values.astype(float)
            
            mask = np.isfinite(time_days) & np.isfinite(mag) & np.isfinite(err)
            time_days = time_days[mask]
            mag = mag[mask]
            err = err[mask]
            
            if len(time_days) < 10:
                continue
            
            feats = extract_features(fs, time_days, mag, err)
            if feats is None:
                continue
            
            feats_arr = np.array(feats).reshape(1, -1)
            probs = model.predict_proba(feats_arr)[0]
            max_idx = np.argmax(probs)
            pred_idx = model.classes_[max_idx] if hasattr(model, 'classes_') else max_idx
            pred_class = idx_to_class.get(pred_idx, str(pred_idx))
            confidence = probs[max_idx]
            
            results.append({
                "source_id": str(source_id),
                "healpix_id": healpix_id,
                "ra": item.get('ra'),
                "dec": item.get('dec'),
                "prediction": pred_class,
                "confidence": round(float(confidence), 4),
                "data_points": len(time_days)
            })
        
        update_progress(85, f"Completed: {len(results)}/{total_objects}")
        
        cursor.close()
        conn.close()
        
        # Save results
        limit = len(batch)
        result_file = f"/tmp/classify_results_{limit}.json"
        with open(result_file, 'w') as f:
            json.dump({"results": results, "count": len(results)}, f, indent=2)
        
        update_progress(100, "Done")
        
        return {
            "results": results, 
            "count": len(results),
            "model": "lgbm_111w_15features_tuned",
            "saved_to": result_file
        }

    except Exception as e:
        return {"error": f"Execution error: {str(e)}", "trace": traceback.format_exc()}

if __name__ == "__main__":
    try:
        result = run_classification()
        # Restore stdout and print result
        sys.stdout = original_stdout
        print(json.dumps(result))
    except Exception as e:
        sys.stdout = original_stdout
        print(json.dumps({"error": f"Fatal error: {str(e)}", "trace": traceback.format_exc()}))
