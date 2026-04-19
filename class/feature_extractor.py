#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TDlight Feature Extractor
Extracts 15 feets features from light curve CSV files.
Supports both single CSV (multi-object) and ZIP archives (one CSV per object).
"""

import os
import sys
import warnings
import json
import zipfile
import tempfile
import shutil
from pathlib import Path

import numpy as np
import pandas as pd
import feets
from concurrent.futures import ProcessPoolExecutor, as_completed

warnings.filterwarnings('ignore')

# The 15 features used by TDlight hierarchical predictor
SELECTED_FEATURES = [
    'PeriodLS', 'Mean', 'Rcs', 'Psi_eta', 'StetsonK_AC',
    'Gskew', 'Psi_CS', 'Skew', 'Freq1_harmonics_amplitude_1', 'Eta_e',
    'LinearTrend', 'Freq1_harmonics_amplitude_0', 'AndersonDarling', 'MaxSlope', 'StetsonK'
]

# Expected CSV columns (flexible matching)
REQUIRED_COLS = {'time', 'magnitude', 'error'}
OPTIONAL_COLS = {'source_id', 'band', 'mag', 'mag_error', 'flux', 'flux_error', 'class', 'label', 'type'}

# Valid class labels for auto-detection
VALID_CLASSES = {'Non-var', 'ROT', 'EA', 'EW', 'CEP', 'DSCT', 'RRAB', 'RRC', 'M', 'SR', 'EB',
                 'Variable', 'Intrinsic', 'Extrinsic', 'RR', 'LPV'}  # include hierarchy aliases


def _normalize_columns(df):
    """Normalize column names to standard names.
    Handles duplicates: if both mag_err and flux_err exist, prefer mag_err.
    """
    col_map = {}
    has_mag_err = any(c.lower().strip() in ('mag_error', 'mag_err') for c in df.columns)
    for c in df.columns:
        cl = c.lower().strip()
        if cl in ('time', 'mjd', 'hjd', 'bjd'):
            col_map[c] = 'time'
        elif cl in ('mag', 'magnitude'):
            col_map[c] = 'magnitude'
        elif cl in ('err', 'error', 'mag_error', 'mag_err'):
            col_map[c] = 'error'
        elif cl in ('flux_err', 'flux_error'):
            # Only map flux_err if no mag_err exists
            if not has_mag_err:
                col_map[c] = 'error'
            # else: leave flux_err untouched (won't be used)
        elif cl == 'source_id':
            col_map[c] = 'source_id'
        elif cl == 'band':
            col_map[c] = 'band'
        elif cl in ('class', 'label', 'type'):
            col_map[c] = 'class'
    df = df.rename(columns=col_map)
    return df


def _sanitize_lightcurve(t, mag, err):
    """Remove NaN/Inf and require at least 5 points."""
    t = np.asarray(t, dtype=float)
    mag = np.asarray(mag, dtype=float)
    err = np.asarray(err, dtype=float)
    mask = np.isfinite(t) & np.isfinite(mag) & np.isfinite(err)
    if mask.sum() < 5:
        return None, None, None
    return t[mask], mag[mask], err[mask]


def extract_features_from_array(t, mag, err):
    """Extract features from time/mag/error arrays. Returns list of 15 floats or None."""
    t, mag, err = _sanitize_lightcurve(t, mag, err)
    if t is None:
        return None
    try:
        fs = feets.FeatureSpace(data=['time', 'magnitude', 'error'], only=SELECTED_FEATURES)
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


def _extract_single_csv(csv_path):
    """Read a single-object CSV and return (source_id, features, class_label).
    
    class_label is None if no 'class' column is present.
    """
    try:
        df = pd.read_csv(csv_path)
        df = _normalize_columns(df)
        if 'source_id' in df.columns:
            source_id = str(df['source_id'].iloc[0])
        else:
            source_id = Path(csv_path).stem
        if 'time' not in df.columns or 'magnitude' not in df.columns:
            return source_id, None, None
        # Extract class label if present
        class_label = None
        if 'class' in df.columns:
            raw = str(df['class'].dropna().iloc[0]).strip()
            if raw in VALID_CLASSES:
                class_label = raw
        feats = extract_features_from_array(df['time'].values, df['magnitude'].values, df.get('error', pd.Series([0.01]*len(df))).values)
        return source_id, feats, class_label
    except Exception:
        return Path(csv_path).stem, None, None


def extract_from_multi_object_csv(csv_path):
    """Read a multi-object CSV (with source_id column) and return list of (source_id, features, class_label).
    
    If 'class' column is present, each source_id gets its own class label.
    """
    try:
        df = pd.read_csv(csv_path)
        df = _normalize_columns(df)
        if 'source_id' not in df.columns:
            return []
        results = []
        for sid, group in df.groupby('source_id'):
            if 'time' not in group.columns or 'magnitude' not in group.columns:
                continue
            err = group.get('error', pd.Series([0.01]*len(group))).values
            feats = extract_features_from_array(group['time'].values, group['magnitude'].values, err)
            if feats is not None:
                # Extract class label if present (use first non-null value)
                class_label = None
                if 'class' in group.columns:
                    raw_vals = group['class'].dropna()
                    if len(raw_vals) > 0:
                        raw = str(raw_vals.iloc[0]).strip()
                        if raw in VALID_CLASSES:
                            class_label = raw
                results.append((str(sid), feats, class_label))
        return results
    except Exception as e:
        print(f"[ERROR] Failed to read {csv_path}: {e}")
        return []


def extract_from_zip(zip_path, tmpdir=None):
    """Extract features from a ZIP of single-object CSV files.
    
    Returns list of (source_id, features, class_label).
    """
    work_dir = tempfile.mkdtemp() if tmpdir is None else tmpdir
    try:
        with zipfile.ZipFile(zip_path, 'r') as z:
            z.extractall(work_dir)
        csv_files = list(Path(work_dir).rglob('*.csv'))
        results = []
        for cf in csv_files:
            sid, feats, class_label = _extract_single_csv(str(cf))
            if feats is not None:
                results.append((sid, feats, class_label))
        return results
    finally:
        if tmpdir is None:
            shutil.rmtree(work_dir, ignore_errors=True)


def extract_from_csv(input_path):
    """Auto-detect format and extract features.
    
    Returns list of (source_id, features, class_label) tuples.
    class_label is None if no 'class' column is present in the input.
    """
    input_path = Path(input_path)
    if not input_path.exists():
        return []
    if input_path.suffix.lower() == '.zip':
        return extract_from_zip(str(input_path))
    # Try multi-object CSV first
    results = extract_from_multi_object_csv(str(input_path))
    if results:
        return results
    # Fallback: single-object CSV
    sid, feats, class_label = _extract_single_csv(str(input_path))
    if feats is not None:
        return [(sid, feats, class_label)]
    return []


def extract_with_progress(input_path, progress_callback=None, n_workers=None):
    """Extract features with optional progress callback.
    
    progress_callback(percent, message, step) -> void
    """
    def _cb(pct, msg, step="extract"):
        if progress_callback:
            progress_callback(pct, msg, step)

    _cb(0, "Reading input files...", "read")
    results = extract_from_csv(input_path)
    total = len(results)
    _cb(30, f"Extracted features for {total} objects", "extract")
    return results


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Extract feets features from light curve CSV/ZIP")
    parser.add_argument("--input", required=True, help="Input CSV or ZIP file")
    parser.add_argument("--output", required=True, help="Output JSON file")
    args = parser.parse_args()

    data = extract_from_csv(args.input)
    out = [{"source_id": sid, "features": feats, "class": cls} for sid, feats, cls in data]
    with open(args.output, 'w') as f:
        json.dump(out, f, indent=2)
    print(f"Extracted {len(out)} objects to {args.output}")
