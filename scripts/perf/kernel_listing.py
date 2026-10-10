# Reads a disassembly listing into functions of instructions, for the scripts beside it.
# Takes `dumpbin /disasm` and `objdump -d` output, both with the instruction bytes: a length comes from its bytes.
import re
from dataclasses import dataclass


# The resizer's kernels, in MSVC's decorated names and in demangled ones alike
KERNEL_PATTERN = r"resizeRows"


@dataclass
class Instruction:
    address: int
    length: int
    mnemonic: str
    operands: str

    @property
    def end(self):
        return self.address + self.length


_DUMPBIN_INSTRUCTION = re.compile(r"\s+([0-9A-F]{8,16}): ((?:[0-9A-F]{2} ?)+?)\s+([a-z][a-z0-9]*)\s*(.*)")
_DUMPBIN_MORE_BYTES = re.compile(r"\s+((?:[0-9A-F]{2} ?)+)")
_OBJDUMP_LABEL = re.compile(r"[0-9a-f]+ <(.+)>:")


def _byte_count(text):
    return len(text.split())


# Functions in listing order: name -> instructions, padding included
def read_functions(path, name_pattern=KERNEL_PATTERN):
    wanted = re.compile(name_pattern)
    functions = {}
    current = None
    with open(path, encoding="latin-1") as listing:
        for line in listing:
            line = line.rstrip("\r\n")
            if not line:
                continue

            if not line[0].isspace():
                label = _OBJDUMP_LABEL.fullmatch(line)
                name = label.group(1) if label else line[:-1] if line.endswith(":") else None
                current = functions.setdefault(name, []) if name and wanted.search(name) else None
                continue
            if current is None:
                continue

            if "\t" in line:
                # objdump: address, bytes and text are tab-separated; a line without text continues the bytes
                fields = line.split("\t")
                if len(fields) < 2 or not fields[0].strip().endswith(":"):
                    continue
                if len(fields) == 2 or not fields[2].strip():
                    if current:
                        current[-1].length += _byte_count(fields[1])
                    continue
                mnemonic, _, operands = fields[2].strip().partition(" ")
                current.append(Instruction(int(fields[0].strip()[:-1], 16), _byte_count(fields[1]), mnemonic, operands.strip()))
                continue

            instruction = _DUMPBIN_INSTRUCTION.fullmatch(line)
            if instruction:
                current.append(Instruction(int(instruction.group(1), 16), _byte_count(instruction.group(2)), instruction.group(3), instruction.group(4)))
            elif current and _DUMPBIN_MORE_BYTES.fullmatch(line):
                current[-1].length += _byte_count(line)
    return functions



def is_padding(instruction):
    text = f"{instruction.mnemonic} {instruction.operands}"
    return "nop" in text or instruction.mnemonic == "int3" or text in ("int 3", "xchg %ax,%ax")


# The index of the instruction a jump goes to, None for a target outside the function or a computed one
def jump_target_index(instruction, index_by_address):
    if not instruction.mnemonic.startswith("j"):
        return None
    target = re.match(r"(?:0x)?([0-9A-Fa-f]{5,16})\b", instruction.operands)
    return index_by_address.get(int(target.group(1), 16)) if target else None


# Loops as (first, last) instruction indexes, a loop being a backward jump within the function; outer loops precede the loops they hold
def find_loops(instructions):
    index_by_address = {instruction.address: index for index, instruction in enumerate(instructions)}
    loops = []
    for index, instruction in enumerate(instructions):
        target = jump_target_index(instruction, index_by_address)
        if target is not None and target <= index:
            loops.append((target, index))
    return sorted(loops, key=lambda loop: (loop[0], -loop[1]))


def is_innermost(loop, loops):
    return not any(other != loop and other[0] >= loop[0] and other[1] <= loop[1] for other in loops)


def references_stack(instruction):
    return re.search(r"\[(rsp|rbp)\b|\((%rsp|%rbp)", instruction.operands) is not None
