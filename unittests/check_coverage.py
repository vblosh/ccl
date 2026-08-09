#!/usr/bin/env python3
"""Run one CTest unit in isolation and check its GCC JSON coverage.

The checker deliberately owns the gcda lifecycle.  Coverage targets must not
run in parallel because all suites linked with ccl update the same counters.
"""

from __future__ import annotations

import argparse
import gzip
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


LINE_THRESHOLD = 80.0
BRANCH_THRESHOLD = 70.0


class CoverageError(RuntimeError):
    pass


def fail(message: str) -> None:
    raise CoverageError(message)


def run_command(command: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            command,
            cwd=str(cwd) if cwd is not None else None,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except OSError as exc:
        fail(f"unable to execute {' '.join(command)}: {exc}")


def canonical_source(raw: str, source_root: Path, working_directory: Path) -> Path:
    path = Path(raw)
    candidates = []
    if path.is_absolute():
        candidates.append(path)
    else:
        candidates.extend((source_root / path, working_directory / path))
    for candidate in candidates:
        resolved = candidate.resolve()
        if resolved.exists():
            return resolved
    if candidates:
        return candidates[0].resolve()
    return path.resolve()


def load_manifest(manifest_path: Path, source_root: Path) -> dict[str, Any]:
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read coverage manifest {manifest_path}: {exc}")

    units = manifest.get("units")
    if not isinstance(units, dict) or not units:
        fail("coverage manifest must contain a non-empty 'units' object")

    source_directory = (source_root / "src").resolve()
    physical_files = {
        path.resolve() for path in source_directory.glob("*.c")
    }
    if not physical_files:
        fail(f"no C sources found below {source_directory}")

    owners: dict[Path, str] = {}
    for unit_name, unit in units.items():
        if not isinstance(unit, dict):
            fail(f"manifest unit {unit_name!r} is not an object")
        physical = unit.get("physical_sources")
        coverage = unit.get("coverage_sources")
        if not isinstance(physical, list) or not physical:
            fail(f"unit {unit_name!r} has no physical_sources list")
        if not isinstance(coverage, list) or not coverage:
            fail(f"unit {unit_name!r} has no coverage_sources list")

        physical_paths = []
        for raw in physical:
            if not isinstance(raw, str):
                fail(f"unit {unit_name!r} has a non-string physical source")
            path = canonical_source(raw, source_root, source_root)
            if path not in physical_files:
                fail(f"unit {unit_name!r} lists missing source {raw!r}")
            if path in owners:
                fail(f"source {raw!r} is owned by both {owners[path]!r} and {unit_name!r}")
            owners[path] = unit_name
            physical_paths.append(path)

        coverage_paths = []
        for raw in coverage:
            if not isinstance(raw, str):
                fail(f"unit {unit_name!r} has a non-string coverage source")
            path = canonical_source(raw, source_root, source_root)
            if path not in physical_paths:
                fail(f"coverage source {raw!r} is not physical source of {unit_name!r}")
            coverage_paths.append(path)
        unit["_physical_paths"] = physical_paths
        unit["_coverage_paths"] = coverage_paths

    missing = sorted(physical_files - set(owners), key=str)
    if missing:
        fail("coverage manifest does not own: " + ", ".join(str(path) for path in missing))
    extras = sorted(set(owners) - physical_files, key=str)
    if extras:
        fail("coverage manifest lists sources outside src/: " + ", ".join(map(str, extras)))
    return manifest


def clear_counters(build_root: Path) -> None:
    for counter in build_root.rglob("*.gcda"):
        if counter.is_file() or counter.is_symlink():
            counter.unlink()


def run_exact_ctest(build_root: Path, unit_name: str, test_name: str) -> None:
    ctest = shutil.which("ctest")
    if ctest is None:
        fail("ctest is not available in PATH")
    expression = f"^test_{test_name}$"
    listed = run_command([ctest, "--test-dir", str(build_root), "-N", "-R", expression])
    if listed.returncode != 0:
        fail(f"ctest discovery failed for {unit_name}:\n{listed.stdout}{listed.stderr}")
    if "Total Tests: 0" in listed.stdout:
        fail(
            f"CTest has no registered test named test_{test_name} "
            f"for coverage unit {unit_name}"
        )

    result = run_command(
        [ctest, "--test-dir", str(build_root), "-R", expression, "--output-on-failure"]
    )
    if result.returncode != 0:
        fail(f"CTest failed for {unit_name}:\n{result.stdout}{result.stderr}")


def as_count(value: Any) -> int:
    if value is None:
        return 0
    try:
        return int(value)
    except (TypeError, ValueError):
        return 0


def parse_gcov_json(
    json_path: Path,
    source_root: Path,
    coverage_paths: set[Path],
    line_counts: dict[tuple[Path, int], int],
    branch_counts: dict[tuple[Path, int, Any, Any, Any, Any], int],
    found_sources: set[Path],
) -> None:
    try:
        with gzip.open(json_path, "rt", encoding="utf-8") as stream:
            report = json.load(stream)
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot parse gcov report {json_path}: {exc}")

    for source_record in report.get("files", []):
        raw_source = source_record.get("file")
        if not isinstance(raw_source, str):
            continue
        source = canonical_source(raw_source, source_root, json_path.parent)
        if source not in coverage_paths:
            continue
        found_sources.add(source)
        for line in source_record.get("lines", []):
            line_number = line.get("line_number")
            if line_number is None:
                line_number = line.get("line")
            if line_number is None:
                continue
            try:
                line_number = int(line_number)
            except (TypeError, ValueError):
                continue
            key = (source, line_number)
            line_counts[key] = line_counts.get(key, 0) + as_count(
                line.get("count", line.get("execution_count", 0))
            )
            for branch in line.get("branches", []) or []:
                branch_key = (
                    source,
                    line_number,
                    branch.get("source_block_id"),
                    branch.get("destination_block_id"),
                    branch.get("fallthrough"),
                    branch.get("throw"),
                )
                branch_counts[branch_key] = branch_counts.get(branch_key, 0) + as_count(
                    branch.get("count", branch.get("execution_count", 0))
                )


def collect_gcov(
    build_root: Path,
    source_root: Path,
    coverage_paths: set[Path],
    gcov_executable: str | None,
) -> tuple[dict[tuple[Path, int], int], dict[tuple[Path, int, Any, Any, Any, Any], int], set[Path]]:
    gcov = gcov_executable or shutil.which("gcov")
    if gcov is None:
        fail("gcov is not available in PATH")
    notes = sorted(build_root.rglob("*.gcno"))
    if not notes:
        fail(f"no .gcno files found below {build_root}")

    line_counts: dict[tuple[Path, int], int] = {}
    branch_counts: dict[tuple[Path, int, Any, Any, Any, Any], int] = {}
    found_sources: set[Path] = set()
    output_root = build_root / "coverage"
    output_root.mkdir(parents=True, exist_ok=True)

    for note in notes:
        with tempfile.TemporaryDirectory(prefix="gcov-", dir=str(output_root)) as temporary:
            output_directory = Path(temporary)
            result = run_command(
                [
                    gcov,
                    "--json-format",
                    "--branch-probabilities",
                    "--branch-counts",
                    "-o",
                    str(note.parent),
                    str(note),
                ],
                cwd=output_directory,
            )
            reports = sorted(output_directory.glob("*.gcov.json.gz"))
            if result.returncode != 0 or not reports:
                fail(
                    f"gcov failed for {note}:\n{result.stdout}{result.stderr}"
                )
            for report in reports:
                parse_gcov_json(
                    report,
                    source_root,
                    coverage_paths,
                    line_counts,
                    branch_counts,
                    found_sources,
                )
    return line_counts, branch_counts, found_sources


def check_unit(
    unit_name: str,
    unit: dict[str, Any],
    source_root: Path,
    build_root: Path,
    gcov_executable: str | None,
) -> bool:
    coverage_paths = set(unit["_coverage_paths"])
    clear_counters(build_root)
    test_name = unit.get("test", unit_name)
    if not isinstance(test_name, str) or not test_name:
        fail(f"manifest unit {unit_name!r} has an invalid test name")
    run_exact_ctest(build_root, unit_name, test_name)
    line_counts, branch_counts, found_sources = collect_gcov(
        build_root, source_root, coverage_paths, gcov_executable
    )
    missing = coverage_paths - found_sources
    if missing:
        fail(
            f"unit {unit_name!r} has no gcov data for: "
            + ", ".join(sorted(str(path) for path in missing))
        )

    line_total = len(line_counts)
    line_covered = sum(count > 0 for count in line_counts.values())
    branch_total = len(branch_counts)
    branch_covered = sum(count > 0 for count in branch_counts.values())
    line_percent = (100.0 * line_covered / line_total) if line_total else 0.0
    branch_percent = (100.0 * branch_covered / branch_total) if branch_total else None
    line_pass = line_total > 0 and line_percent >= LINE_THRESHOLD
    branch_pass = branch_total == 0 or (branch_percent is not None and branch_percent >= BRANCH_THRESHOLD)
    passed = line_pass and branch_pass

    report = {
        "unit": unit_name,
        "physical_sources": [str(path.relative_to(source_root)) for path in unit["_physical_paths"]],
        "coverage_sources": [str(path.relative_to(source_root)) for path in unit["_coverage_paths"]],
        "lines": {"covered": line_covered, "total": line_total, "percent": line_percent},
        "branches": {
            "covered": branch_covered,
            "total": branch_total,
            "percent": branch_percent,
            "status": "N/A" if branch_total == 0 else "measured",
        },
        "thresholds": {"lines": LINE_THRESHOLD, "branches": BRANCH_THRESHOLD},
        "passed": passed,
    }
    report_path = build_root / "coverage" / f"{unit_name}.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    branch_summary = "N/A" if branch_percent is None else f"{branch_percent:.2f}%"
    status = "PASS" if passed else "FAIL"
    print(
        f"{unit_name}: {status}; lines {line_covered}/{line_total} "
        f"({line_percent:.2f}%), branches {branch_covered}/{branch_total} "
        f"({branch_summary})"
    )
    return passed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--build-root", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument(
        "--gcov",
        help="gcov executable selected by CMake (defaults to PATH)",
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--unit")
    group.add_argument("--all", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source_root = args.source_root.resolve()
    build_root = args.build_root.resolve()
    manifest_path = args.manifest.resolve()
    if not source_root.is_dir():
        fail(f"source root is not a directory: {source_root}")
    if not build_root.is_dir():
        fail(f"build root is not a directory: {build_root}")
    if not (build_root / "CMakeCache.txt").is_file():
        fail(f"build root has no CMakeCache.txt: {build_root}")
    manifest = load_manifest(manifest_path, source_root)
    units = manifest["units"]
    selected = list(units) if args.all else [args.unit]
    if not args.all and args.unit not in units:
        fail(f"unknown coverage unit: {args.unit}")

    all_passed = True
    for unit_name in selected:
        try:
            passed = check_unit(
                unit_name,
                units[unit_name],
                source_root,
                build_root,
                args.gcov,
            )
        except CoverageError:
            raise
        all_passed = all_passed and passed
    return 0 if all_passed else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CoverageError as exc:
        print(f"coverage error: {exc}", file=sys.stderr)
        sys.exit(2)
