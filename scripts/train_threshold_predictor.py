#!/usr/bin/env python3
"""BUDGET-SYM Review-2 extension: trains a per-threshold linear model that
predicts a good PolicyConfig for a workload from 5 cheap, online-computable
features (WorkloadFeatures, see include/workload_profiler.hpp), instead of
running the full C++ grid search at compile time or on every compilation.

Pipeline:
  1. Generate 300 synthetic identifier workloads with varied characteristics.
  2. For each workload, grid-search PolicyConfig thresholds against a Python
     port of BudgetSym's decide()/cost model (include/budget_sym.hpp) to find
     the config that maximizes compression ratio for that workload, and
     record (actual WorkloadFeatures, optimal PolicyConfig) as a training pair.
  3. Train sklearn.linear_model.Ridge(alpha=1.0) per threshold on the 5
     features, report R^2, and export the learned coefficients as a hardcoded
     C++ header (include/predicted_thresholds.hpp) -- no sklearn dependency
     at BUDGET-SYM runtime.

Honesty note on scope reduction: src/grid_search_main.cpp's C++ grid search
sweeps 15,000 configs over 8 fixed datasets *once*. Doing that per-workload
in pure Python for 300 workloads (4.5M dataset x config combinations) is not
compute-tractable for an offline training script. This script instead uses a
144-combination reduced grid (3x3x2x2x2x2) per workload -- still a genuine
sweep of all six thresholds, just coarser than the C++ tool's. This is a
documented trade-off, not a hidden one; see metis_v2.tex Section VIII-F.
"""
import csv
import math
import random

import numpy as np
from sklearn.dummy import DummyRegressor
from sklearn.ensemble import RandomForestRegressor
from sklearn.linear_model import Lasso, Ridge
from sklearn.model_selection import train_test_split
from sklearn.metrics import mean_absolute_error, r2_score
from sklearn.neighbors import KNeighborsRegressor
from sklearn.svm import SVR
from sklearn.tree import DecisionTreeRegressor

# ---- Review-2 Phase 5: model comparison + feature ablation ----------------
# PRD Section 6.5 Step 2/3: compare 7 regression models per threshold (R^2,
# MAE, downstream compression ratio on the 8 real benchmark datasets), and
# ablate Ridge's 5 input features one at a time. Ridge stays the model
# actually exported to include/predicted_thresholds.hpp regardless of how
# this comparison turns out -- it's the only one of the seven that is both
# competitive and exportable as clamped linear expressions with no runtime
# ML dependency.
MODEL_SPECS = {
    "Baseline Mean": lambda: DummyRegressor(strategy="mean"),
    "Lasso": lambda: Lasso(alpha=1.0),
    "Ridge": lambda: Ridge(alpha=1.0),
    "Decision Tree": lambda: DecisionTreeRegressor(max_depth=5, random_state=RNG_SEED),
    "Random Forest": lambda: RandomForestRegressor(n_estimators=100, random_state=RNG_SEED),
    "SVR": lambda: SVR(kernel="rbf"),
    "KNN": lambda: KNeighborsRegressor(n_neighbors=5),
}

# Whether this model's learned parameters can be exported as a fixed set of
# C++ constants (a handful of dot products + clamp, no runtime ML library).
# Only the linear models qualify -- trees/forests/SVR/KNN all need either the
# full training data (KNN), many branch conditions (trees/forests), or the
# full kernel/support-vector set (SVR) at inference time.
MODEL_EXPORTABLE = {
    "Baseline Mean": True,
    "Lasso": True,
    "Ridge": True,
    "Decision Tree": False,
    "Random Forest": False,
    "SVR": False,
    "KNN": False,
}

RNG_SEED = 1337
N_WORKLOADS = 300
IDENTIFIERS_PER_WORKLOAD = 100  # matches WorkloadProfiler's 100-symbol sampling window

# ---- Python port of BudgetSym's PolicyConfig / cost model -----------------
# Mirrors include/common.hpp's PolicyConfig defaults/fields and
# include/budget_sym.hpp's decide()/materialize()/costOf() -- insert-only,
# no lookups/promotions, matching src/grid_search_main.cpp's own scope.

K_INLINE_OVERHEAD = 28
K_INTERNED_OVERHEAD = 28
K_COMPRESSED_LINK_OVERHEAD = 29
SIZEOF_STD_STRING = 32  # matches the libstdc++ SSO control-block size assumed elsewhere in this project

