#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TDlight Training HTTP Server
Lightweight Flask service for incremental training.
Runs on port 5002 (or PORT env var).

Endpoints:
  POST /api/train/upload     - Upload CSV/ZIP file
  POST /api/train/start      - Start feature extraction / training
  GET  /api/train/stream     - SSE progress stream
  POST /api/train/stop       - Stop running training
  GET  /api/train/summary    - Current training data summary
"""

import os
import sys
import json
import time
import subprocess
import signal
from pathlib import Path
from threading import Thread

from flask import Flask, request, jsonify, Response
from flask_cors import CORS
from werkzeug.utils import secure_filename

sys.path.insert(0, str(Path(__file__).parent))
from training_data_manager import get_training_summary, clear_training_data

app = Flask(__name__)
CORS(app)

# Config
UPLOAD_FOLDER = Path("/tmp/tdlight_train_uploads")
UPLOAD_FOLDER.mkdir(parents=True, exist_ok=True)
MODEL_DIR = Path(__file__).parent.parent / "models" / "hierarchical_unlimited"
PYTHON_PATH = os.environ.get("PYTHON_EXECUTABLE", sys.executable)
PROJECT_ROOT = Path(__file__).parent.parent

# Global process handle for training subprocess
current_train_proc = None

# Valid labels
ALL_CLASSES = ['Non-var', 'ROT', 'EA', 'EW', 'CEP', 'DSCT', 'RRAB', 'RRC', 'M', 'SR', 'EB']


def _do_export_onnx(submodels=None):
    """Export ONNX for given sub-models (or all if None). Runs in background thread."""
    import joblib
    import onnxmltools
    from onnxmltools.convert.common.data_types import FloatTensorType
    
    targets = submodels or ['init', 'variable', 'extrinsic', 'intrinsic', 'eb', 'rr', 'lpv']
    results = {}
    for name in targets:
        pkl_path = MODEL_DIR / f"{name}.pkl"
        onnx_path = MODEL_DIR / f"{name}.onnx"
        if not pkl_path.exists():
            results[name] = "pkl not found"
            continue
        try:
            m = joblib.load(pkl_path)
            initial_type = [('float_input', FloatTensorType([None, 15]))]
            onnx_model = onnxmltools.convert_lightgbm(m, initial_types=initial_type, zipmap=False)
            with open(onnx_path, 'wb') as f:
                f.write(onnx_model.SerializeToString())
            results[name] = "ok"
        except Exception as e:
            results[name] = str(e)
    return results


@app.route('/api/train/upload', methods=['POST'])
def upload_file():
    """Receive uploaded CSV/ZIP file."""
    if 'file' not in request.files:
        return jsonify({"success": False, "error": "No file part"}), 400
    file = request.files['file']
    if file.filename == '':
        return jsonify({"success": False, "error": "Empty filename"}), 400

    filename = secure_filename(file.filename)
    timestamp = str(int(time.time()))
    saved_name = f"{timestamp}_{filename}"
    filepath = UPLOAD_FOLDER / saved_name
    file.save(filepath)

    return jsonify({"success": True, "filename": saved_name, "path": str(filepath)})


@app.route('/api/train/start', methods=['POST'])
def start_training():
    """Start incremental training pipeline."""
    global current_train_proc

    data = request.get_json(force=True, silent=True) or {}
    filepath = data.get('filepath', '')
    label = data.get('label', '')
    do_train = data.get('train', False)
    task_id = data.get('task_id', f"train_{int(time.time())}")

    if not filepath or not Path(filepath).exists():
        return jsonify({"success": False, "error": "File not found. Upload first."}), 400
    
    # Support auto-detect mode (from 'class' column in CSV)
    auto_detect = (label == 'auto' or not label)
    if not auto_detect and label not in ALL_CLASSES:
        return jsonify({"success": False, "error": f"Invalid label. Must be one of: {ALL_CLASSES} or 'auto'"}), 400

    # Clean old progress
    progress_file = "/tmp/train_progress.json"
    stop_file = "/tmp/train_stop"
    for f in (progress_file, stop_file):
        if Path(f).exists():
            Path(f).unlink()

    # Write initial progress
    with open(progress_file, 'w') as f:
        json.dump({"percent": 1, "message": "Initializing...", "step": "init", "task_id": task_id}, f)

    # Build command
    script_path = PROJECT_ROOT / "class" / "incremental_train.py"
    cmd = [
        PYTHON_PATH, str(script_path),
        "--input", filepath,
        "--label", label if not auto_detect else "auto",
        "--model-dir", str(MODEL_DIR),
        "--task-id", task_id,
        "--progress-file", progress_file,
    ]
    if do_train:
        cmd.append("--train")

    # Start subprocess
    env = os.environ.copy()
    env["LD_LIBRARY_PATH"] = str(PROJECT_ROOT / "libs") + ":" + env.get("LD_LIBRARY_PATH", "")

    current_train_proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        cwd=str(PROJECT_ROOT / "class")
    )

    return jsonify({"success": True, "task_id": task_id, "message": "Training started"})


@app.route('/api/train/stream')
def train_stream():
    """SSE stream for training progress."""
    task_id = request.args.get('task_id', '')
    progress_file = "/tmp/train_progress.json"

    def generate():
        last_data = ""
        last_heartbeat = time.time()
        while True:
            data = ""
            if Path(progress_file).exists():
                try:
                    with open(progress_file, 'r') as f:
                        content = f.read()
                        if content:
                            data = content
                except Exception:
                    pass
            if not data:
                data = '{"percent":0, "message":"Waiting...", "step":""}'

            if data != last_data:
                yield f"data: {data}\n\n"
                last_data = data
                last_heartbeat = time.time()

            # Check completion
            if '"percent": 100' in data or '"percent":100' in data or '"step":"done"' in data or '"step":"stopped"' in data or '"step":"error"' in data:
                time.sleep(0.5)
                break

            # Heartbeat
            if time.time() - last_heartbeat >= 2:
                yield ": keep-alive\n\n"
                last_heartbeat = time.time()

            time.sleep(0.1)

    return Response(generate(), mimetype='text/event-stream',
                    headers={"Cache-Control": "no-cache", "Connection": "keep-alive"})


@app.route('/api/train/stop', methods=['POST'])
def stop_training():
    """Signal training to stop."""
    global current_train_proc

    with open("/tmp/train_stop", 'w') as f:
        f.write("stop\n")

    if current_train_proc and current_train_proc.poll() is None:
        current_train_proc.terminate()
        try:
            current_train_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            current_train_proc.kill()

    return jsonify({"success": True, "message": "Training stop signal sent"})


@app.route('/api/train/summary', methods=['GET'])
def training_summary():
    """Return current accumulated training data summary."""
    try:
        summary = get_training_summary()
        return jsonify({"success": True, "summary": summary})
    except Exception as e:
        return jsonify({"success": False, "error": str(e)}), 500


@app.route('/api/train/clear', methods=['POST'])
def clear_data():
    """Clear all accumulated training data."""
    try:
        clear_training_data()
        return jsonify({"success": True, "message": "Training data cleared"})
    except Exception as e:
        return jsonify({"success": False, "error": str(e)}), 500


@app.route('/api/train/labels', methods=['GET'])
def get_labels():
    """Return valid class labels."""
    return jsonify({"success": True, "labels": ALL_CLASSES})


@app.route('/api/train/export_onnx', methods=['POST'])
def export_onnx():
    """Manually trigger ONNX export for current .pkl models."""
    try:
        data = request.get_json(silent=True) or {}
        submodels = data.get('submodels')
        if submodels and isinstance(submodels, str):
            submodels = [s.strip() for s in submodels.split(',') if s.strip()]
        
        # Run in background thread so API returns immediately
        def bg():
            res = _do_export_onnx(submodels)
            print(f"[ONNX Export] Results: {res}")
        Thread(target=bg, daemon=True).start()
        
        return jsonify({"success": True, "message": "ONNX export started in background", "submodels": submodels or ["all"]})
    except Exception as e:
        return jsonify({"success": False, "error": str(e)}), 500


def run_server(port=5002, host='0.0.0.0'):
    print(f"[TrainServer] Starting on http://{host}:{port}")
    app.run(host=host, port=port, threaded=True, debug=False)


if __name__ == "__main__":
    port = int(os.environ.get("TRAIN_PORT", 5002))
    run_server(port=port)
