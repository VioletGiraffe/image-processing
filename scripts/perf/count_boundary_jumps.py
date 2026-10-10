# Counts the kernels' jumps that cross or end on a 32-byte boundary: the Skylake family's JCC erratum keeps such
# a jump's code out of the decoded cache. A build padded against it should count almost none.
# The listing needs the instruction bytes: `dumpbin /disasm` with the PDB beside the executable, or `objdump -d -C`.
import argparse
import re
from collections import Counter

from kernel_listing import KERNEL_PATTERN, read_functions


# A conditional jump fuses with the flag-setting instruction before it, and the pair then counts as one jump.
# Approximated: the real rules also depend on the condition, and exclude a memory operand with an immediate.
FUSING_MNEMONIC = re.compile(r"(cmp|test|and|add|sub|inc|dec)[bwlq]?")


def jump_kind(mnemonic):
    if mnemonic.startswith("jmp"):
        return "jmp"
    if mnemonic.startswith("j"):
        return "conditional"
    if mnemonic.startswith("call"):
        return "call"
    if mnemonic.startswith("ret"):
        return "ret"
    return None


def on_boundary(first_byte, end):
    return first_byte // 32 != (end - 1) // 32 or end % 32 == 0


def main():
    parser = argparse.ArgumentParser(description="Counts the jumps that cross or end on a 32-byte boundary.")
    parser.add_argument("listing", help="dumpbin /disasm or objdump -d output, with instruction bytes")
    parser.add_argument("--functions", default=KERNEL_PATTERN, help="regex a function's name must contain (default: %(default)s)")
    parser.add_argument("--list", action="store_true", help="also print each such jump")
    arguments = parser.parse_args()

    functions = read_functions(arguments.listing, arguments.functions)
    if not functions:
        raise SystemExit(f"No function matching '{arguments.functions}' in {arguments.listing}")

    total, affected = Counter(), Counter()
    for name, instructions in functions.items():
        for index, instruction in enumerate(instructions):
            kind = jump_kind(instruction.mnemonic)
            if not kind:
                continue

            first_byte = instruction.address
            previous = instructions[index - 1] if index else None
            if kind == "conditional" and previous and previous.end == first_byte and FUSING_MNEMONIC.fullmatch(previous.mnemonic):
                first_byte = previous.address

            total[kind] += 1
            if on_boundary(first_byte, instruction.end):
                affected[kind] += 1
                if arguments.list:
                    print(f"{instruction.address:X} {instruction.mnemonic} {instruction.operands}  [{first_byte:X}, {instruction.end:X})  {name[:60]}")

    print(f"{len(functions)} functions")
    for kind in ("conditional", "jmp", "call", "ret"):
        print(f"{kind}: {affected[kind]} of {total[kind]} on a boundary")


if __name__ == "__main__":
    main()
