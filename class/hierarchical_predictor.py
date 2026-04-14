#!/usr/bin/env python3
"""
Hierarchical LightGBM Predictor for TDLight.

Supports both ONNX Runtime (fast) and sklearn/pickle (fallback) backends.
7 sub-models form a 4-level classification tree:

    Level 1: init        → Non-var / Variable
    Level 2: variable    → Extrinsic / Intrinsic
    Level 3a: extrinsic  → ROT / EB
    Level 3b: intrinsic  → CEP / DSCT / RR / LPV
    Level 4a: eb         → EA / EW
    Level 4b: rr         → RRAB / RRC
    Level 4c: lpv        → M / SR

Usage:
    predictor = load_hierarchical_predictor("../models/hierarchical_unlimited", n_threads=8)
    labels, confidences = predictor.predict(X)
"""

import os
import sys
import time
import pickle
import numpy as np
import warnings

warnings.filterwarnings('ignore')

MODEL_NAMES = ['init', 'variable', 'extrinsic', 'intrinsic', 'eb', 'rr', 'lpv']

ALL_CLASSES = ['Non-var', 'ROT', 'EA', 'EW', 'CEP', 'DSCT', 'RRAB', 'RRC', 'M', 'SR']


class _OnnxSubModel:
    """Single ONNX sub-model wrapper."""

    def __init__(self, onnx_path, n_threads=4):
        import onnxruntime as ort
        so = ort.SessionOptions()
        so.intra_op_num_threads = n_threads
        so.inter_op_num_threads = 1
        so.log_severity_level = 3
        so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
        self.session = ort.InferenceSession(onnx_path, so, providers=['CPUExecutionProvider'])
        self.input_name = self.session.get_inputs()[0].name
        # Warmup
        n_features = self.session.get_inputs()[0].shape[1]
        dummy = np.zeros((1, n_features), dtype=np.float32)
        self.session.run(None, {self.input_name: dummy})

    def predict_proba(self, X):
        if X.dtype != np.float32:
            X = X.astype(np.float32)
        if X.ndim == 1:
            X = X.reshape(1, -1)
        outputs = self.session.run(None, {self.input_name: X})
        proba = outputs[1]
        if isinstance(proba, list) and isinstance(proba[0], dict):
            n_classes = len(proba[0])
            arr = np.zeros((len(proba), n_classes), dtype=np.float32)
            for i, d in enumerate(proba):
                for cls_idx, p in d.items():
                    arr[i, cls_idx] = p
            return arr
        return proba

    def predict(self, X):
        if X.dtype != np.float32:
            X = X.astype(np.float32)
        if X.ndim == 1:
            X = X.reshape(1, -1)
        outputs = self.session.run(None, {self.input_name: X})
        return outputs[0]


class _SklearnSubModel:
    """Single sklearn/pickle sub-model wrapper."""

    def __init__(self, model):
        self.model = model

    def predict_proba(self, X):
        if X.ndim == 1:
            X = X.reshape(1, -1)
        return self.model.predict_proba(X)

    def predict(self, X):
        if X.ndim == 1:
            X = X.reshape(1, -1)
        return self.model.predict(X)


