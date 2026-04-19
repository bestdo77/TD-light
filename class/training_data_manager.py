#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TDlight Training Data Manager
Manages accumulated training features and labels under models/training_data/.
"""

import os
import json
import shutil
from pathlib import Path
from datetime import datetime

import numpy as np

# Default storage location (relative to project root)
DEFAULT_DATA_DIR = Path(__file__).parent.parent / "models" / "training_data"

# Hierarchical path mapping (which sub-model handles which label)
# This mirrors the tree in hierarchical_predictor.py
HIERARCHY = {
    "init":       {"pos": ["Variable"], "neg": ["Non-var"]},
    "variable":   {"pos": ["Intrinsic"], "neg": ["Extrinsic"]},
    "extrinsic":  {"pos": ["EB"], "neg": ["ROT"]},
    "intrinsic":  {"pos": ["CEP", "DSCT", "RR", "LPV"], "neg": None},
    "eb":         {"pos": ["EW"], "neg": ["EA"]},
    "rr":         {"pos": ["RRC"], "neg": ["RRAB"]},
    "lpv":        {"pos": ["SR"], "neg": ["M"]},
}

# Flat class list
ALL_CLASSES = ['Non-var', 'ROT', 'EA', 'EW', 'CEP', 'DSCT', 'RRAB', 'RRC', 'M', 'SR', 'EB']

# Sub-model class lists
SUBMODEL_CLASSES = {
    "init":       ['Non-var', 'Variable'],
    "variable":   ['Extrinsic', 'Intrinsic'],
    "extrinsic":  ['ROT', 'EB'],
    "intrinsic":  ['CEP', 'DSCT', 'RR', 'LPV'],
    "eb":         ['EA', 'EW'],
    "rr":         ['RRAB', 'RRC'],
    "lpv":        ['M', 'SR'],
}


def get_training_dir():
    """Return the training data directory, creating it if needed."""
    d = Path(DEFAULT_DATA_DIR)
    d.mkdir(parents=True, exist_ok=True)
    return d


def save_features(features_list, label, session_id=None):
    """Save extracted features with a label.
    
    features_list: list of dicts [{'source_id': str, 'features': [15 floats], 'label': str}, ...]
    label: str or None. If None, uses 'label' field from each item in features_list.
           If provided, uses this label for all items (unless item already has 'label').
    session_id: optional session identifier (default: timestamp)
    """
    d = get_training_dir()
    if session_id is None:
        session_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    
    # Collect records by their actual label
    records_by_label = {}
    for item in features_list:
        item_label = item.get("label", label)
        if item_label is None:
            continue
        if item_label not in ALL_CLASSES:
            # Skip unknown labels with a warning
            print(f"[WARN] Skipping sample with unknown label '{item_label}'. Must be one of {ALL_CLASSES}")
            continue
        if item_label not in records_by_label:
            records_by_label[item_label] = []
        records_by_label[item_label].append(item)
    
    if not records_by_label:
        raise ValueError("No valid features to save. Check that labels are valid.")
    
    # Write all records to a single session file
    labels_joined = "-".join(sorted(records_by_label.keys()))
    filename = d / f"{session_id}_{labels_joined}.jsonl"
    with open(filename, 'w') as f:
        for lbl, items in records_by_label.items():
            for item in items:
                record = {
                    "source_id": item.get("source_id", ""),
                    "features": item.get("features", []),
                    "label": lbl,
                    "session": session_id,
                    "timestamp": datetime.now().isoformat()
                }
                f.write(json.dumps(record) + "\n")
    return str(filename)


def load_all_training_data():
    """Load all accumulated training data.
    
    Returns dict: {submodel_name: {'X': np.array(N,15), 'y': np.array(N), 'labels': [str]}}
    """
    d = get_training_dir()
    if not d.exists():
        return {}
    
    # Collect all records by flat label
    records_by_label = {cls: [] for cls in ALL_CLASSES}
    for fpath in d.glob("*.jsonl"):
        with open(fpath, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                rec = json.loads(line)
                lbl = rec.get("label")
                if lbl in records_by_label:
                    records_by_label[lbl].append(rec)
    
    # Build sub-model datasets
    datasets = {}
    for submodel, classes in SUBMODEL_CLASSES.items():
        X_list = []
        y_list = []
        labels_list = []
        for idx, cls in enumerate(classes):
            for rec in records_by_label.get(cls, []):
                feats = rec.get("features", [])
                if len(feats) != 15:
                    continue
                X_list.append(feats)
                y_list.append(idx)
                labels_list.append(cls)
        if X_list:
            datasets[submodel] = {
                "X": np.array(X_list, dtype=np.float32),
                "y": np.array(y_list, dtype=np.int32),
                "labels": labels_list,
                "classes": classes,
            }
    return datasets


def get_training_summary():
    """Return a human-readable summary of accumulated training data."""
    datasets = load_all_training_data()
    summary = {}
    for submodel, data in datasets.items():
        summary[submodel] = {
            "total_samples": len(data["y"]),
            "class_distribution": {cls: int((data["y"] == i).sum()) for i, cls in enumerate(data["classes"])}
        }
    return summary


def backup_model_files(model_dir, backup_suffix=".bak"):
    """Backup existing .pkl and .onnx files before overwriting."""
    model_dir = Path(model_dir)
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    for pattern in ("*.pkl", "*.onnx"):
        for f in model_dir.glob(pattern):
            if f.name == "label_encoders.pkl":
                continue
            backup = f.with_suffix(f.suffix + backup_suffix + f".{ts}")
            shutil.copy2(f, backup)
    return True


def restore_model_files(model_dir, backup_suffix=".bak"):
    """Restore from the most recent backup."""
    model_dir = Path(model_dir)
    backups = sorted(model_dir.glob(f"*{backup_suffix}.*"), key=lambda p: p.stat().st_mtime, reverse=True)
    if not backups:
        return False
    # Group by stem (original filename without backup suffix)
    restored = set()
    for f in backups:
        # f.name like "init.pkl.bak.20260419_150309"
        parts = f.name.split(backup_suffix)
        if len(parts) < 2:
            continue
        original_name = parts[0]
        original = model_dir / original_name
        if original_name not in restored:
            shutil.copy2(f, original)
            restored.add(original_name)
    return True


def clear_training_data():
    """Remove all accumulated training data files. Use with caution."""
    d = get_training_dir()
    if d.exists():
        for f in d.glob("*.jsonl"):
            f.unlink()
    return True


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Training data manager CLI")
    parser.add_argument("--summary", action="store_true", help="Show training data summary")
    parser.add_argument("--clear", action="store_true", help="Clear all training data")
    args = parser.parse_args()
    
    if args.summary:
        print(json.dumps(get_training_summary(), indent=2))
    elif args.clear:
        clear_training_data()
        print("Training data cleared.")
    else:
        parser.print_help()
