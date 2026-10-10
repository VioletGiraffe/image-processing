# Sums a VTune hotspots profile over the kernels' loops, or over each instruction, of a disassembly listing.
# Grouped by function or source line, VTune gives each forced-inline primitive its own row and a loop's share is lost.
# The profile, from an elevated prompt:
#   vtune -collect hotspots -knob sampling-mode=hw -knob enable-stack-collection=false -result-dir <dir> -- <test executable> ...
#   vtune -report hotspots -result-dir <dir> -group-by address -format csv -csv-delimiter tab -report-output <csv>
# The listing must be of the profiled executable. A sample lands an instruction or two after the one that waited.
# Times are one process's: compare builds per unit of work, by the instructions retired in a loop the change left alone.
import argparse
import bisect
import csv

from kernel_listing import KERNEL_PATTERN, find_loops, is_innermost, read_functions, references_stack


ADDRESS_COLUMN = "Code Location"
TIME_COLUMN = "CPU Time"
INSTRUCTIONS_COLUMN = "Instructions Retired"


# CPU seconds and instructions retired by sampled address
def read_profile(path):
    samples = {}
    with open(path, encoding="utf-8", errors="replace", newline="") as profile:
        rows = csv.DictReader(profile, delimiter="\t")
        if not rows.fieldnames or ADDRESS_COLUMN not in rows.fieldnames or TIME_COLUMN not in rows.fieldnames:
            raise SystemExit(f"{path}: not a tab-delimited hotspots report grouped by address")
        for row in rows:
            try:
                address = int(row[ADDRESS_COLUMN], 16)
            except ValueError:
                continue  # A row VTune could not attribute to an address
            seconds, retired = samples.get(address, (0.0, 0.0))
            samples[address] = (seconds + float(row[TIME_COLUMN] or 0), retired + float(row.get(INSTRUCTIONS_COLUMN) or 0))
    return samples


def main():
    parser = argparse.ArgumentParser(description="Sums a VTune hotspots profile over the kernels' loops or instructions.")
    parser.add_argument("profile", help="vtune -report hotspots -group-by address -format csv -csv-delimiter tab")
    parser.add_argument("listing", help="dumpbin /disasm or objdump -d output of the profiled executable, with instruction bytes")
    parser.add_argument("--functions", default=KERNEL_PATTERN, help="regex a function's name must contain (default: %(default)s)")
    parser.add_argument("--instructions", action="store_true", help="every instruction with its time, not the loops")
    parser.add_argument("--minimum-seconds", type=float, default=0.1, help="functions and loops below this are left out (default: %(default)s)")
    arguments = parser.parse_args()

    samples = read_profile(arguments.profile)
    functions = read_functions(arguments.listing, arguments.functions)
    if not functions:
        raise SystemExit(f"No function matching '{arguments.functions}' in {arguments.listing}")

    for name, instructions in functions.items():
        addresses = [instruction.address for instruction in instructions]
        seconds = [0.0] * len(instructions)
        retired = [0.0] * len(instructions)
        for address, (sample_seconds, sample_retired) in samples.items():
            if addresses[0] <= address < instructions[-1].end:
                index = bisect.bisect_right(addresses, address) - 1
                seconds[index] += sample_seconds
                retired[index] += sample_retired
        if sum(seconds) < arguments.minimum_seconds:
            continue

        print(f"## {name}")
        print(f"{sum(seconds):.2f} s, {sum(retired) / 1e9:.1f} G instructions retired")
        if arguments.instructions:
            for instruction, instruction_seconds, instruction_retired in zip(instructions, seconds, retired):
                print(f"{instruction.address:X} {instruction_seconds:7.3f} s {instruction_retired / 1e9:7.2f} G  {instruction.mnemonic} {instruction.operands}")
            continue

        loops = find_loops(instructions)
        for loop in loops:
            first, last = loop
            loop_seconds = sum(seconds[first:last + 1])
            if loop_seconds < arguments.minimum_seconds:
                continue
            body = instructions[first:last + 1]
            kind = "inner" if is_innermost(loop, loops) else "outer"
            print(f"  {body[0].address:X}-{body[-1].address:X} {kind}: {loop_seconds:.2f} s, {sum(retired[first:last + 1]) / 1e9:.1f} G, "
                  f"{len(body)} instructions, {body[-1].end - body[0].address} bytes, {sum(map(references_stack, body))} stack references")


if __name__ == "__main__":
    main()