THRESHOLD_NAMES = [
    "inlineMaxLen", "compressMinLen", "lowPressureThreshold",
    "highPressureThreshold", "hotAccessThreshold", "prefixSimilarityMinShared",
]

GRID = {
    "inlineMaxLen": [8, 12, 16],
    "compressMinLen": [6, 10, 14],
    "lowPressureThreshold": [0.3, 0.5],
    "highPressureThreshold": [0.7, 0.85],
    "hotAccessThreshold": [2, 4],
    "prefixSimilarityMinShared": [3, 5],
}


def common_prefix_len(a, b):
    n = min(len(a), len(b))
    i = 0
    while i < n and a[i] == b[i]:
        i += 1
    return i


def compression_ratio_for_config(identifiers, cfg, budget_bytes, conventional_bytes):
    """Insert-only simulation of BudgetSym under `cfg`; returns compression
    ratio vs. conventional_bytes (conventional_bytes / tracked_bytes)."""
    inline_max_len = cfg["inlineMaxLen"]
    compress_min_len = cfg["compressMinLen"]
    low_pressure = cfg["lowPressureThreshold"]
    high_pressure = cfg["highPressureThreshold"]
    hot_access = cfg["hotAccessThreshold"]
    prefix_min_shared = cfg["prefixSimilarityMinShared"]

    seen = set()
    pool_seen = set()
    tracked = 0
    last_full = ""
    comp_tail_full = None
    reanchor_interval = 8
    comp_insert_count = 0

    for name in identifiers:
        is_repeat = name in seen
        seen.add(name)
        prefix_shared = common_prefix_len(last_full, name) if last_full else 0
        last_full = name

        pressure = tracked / budget_bytes if budget_bytes > 0 else 0.0
        pressure = max(0.0, min(1.0, pressure))

        hot = 0 >= hot_access  # accessFreqHint is always 0 at insert time here
        long_id = len(name) >= compress_min_len
        prefix_similar = prefix_shared >= prefix_min_shared
        high_p = pressure >= high_pressure
        low_p = pressure < low_pressure

        if is_repeat:
            rep = "INTERNED"
        elif high_p and long_id and not hot:
            rep = "COMPRESSED"
        elif long_id and prefix_similar and not hot:
            rep = "COMPRESSED"
        elif len(name) < inline_max_len and low_p:
            rep = "INLINE"
        else:
            rep = "INTERNED"

        if rep == "INLINE":
            tracked += len(name) + 1 + K_INLINE_OVERHEAD
        elif rep == "INTERNED":
            if name not in pool_seen:
                pool_seen.add(name)
                tracked += SIZEOF_STD_STRING + len(name) + 1
            tracked += K_INTERNED_OVERHEAD
        else:  # COMPRESSED
            reanchor = comp_tail_full is None or (comp_insert_count % reanchor_interval == 0)
            comp_insert_count += 1
            if reanchor:
                suffix = name
            else:
                shared = min(common_prefix_len(comp_tail_full, name), 255)
                suffix = name[shared:]
            tracked += 1 + len(suffix) + 1 + K_COMPRESSED_LINK_OVERHEAD
            comp_tail_full = name

    if tracked <= 0 or conventional_bytes <= 0:
        return 1.0
    return conventional_bytes / tracked


def conventional_bytes_for(identifiers):
    # ConventionalSymbolTable: one unordered_map<string,Symbol> slot per
    # insert (no dedup), each paying a full string copy + metadata overhead.
    return sum(SIZEOF_STD_STRING + len(name) + 1 + K_INLINE_OVERHEAD for name in identifiers)


# ---- Synthetic workload generation -----------------------------------------

ALPHABET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"


