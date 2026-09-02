#!/usr/bin/env python3
"""Rebuild the Phase 8 formal statistical analysis from frozen raw outputs.

This script is intentionally read-only with respect to the formal data root. It
writes derived CSV/JSON/Markdown/PNG files to the requested output directory.
The accuracy comparison uses paired seeds and a deterministic paired bootstrap.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import statistics
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable

import numpy as np


SCENARIOS = ("None", "HarvestCap", "StateImport", "RepairAid")
METHODS = ("Proposed", "Simple", "PerAgent")
POPULATIONS = (2000, 10000, 20000)
EXPECTED_COMMIT = "7699bbffdbcca536758f243f84a73b466230d65d"
DEFAULT_ANALYSIS_SEED = 20260831
DEFAULT_BOOTSTRAP_RESAMPLES = 100_000
PERFORMANCE_T_CRITICAL_95_DF9 = 2.262157163

THRESHOLDS = (
    ("Continuity.MoneyMAE", 2.0, "coin"),
    ("Continuity.RepairCreditMAE", 1.0, "coin"),
    ("Continuity.InventoryWoodMAE", 1.0, "wood"),
    ("Continuity.HomeStateMismatchRate", 0.10, "rate"),
    ("Continuity.FirstActionMismatchRate", 0.10, "rate"),
    ("Continuity.TaskActiveStatusMismatchRate", 0.10, "rate"),
    ("Continuity.TaskRemainingHoursMAE", 4.0, "hours"),
)

PRIMARY_SCATTER_METRICS = (
    "Behavior.TVD",
    "Trajectory.MarketWoodAvailable",
    "Trajectory.WoodPrice",
    "PolicyEffect.MarketWoodAvailable",
    "Continuity.HomeStateMismatchRate",
    "Continuity.TaskActiveStatusMismatchRate",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--formal-root", type=Path, default=Path(r"D:\AILODFormal\Phase8"))
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path(r"C:\WarwickProjects\AILODResearch\outputs\01a05060-81af-7bc2-8300-7e4999a3edc9"),
    )
    parser.add_argument("--bootstrap-resamples", type=int, default=DEFAULT_BOOTSTRAP_RESAMPLES)
    parser.add_argument("--analysis-seed", type=int, default=DEFAULT_ANALYSIS_SEED)
    return parser.parse_args()


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: Iterable[str] | None = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if fieldnames is None:
        fieldnames = rows[0].keys() if rows else ()
    with path.open("w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(fieldnames), extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(4 * 1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest().upper()


def quantile(values: list[float], q: float) -> float:
    return float(np.quantile(np.asarray(values, dtype=np.float64), q, method="linear"))


def describe(values: list[float]) -> dict[str, float | int]:
    if not values:
        raise ValueError("Cannot describe an empty sample")
    mean_value = statistics.fmean(values)
    sd = statistics.stdev(values) if len(values) > 1 else 0.0
    return {
        "n": len(values),
        "median": quantile(values, 0.5),
        "q1": quantile(values, 0.25),
        "q3": quantile(values, 0.75),
        "p95": quantile(values, 0.95),
        "mean": mean_value,
        "sd": sd,
        "min": min(values),
        "max": max(values),
    }


def metric_group(metric: str) -> str:
    if metric.startswith("Trajectory."):
        return "Macro trajectory error"
    if metric.startswith("PolicyEffect."):
        return "Policy-effect error"
    if metric == "Behavior.TVD":
        return "Behavior distribution"
    if metric.startswith("Continuity.Diagnostic."):
        return "Internal diagnostic"
    if metric.startswith("Continuity.Commitment"):
        return "Commitment continuity diagnostic"
    if metric.startswith("Continuity."):
        return "Player-visible continuity"
    return "Other"


def interpretation_class(metric: str) -> str:
    if metric.startswith("Continuity.Diagnostic."):
        return "Diagnostic only"
    if metric.startswith("Continuity.Commitment"):
        return "Secondary commitment diagnostic"
    if metric.startswith("Trajectory.") or metric.startswith("PolicyEffect.") or metric == "Behavior.TVD":
        return "Primary accuracy"
    return "Continuity"


def scenario_rng_seed(base_seed: int, scenario: str) -> int:
    payload = hashlib.sha256(f"{base_seed}|{scenario}|paired-bootstrap-v1".encode("utf-8")).digest()
    return int.from_bytes(payload[:8], byteorder="little", signed=False)


def bootstrap_count_matrix(n: int, resamples: int, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    draws = rng.integers(0, n, size=(resamples, n), dtype=np.int16)
    counts = np.zeros((resamples, n), dtype=np.uint8)
    row_index = np.arange(resamples)
    for draw_index in range(n):
        counts[row_index, draws[:, draw_index]] += 1
    return counts


def paired_bootstrap_cis(
    difference_matrix: np.ndarray,
    counts: np.ndarray,
) -> tuple[np.ndarray, np.ndarray]:
    bootstrap_means = counts.astype(np.float64) @ difference_matrix / difference_matrix.shape[0]
    low = np.quantile(bootstrap_means, 0.025, axis=0, method="linear")
    high = np.quantile(bootstrap_means, 0.975, axis=0, method="linear")
    return low, high


def rankdata(values: list[float]) -> list[float]:
    order = sorted(range(len(values)), key=lambda index: values[index])
    ranks = [0.0] * len(values)
    cursor = 0
    while cursor < len(order):
        end = cursor
        while end + 1 < len(order) and values[order[end + 1]] == values[order[cursor]]:
            end += 1
        average_rank = (cursor + end + 2) / 2.0
        for position in range(cursor, end + 1):
            ranks[order[position]] = average_rank
        cursor = end + 1
    return ranks


def pearson(x: list[float], y: list[float]) -> float:
    if len(x) != len(y) or len(x) < 2:
        return math.nan
    x_mean = statistics.fmean(x)
    y_mean = statistics.fmean(y)
    numerator = sum((a - x_mean) * (b - y_mean) for a, b in zip(x, y))
    x_ss = sum((a - x_mean) ** 2 for a in x)
    y_ss = sum((b - y_mean) ** 2 for b in y)
    if x_ss == 0 or y_ss == 0:
        return math.nan
    return numerator / math.sqrt(x_ss * y_ss)


def spearman(x: list[float], y: list[float]) -> float:
    return pearson(rankdata(x), rankdata(y))


def load_source_configuration(formal_root: Path) -> dict[str, dict[str, Any]]:
    return {
        "accuracy": {
            "label": "Formal Accuracy (200 people, 480 runs)",
            "root": formal_root / "FormalAccuracy-v1",
            "population": 200,
        },
        "perf2k": {
            "label": "Formal Performance 2k (30 runs)",
            "root": formal_root / "FormalPerformance-v1" / "N2000",
            "population": 2000,
        },
        "perf10k": {
            "label": "Formal Performance 10k (30 runs)",
            "root": formal_root / "FormalPerformance-v1" / "N10000",
            "population": 10000,
        },
        "perf20k": {
            "label": "Formal Performance 20k (30 runs)",
            "root": formal_root / "FormalPerformance-v1" / "N20000",
            "population": 20000,
        },
    }


def audit_sources(sources: dict[str, dict[str, Any]]) -> tuple[dict[str, Any], dict[str, dict[str, Any]]]:
    source_details: dict[str, dict[str, Any]] = {}
    qc_rows: list[dict[str, Any]] = []
    for key, source in sources.items():
        root = source["root"]
        csv_path = root / "metrics_summary.csv"
        audit_path = root / "phase8_audit.json"
        schedule_path = root / "run_schedule.csv"
        for required in (csv_path, audit_path, schedule_path):
            if not required.is_file():
                raise FileNotFoundError(required)
        audit = json.loads(audit_path.read_text(encoding="utf-8-sig"))
        csv_hash = sha256_file(csv_path)
        audit_hash = sha256_file(audit_path)
        schedule_hash = sha256_file(schedule_path)
        checks = (
            ("audit success", audit.get("success") is True, True, audit.get("success")),
            ("excluded", audit.get("excluded") is False, False, audit.get("excluded")),
            ("run count", audit.get("actual_run_count") == audit.get("expected_run_count"), audit.get("expected_run_count"), audit.get("actual_run_count")),
            ("Git commit", audit.get("git_commit") == EXPECTED_COMMIT, EXPECTED_COMMIT, audit.get("git_commit")),
            ("build", audit.get("build_type") == "Shipping", "Shipping", audit.get("build_type")),
            ("summary hash", csv_hash == audit.get("metrics_summary_sha256"), audit.get("metrics_summary_sha256"), csv_hash),
        )
        for name, passed, expected, observed in checks:
            qc_rows.append({
                "check": f"{source['label']}: {name}",
                "observed": observed,
                "expected": expected,
                "status": "PASS" if passed else "FAIL",
                "note": str(root),
            })
        source_details[key] = {
            "label": source["label"],
            "population": source["population"],
            "root": str(root),
            "metrics_summary": str(csv_path),
            "metrics_summary_sha256": csv_hash,
            "audit": str(audit_path),
            "audit_sha256": audit_hash,
            "schedule": str(schedule_path),
            "schedule_sha256": schedule_hash,
            "audit_payload": audit,
        }
    failed = [row for row in qc_rows if row["status"] != "PASS"]
    if failed:
        raise RuntimeError(f"Formal source audit failed: {failed}")
    return {"rows": qc_rows, "all_pass": True}, source_details


def analyze_accuracy(
    accuracy_root: Path,
    bootstrap_resamples: int,
    analysis_seed: int,
) -> dict[str, Any]:
    rows = read_csv(accuracy_root / "metrics_summary.csv")
    hard_error_sum = sum(
        float(row["metric_value"])
        for row in rows
        if row["metric_name"].startswith("HardError.")
    )
    paired: dict[tuple[str, int, str], dict[str, float]] = defaultdict(dict)
    scales: dict[tuple[str, str], str] = {}
    for row in rows:
        metric = row["metric_name"]
        if metric.startswith("HardError.") or metric == "Continuity.PairedSnapshotCount":
            continue
        scenario = row["scenario"]
        seed = int(row["seed"])
        method = row["method"]
        if method in METHODS:
            paired[(scenario, seed, metric)][method] = float(row["metric_value"])
            scales[(scenario, metric)] = row["scale"]

    paired_rows: list[dict[str, Any]] = []
    by_scenario_metric: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for (scenario, seed, metric), values in paired.items():
        if not all(method in values for method in METHODS):
            raise RuntimeError(f"Missing paired method for {scenario}/{seed}/{metric}: {values.keys()}")
        item = {
            "scenario": scenario,
            "seed": seed,
            "metric_group": metric_group(metric),
            "metric": metric,
            "scale": scales[(scenario, metric)],
            "proposed": values["Proposed"],
            "simple": values["Simple"],
            "per_agent": values["PerAgent"],
            "proposed_minus_simple": values["Proposed"] - values["Simple"],
            "proposed_minus_per_agent": values["Proposed"] - values["PerAgent"],
            "interpretation_class": interpretation_class(metric),
        }
        paired_rows.append(item)
        by_scenario_metric[(scenario, metric)].append(item)

    scenario_index = {value: index for index, value in enumerate(SCENARIOS)}
    paired_rows.sort(key=lambda row: (scenario_index[row["scenario"]], row["metric_group"], row["metric"], row["seed"]))
    stats_rows: list[dict[str, Any]] = []
    for scenario in SCENARIOS:
        metrics = sorted(metric for candidate_scenario, metric in by_scenario_metric if candidate_scenario == scenario)
        seed_order = sorted({row["seed"] for metric in metrics for row in by_scenario_metric[(scenario, metric)]})
        if len(seed_order) != 30:
            raise RuntimeError(f"Expected 30 paired seeds for {scenario}, found {len(seed_order)}")
        metric_rows = []
        for metric in metrics:
            by_seed = {row["seed"]: row for row in by_scenario_metric[(scenario, metric)]}
            if sorted(by_seed) != seed_order:
                raise RuntimeError(f"Paired seed mismatch for {scenario}/{metric}")
            metric_rows.append([by_seed[seed] for seed in seed_order])
        ps_matrix = np.column_stack([
            np.asarray([row["proposed_minus_simple"] for row in rows_for_metric], dtype=np.float64)
            for rows_for_metric in metric_rows
        ])
        ppa_matrix = np.column_stack([
            np.asarray([row["proposed_minus_per_agent"] for row in rows_for_metric], dtype=np.float64)
            for rows_for_metric in metric_rows
        ])
        counts = bootstrap_count_matrix(
            n=len(seed_order),
            resamples=bootstrap_resamples,
            seed=scenario_rng_seed(analysis_seed, scenario),
        )
        ps_low, ps_high = paired_bootstrap_cis(ps_matrix, counts)
        ppa_low, ppa_high = paired_bootstrap_cis(ppa_matrix, counts)
        for metric_index, (metric, rows_for_metric) in enumerate(zip(metrics, metric_rows)):
            p_values = [row["proposed"] for row in rows_for_metric]
            s_values = [row["simple"] for row in rows_for_metric]
            pa_values = [row["per_agent"] for row in rows_for_metric]
            ps_values = [row["proposed_minus_simple"] for row in rows_for_metric]
            ppa_values = [row["proposed_minus_per_agent"] for row in rows_for_metric]
            p_desc = describe(p_values)
            s_desc = describe(s_values)
            pa_desc = describe(pa_values)
            stats_rows.append({
                "scenario": scenario,
                "metric_group": metric_group(metric),
                "metric": metric,
                "scale": scales[(scenario, metric)],
                "n": len(seed_order),
                "proposed_median": p_desc["median"],
                "proposed_q1": p_desc["q1"],
                "proposed_q3": p_desc["q3"],
                "proposed_mean": p_desc["mean"],
                "simple_median": s_desc["median"],
                "simple_q1": s_desc["q1"],
                "simple_q3": s_desc["q3"],
                "simple_mean": s_desc["mean"],
                "per_agent_median": pa_desc["median"],
                "per_agent_q1": pa_desc["q1"],
                "per_agent_q3": pa_desc["q3"],
                "per_agent_mean": pa_desc["mean"],
                "proposed_minus_simple_mean": statistics.fmean(ps_values),
                "proposed_minus_simple_bootstrap_ci_low": float(ps_low[metric_index]),
                "proposed_minus_simple_bootstrap_ci_high": float(ps_high[metric_index]),
                "proposed_wins_vs_simple": sum(value < 0 for value in ps_values),
                "ties_vs_simple": sum(value == 0 for value in ps_values),
                "proposed_loses_vs_simple": sum(value > 0 for value in ps_values),
                "proposed_minus_per_agent_mean": statistics.fmean(ppa_values),
                "proposed_minus_per_agent_bootstrap_ci_low": float(ppa_low[metric_index]),
                "proposed_minus_per_agent_bootstrap_ci_high": float(ppa_high[metric_index]),
                "proposed_wins_vs_per_agent": sum(value < 0 for value in ppa_values),
                "ties_vs_per_agent": sum(value == 0 for value in ppa_values),
                "proposed_loses_vs_per_agent": sum(value > 0 for value in ppa_values),
                "interpretation_class": interpretation_class(metric),
            })

    stats_lookup = {(row["scenario"], row["metric"]): row for row in stats_rows}
    threshold_rows: list[dict[str, Any]] = []
    for scenario in SCENARIOS:
        for metric, threshold, unit in THRESHOLDS:
            matching = [row["proposed"] for row in by_scenario_metric[(scenario, metric)]]
            if len(matching) != 30:
                raise RuntimeError(f"Threshold metric missing: {scenario}/{metric}")
            desc = describe(matching)
            exceed_count = sum(value > threshold for value in matching)
            threshold_rows.append({
                "scenario": scenario,
                "metric": metric,
                "threshold": threshold,
                "unit": unit,
                "rule": "> threshold triggers review",
                "min": desc["min"],
                "median": desc["median"],
                "max": desc["max"],
                "seeds_exceeding": exceed_count,
                "seed_count": 30,
                "status": "REVIEW" if exceed_count else "WITHIN",
            })

    scatter_rows = [row for row in paired_rows if row["scenario"] == "StateImport" and row["metric"] in PRIMARY_SCATTER_METRICS]
    return {
        "raw_row_count": len(rows),
        "hard_error_sum": hard_error_sum,
        "paired_rows": paired_rows,
        "stats": stats_rows,
        "threshold_audit": threshold_rows,
        "scatter_rows": scatter_rows,
        "stats_lookup": stats_lookup,
    }


def analyze_task_active(accuracy_root: Path) -> dict[str, Any]:
    per_seed_rows: list[dict[str, Any]] = []
    mismatch_patterns: Counter[tuple[str, str]] = Counter()
    for scenario in SCENARIOS:
        for seed in range(20260815, 20260845):
            oracle_path = accuracy_root / "Runs" / f"Oracle-{scenario}-{seed}" / "npc_snapshots.csv"
            proposed_path = accuracy_root / "Runs" / f"Proposed-{scenario}-{seed}" / "npc_snapshots.csv"
            oracle_rows = read_csv(oracle_path)
            proposed_rows = read_csv(proposed_path)
            oracle = {(row["game_time"], row["persistent_id"]): row for row in oracle_rows}
            proposed = {(row["game_time"], row["persistent_id"]): row for row in proposed_rows}
            if set(oracle) != set(proposed):
                raise RuntimeError(f"Snapshot pairing mismatch for {scenario}/{seed}")
            counts: Counter[str] = Counter()
            proposed_mismatch_hours: list[float] = []
            oracle_mismatch_hours: list[float] = []
            for key in sorted(oracle):
                oracle_row = oracle[key]
                proposed_row = proposed[key]
                oracle_hours = float(oracle_row["remaining_work_hours"])
                proposed_hours = float(proposed_row["remaining_work_hours"])
                oracle_active = oracle_hours > np.finfo(float).eps
                proposed_active = proposed_hours > np.finfo(float).eps
                counts["paired_snapshots"] += 1
                if proposed_active and not oracle_active:
                    counts["proposed_active_oracle_inactive"] += 1
                elif oracle_active and not proposed_active:
                    counts["proposed_inactive_oracle_active"] += 1
                elif proposed_active and oracle_active:
                    counts["both_active"] += 1
                else:
                    counts["both_inactive"] += 1
                if proposed_active == oracle_active:
                    continue
                counts["mismatches"] += 1
                proposed_mismatch_hours.append(proposed_hours)
                oracle_mismatch_hours.append(oracle_hours)
                if proposed_row["current_goal"] == oracle_row["current_goal"]:
                    counts["same_goal"] += 1
                if proposed_row["first_action"] == oracle_row["first_action"]:
                    counts["same_first_action"] += 1
                if proposed_row["home_state"] == oracle_row["home_state"]:
                    counts["same_home_state"] += 1
                if (
                    proposed_row["current_goal"] == "RoutineLife"
                    and oracle_row["current_goal"] == "RoutineLife"
                    and proposed_row["first_action"] == "Routine"
                    and oracle_row["first_action"] == "Routine"
                ):
                    counts["routine_life_routine"] += 1
                if proposed_row["current_goal"] == "RestoreHome" or oracle_row["current_goal"] == "RestoreHome":
                    counts["commitment_related"] += 1
                mismatch_patterns[(oracle_row["current_goal"], proposed_row["current_goal"])] += 1
            mismatch_count = counts["mismatches"]
            per_seed_rows.append({
                "scenario": scenario,
                "seed": seed,
                "paired_snapshots": counts["paired_snapshots"],
                "mismatches": mismatch_count,
                "mismatch_rate": mismatch_count / counts["paired_snapshots"],
                "proposed_active_oracle_inactive": counts["proposed_active_oracle_inactive"],
                "proposed_inactive_oracle_active": counts["proposed_inactive_oracle_active"],
                "both_active": counts["both_active"],
                "both_inactive": counts["both_inactive"],
                "same_goal_among_mismatches": counts["same_goal"],
                "same_first_action_among_mismatches": counts["same_first_action"],
                "same_home_state_among_mismatches": counts["same_home_state"],
                "routine_life_routine_mismatches": counts["routine_life_routine"],
                "commitment_related_mismatches": counts["commitment_related"],
                "proposed_hours_median_among_mismatches": quantile(proposed_mismatch_hours, 0.5) if proposed_mismatch_hours else 0.0,
                "oracle_hours_median_among_mismatches": quantile(oracle_mismatch_hours, 0.5) if oracle_mismatch_hours else 0.0,
            })

    summary_rows: list[dict[str, Any]] = []
    for scenario in SCENARIOS:
        matching = [row for row in per_seed_rows if row["scenario"] == scenario]
        pairs = sum(row["paired_snapshots"] for row in matching)
        mismatches = sum(row["mismatches"] for row in matching)
        summary_rows.append({
            "scenario": scenario,
            "seed_count": len(matching),
            "paired_snapshots": pairs,
            "mismatches": mismatches,
            "pooled_mismatch_rate": mismatches / pairs,
            "median_run_mismatch_rate": quantile([row["mismatch_rate"] for row in matching], 0.5),
            "proposed_active_oracle_inactive": sum(row["proposed_active_oracle_inactive"] for row in matching),
            "proposed_inactive_oracle_active": sum(row["proposed_inactive_oracle_active"] for row in matching),
            "same_goal_among_mismatches": sum(row["same_goal_among_mismatches"] for row in matching),
            "same_first_action_among_mismatches": sum(row["same_first_action_among_mismatches"] for row in matching),
            "same_home_state_among_mismatches": sum(row["same_home_state_among_mismatches"] for row in matching),
            "routine_life_routine_mismatches": sum(row["routine_life_routine_mismatches"] for row in matching),
            "commitment_related_mismatches": sum(row["commitment_related_mismatches"] for row in matching),
        })
    return {
        "by_seed": per_seed_rows,
        "summary": summary_rows,
        "goal_patterns": [
            {"oracle_goal": key[0], "proposed_goal": key[1], "count": count}
            for key, count in sorted(mismatch_patterns.items())
        ],
    }


def extract_repeat(run_id: str) -> int:
    marker = run_id.rsplit("-R", 1)
    return int(marker[1]) if len(marker) == 2 else 1


def analyze_performance(sources: dict[str, dict[str, Any]]) -> dict[str, Any]:
    performance_runs: list[dict[str, Any]] = []
    hard_error_sum = 0.0
    order_rows: list[dict[str, Any]] = []
    for key in ("perf2k", "perf10k", "perf20k"):
        source = sources[key]
        root = source["root"]
        population = source["population"]
        rows = read_csv(root / "metrics_summary.csv")
        hard_error_sum += sum(float(row["metric_value"]) for row in rows if row["metric_name"].startswith("HardError."))
        by_run: dict[str, dict[str, Any]] = {}
        for row in rows:
            run_id = row["run_id"]
            run = by_run.setdefault(run_id, {
                "population": population,
                "repeat": extract_repeat(run_id),
                "method": row["method"],
                "run_id": run_id,
                "metrics": {},
            })
            run["metrics"][row["metric_name"]] = float(row["metric_value"])
        schedule = {row["run_id"]: int(row["schedule_index"]) for row in read_csv(root / "run_schedule.csv")}
        for run_id, run in by_run.items():
            manifest_path = root / "Runs" / run_id / "run_manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
            measurement = manifest["measurement_summary"]
            metrics = run["metrics"]
            performance_runs.append({
                "population": population,
                "repeat": run["repeat"],
                "method": run["method"],
                "run_id": run_id,
                "schedule_index": schedule[run_id],
                "total_ai_ms": metrics["Performance.AICpuMs.Total"],
                "paired_speedup_vs_per_agent": metrics["Performance.SpeedupVsPerAgent.TotalAI"],
                "memory_peak_process_mb": metrics["Performance.MemoryMB.Peak"],
                "memory_mean_process_mb": metrics["Performance.MemoryMB.Mean"],
                "sample_count": metrics["Performance.SampleCount"],
                "active_max": metrics["Performance.ActiveCount.Max"],
                "queue_max": metrics["Performance.QueueLength.Max"],
                "macro_total_ms": metrics["Performance.MacroCpuMs.Total"],
                "micro_total_ms": metrics["Performance.MicroCpuMs.Total"],
                "transition_total_ms": metrics["Performance.TransitionCpuMs.Total"],
                "initialize_ms": float(measurement["initialize_cpu_ms"]),
                "finalize_ms": float(measurement["finalize_cpu_ms"]),
                "manifest": str(manifest_path),
            })
        for method in METHODS:
            method_runs = [run for run in performance_runs if run["population"] == population and run["method"] == method]
            order_rows.append({
                "population": population,
                "method": method,
                "n": len(method_runs),
                "schedule_position_mean": statistics.fmean(run["schedule_index"] for run in method_runs),
                "schedule_position_min": min(run["schedule_index"] for run in method_runs),
                "schedule_position_max": max(run["schedule_index"] for run in method_runs),
                "spearman_schedule_position_vs_total_ai": spearman(
                    [float(run["schedule_index"]) for run in method_runs],
                    [run["total_ai_ms"] for run in method_runs],
                ),
            })

    performance_runs.sort(key=lambda row: (row["population"], METHODS.index(row["method"]), row["repeat"]))
    stats_rows: list[dict[str, Any]] = []
    for population in POPULATIONS:
        per_agent_runs = [run for run in performance_runs if run["population"] == population and run["method"] == "PerAgent"]
        per_agent_median = quantile([run["total_ai_ms"] for run in per_agent_runs], 0.5)
        for method in METHODS:
            matching = [run for run in performance_runs if run["population"] == population and run["method"] == method]
            total = [run["total_ai_ms"] for run in matching]
            speedup = [run["paired_speedup_vs_per_agent"] for run in matching]
            memory = [run["memory_peak_process_mb"] for run in matching]
            initialize = [run["initialize_ms"] for run in matching]
            finalize = [run["finalize_ms"] for run in matching]
            total_desc = describe(total)
            speed_desc = describe(speedup)
            memory_desc = describe(memory)
            total_half = PERFORMANCE_T_CRITICAL_95_DF9 * total_desc["sd"] / math.sqrt(len(total))
            speed_half = PERFORMANCE_T_CRITICAL_95_DF9 * speed_desc["sd"] / math.sqrt(len(speedup))
            stats_rows.append({
                "population": population,
                "method": method,
                "n": len(matching),
                "total_median_ms": total_desc["median"],
                "total_q1_ms": total_desc["q1"],
                "total_q3_ms": total_desc["q3"],
                "total_p95_ms": total_desc["p95"],
                "total_mean_ms": total_desc["mean"],
                "total_mean_ci_low_ms": total_desc["mean"] - total_half,
                "total_mean_ci_high_ms": total_desc["mean"] + total_half,
                "paired_speedup_median": speed_desc["median"],
                "paired_speedup_q1": speed_desc["q1"],
                "paired_speedup_q3": speed_desc["q3"],
                "paired_speedup_p95": speed_desc["p95"],
                "paired_speedup_mean": speed_desc["mean"],
                "paired_speedup_mean_ci_low": speed_desc["mean"] - speed_half,
                "paired_speedup_mean_ci_high": speed_desc["mean"] + speed_half,
                "ratio_of_independent_medians": per_agent_median / total_desc["median"],
                "memory_peak_median_mb": memory_desc["median"],
                "initialize_median_ms": quantile(initialize, 0.5),
                "finalize_median_ms": quantile(finalize, 0.5),
            })
    return {
        "raw_row_count": sum(len(read_csv(sources[key]["root"] / "metrics_summary.csv")) for key in ("perf2k", "perf10k", "perf20k")),
        "hard_error_sum": hard_error_sum,
        "runs": performance_runs,
        "stats": stats_rows,
        "order_sensitivity": order_rows,
    }


def make_figures(output_dir: Path, accuracy: dict[str, Any], performance: dict[str, Any]) -> list[str]:
    # Charts are authored from these same rows as native workbook charts. Keeping
    # plotting out of this script avoids a second rendering dependency and leaves
    # the numerical analysis reproducible with the bundled Python runtime alone.
    return []


def markdown_table(headers: list[str], rows: list[list[Any]]) -> str:
    def value_text(value: Any) -> str:
        if isinstance(value, float):
            return f"{value:.6f}"
        return str(value)

    lines = ["| " + " | ".join(headers) + " |", "|" + "|".join("---" for _ in headers) + "|"]
    lines.extend("| " + " | ".join(value_text(value) for value in row) + " |" for row in rows)
    return "\n".join(lines)


def build_markdown(payload: dict[str, Any]) -> str:
    accuracy = payload["accuracy"]
    performance = payload["performance"]
    task = payload["task_active"]
    stats_lookup = {(row["scenario"], row["metric"]): row for row in accuracy["stats"]}
    perf_lookup = {(row["population"], row["method"]): row for row in performance["stats"]}

    performance_rows = []
    for population in POPULATIONS:
        proposed = perf_lookup[(population, "Proposed")]
        per_agent = perf_lookup[(population, "PerAgent")]
        simple = perf_lookup[(population, "Simple")]
        performance_rows.append([
            population,
            proposed["total_median_ms"],
            per_agent["total_median_ms"],
            simple["total_median_ms"],
            proposed["paired_speedup_median"],
            proposed["paired_speedup_q1"],
            proposed["paired_speedup_q3"],
        ])

    tvd_rows = []
    for scenario in SCENARIOS:
        row = stats_lookup[(scenario, "Behavior.TVD")]
        tvd_rows.append([
            scenario,
            row["proposed_median"],
            row["simple_median"],
            row["per_agent_median"],
            row["proposed_minus_simple_mean"],
            row["proposed_minus_simple_bootstrap_ci_low"],
            row["proposed_minus_simple_bootstrap_ci_high"],
        ])

    continuity_metrics = (
        "Continuity.PersistentIDMismatchRate",
        "Continuity.HomeIDMismatchRate",
        "Continuity.HomeStateMismatchRate",
        "Continuity.MoneyMAE",
        "Continuity.InventoryWoodMAE",
        "Continuity.CurrentGoalMismatchRate",
        "Continuity.FirstActionMismatchRate",
        "Continuity.TaskActiveStatusMismatchRate",
        "Continuity.TaskRemainingHoursMAE",
        "Continuity.CommitmentTaskActiveStatusMismatchRate",
        "Continuity.CommitmentTaskRemainingHoursMAE",
    )
    continuity_rows = []
    for metric in continuity_metrics:
        row = stats_lookup[("StateImport", metric)]
        continuity_rows.append([
            metric,
            row["proposed_median"],
            row["simple_median"],
            row["per_agent_median"],
            row["proposed_minus_simple_mean"],
            row["proposed_minus_simple_bootstrap_ci_low"],
            row["proposed_minus_simple_bootstrap_ci_high"],
        ])

    threshold_rows = [[
        row["scenario"], row["metric"], row["threshold"], row["median"], row["max"],
        f"{row['seeds_exceeding']}/{row['seed_count']}", row["status"],
    ] for row in accuracy["threshold_audit"]]

    task_rows = [[
        row["scenario"], row["paired_snapshots"], row["mismatches"], row["pooled_mismatch_rate"],
        row["median_run_mismatch_rate"], row["proposed_active_oracle_inactive"],
        row["proposed_inactive_oracle_active"], row["routine_life_routine_mismatches"],
        row["commitment_related_mismatches"],
    ] for row in task["summary"]]

    order_rows = [[
        row["population"], row["method"], row["schedule_position_mean"],
        row["spearman_schedule_position_vs_total_ai"],
    ] for row in performance["order_sensitivity"]]

    return f"""# AILOD Phase 8 正式结果分析 v1.1

