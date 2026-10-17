# Prints the kernels of a disassembly listing in a form two builds can be diffed in: padding dropped, a jump's target as
# its distance in instructions, other absolute addresses blanked. With --loops, prints each kernel's loops instead;
# with --loop, one loop's instructions.
# The listing: `dumpbin /disasm` with the PDB beside the executable, or `objdump -d -C`.
import argparse
import re

from kernel_listing import KERNEL_PATTERN, find_loops, is_call, is_innermost, is_multiply, is_padding, jump_target_index, read_functions, references_stack


def comparable_operands(instruction, index, index_by_address):
    target = jump_target_index(instruction, index_by_address)
    if target is not None:
        return f"@{target - index:+d}"
    # Data and call addresses differ between builds of the same code
    return re.sub(r"\b(?:0x)?[0-9A-Fa-f]{8,16}h?\b", "<address>", instruction.operands)


def print_instructions(instructions, with_addresses):
    index_by_address = {instruction.address: index for index, instruction in enumerate(instructions)}
    for index, instruction in enumerate(instructions):
        address = f"{instruction.address:X}: " if with_addresses else ""
        print(f"{address}{instruction.mnemonic} {comparable_operands(instruction, index, index_by_address)}".rstrip())


def print_loops(instructions, with_stack_references):
    loops = find_loops(instructions)
    print(f"{len(instructions)} instructions, {instructions[-1].end - instructions[0].address} bytes, {sum(map(references_stack, instructions))} stack references")
    for number, loop in enumerate(loops, 1):
        body = instructions[loop[0]:loop[1] + 1]
        kind = "inner" if is_innermost(loop, loops) else "outer"
        print(f"  #{number} {body[0].address:X} {kind}: {len(body)} instructions, {body[-1].end - body[0].address} bytes, {sum(map(is_multiply, body))} multiplies, "
              f"{sum(map(is_call, body))} calls, {sum(map(references_stack, body))} stack references")
        if with_stack_references:
            for instruction in filter(references_stack, body):
                print(f"      {instruction.mnemonic} {instruction.operands}")


# number: the loop's in print_loops' output
def print_loop(instructions, number):
    loops = find_loops(instructions)
    if number > len(loops):
        print(f"{len(loops)} loops, no #{number}")
        return
    first, last = loops[number - 1]
    print_instructions(instructions[first:last + 1], with_addresses=False)


def main():
    parser = argparse.ArgumentParser(description="Prints a listing's kernels in a diffable form, or their loops.")
    parser.add_argument("listing", help="dumpbin /disasm or objdump -d output, with instruction bytes")
    parser.add_argument("--functions", default=KERNEL_PATTERN, help="regex a function's name must contain (default: %(default)s)")
    parser.add_argument("--addresses", action="store_true", help="prefix each instruction with its address: for reading, not for diffing")
    parser.add_argument("--loops", action="store_true", help="each kernel's loops, numbered, with their sizes and what they hold, no instructions")
    parser.add_argument("--stack-references", action="store_true", help="with --loops: also each loop's instructions that reference the stack")
    parser.add_argument("--loop", type=int, metavar="NUMBER", help="only that loop's instructions, by its number in the --loops output")
    arguments = parser.parse_args()

    functions = read_functions(arguments.listing, arguments.functions)
    if not functions:
        raise SystemExit(f"No function matching '{arguments.functions}' in {arguments.listing}")

    for name, instructions in functions.items():
        instructions = [instruction for instruction in instructions if not is_padding(instruction)]
        print(f"## {name}")
        if arguments.loops:
            print_loops(instructions, arguments.stack_references)
        elif arguments.loop:
            print_loop(instructions, arguments.loop)
        else:
            print_instructions(instructions, arguments.addresses)


if __name__ == "__main__":
    main()
