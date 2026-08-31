#!/usr/bin/env python3
"""Summarise UE CSV Profiler captures produced by Phase 7F-E."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path
from statistics import fmean
from typing import Iterable


SCALAR_STATS = {
    "frame_ms": "FrameTime",
    "game_ms": "GameThreadTime",
    "render_ms": "RenderThreadTime",
    "gpu_ms": "GPUTime",
    "draw_calls": "RHI/DrawCalls",
    "process_ram_mb": "PhysicalUsedMB",
    "vram_used_mb": "GPUMem/UsedMB",
    "gpu_scene_instances": "GPUSceneInstanceCount",
    "population": "Population",
    "time_scale": "TimeScale",
    "runtime_state": "RuntimeState",
    "active_count": "ActiveCount",
    "proxy_count": "ProxyCount",
    "active_actor_count": "ActiveActorCount",
    "actor_pool_capacity": "ActorPoolCapacity",
    "motion_updates": "MotionUpdates",
    "normal_visited_cells": "NormalVisitedCells",
    "normal_visited_residents": "NormalVisitedResidents",
    "telescope_visited_cells": "TelescopeVisitedCells",
    "telescope_visited_residents": "TelescopeVisitedResidents",
    "telescope_enabled": "TelescopeEnabled",
    "telescope_streaming_ready": "TelescopeStreamingReady",
    "tracked_resident": "TrackedResident",
    "full_population_scan": "FullPopulationScan",
    "view_speed": "View/Speed",
    "view_pos_x": "View/PosX",
    "view_pos_y": "View/PosY",
    "loaded_level_count": "LoadedLevelCount",
    "world_partition_streaming_ready": "WorldPartitionStreamingReady",
}


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        return math.nan
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def parse_number(value: str) -> float | None:
    if not value:
        return None
    try:
        number = float(value)
    except ValueError:
        return None
    return number if math.isfinite(number) else None


def find_column(header: list[str], requested: str) -> int | None:
    try:
        return header.index(requested)
    except ValueError:
        pass
    suffix = f"/{requested}".lower()
    matches = [
        index
        for index, name in enumerate(header)
        if name.lower().endswith(suffix) and not name.startswith("COUNTS/")
    ]
    return matches[0] if len(matches) == 1 else None


def read_capture(path: Path) -> tuple[list[str], list[list[str]], dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.reader(stream))
    if len(rows) < 3:
        raise ValueError("CSV has fewer than three rows")

    metadata_row = rows[-1]
    has_header_at_end = (
        len(metadata_row) >= 2
        and metadata_row[0] == "[HasHeaderRowAtEnd]"
        and metadata_row[1] == "1"
    )
    header = rows[-2] if has_header_at_end else rows[0]
    data_start = 1
    data_end = -2 if has_header_at_end else -1
    data_rows = rows[data_start:data_end]

    metadata: dict[str, str] = {}
    for index in range(0, len(metadata_row) - 1, 2):
        key = metadata_row[index].strip("[]").lower()
        metadata[key] = metadata_row[index + 1]
    return header, data_rows, metadata


def column_values(rows: list[list[str]], index: int | None) -> list[float]:
    if index is None:
        return []
    values: list[float] = []
    for row in rows:
        if index >= len(row):
            continue
        number = parse_number(row[index])
        if number is not None:
            values.append(number)
    return values


def distribution(prefix: str, values: list[float]) -> dict[str, float | int]:
    if not values:
        return {
            f"{prefix}_mean": math.nan,
            f"{prefix}_p50": math.nan,
            f"{prefix}_p95": math.nan,
            f"{prefix}_p99": math.nan,
            f"{prefix}_max": math.nan,
        }
    return {
        f"{prefix}_mean": fmean(values),
        f"{prefix}_p50": percentile(values, 0.50),
        f"{prefix}_p95": percentile(values, 0.95),
        f"{prefix}_p99": percentile(values, 0.99),
        f"{prefix}_max": max(values),
    }


def summarise(path: Path) -> dict[str, object]:
    header, rows, metadata = read_capture(path)
    indices = {key: find_column(header, stat) for key, stat in SCALAR_STATS.items()}

    runtime_index = indices["runtime_state"]
    if runtime_index is not None:
        running_rows = []
        for row in rows:
            state = parse_number(row[runtime_index]) if runtime_index < len(row) else None
            if state == 2.0:
                running_rows.append(row)
        rows = running_rows

    values = {key: column_values(rows, index) for key, index in indices.items()}
    result: dict[str, object] = {
        "file": str(path.resolve()),
        "scenario": metadata.get("ailodscenario", ""),
        "metadata_population": metadata.get("ailodpopulation", ""),
        "metadata_time_scale": metadata.get("ailodtimescale", ""),
        "git_commit": metadata.get("gitcommit", ""),
        "frames": len(rows),
    }
    for key in ("frame_ms", "game_ms", "render_ms", "gpu_ms", "draw_calls"):
        result.update(distribution(key, values[key]))
    for key in (
        "process_ram_mb",
        "vram_used_mb",
        "gpu_scene_instances",
        "active_count",
        "proxy_count",
        "active_actor_count",
        "actor_pool_capacity",
        "motion_updates",
        "normal_visited_cells",
        "normal_visited_residents",
        "telescope_visited_cells",
        "telescope_visited_residents",
        "view_speed",
        "loaded_level_count",
    ):
        result.update(distribution(key, values[key]))

    frame_values = values["frame_ms"]
    frame_mean = result["frame_ms_mean"]
    result["derived_fps_from_mean"] = (
        1000.0 / frame_mean
        if isinstance(frame_mean, float) and math.isfinite(frame_mean) and frame_mean > 0
        else math.nan
    )
    for threshold in (16.67, 33.33, 50.0, 100.0):
        result[f"frames_over_{str(threshold).replace('.', '_')}ms"] = sum(
            value > threshold for value in frame_values
        )

    result["population_min"] = min(values["population"], default=math.nan)
    result["population_max"] = max(values["population"], default=math.nan)
    result["time_scale_min"] = min(values["time_scale"], default=math.nan)
    result["time_scale_max"] = max(values["time_scale"], default=math.nan)
    result["full_population_scan_max"] = max(values["full_population_scan"], default=math.nan)
    result["telescope_enabled_max"] = max(values["telescope_enabled"], default=0.0)
    result["telescope_streaming_ready_max"] = max(
        values["telescope_streaming_ready"], default=0.0
    )
    result["tracked_resident_max"] = max(values["tracked_resident"], default=0.0)

    def rising_transitions(series: list[float], predicate) -> int:
        count = 0
        previous = False
        for value in series:
            current = predicate(value)
            count += int(current and not previous)
            previous = current
        return count

    def falling_transitions(series: list[float], predicate) -> int:
        count = 0
        previous = False
        for value in series:
            current = predicate(value)
            count += int(previous and not current)
            previous = current
        return count

    result["telescope_enable_count"] = rising_transitions(
        values["telescope_enabled"], lambda value: value > 0.0
    )
    result["telescope_lift_count"] = rising_transitions(
        values["tracked_resident"], lambda value: value > 0.0
    )
    result["telescope_clear_count"] = falling_transitions(
        values["tracked_resident"], lambda value: value > 0.0
    )
    result["world_partition_busy_count"] = falling_transitions(
        values["world_partition_streaming_ready"], lambda value: value > 0.0
    )
    if values["view_pos_x"] and values["view_pos_y"]:
        result["view_x_range"] = max(values["view_pos_x"]) - min(values["view_pos_x"])
        result["view_y_range"] = max(values["view_pos_y"]) - min(values["view_pos_y"])
    else:
        result["view_x_range"] = math.nan
        result["view_y_range"] = math.nan

    required_custom_stats = (
        values["population"]
        and values["time_scale"]
        and values["full_population_scan"]
    )
    metadata_population = parse_number(metadata.get("ailodpopulation", ""))
    metadata_scale = parse_number(metadata.get("ailodtimescale", ""))
    result["valid_custom_stats"] = bool(required_custom_stats)
    result["valid_population"] = bool(
        metadata_population is not None
        and values["population"]
        and min(values["population"]) == metadata_population
        and max(values["population"]) == metadata_population
    )
    result["valid_time_scale"] = bool(
        metadata_scale is not None
        and values["time_scale"]
        and min(values["time_scale"]) == metadata_scale
        and max(values["time_scale"]) == metadata_scale
    )
    result["valid_no_full_scan"] = bool(
        values["full_population_scan"] and max(values["full_population_scan"]) == 0.0
    )
    result["valid_capture"] = bool(
        rows
        and result["valid_custom_stats"]
        and result["valid_population"]
        and result["valid_time_scale"]
        and result["valid_no_full_scan"]
    )
    scenario = str(result["scenario"])
    visual_residents_seen = bool(
        values["normal_visited_residents"]
        and max(values["normal_visited_residents"]) > 0.0
        and (
            max(values["proxy_count"], default=0.0) > 0.0
            or max(values["active_actor_count"], default=0.0) > 0.0
        )
    )
    scenario_evidence = visual_residents_seen
    if scenario == "FastTraversal":
        scenario_evidence = scenario_evidence and max(values["view_speed"], default=0.0) > 0.0
    elif scenario == "ActorCap50":
        result["actor_cap_reached"] = max(values["active_actor_count"], default=0.0) >= 50.0
        scenario_evidence = (
            max(values["actor_pool_capacity"], default=0.0) == 50.0
            and max(values["active_actor_count"], default=0.0) > 0.0
        )
    elif scenario == "WorldPartitionTravel":
        scenario_evidence = (
            scenario_evidence
            and max(values["view_speed"], default=0.0) > 0.0
            and values["loaded_level_count"]
            and max(values["loaded_level_count"]) > min(values["loaded_level_count"])
        )
    elif scenario == "TelescopeLift":
        scenario_evidence = (
            result["telescope_enable_count"] >= 2
            and max(values["telescope_streaming_ready"], default=0.0) > 0.0
            and result["telescope_lift_count"] >= 2
            and result["telescope_clear_count"] >= 2
        )
    result["scenario_evidence_present"] = scenario_evidence
    return result


def input_files(path: Path, pattern: str) -> Iterable[Path]:
    if path.is_file():
        yield path
        return
    yield from sorted(path.glob(pattern))


def format_console(result: dict[str, object]) -> str:
    def number(key: str) -> str:
        value = result.get(key)
        return f"{value:.3f}" if isinstance(value, float) and math.isfinite(value) else "n/a"

    return (
        f"{Path(str(result['file'])).name}: valid={result['valid_capture']}, "
        f"scenario_evidence={result['scenario_evidence_present']}, "
        f"frames={result['frames']}, frame p50/p95/p99="
        f"{number('frame_ms_p50')}/{number('frame_ms_p95')}/{number('frame_ms_p99')} ms, "
        f"GPU p99={number('gpu_ms_p99')} ms, RAM max={number('process_ram_mb_max')} MB, "
        f"VRAM max={number('vram_used_mb_max')} MB"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path, help="A Phase 7F-E CSV file or its directory")
    parser.add_argument("--pattern", default="Phase7F_E_*.csv", help="Directory glob pattern")
    parser.add_argument("--output", type=Path, help="Optional combined summary CSV")
    args = parser.parse_args()

    files = list(input_files(args.input, args.pattern))
    if not files:
        parser.error(f"no Phase7F_E CSV captures found under {args.input}")
    results = [summarise(path) for path in files]
    for result in results:
        print(format_console(result))

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        fieldnames: list[str] = []
        for result in results:
            for key in result:
                if key not in fieldnames:
                    fieldnames.append(key)
        with args.output.open("w", encoding="utf-8-sig", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(results)
        print(f"Summary: {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
