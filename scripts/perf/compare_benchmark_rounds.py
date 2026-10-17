# Compares the builds that run_benchmark_rounds.py ran, per resizer benchmark or per QImage control, against a reference build:
# the median over rounds of the same-round time ratio, and the ratio of the two minimum times.
# The median resists a round that one build ran disturbed; the two figures disagreeing marks a noisy row.
import argparse
import re
import statistics
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tests"))
from report_benchmark_ratios import CONTROL_PREFIXES, RESIZER_PREFIX, SSE41_RESIZER_PREFIX, read_benchmarks  # noqa: E402


# Mean ns by benchmark name, by round
def read_rounds(results, label):
    rounds = {}
    for path in results.iterdir():
        match = re.fullmatch(rf"r(\d+)_{re.escape(label)}\.xml", path.name)
        if match:
            rounds[int(match.group(1))] = read_benchmarks(ET.parse(path))
    if not rounds:
        raise SystemExit(f"No r<round>_{label}.xml in {results}")
    return rounds


def percent(ratio):
    return f"{(ratio - 1) * 100:+.1f}%"


# The benchmark's name without its "<runner> | " prefix
def scenario(name):
    return name.partition(" | ")[2]


# A row that reads two levels by launch shows here, and only as noise in the ratios
def print_round_times(names, builds):
    round_numbers = sorted(set().union(*builds.values()))
    print("| Scenario | Build | " + " | ".join(f"r{round_number}, ms" for round_number in round_numbers) + " |")
    print("|---|---|" + "---:|" * len(round_numbers))
    for name in names:
        for label, rounds in builds.items():
            cells = [f"{rounds[round_number][name] / 1e6:.2f}" if name in rounds.get(round_number, {}) else "-" for round_number in round_numbers]
            print(f"| {scenario(name)} | {label} | " + " | ".join(cells) + " |")


def main():
    parser = argparse.ArgumentParser(description="Per resizer benchmark or QImage control: each build's time against the reference build's.")
    parser.add_argument("reference", help="label of the build the others are compared to")
    parser.add_argument("labels", nargs="*", help="labels of the builds to compare")
    parser.add_argument("--results", type=Path, default=Path("benchmark-rounds"), help="directory of r<round>_<label>.xml (default: %(default)s)")
    runs = parser.add_mutually_exclusive_group()
    runs.add_argument("--sse41", action="store_true", help="the SSE4.1 runs instead of the default-level ones")
    runs.add_argument("--controls", action="store_true", help="the QImage controls instead of the resizer")
    parser.add_argument("--rounds", action="store_true", help="each build's time in each round, the reference's too, instead of the ratios")
    arguments = parser.parse_args()
    if not arguments.labels and not arguments.rounds:
        parser.error("no build to compare to the reference")

    prefixes = CONTROL_PREFIXES if arguments.controls else (SSE41_RESIZER_PREFIX,) if arguments.sse41 else (RESIZER_PREFIX,)
    reference = read_rounds(arguments.results, arguments.reference)
    builds = {label: read_rounds(arguments.results, label) for label in arguments.labels}
    names = [name for name in reference[min(reference)] if name.startswith(prefixes)]
    if not names:
        raise SystemExit(f"The reference build's results hold no {' or '.join(repr(prefix.strip()) for prefix in prefixes)} benchmark")

    if arguments.rounds:
        print_round_times(names, {arguments.reference: reference, **builds})
        return

    print(f"| Scenario | {arguments.reference}, min ms | " + " | ".join(f"{label}: median of ratios / min to min" for label in builds) + " |")
    print("|---|---:|" + "---:|" * len(builds))
    for name in names:
        reference_times = {round_number: results[name] for round_number, results in reference.items() if name in results}
        cells = [f"{min(reference_times.values()) / 1e6:.2f}"]
        for rounds in builds.values():
            times = {round_number: results[name] for round_number, results in rounds.items() if name in results}
            shared_rounds = sorted(set(times) & set(reference_times))
            if not shared_rounds:
                cells.append("no shared round")
                continue
            median_ratio = statistics.median(times[round_number] / reference_times[round_number] for round_number in shared_rounds)
            cells.append(f"{percent(median_ratio)} / {percent(min(times.values()) / min(reference_times.values()))}")
        print(f"| {scenario(name)} | " + " | ".join(cells) + " |")


if __name__ == "__main__":
    main()
