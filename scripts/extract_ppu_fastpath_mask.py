#!/usr/bin/env python3
"""Extract the exact PPU fast-path predicate for a standalone proxy benchmark.

The Windows proxy benchmark must not keep a second handwritten copy of the
primary opcode mask. Refuse to build if its definition cannot be found.
"""
from pathlib import Path
import re

header = Path("rpcs3/Emu/Cell/PPUInterpreter.h").read_text(encoding="utf-8")
match = re.search(
    r"^namespace ppu_interpreter_detail\s*\{.*?^\}",
    header,
    flags=re.MULTILINE | re.DOTALL,
)
if not match or "needs_special_decode" not in match.group(0):
    raise RuntimeError("Unable to extract the PPU special-case predicate from the production header")

generated = (
    "#pragma once\n"
    "#include <cstdint>\n"
    "using u32 = std::uint32_t;\n"
    "using u64 = std::uint64_t;\n\n"
    + match.group(0)
    + "\n"
)
Path("mask_generated.h").write_text(generated, encoding="utf-8")
print("Extracted production special-case predicate from PPUInterpreter.h")
