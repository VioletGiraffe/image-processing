import os
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


RESIZER_PREFIX = "CImageResizer | "
CONTROL_PREFIXES = ("QImage::scaled | ", "QImage::copy | ")


def read_benchmarks(xml_path):
    results = {}
    for benchmark in ET.parse(xml_path).iter("BenchmarkResults"):
        name = benchmark.attrib["name"]
        mean = benchmark.find("mean")
        if mean is None:
            raise RuntimeError(f"Benchmark has no mean result: {name}")
        results[name] = float(mean.attrib["value"])
    return results


def build_report(results):
    rows = []
    for name, resizer_ns in results.items():
        if not name.startswith(RESIZER_PREFIX):
            continue

        scenario = name[len(RESIZER_PREFIX):]
        # Parallel-resize benchmarks share the QImage control of their serial counterpart
        control_scenario = scenario.removesuffix(" [4 threads]")
        matching_controls = [(prefix, results[prefix + control_scenario]) for prefix in CONTROL_PREFIXES if prefix + control_scenario in results]
        if len(matching_controls) != 1:
            raise RuntimeError(f"Expected exactly one QImage control for: {scenario}")

        control_prefix, control_ns = matching_controls[0]
        if control_ns <= 0.0:
            raise RuntimeError(f"QImage control reported a non-positive duration for: {scenario}")

        rows.append((scenario, resizer_ns / 1_000_000.0, control_prefix.removesuffix(" | "), control_ns / 1_000_000.0, resizer_ns / control_ns))

    if not rows:
        raise RuntimeError("No CImageResizer benchmark results found")

    lines = [
        "## Image resizer benchmark ratios",
        "",
        "Lower is better; ratios use measurements from this job only.",
        "",
        "| Scenario | CImageResizer (ms) | Control | Control (ms) | Resizer / control |",
        "|---|---:|---|---:|---:|",
    ]
    for scenario, resizer_ms, control, control_ms, ratio in rows:
        lines.append(f"| {scenario} | {resizer_ms:.3f} | {control} | {control_ms:.3f} | {ratio:.3f}x |")

    return "\n".join(lines) + "\n"


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} <catch2-benchmark-results.xml>")

    report = build_report(read_benchmarks(sys.argv[1]))
    print(report, end="")

    github_summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if github_summary:
        with open(github_summary, "a", encoding="utf-8") as summary:
            summary.write(report)


if __name__ == "__main__":
    main()
