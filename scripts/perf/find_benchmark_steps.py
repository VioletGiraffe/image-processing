# Finds the steps in a history scan: run_benchmark_rounds.py results of builds in history order, usually one round each.
# Reads only rows one run resolves: serial resizer rows of --min-ms or more; --threaded adds the threaded rows.
# A row's time per build is its minimum over the build's rounds. Two readings per row:
#   step       - each build against the one before it, beyond --threshold; single-run noise flips such steps both ways
#   persistent - the median of the --window builds from a build on against the window before it, beyond --persistent
# Rounds of the builds on each side confirm a step; this report only finds the candidates.
import argparse
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tests"))
from compare_benchmark_rounds import read_rounds, scenario  # noqa: E402
from report_benchmark_ratios import RESIZER_PREFIX  # noqa: E402

THREADED_SUFFIX = " [multithreaded]"


def percent(ratio):
    return f"{(ratio - 1) * 100:+.1f}%"


def main():
    parser = argparse.ArgumentParser(description="Steps between consecutive builds of a history scan, per resizer row.")
    parser.add_argument("labels", nargs="+", help="the builds' labels in history order")
    parser.add_argument("--results", type=Path, default=Path("benchmark-rounds"), help="directory of r<round>_<label>.xml (default: %(default)s)")
    parser.add_argument("--threshold", type=float, default=4, help="percent beyond which a step is listed (default: %(default)s)")
    parser.add_argument("--persistent", type=float, default=3, help="percent beyond which a persistent step is listed (default: %(default)s)")
    parser.add_argument("--window", type=int, default=3, help="builds on each side of a persistent step (default: %(default)s)")
    parser.add_argument("--min-ms", type=float, default=1, help="rows faster than this in the earlier build are not read (default: %(default)s)")
    parser.add_argument("--threaded", action="store_true", help="read the threaded rows too")
    parser.add_argument("--tsv", type=Path, help="writes every resizer row's time per build, ms, for tables")
    arguments = parser.parse_args()

    labels = [label for label in arguments.labels if any(arguments.results.glob(f"r*_{label}.xml"))]
    for label in sorted(set(arguments.labels) - set(labels)):
        print(f"No results: {label}")
    # Minimum ns by benchmark name, by label
    times = {}
    for label in labels:
        rounds = read_rounds(arguments.results, label).values()
        names = {name for results in rounds for name in results if name.startswith(RESIZER_PREFIX)}
        times[label] = {name: min(results[name] for results in rounds if name in results) for name in names}

    names = list(dict.fromkeys(name for label in labels for name in sorted(times[label])))
    if arguments.tsv:
        with open(arguments.tsv, "w") as output:
            output.write("Scenario\t" + "\t".join(labels) + "\n")
            for name in names:
                output.write(scenario(name) + "\t" + "\t".join(f"{times[label][name] / 1e6:.3f}" if name in times[label] else "" for label in labels) + "\n")

    def readable(name, label):
        return (arguments.threaded or not name.endswith(THREADED_SUFFIX)) and times[label][name] >= arguments.min_ms * 1e6

    print(f"Steps beyond {arguments.threshold:g}%:")
    for previous, label in zip(labels, labels[1:]):
        ratios = {name: times[label][name] / times[previous][name] for name in times[label] if name in times[previous] and readable(name, previous)}
        median = percent(statistics.median(ratios.values())) if ratios else "-"
        print(f"{previous} -> {label}: median {median} over {len(ratios)} rows")
        for ratio, name in sorted(((ratio, name) for name, ratio in ratios.items() if abs(ratio - 1) * 100 > arguments.threshold), reverse=True):
            print(f"    {percent(ratio):>7}  {times[previous][name] / 1e6:8.2f} -> {times[label][name] / 1e6:8.2f} ms  {scenario(name)}")

    window = arguments.window
    # A step shows at up to window - 1 neighboring builds too
    persistent = {label: [] for label in labels}
    for name in names:
        series = [(label, times[label][name]) for label in labels if name in times[label] and readable(name, label)]
        for index in range(window, len(series) - window + 1):
            before = statistics.median(time for _, time in series[index - window:index])
            after = statistics.median(time for _, time in series[index:index + window])
            if abs(after / before - 1) * 100 > arguments.persistent:
                persistent[series[index][0]].append((after / before, name))
    print(f"\nPersistent steps beyond {arguments.persistent:g}%, median of {window} builds from a build on against {window} before:")
    for label, steps in persistent.items():
        if steps:
            print(f"at {label}:")
            for ratio, name in sorted(steps, reverse=True):
                print(f"    {percent(ratio):>7}  {scenario(name)}")


if __name__ == "__main__":
    main()
