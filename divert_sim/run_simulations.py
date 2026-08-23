#!/usr/bin/env python3
"""Scenario runner helpers for divert_sim.

This module is the single Python entrypoint for:
- running one scenario and parsing its unified CSV output,
- discovering scenario metadata from data/scenarios/*.json,
- writing output/index.json for view.html / interactive.html.
"""

from __future__ import annotations

import csv
import io
import json
import os
import subprocess
import tempfile
from dataclasses import dataclass
from datetime import datetime, timezone
from glob import glob
from pathlib import Path
from typing import Any, Dict, List, Optional


ROOT = Path(__file__).resolve().parent
DATA_DIR = ROOT / "data"
SCENARIO_DIR = DATA_DIR / "scenarios"
OUTPUT_DIR = ROOT / "output"
PIO_ENV = os.environ.get("DIVERT_SIM_PIO_ENV", "native_simulator")


def resolve_binary() -> Path:
    local_binary = ROOT / "divert_sim"
    if local_binary.exists():
        return local_binary

    pio_binary = ROOT.parent / ".pio" / "build" / PIO_ENV / "program"
    if pio_binary.exists():
        return pio_binary

    raise FileNotFoundError(
        "divert_sim binary not found. Build with `pio run -e native_simulator` "
        "(or set DIVERT_SIM_PIO_ENV) or provide ./divert_sim."
    )


BINARY = resolve_binary()


@dataclass
class ScenarioMeta:
    id: str
    title: str
    category: str
    profile: str
    path: Path
    peers: List[str]


def _read_json(path: Path) -> Dict[str, Any]:
    with path.open() as f:
        return json.load(f)


def _merge_dict(base: Dict[str, Any], overrides: Dict[str, Any]) -> Dict[str, Any]:
    merged = dict(base)
    for key, value in overrides.items():
        if isinstance(value, dict) and isinstance(merged.get(key), dict):
            merged[key] = _merge_dict(merged[key], value)
        else:
            merged[key] = value
    return merged


def _scenario_base_dir(path: Path) -> Path:
    return path.parent if path.parent else ROOT


def _resolve_config_includes(scenario_doc: Dict[str, Any], scenario_path: Path) -> Dict[str, Any]:
    resolved = dict(scenario_doc)
    includes = resolved.get("config_include")
    if includes is None:
        includes = resolved.get("config_includes")

    include_paths: List[str] = []
    if isinstance(includes, str) and includes.strip():
        include_paths = [includes]
    elif isinstance(includes, list):
        include_paths = [str(item) for item in includes if isinstance(item, str) and item.strip()]

    merged_config: Dict[str, Any] = {}
    base_dir = _scenario_base_dir(scenario_path)
    for include_path in include_paths:
        candidate = Path(include_path)
        if not candidate.is_absolute():
            candidate = (base_dir / candidate).resolve()
        include_doc = _read_json(candidate)
        if not isinstance(include_doc, dict):
            raise ValueError(f"Config include is not an object: {candidate}")
        if isinstance(include_doc.get("config"), dict):
            include_doc = include_doc["config"]
        merged_config = _merge_dict(merged_config, include_doc)

    inline_config = resolved.get("config", {})
    if isinstance(inline_config, dict):
        merged_config = _merge_dict(merged_config, inline_config)
    resolved["config"] = merged_config
    return resolved


def prepare_scenario_doc(
    scenario_doc: Dict[str, Any],
    scenario_path: Path,
    config_overrides: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    resolved = _resolve_config_includes(scenario_doc, scenario_path)
    if config_overrides:
        resolved["config"] = _merge_dict(resolved.get("config", {}), config_overrides)
    return resolved


def _parse_time(ts: str) -> datetime:
    return datetime.fromisoformat(ts.replace("Z", "+00:00"))


def setup_summary(_: str = "") -> None:
    """Backwards-compatible shim used by old test modules."""
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)


def _is_config_profile(path: Path) -> bool:
    return path.name.startswith("config-")