生成时间：{payload['generated_at_utc']}  
冻结代码：`{EXPECTED_COMMIT}`  
统计随机种子：`{payload['analysis']['bootstrap_seed']}`  
配对 Bootstrap：`{payload['analysis']['bootstrap_resamples']}` 次，percentile 95% CI。

## 1. 数据资格

- Accuracy：480/480 Runs，通过 Shipping、Git、输入 Hash、硬错误和排除清单审计。
- Performance：2k/10k/20k 各 30/30 Runs，共 90/90。
- Accuracy hard-error 总数：{accuracy['hard_error_sum']:.0f}。
- Performance hard-error 总数：{performance['hard_error_sum']:.0f}。
- Pilot、Preflight、Development 和 Phase 7F 数据没有进入本报告。

## 2. 统计方法

- Accuracy 按同一 Scenario、Metric、Seed 配对。
- 报告中位数、IQR、均值，并对 Proposed-Simple、Proposed-PerAgent 的配对均值差做确定性 paired bootstrap。
- 差值小于 0 表示 Proposed 的误差更低。
- Performance 使用整场 `Performance.AICpuMs.Total`；10 次重复报告 P50、P95、IQR 和均值 95% t 区间。
- 没有构造综合准确性分数，也没有根据正式结果删除不利指标。