def generate_workload(rng, mean_len, prefix_coeff, repeat_rate, depth_var, entropy_target, n):
    """Generates n identifiers biased toward the requested characteristics.
    Targets are approximate generation knobs; actual WorkloadFeatures are
    recomputed from the resulting identifiers (see extract_features_python)
    so training pairs are always self-consistent."""
    identifiers = []
    depths = []
    prev = None
    # entropy_target in [2,5] -> lower entropy means a smaller effective alphabet
    alphabet_size = max(4, min(52, int(4 + (entropy_target - 2) * (48 / 3))))
    alphabet = ALPHABET[:alphabet_size]

    depth_std = math.sqrt(max(0.0, depth_var))

    for _ in range(n):
        depth = max(0, int(round(rng.gauss(3, depth_std))))
        depths.append(depth)

        if prev is not None and rng.random() < repeat_rate:
            identifiers.append(prev)
            continue

        length = max(1, int(round(rng.gauss(mean_len, 2))))
        if prev is not None and rng.random() < prefix_coeff:
            shared_len = min(len(prev), max(4, length // 2))
            suffix_len = max(1, length - shared_len)
            name = prev[:shared_len] + "".join(rng.choice(alphabet) for _ in range(suffix_len))
        else:
            name = "".join(rng.choice(alphabet) for _ in range(length))

        identifiers.append(name)
        prev = name

    return identifiers, depths


def extract_features_python(identifiers, depths):
    """Python port of WorkloadProfiler::extract() (include/workload_profiler.hpp)."""
    n = len(identifiers)
    seen = set()
    repeat_count = 0
    total_len = 0
    prefix_similar_pairs = 0
    consecutive_pairs = 0
    char_freq = {}
    total_chars = 0
    last = None

    for name in identifiers:
        total_len += len(name)
        if name in seen:
            repeat_count += 1
        seen.add(name)
        if last is not None:
            shared = common_prefix_len(last, name)
            if shared >= 4:
                prefix_similar_pairs += 1
            consecutive_pairs += 1
        last = name
        for c in name:
            char_freq[c] = char_freq.get(c, 0) + 1
        total_chars += len(name)

    mean_len = total_len / n
    repeat_rate = repeat_count / n
    prefix_coeff = prefix_similar_pairs / consecutive_pairs if consecutive_pairs > 0 else 0.0

    depth_arr = np.array(depths, dtype=float)
    depth_var = float(np.var(depth_arr)) if len(depth_arr) > 0 else 0.0

    entropy = 0.0
    if total_chars > 0:
        for c, cnt in char_freq.items():
            p = cnt / total_chars
            if p > 0:
                entropy -= p * math.log2(p)

    return {
        "meanNameLength": mean_len,
        "prefixSimilarityCoeff": prefix_coeff,
        "repeatRate": repeat_rate,
        "scopeDepthVariance": depth_var,
        "nameEntropy": entropy,
    }


def grid_search_best_config(identifiers):
    budget_bytes = 64 * 1024 * 1024
    conv_bytes = conventional_bytes_for(identifiers)

    best_cfg = None
    best_ratio = -1.0
    for a in GRID["inlineMaxLen"]:
        for b in GRID["compressMinLen"]:
            for c in GRID["lowPressureThreshold"]:
                for d in GRID["highPressureThreshold"]:
                    for e in GRID["hotAccessThreshold"]:
                        for f in GRID["prefixSimilarityMinShared"]:
                            cfg = {
                                "inlineMaxLen": a, "compressMinLen": b,
                                "lowPressureThreshold": c, "highPressureThreshold": d,
                                "hotAccessThreshold": e, "prefixSimilarityMinShared": f,
                            }
                            ratio = compression_ratio_for_config(identifiers, cfg, budget_bytes, conv_bytes)
                            if ratio > best_ratio:
                                best_ratio = ratio
                                best_cfg = cfg
    return best_cfg, best_ratio


# ---- Python reproduction of the 8 C++ benchmark datasets -------------------
# Structurally matches include/dataset_generators.hpp / src/benchmark_main.cpp
# (same identifier alphabet, length ranges, and construction patterns) using
# Python's random module -- not bit-for-bit identical to C++ mt19937 output,
# but the same dataset *shape*, which is what "downstream compression ratio
# on the 8 benchmark datasets" needs: a fair, independent check of how well
# each model's predicted thresholds generalize to the actual named workloads,
# not a byte-for-byte replay of the C++ benchmark's RNG stream.
_ALPHA = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"


def _rand_identifier(rng, min_len, max_len):
    length = rng.randint(min_len, max_len)
    return "".join(rng.choice(_ALPHA) for _ in range(length))


def _gen_uniform_random(rng, n, min_len, max_len):
    return [_rand_identifier(rng, min_len, max_len) for _ in range(n)]


def _gen_high_prefix_similarity(n):
    prefixes = [
        "moduleConfigParameter", "temperatureSensorCalibration", "networkInterfaceBuffer",
        "userAuthenticationToken", "compilerSymbolTableEntry",
    ]
    group_size = n // 5
    identifiers = []
    for p, prefix in enumerate(prefixes):
        for i in range(group_size):
            identifiers.append(f"{prefix}{p * group_size + i}")
    return identifiers


def _gen_nested_scopes(rng, scopes_count, symbols_per_scope):
    identifiers = []
    for s in range(scopes_count):
        for _ in range(symbols_per_scope):
            identifiers.append(f"scope{s}_{_rand_identifier(rng, 4, 10)}")
    return identifiers


def _gen_hot_cold_access(rng, n):
    hot_n = n // 10
    identifiers = [f"hotPathVariableAccessedFrequently{i}" for i in range(hot_n)]
    identifiers += [_rand_identifier(rng, 6, 18) for _ in range(n - hot_n)]
    return identifiers


def _gen_memory_stress(rng, n):
    return [_rand_identifier(rng, 15, 30) for _ in range(n)]


def benchmark_style_datasets():
    """The 8 named datasets benchmark_main.cpp runs, reproduced in Python for
    the downstream-compression-ratio metric (PRD Section 6.5 Step 2)."""
    seed = 42
    default_budget = 64 * 1024 * 1024
    tiny_budget = 8192
    datasets = []
    datasets.append(("small", _gen_uniform_random(random.Random(seed), 100, 4, 12), default_budget))
    datasets.append(("medium", _gen_uniform_random(random.Random(seed + 1), 2000, 4, 16), default_budget))
    datasets.append(("large", _gen_uniform_random(random.Random(seed + 2), 20000, 4, 16), default_budget))
    datasets.append(("high-prefix-similarity", _gen_high_prefix_similarity(2000), default_budget))
    datasets.append(("random-identifiers", _gen_uniform_random(random.Random(seed + 4), 2000, 3, 24), default_budget))
    datasets.append(("nested-scopes", _gen_nested_scopes(random.Random(seed + 5), 40, 25), default_budget))
    datasets.append(("hot-cold-access", _gen_hot_cold_access(random.Random(seed + 6), 1500), default_budget))
    datasets.append(("memory-stress", _gen_memory_stress(random.Random(seed + 7), 1500), tiny_budget))
    return datasets


CLAMP_BOUNDS = {
    "inlineMaxLen": (6, 20),
    "compressMinLen": (4, 20),
    "lowPressureThreshold": (0.1, 0.7),
    "highPressureThreshold": (0.5, 0.95),
    "hotAccessThreshold": (1, 10),
    "prefixSimilarityMinShared": (1, 10),
}

CPP_TYPE = {
    "inlineMaxLen": "size_t", "compressMinLen": "size_t",
    "lowPressureThreshold": "double", "highPressureThreshold": "double",
    "hotAccessThreshold": "size_t", "prefixSimilarityMinShared": "size_t",
}


def build_full_policy_config(models_by_threshold, feature_vec):
    """Applies a per-threshold model dict (one fitted regressor per
    THRESHOLD_NAMES entry, whatever model type) to a single feature vector,
    clamping each output the same way ThresholdPredictor::predict() does."""
    cfg = {}
    for name in THRESHOLD_NAMES:
        pred = models_by_threshold[name].predict([feature_vec])[0]
        lo, hi = CLAMP_BOUNDS[name]
        pred = min(max(pred, lo), hi)
        if CPP_TYPE[name] == "size_t":
            pred = int(round(pred))
        cfg[name] = pred
    return cfg


def downstream_compression_ratio(models_by_threshold):
    """Mean compression ratio (vs. conventional) achieved when this model's
    predicted thresholds are applied to the 8 real benchmark datasets,
    profiling each dataset's first 100 identifiers the same way
    WorkloadProfiler's sampling window does (PRD Section 6.5 Step 2)."""
    ratios = []
    for _name, identifiers, budget in benchmark_style_datasets():
        sample = identifiers[:IDENTIFIERS_PER_WORKLOAD]
        depths = [0] * len(sample)  # depth tracking is nested-scopes-only in these datasets
        features = extract_features_python(sample, depths)
        feature_vec = [
            features["meanNameLength"], features["prefixSimilarityCoeff"],
            features["repeatRate"], features["scopeDepthVariance"], features["nameEntropy"],
        ]
        cfg = build_full_policy_config(models_by_threshold, feature_vec)
        conv_bytes = conventional_bytes_for(identifiers)
        ratios.append(compression_ratio_for_config(identifiers, cfg, budget, conv_bytes))
    return float(np.mean(ratios))


def write_header(models):
    lines = []
    lines.append("#pragma once")
    lines.append("// BUDGET-SYM Review-2 extension: hardcoded Ridge-regression coefficients")
    lines.append("// trained offline by scripts/train_threshold_predictor.py against a Python")
    lines.append("// port of BudgetSym's decide()/cost model over 300 synthetic workloads.")
    lines.append("// No sklearn dependency at runtime -- this file is generated once and")
    lines.append("// committed; regenerate by re-running the training script.")
    lines.append("//")
    lines.append("// GENERATED FILE -- do not hand-edit. See scripts/train_threshold_predictor.py.")
    lines.append("#include <algorithm>")
    lines.append("#include \"common.hpp\"")
    lines.append("#include \"workload_profiler.hpp\"")
    lines.append("")
    lines.append("namespace budgetsym {")
    lines.append("")
    lines.append("struct ThresholdPredictor {")
    lines.append("    template <typename T>")
    lines.append("    static T clampVal(double v, T lo, T hi) {")
    lines.append("        if (v < static_cast<double>(lo)) return lo;")
    lines.append("        if (v > static_cast<double>(hi)) return hi;")
    lines.append("        return static_cast<T>(v);")
    lines.append("    }")
    lines.append("")
    lines.append("    static PolicyConfig predict(const WorkloadFeatures& f) {")
    lines.append("        PolicyConfig c;")
    for name in THRESHOLD_NAMES:
        model = models[name]
        coefs = model.coef_
        intercept = model.intercept_
        lo, hi = CLAMP_BOUNDS[name]
        cpp_type = CPP_TYPE[name]
        expr = (
            f"{coefs[0]:.8f} * f.meanNameLength + "
            f"{coefs[1]:.8f} * f.prefixSimilarityCoeff + "
            f"{coefs[2]:.8f} * f.repeatRate + "
            f"{coefs[3]:.8f} * f.scopeDepthVariance + "
            f"{coefs[4]:.8f} * f.nameEntropy + "
            f"({intercept:.8f})"
        )
        lines.append(f"        c.{name} = clampVal<{cpp_type}>({expr}, "
                      f"static_cast<{cpp_type}>({lo}), static_cast<{cpp_type}>({hi}));")
    lines.append("        return c;")
    lines.append("    }")
    lines.append("};")
    lines.append("")
    lines.append("} // namespace budgetsym")
    lines.append("")

    with open("include/predicted_thresholds.hpp", "w") as fh:
        fh.write("\n".join(lines))


def main():
    rng = random.Random(RNG_SEED)

    feature_rows = []
    target_rows = {name: [] for name in THRESHOLD_NAMES}

    combos = 1
    for v in GRID.values():
        combos *= len(v)
    print(f"Generating {N_WORKLOADS} synthetic workloads and grid-searching {combos} configs each...")

    for i in range(N_WORKLOADS):
        mean_len = rng.uniform(4, 20)
        prefix_coeff = rng.uniform(0, 0.8)
        repeat_rate = rng.uniform(0, 0.5)
        depth_var = rng.uniform(0, 5)
        entropy = rng.uniform(2, 5)

        identifiers, depths = generate_workload(
            rng, mean_len, prefix_coeff, repeat_rate, depth_var, entropy, IDENTIFIERS_PER_WORKLOAD)
        features = extract_features_python(identifiers, depths)
        best_cfg, best_ratio = grid_search_best_config(identifiers)

        feature_rows.append([
            features["meanNameLength"], features["prefixSimilarityCoeff"],
            features["repeatRate"], features["scopeDepthVariance"], features["nameEntropy"],
        ])
        for name in THRESHOLD_NAMES:
            target_rows[name].append(best_cfg[name])

        if (i + 1) % 50 == 0:
            print(f"  {i + 1}/{N_WORKLOADS} workloads processed "
                  f"(last best compression ratio: {best_ratio:.3f}x)")

    X = np.array(feature_rows)
    print(f"\nTraining data: {X.shape[0]} samples x {X.shape[1]} features")

    # ---- Step 2: 7-model comparison (PRD Section 6.5 Step 2, Table III) ----
    print("\nComparing 7 models per threshold...")
    ridge_models = {}  # kept for include/predicted_thresholds.hpp export below
    comparison_rows = []
    for model_name, make_model in MODEL_SPECS.items():
        per_threshold_models = {}
        r2s, maes = [], []
        for name in THRESHOLD_NAMES:
            y = np.array(target_rows[name], dtype=float)
            X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=RNG_SEED)
            model = make_model()
            model.fit(X_train, y_train)
            y_pred = model.predict(X_test)
            r2 = r2_score(y_test, y_pred)
            mae = mean_absolute_error(y_test, y_pred)
            r2s.append(r2)
            maes.append(mae)
            per_threshold_models[name] = model
            if model_name == "Ridge":
                ridge_models[name] = model

        downstream_ratio = downstream_compression_ratio(per_threshold_models)
        mean_r2 = float(np.mean(r2s))
        mean_mae = float(np.mean(maes))
        comparison_rows.append({
            "model": model_name,
            "mean_r2": mean_r2,
            "mean_mae": mean_mae,
            "downstream_compression_ratio": downstream_ratio,
            "exportable_as_cpp": MODEL_EXPORTABLE[model_name],
        })
        print(f"  {model_name}: mean R^2 = {mean_r2:.4f}  mean MAE = {mean_mae:.4f}  "
              f"downstream compression = {downstream_ratio:.3f}x  "
              f"exportable = {MODEL_EXPORTABLE[model_name]}")

    with open("results/model_comparison.csv", "w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=[
            "model", "mean_r2", "mean_mae", "downstream_compression_ratio", "exportable_as_cpp",
        ])
        writer.writeheader()
        writer.writerows(comparison_rows)
    print("Wrote results/model_comparison.csv")

    for name in THRESHOLD_NAMES:
        model = ridge_models[name]
        print(f"  Ridge/{name}: coef = {np.round(model.coef_, 5)}  intercept = {model.intercept_:.5f}")

    write_header(ridge_models)
    print("\nWrote include/predicted_thresholds.hpp")

    # ---- Step 3: Ridge feature ablation (PRD Section 6.5 Step 3, Table VII) ----
    print("\nRunning Ridge feature ablation (drop one feature at a time)...")
    full_r2_by_threshold = {}
    for name in THRESHOLD_NAMES:
        y = np.array(target_rows[name], dtype=float)
        X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=RNG_SEED)
        model = Ridge(alpha=1.0)
        model.fit(X_train, y_train)
        full_r2_by_threshold[name] = r2_score(y_test, model.predict(X_test))

    feature_names = ["meanNameLength", "prefixSimilarityCoeff", "repeatRate", "scopeDepthVariance", "nameEntropy"]
    ablation_rows = []
    for dropped_idx, dropped_feature in enumerate(feature_names):
        keep_idx = [i for i in range(len(feature_names)) if i != dropped_idx]
        X_ablated = X[:, keep_idx]
        deltas = {}
        for name in THRESHOLD_NAMES:
            y = np.array(target_rows[name], dtype=float)
            X_train, X_test, y_train, y_test = train_test_split(
                X_ablated, y, test_size=0.2, random_state=RNG_SEED)
            model = Ridge(alpha=1.0)
            model.fit(X_train, y_train)
            ablated_r2 = r2_score(y_test, model.predict(X_test))
            deltas[name] = full_r2_by_threshold[name] - ablated_r2

        mean_delta = float(np.mean(list(deltas.values())))
        most_affected = max(deltas, key=lambda n: abs(deltas[n]))
        ablation_rows.append({
            "dropped_feature": dropped_feature,
            "mean_r2_delta": mean_delta,
            "most_affected_threshold": most_affected,
            "most_affected_r2_delta": deltas[most_affected],
        })
        print(f"  drop {dropped_feature}: mean R^2 delta = {mean_delta:+.4f}  "
              f"most affected = {most_affected} ({deltas[most_affected]:+.4f})")

    with open("results/feature_ablation.csv", "w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=[
            "dropped_feature", "mean_r2_delta", "most_affected_threshold", "most_affected_r2_delta",
        ])
        writer.writeheader()
        writer.writerows(ablation_rows)
    print("Wrote results/feature_ablation.csv")


if __name__ == "__main__":
    main()
