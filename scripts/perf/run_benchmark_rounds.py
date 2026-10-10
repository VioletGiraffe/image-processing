# Runs benchmark binaries in alternating rounds, one Catch2 XML per run, for compare_benchmark_rounds.py.
# Alternating: the machine's drift over the session lands on every binary alike.
# A binary's label is its file name without the extension; labels must differ.
# The Qt libraries must be loadable: QT_ROOT_DIR's bin directory is put on PATH when the variable is set.
import argparse
import os
import platform
import subprocess
import sys
import time
from pathlib import Path


NO_CONTROLS_VARIABLE = "IMAGE_PROCESSING_BENCHMARK_NO_CONTROLS"
# Other processes below this much CPU time over a run are not reported
REPORTED_CPU_SECONDS = 0.5


# CPU seconds and name by process id; empty where the platform has no reader here
def process_cpu_seconds():
    system = platform.system()
    processes = {}
    if system == "Linux":
        ticks_per_second = os.sysconf("SC_CLK_TCK")
        for entry in Path("/proc").iterdir():
            if not entry.name.isdigit():
                continue
            try:
                stat = (entry / "stat").read_text()
            except OSError:
                continue  # The process ended
            # The name is parenthesized and may itself hold spaces and parentheses
            name = stat[stat.index("(") + 1:stat.rindex(")")]
            fields = stat[stat.rindex(")") + 2:].split()
            processes[int(entry.name)] = ((int(fields[11]) + int(fields[12])) / ticks_per_second, name)
    elif system == "Windows":
        # Protected processes report no CPU time
        command = "Get-Process | Where-Object { $null -ne $_.CPU } | ForEach-Object { '{0}|{1}|{2}' -f $_.Id, $_.CPU.ToString([cultureinfo]::InvariantCulture), $_.Name }"
        listing = subprocess.run(["powershell", "-NoProfile", "-Command", command], capture_output=True, text=True).stdout
        for line in listing.splitlines():
            process_id, seconds, name = line.split("|", 2)
            processes[int(process_id)] = (float(seconds), name)
    return processes


def busiest_other_processes(before, after, own_name):
    # Linux reports a name's first 15 characters
    own_names = (own_name, own_name[:15])
    used = [(after[process_id][0] - seconds, name) for process_id, (seconds, name) in before.items() if process_id in after and name not in own_names]
    used = sorted((item for item in used if item[0] >= REPORTED_CPU_SECONDS), reverse=True)[:4]
    return ", ".join(f"{name} {seconds:.1f} s" for seconds, name in used) or f"none above {REPORTED_CPU_SECONDS} s"


def main():
    parser = argparse.ArgumentParser(
        description="Runs benchmark binaries in alternating rounds, one Catch2 XML per run.",
        epilog="Arguments after -- go to each binary: a test spec, -c <section>, --benchmark-samples <n>.")
    parser.add_argument("binaries", nargs="+", type=Path, help="test executables, each built from one variant")
    parser.add_argument("--rounds", type=int, default=6)
    parser.add_argument("--results", type=Path, default=Path("benchmark-rounds"), help="directory for r<round>_<label>.xml (default: %(default)s)")
    parser.add_argument("--no-controls", action="store_true", help="skip the QImage and SSE4.1 runs: about half the time")
    # Split by hand: argparse would take what follows -- as more binaries
    own_arguments, catch_arguments = sys.argv[1:], []
    if "--" in own_arguments:
        separator = own_arguments.index("--")
        own_arguments, catch_arguments = own_arguments[:separator], own_arguments[separator + 1:]
    arguments = parser.parse_args(own_arguments)

    labels = [binary.stem for binary in arguments.binaries]
    if len(set(labels)) != len(labels):
        raise SystemExit("Two binaries share a label: rename one file.")
    for binary in arguments.binaries:
        if not binary.is_file():
            raise SystemExit(f"Not found: {binary}")

    # Catch2 rejects a repeated option, so a default is added only when the caller did not pass that option
    if not catch_arguments or catch_arguments[0].startswith("-"):
        catch_arguments.insert(0, "[!benchmark]")
    if "--benchmark-samples" not in catch_arguments:
        catch_arguments += ["--benchmark-samples", "10"]
    if "--benchmark-no-analysis" not in catch_arguments:
        catch_arguments.append("--benchmark-no-analysis")

    environment = dict(os.environ)
    # Keeps QImage::scaled, the control, single-threaded
    environment["QT_NO_GUI_THREADPOOL"] = "1"
    if arguments.no_controls:
        environment[NO_CONTROLS_VARIABLE] = "1"
    if environment.get("QT_ROOT_DIR"):
        environment["PATH"] = str(Path(environment["QT_ROOT_DIR"]) / "bin") + os.pathsep + environment["PATH"]

    arguments.results.mkdir(parents=True, exist_ok=True)
    failed = False
    for round_number in range(1, arguments.rounds + 1):
        for binary, label in zip(arguments.binaries, labels):
            output = arguments.results / f"r{round_number}_{label}.xml"
            # The snapshots stay outside the timed work
            before = process_cpu_seconds()
            started = time.monotonic()
            exit_code = subprocess.run([str(binary.resolve()), *catch_arguments, "--reporter", "xml", "--out", str(output)], env=environment, stdout=subprocess.DEVNULL).returncode
            seconds = time.monotonic() - started
            others = busiest_other_processes(before, process_cpu_seconds(), binary.stem)
            print(f"{time.strftime('%H:%M:%S')} round {round_number} {label}: exit {exit_code}, {seconds:.0f} s; other CPU: {others}", flush=True)
            failed = failed or exit_code != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