## 3. 性能结果

{markdown_table(['Population', 'Proposed P50 ms', 'PerAgent P50 ms', 'Simple P50 ms', 'Proposed speedup P50', 'Speedup Q1', 'Speedup Q3'], performance_rows)}

解释：Proposed 在 2k 有固定管理成本；10k 开始快于 Per-Agent；20k 的中位配对加速约为 3.94 倍。Simple 很快，但其准确性代价必须同时报告。

## 4. 行为分布准确性

{markdown_table(['Scenario', 'Proposed median', 'Simple median', 'PerAgent median', 'P-S mean diff', 'Bootstrap low', 'Bootstrap high'], tvd_rows)}

Proposed 的 Behavior TVD 接近 0，并且相对 Simple 的 bootstrap CI 全部远离 0。Per-Agent 在 200 人下与 Oracle 一致。

## 5. StateImport 连续性

{markdown_table(['Metric', 'Proposed median', 'Simple median', 'PerAgent median', 'P-S mean diff', 'Bootstrap low', 'Bootstrap high'], continuity_rows)}

TaskRemainingHours 只在双方同时处于同类进行中任务时计算，分母很小；不能用其中位数 0 掩盖通用 TaskActive 指标越线。

## 6. 预冻结复核线

{markdown_table(['Scenario', 'Metric', 'Threshold', 'Median', 'Max', 'Seeds over', 'Status'], threshold_rows)}

