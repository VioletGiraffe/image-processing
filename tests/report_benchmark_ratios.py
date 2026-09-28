import os
import platform
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


RESIZER_PREFIX = "CImageResizer | "
SCALAR_RESIZER_PREFIX = "CImageResizer [scalar] | "
CONTROL_PREFIXES = ("QImage::scaled | ", "QImage::copy | ")
# Its warnings carry what only the binary can report: the compiler and the cache sizing
ENVIRONMENT_TEST = "Benchmark environment"


def command_output(*command, cwd=None):
    try:
        return subprocess.run(command, cwd=cwd, capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


# The working tree's commit: a binary built from other sources goes unnoticed
def commit_description():
    repository = Path(__file__).resolve().parent.parent
    commit = command_output("git", "rev-parse", "--short", "HEAD", cwd=repository)
    if not commit:
        return "unknown"

    if command_output("git", "status", "--porcelain", "--untracked-files=no", cwd=repository):
        commit += ", with uncommitted changes"
    return commit


def cpu_model():
    system = platform.system()
    if system == "Windows":
        import winreg
        try:
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
                return str(winreg.QueryValueEx(key, "ProcessorNameString")[0]).strip()
        except OSError:
            return "unknown"

    if system == "Darwin":
        return command_output("sysctl", "-n", "machdep.cpu.brand_string") or "unknown"

    for line in command_output("lscpu").splitlines():
        label, _, value = line.partition(":")
        if label.strip() == "Model name":
            return value.strip()
    return "unknown"


def read_binary_environment(results_tree):
    for test_case in results_tree.iter("TestCase"):
        if test_case.attrib.get("name") == ENVIRONMENT_TEST:
            return [warning.text.strip() for warning in test_case.iter("Warning") if warning.text]
    return []


def read_benchmarks(results_tree):
    results = {}
    for benchmark in results_tree.iter("BenchmarkResults"):
        name = benchmark.attrib["name"]
        mean = benchmark.find("mean")
        if mean is None:
            raise RuntimeError(f"Benchmark has no mean result: {name}")
        results[name] = float(mean.attrib["value"])
    return results


def build_report(results, environment):
    rows = []
    for name, resizer_ns in results.items():
        if not name.startswith(RESIZER_PREFIX):
            continue

        scenario = name[len(RESIZER_PREFIX):]
        # Parallel-resize benchmarks share the QImage control of their serial counterpart
        control_scenario = scenario.removesuffix(" [multithreaded]")
        matching_controls = [(prefix, results[prefix + control_scenario]) for prefix in CONTROL_PREFIXES if prefix + control_scenario in results]
        if len(matching_controls) != 1:
            raise RuntimeError(f"Expected exactly one QImage control for: {scenario}")

        control_prefix, control_ns = matching_controls[0]
        if control_ns <= 0.0:
            raise RuntimeError(f"QImage control reported a non-positive duration for: {scenario}")

        # Only scenarios that reach the SIMD kernels have a scalar run
        scalar_ns = results.get(SCALAR_RESIZER_PREFIX + scenario)
        scalar_cells = f"{scalar_ns / 1_000_000.0:.3f} | {scalar_ns / resizer_ns:.2f}x" if scalar_ns is not None else " | "
        rows.append((scenario, resizer_ns / 1_000_000.0, control_prefix.removesuffix(" | "), control_ns / 1_000_000.0, resizer_ns / control_ns, scalar_cells))

    if not rows:
        raise RuntimeError("No CImageResizer benchmark results found")

    lines = [
        "## Image resizer benchmark ratios",
        "",
        *(f"- {item}" for item in environment),
        "",
        "Lower is better, except SIMD speedup (scalar / SIMD time); ratios use measurements from this job only.",
        "",
        "| Scenario | CImageResizer (ms) | Control | Control (ms) | Resizer / control | Scalar (ms) | SIMD speedup |",
        "|---|---:|---|---:|---:|---:|---:|",
    ]
    for scenario, resizer_ms, control, control_ms, ratio, scalar_cells in rows:
        lines.append(f"| {scenario} | {resizer_ms:.3f} | {control} | {control_ms:.3f} | {ratio:.3f}x | {scalar_cells} |")

    return "\n".join(lines) + "\n"


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} <catch2-benchmark-results.xml>")

    results_tree = ET.parse(sys.argv[1])
    environment = [f"Commit: {commit_description()}", f"CPU: {cpu_model()}", *read_binary_environment(results_tree)]
    report = build_report(read_benchmarks(results_tree), environment)
    print(report, end="")

    github_summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if github_summary:
        with open(github_summary, "a", encoding="utf-8") as summary:
            summary.write(report)


if __name__ == "__main__":
    main()