class HierarchicalPredictor:
    """
    Hierarchical LightGBM predictor with path-probability confidence.

    Attributes:
        backend (str): "onnxruntime" or "sklearn"
    """

    def __init__(self, model_dir, n_threads=8, prefer_onnx=True):
        self.model_dir = model_dir
        self.sub_models = {}  # name -> sub-model predictor
        self.label_encoders = {}
        self.backend = "unknown"

        # Load label encoders
        le_path = os.path.join(model_dir, 'label_encoders.pkl')
        if os.path.exists(le_path):
            with open(le_path, 'rb') as f:
                self.label_encoders = pickle.load(f)
        else:
            raise FileNotFoundError(f"label_encoders.pkl not found in {model_dir}")

        # Try ONNX first
        loaded_onnx = False
        if prefer_onnx:
            try:
                import onnxruntime  # noqa: F401
                t0 = time.perf_counter()
                for name in MODEL_NAMES:
                    onnx_path = os.path.join(model_dir, f'{name}.onnx')
                    if not os.path.exists(onnx_path):
                        raise FileNotFoundError(f"ONNX model not found: {onnx_path}")
                    self.sub_models[name] = _OnnxSubModel(onnx_path, n_threads=max(1, n_threads // len(MODEL_NAMES)))
                dt = (time.perf_counter() - t0) * 1000
                self.backend = "onnxruntime"
                loaded_onnx = True
                print(f"[INFO] Hierarchical ONNX predictor loaded ({dt:.0f} ms, {len(MODEL_NAMES)} sub-models)")
            except ImportError:
                print("[WARN] onnxruntime not installed, falling back to sklearn")
            except Exception as e:
                print(f"[WARN] ONNX load failed: {e}, falling back to sklearn")

        # Fallback to sklearn/pkl
        if not loaded_onnx:
            import joblib
            t0 = time.perf_counter()
            for name in MODEL_NAMES:
                pkl_path = os.path.join(model_dir, f'{name}.pkl')
                if not os.path.exists(pkl_path):
                    raise FileNotFoundError(f"PKL model not found: {pkl_path}")
                model = joblib.load(pkl_path)
                self.sub_models[name] = _SklearnSubModel(model)
            dt = (time.perf_counter() - t0) * 1000
            self.backend = "sklearn"
            print(f"[INFO] Hierarchical sklearn predictor loaded ({dt:.0f} ms, {len(MODEL_NAMES)} sub-models)")

    def predict(self, X):
        """
        Predict class labels with path-probability confidence.

        Args:
            X: ndarray of shape (N, 15), feature matrix

        Returns:
            labels: ndarray of shape (N,), predicted class name strings
            confidences: ndarray of shape (N,), path probability product [0, 1]
        """
        X = np.asarray(X, dtype=np.float32)
        if X.ndim == 1:
            X = X.reshape(1, -1)

        n = len(X)
        preds = np.array([''] * n, dtype=object)
        confs = np.ones(n, dtype=np.float64)

        # ---- Level 1: init (Non-var / Variable) ----
        clf = self.sub_models['init']
        le = self.label_encoders['init']
        prob = clf.predict_proba(X)
        p = clf.predict(X)

        non_var_idx = le.transform(['Non-var'])[0]
        var_idx = le.transform(['Variable'])[0]

        mask_nv = (p == non_var_idx)
        preds[mask_nv] = 'Non-var'
        confs[mask_nv] = prob[mask_nv, non_var_idx]

        mask_var = (p == var_idx)
        if not mask_var.any():
            return preds, confs

        confs[mask_var] *= prob[mask_var, var_idx]
        iv = np.where(mask_var)[0]

        # ---- Level 2: variable (Extrinsic / Intrinsic) ----
        clf = self.sub_models['variable']
        le = self.label_encoders['variable']
        prob_v = clf.predict_proba(X[iv])
        p_v = clf.predict(X[iv])

        ext_idx = le.transform(['Extrinsic'])[0]
        int_idx = le.transform(['Intrinsic'])[0]

        # -- Extrinsic branch --
        mask_ext = (p_v == ext_idx)
        me = iv[mask_ext]
        if me.size > 0:
            confs[me] *= prob_v[mask_ext, ext_idx]

            # Level 3a: extrinsic (ROT / EB)
            clf_e = self.sub_models['extrinsic']
            le_e = self.label_encoders['extrinsic']
            prob_e = clf_e.predict_proba(X[me])
            p_e = clf_e.predict(X[me])

            rot_idx = le_e.transform(['ROT'])[0]
            eb_idx = le_e.transform(['EB'])[0]

            mask_rot = (p_e == rot_idx)
            preds[me[mask_rot]] = 'ROT'
            confs[me[mask_rot]] *= prob_e[mask_rot, rot_idx]

            mask_eb = (p_e == eb_idx)
            mb = me[mask_eb]
            if mb.size > 0:
                confs[mb] *= prob_e[mask_eb, eb_idx]

                # Level 4a: eb (EA / EW)
                clf_b = self.sub_models['eb']
                le_b = self.label_encoders['eb']
                prob_b = clf_b.predict_proba(X[mb])
                p_b = clf_b.predict(X[mb])
                preds[mb] = le_b.inverse_transform(p_b)
                confs[mb] *= prob_b[np.arange(len(p_b)), p_b]

        # -- Intrinsic branch --
        mask_int = (p_v == int_idx)
        mi = iv[mask_int]
        if mi.size > 0:
            confs[mi] *= prob_v[mask_int, int_idx]

            # Level 3b: intrinsic (CEP / DSCT / RR / LPV)
            clf_n = self.sub_models['intrinsic']
            le_n = self.label_encoders['intrinsic']
            prob_n = clf_n.predict_proba(X[mi])
            p_n = clf_n.predict(X[mi])

            cep_idx = le_n.transform(['CEP'])[0]
            dsct_idx = le_n.transform(['DSCT'])[0]
            rr_idx = le_n.transform(['RR'])[0]
            lpv_idx = le_n.transform(['LPV'])[0]

            mask_cep = (p_n == cep_idx)
            preds[mi[mask_cep]] = 'CEP'
            confs[mi[mask_cep]] *= prob_n[mask_cep, cep_idx]

            mask_dsct = (p_n == dsct_idx)
            preds[mi[mask_dsct]] = 'DSCT'
            confs[mi[mask_dsct]] *= prob_n[mask_dsct, dsct_idx]

            mask_rr = (p_n == rr_idx)
            mr = mi[mask_rr]
            if mr.size > 0:
                confs[mr] *= prob_n[mask_rr, rr_idx]

                # Level 4b: rr (RRAB / RRC)
                clf_r = self.sub_models['rr']
                le_r = self.label_encoders['rr']
                prob_r = clf_r.predict_proba(X[mr])
                p_r = clf_r.predict(X[mr])
                preds[mr] = le_r.inverse_transform(p_r)
                confs[mr] *= prob_r[np.arange(len(p_r)), p_r]

            mask_lpv = (p_n == lpv_idx)
            ml = mi[mask_lpv]
            if ml.size > 0:
                confs[ml] *= prob_n[mask_lpv, lpv_idx]

                # Level 4c: lpv (M / SR)
                clf_l = self.sub_models['lpv']
                le_l = self.label_encoders['lpv']
                prob_l = clf_l.predict_proba(X[ml])
                p_l = clf_l.predict(X[ml])
                preds[ml] = le_l.inverse_transform(p_l)
                confs[ml] *= prob_l[np.arange(len(p_l)), p_l]

        return preds, confs

    def predict_proba_flat(self, X):
        """
        Compatibility wrapper: returns (N, 10) probability matrix
        with columns ordered as ALL_CLASSES.
        Confidence for each class is approximated by path probability.

        Note: For hierarchical models, the "probability" of non-predicted
        classes is 0; only the predicted class gets the path confidence.
        """
        labels, confs = self.predict(X)
        n = len(labels)
        class_to_idx = {c: i for i, c in enumerate(ALL_CLASSES)}
        proba = np.zeros((n, len(ALL_CLASSES)), dtype=np.float32)
        for i in range(n):
            cls = labels[i]
            idx = class_to_idx.get(cls, -1)
            if idx >= 0:
                proba[i, idx] = confs[i]
        return proba


def load_hierarchical_predictor(model_dir, n_threads=8, prefer_onnx=True):
    """
    Load the hierarchical predictor.

    Args:
        model_dir: Path to directory containing the 7 sub-models + label_encoders.pkl
        n_threads: Number of threads for ONNX inference
        prefer_onnx: Try ONNX Runtime first

    Returns:
        HierarchicalPredictor instance
    """
    return HierarchicalPredictor(model_dir, n_threads=n_threads, prefer_onnx=prefer_onnx)