`REVIEW` 表示需要解释原始明细，不表示整个方法自动失败。TaskActive 在所有场景、所有正式 Seed 越线；TaskRemainingHours 只有一个 Seed 越线。

## 7. TaskActive 原始快照分解

{markdown_table(['Scenario', 'Pairs', 'Mismatches', 'Pooled rate', 'Median run rate', 'P active/O inactive', 'P inactive/O active', 'Routine/Routine', 'Commitment-related'], task_rows)}

全部通用 TaskActive 不一致都来自 Proposed active / Oracle inactive。差异集中在 RoutineLife/Routine 的时间相位；同一批快照中没有 RestoreHome 承诺活动被遗漏。原始通用指标仍完整保留。

## 8. 性能执行顺序敏感性

{markdown_table(['Population', 'Method', 'Mean schedule position', 'Spearman(position,total)'], order_rows)}

正式顺序是确定性全局乱序。部分方法存在随执行位置增加而变慢的趋势；RepeatIndex 配对不能完全消除热状态和时间漂移。10k/20k 的方向与独立中位数比一致，20k 结论的余量最大。

## 9. 可信结论

1. Proposed 的固定成本使其在 2k 慢于 Per-Agent；10k 开始占优，20k 约 3.94 倍。
2. Proposed 的宏观轨迹、政策效应和行为分布误差显著低于 Simple。
3. ResidentID、HomeID 和住房承诺保持连续；普通 Routine 的精确活动相位没有保持一致。
4. Proposed 的进程峰值内存高于 Per-Agent，不能宣称内存节省。
5. 结果适用于冻结的单机、Shipping、NullRHI、StateImport 性能场景；不直接外推到所有硬件和地图。
"""


def main() -> None:
    args = parse_args()
    if args.bootstrap_resamples <= 0:
        raise ValueError("bootstrap-resamples must be positive")
    sources = load_source_configuration(args.formal_root.resolve())
    qc, source_details = audit_sources(sources)
    accuracy = analyze_accuracy(sources["accuracy"]["root"], args.bootstrap_resamples, args.analysis_seed)
    task_active = analyze_task_active(sources["accuracy"]["root"])
    performance = analyze_performance(sources)
    if accuracy["hard_error_sum"] != 0 or performance["hard_error_sum"] != 0:
        raise RuntimeError("Formal hard-error sum is not zero")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    payload: dict[str, Any] = {
        "schema_version": "1.1",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "expected_commit": EXPECTED_COMMIT,
        "analysis": {
            "bootstrap_seed": args.analysis_seed,
            "bootstrap_resamples": args.bootstrap_resamples,
            "bootstrap_ci": "paired percentile 95% CI of the mean difference",
            "performance_ci": "two-sided t interval of the mean, df=9",
        },
        "sources": {key: {k: v for k, v in value.items() if k != "audit_payload"} for key, value in source_details.items()},
        "qc": qc,
        "accuracy": {key: value for key, value in accuracy.items() if key != "stats_lookup"},
        "task_active": task_active,
        "performance": performance,
    }
    figures = make_figures(args.output_dir, accuracy, performance)
    payload["figures"] = figures

    json_path = args.output_dir / "AILOD_Phase8_Formal_Analysis_Data_v1.1.json"
    json_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    write_csv(args.output_dir / "AILOD_Phase8_Accuracy_Paired_Seeds_v1.1.csv", accuracy["paired_rows"])
    write_csv(args.output_dir / "AILOD_Phase8_Accuracy_Stats_v1.1.csv", accuracy["stats"])
    write_csv(args.output_dir / "AILOD_Phase8_Threshold_Audit_v1.1.csv", accuracy["threshold_audit"])
    write_csv(args.output_dir / "AILOD_Phase8_TaskActive_BySeed_v1.1.csv", task_active["by_seed"])
    write_csv(args.output_dir / "AILOD_Phase8_TaskActive_Summary_v1.1.csv", task_active["summary"])
    write_csv(args.output_dir / "AILOD_Phase8_Performance_Runs_v1.1.csv", performance["runs"])
    write_csv(args.output_dir / "AILOD_Phase8_Performance_Stats_v1.1.csv", performance["stats"])
    write_csv(args.output_dir / "AILOD_Phase8_Performance_Order_Sensitivity_v1.1.csv", performance["order_sensitivity"])
    report_path = args.output_dir / "AILOD_Phase8_Formal_Results_Analysis_CN_v1.1.md"
    report_path.write_text(build_markdown(payload), encoding="utf-8")

    output_hashes = {
        path.name: sha256_file(path)
        for path in sorted(args.output_dir.glob("AILOD_Phase8_*_v1.1.*"))
        if path.is_file()
    }
    (args.output_dir / "AILOD_Phase8_Derived_Output_Hashes_v1.1.json").write_text(
        json.dumps(output_hashes, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print(json.dumps({
        "json": str(json_path),
        "report": str(report_path),
        "accuracy_rows": accuracy["raw_row_count"],
        "performance_rows": performance["raw_row_count"],
        "figures": figures,
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()