def discover_scenarios() -> List[ScenarioMeta]:
    scenarios: List[ScenarioMeta] = []
    for raw_path in sorted(glob(str(SCENARIO_DIR / "*.json"))):
        path = Path(raw_path)
        if _is_config_profile(path):
            continue
        doc = _read_json(path)

        meta = doc.get("meta", {})
        scenario_id = meta.get("id") or path.stem
        title = meta.get("title") or scenario_id.replace("_", " ").title()
        category = meta.get("category") or "misc"
        profile = meta.get("profile") or "default"
        peers = [p.get("id", f"peer-{idx}") for idx, p in enumerate(doc.get("peers", []))]

        scenarios.append(
            ScenarioMeta(
                id=scenario_id,
                title=title,
                category=category,
                profile=profile,
                path=path,
                peers=peers,
            )
        )
    return scenarios


def run_scenario(
    scenario_path: str,
    output: str = "",
    config_overrides: Optional[Dict[str, Any]] = None,
) -> List[Dict[str, str]]:
    """Run divert_sim with one scenario and return parsed CSV rows."""
    path = Path(scenario_path)
    if not path.is_absolute():
        path = ROOT / path

    cmd = [str(BINARY), "--scenario", str(path)]

    temp_scenario: Optional[Path] = None
    scenario_doc = _read_json(path)
    scenario_doc = prepare_scenario_doc(scenario_doc, path, config_overrides=config_overrides)
    with tempfile.NamedTemporaryFile(
        "w",
        suffix=".json",
        delete=False,
        dir=path.parent,
    ) as tf:
        json.dump(scenario_doc, tf)
        temp_scenario = Path(tf.name)
    cmd = [str(BINARY), "--scenario", str(temp_scenario)]

    try:
        if output:
            OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
            out_path = OUTPUT_DIR / f"{output}.csv"
            cmd += ["-o", str(out_path)]
            result = subprocess.run(cmd, capture_output=True, text=True, check=False)
            if result.returncode != 0:
                raise RuntimeError(f"divert_sim failed ({path}): {result.stderr.strip()}")
            with out_path.open() as f:
                return list(csv.DictReader(f))

        result = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise RuntimeError(f"divert_sim failed ({path}): {result.stderr.strip()}")
        return list(csv.DictReader(io.StringIO(result.stdout)))
    finally:
        if temp_scenario and temp_scenario.exists():
            temp_scenario.unlink()


def build_index(
    config_overrides: Optional[Dict[str, Any]] = None,
    profile_suffix: Optional[str] = None,
    index_name: str = "index.json",
    scenario_ids: Optional[List[str]] = None,
    scenario_sources: Optional[List[str]] = None,
) -> Dict[str, Any]:
    """Run all scenarios, write CSV outputs, and write an index file."""
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    scenarios = discover_scenarios()
    if scenario_ids:
        allowed = set(scenario_ids)
        scenarios = [scenario for scenario in scenarios if scenario.id in allowed]
    if scenario_sources:
        allowed_sources = set(scenario_sources)
        scenarios = [
            scenario
            for scenario in scenarios
            if str(scenario.path.relative_to(ROOT)) in allowed_sources
        ]
    entries: List[Dict[str, Any]] = []

    for scenario in scenarios:
        if profile_suffix:
            profile = profile_suffix
            output_name = f"{scenario.id}_{profile}"
        else:
            profile = scenario.profile
            output_name = scenario.id
        rows = run_scenario(str(scenario.path), output=output_name, config_overrides=config_overrides)
        row_count = len(rows)
        entries.append(
            {
                "id": scenario.id,
                "title": scenario.title,
                "category": scenario.category,
                "profile": profile,
                "peers": scenario.peers,
                "source": str(scenario.path.relative_to(ROOT)),
                "csv": f"output/{output_name}.csv",
                "row_count": row_count,
            }
        )

    index = {
        "generated_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "scenarios": entries,
    }
    with (OUTPUT_DIR / index_name).open("w") as f:
        json.dump(index, f, indent=2)
    return index


if __name__ == "__main__":
    build_index()
