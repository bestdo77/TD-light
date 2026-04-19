#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TDlight Incremental Training Script

Pipeline:
  1. Read CSV/ZIP light curves from user
  2. Extract 15 feets features
  3. Save features to models/training_data/
  4. (Optional) Incrementally train all 7 sub-models with new data
  5. Export updated .pkl and .onnx

Progress is written to /tmp/train_progress.json (SSE-compatible).
"""

import os
import sys
import json
import time
import warnings
import argparse
from pathlib import Path

import numpy as np
import joblib
import lightgbm as lgb

warnings.filterwarnings('ignore')

# Add parent dir to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from feature_extractor import extract_from_csv, SELECTED_FEATURES
from training_data_manager import (
    save_features,
    load_all_training_data,
    get_training_summary,
    backup_model_files,
    restore_model_files,
    SUBMODEL_CLASSES,
)

# Progress reporting (same format as classify_pipeline.py)
PROGRESS_FILE = "/tmp/train_progress.json"
LAST_UPDATE_TIME = 0


def update_progress(percent, message, step=""):
    global LAST_UPDATE_TIME
    current_time = time.time()
    if percent != 100 and percent != 0 and (current_time - LAST_UPDATE_TIME < 0.1):
        return
    try:
        data = {"percent": percent, "message": message, "step": step, "task_id": TASK_ID}
        tmp_file = PROGRESS_FILE + ".tmp"
        with open(tmp_file, "w") as f:
            json.dump(data, f)
        os.replace(tmp_file, PROGRESS_FILE)
        LAST_UPDATE_TIME = current_time
    except Exception:
        pass


def check_stop():
    return os.path.exists("/tmp/train_stop")


# Configuration
TASK_ID = ""


def _export_onnx_async(submodel_name, model_path, model_dir):
    """Export ONNX in a background thread to avoid blocking."""
    try:
        import onnxmltools
        from onnxmltools.convert.common.data_types import FloatTensorType
        onnx_path = Path(model_dir) / f"{submodel_name}.onnx"
        initial_type = [('float_input', FloatTensorType([None, 15]))]
        # Load the just-saved pkl to ensure consistency
        m = joblib.load(model_path)
        onnx_model = onnxmltools.convert_lightgbm(
            m, initial_types=initial_type, zipmap=False
        )
        with open(onnx_path, 'wb') as f:
            f.write(onnx_model.SerializeToString())
    except Exception as e:
        print(f"[ONNX Export] Background export failed for {submodel_name}: {e}")


def train_submodel(submodel_name, X, y, classes, model_dir, n_estimators=50):
    """Incrementally train a single sub-model.
    
    Returns True on success, False on failure.
    """
    model_path = Path(model_dir) / f"{submodel_name}.pkl"
    if not model_path.exists():
        update_progress(0, f"Model not found: {model_path.name}", "error")
        return False

    try:
        # Load existing model
        old_model = joblib.load(model_path)

        # Safety check: need at least 2 classes to avoid degenerating to single-class
        unique_classes = np.unique(y)
        if len(unique_classes) < 2:
            update_progress(0, f"Skipped '{submodel_name}': only {len(unique_classes)} class present in training data (need both classes to avoid model degeneration)", "warn")
            return True  # Not a failure, just skipped

        # Train incrementally
        new_model = old_model.fit(
            X, y,
            init_model=old_model,
        )

        # Save updated model
        joblib.dump(new_model, model_path)

        # Export ONNX in background thread so training isn't blocked
        from threading import Thread
        t = Thread(target=_export_onnx_async, args=(submodel_name, model_path, model_dir))
        t.daemon = True
        t.start()

        return True

    except Exception as e:
        update_progress(0, f"Training failed for {submodel_name}: {e}", "error")
        return False


def run_training(input_path, label, model_dir, do_train=True, task_id=""):
    """Main training pipeline.
    
    label: str class label, or None/'auto' to auto-detect from 'class' column in input.
    """
    global TASK_ID
    TASK_ID = task_id

    update_progress(0, "Starting incremental training pipeline...", "init")

    # ---- Step 1: Feature Extraction ----
    update_progress(5, f"Extracting features from {Path(input_path).name}...", "extract")
    results = extract_from_csv(input_path)
    if not results:
        update_progress(0, "No valid light curves found in input file.", "error")
        return 1

    update_progress(25, f"Extracted features for {len(results)} objects", "extract")

    # ---- Step 1b: Determine labels ----
    auto_detect = (label is None or label == "auto")
    if auto_detect:
        # Collect class labels from extraction results
        labels_found = {}
        for sid, feats, cls in results:
            if cls:
                labels_found[cls] = labels_found.get(cls, 0) + 1
        if not labels_found:
            update_progress(0, "Auto-detect enabled but no 'class' column found in input. Please add a 'class' column or manually select a label.", "error")
            return 1
        label_summary = ", ".join(f"{k}:{v}" for k, v in labels_found.items())
        update_progress(28, f"Auto-detected classes: {label_summary}", "extract")
    else:
        labels_found = {label: len(results)}

    # Save features (grouped by label)
    if auto_detect:
        # Save all samples with their own labels in one session
        features_list = [{"source_id": sid, "features": feats, "label": cls} 
                         for sid, feats, cls in results if cls]
        saved_file = save_features(features_list, label=None)
    else:
        features_list = [{"source_id": sid, "features": feats} for sid, feats, _ in results]
        saved_file = save_features(features_list, label=label)
    update_progress(35, f"Saved features to {Path(saved_file).name}", "save")

    if not do_train:
        update_progress(100, "Feature extraction complete. Training skipped.", "done")
        return 0

    # ---- Step 2: Load accumulated training data ----
    update_progress(40, "Loading accumulated training data...", "load")
    datasets = load_all_training_data()
    if not datasets:
        update_progress(0, "No training data available after feature save.", "error")
        return 1

    summary = get_training_summary()
    total_samples = sum(d["total_samples"] for d in summary.values())
    update_progress(45, f"Loaded {total_samples} training samples across {len(datasets)} sub-models", "load")

    # ---- Step 3: Backup existing models ----
    update_progress(48, "Backing up current models...", "backup")
    backup_model_files(model_dir)

    # ---- Step 4: Incremental training ----
    submodels = list(datasets.keys())
    trained_count = 0
    failed_count = 0

    for idx, submodel in enumerate(submodels):
        if check_stop():
            update_progress(0, "Training stopped by user.", "stopped")
            restore_model_files(model_dir)
            return 1

        data = datasets[submodel]
        n_samples = len(data["y"])
        pct_start = 50 + int(40 * idx / len(submodels))
        pct_end = 50 + int(40 * (idx + 1) / len(submodels))

        update_progress(pct_start, f"Training '{submodel}' with {n_samples} samples...", "train")

        success = train_submodel(
            submodel,
            data["X"],
            data["y"],
            data["classes"],
            model_dir,
        )

        if success:
            trained_count += 1
            update_progress(pct_end, f"'{submodel}' updated successfully", "train")
        else:
            failed_count += 1
            update_progress(pct_end, f"'{submodel}' training failed", "train")

    # ---- Step 5: Finalize ----
    if failed_count > 0:
        update_progress(95, f"Training complete with {failed_count} failures. Restoring backups...", "finalize")
        restore_model_files(model_dir)
        update_progress(100, f"Models restored to original state ({trained_count}/{len(submodels)} succeeded).", "done")
    else:
        update_progress(100, f"All {trained_count} sub-models updated successfully!", "done")

    return 0


def main():
    parser = argparse.ArgumentParser(description="TDlight Incremental Training")
    parser.add_argument("--input", required=True, help="Input CSV or ZIP file with light curves")
    parser.add_argument("--label", required=True, help=f"Class label or 'auto'. One of: {', '.join(SUBMODEL_CLASSES)} or 'auto'")
    parser.add_argument("--model-dir", default="../models/hierarchical_unlimited", help="Model directory")
    parser.add_argument("--train", action="store_true", help="Perform incremental training after feature extraction")
    parser.add_argument("--task-id", default="", help="Task ID for progress tracking")
    parser.add_argument("--progress-file", default="/tmp/train_progress.json", help="Progress file path")
    args = parser.parse_args()

    global PROGRESS_FILE
    if args.progress_file:
        PROGRESS_FILE = args.progress_file

    # Clean old stop flag
    if os.path.exists("/tmp/train_stop"):
        os.remove("/tmp/train_stop")

    return run_training(
        input_path=args.input,
        label=args.label,
        model_dir=args.model_dir,
        do_train=args.train,
        task_id=args.task_id,
    )


if __name__ == "__main__":
    sys.exit(main())
