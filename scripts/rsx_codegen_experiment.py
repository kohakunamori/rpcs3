#!/usr/bin/env python3
"""Prepare an isolated noinline experiment and require complete codegen evidence."""
from __future__ import annotations
import argparse
import difflib
import hashlib
import json
from pathlib import Path

MARKER = "\t\tvoid grow_for_append()\n"
CONTROL = """#ifdef RPCS3_EXPERIMENTAL_RSX_ARRAY_OUTLINE_GROWTH
#ifdef _MSC_VER
        __declspec(noinline)
#else
        __attribute__((noinline))
#endif
#endif
"""


def patch_source(source: str) -> str:
    if source.count(MARKER) != 1 or 'RPCS3_EXPERIMENTAL_RSX_ARRAY_OUTLINE_GROWTH' in source:
        raise ValueError('Unexpected or already patched container source')
    return source.replace(MARKER, CONTROL + MARKER, 1)


def file_info(path: Path) -> dict:
    content = path.read_bytes()
    if not content:
        raise ValueError(f'Empty required artifact: {path}')
    return {'bytes': len(content), 'sha256': hashlib.sha256(content).hexdigest()}


def manifest(directory: Path, variants: list[str]) -> dict:
    # upload-artifact only rejects an entirely empty match, not missing files
    # within a multi-path upload. Check every promised artifact ourselves.
    return {f'{v}.{ext}': file_info(directory / f'{v}.{ext}')
            for v in variants for ext in ('exe', 'obj', 'asm', 'build.txt')}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    patch = commands.add_parser('patch')
    patch.add_argument('--header', type=Path, required=True)
    patch.add_argument('--output', type=Path, required=True)
    check = commands.add_parser('manifest')
    check.add_argument('--directory', type=Path, required=True)
    check.add_argument('--variants', nargs='+', required=True)
    args = parser.parse_args()
    if args.command == 'patch':
        original = args.header.read_text(encoding='utf-8')
        updated = patch_source(original)
        args.output.mkdir(parents=True, exist_ok=True)
        diff = ''.join(difflib.unified_diff(original.splitlines(True), updated.splitlines(True),
                      fromfile='a/rpcs3/Emu/RSX/Common/simple_array.hpp',
                      tofile='b/rpcs3/Emu/RSX/Common/simple_array.hpp'))
        (args.output / 'outline-growth.patch').write_text(diff, encoding='utf-8')
        before = file_info(args.header)
        args.header.write_text(updated, encoding='utf-8', newline='\n')
        (args.output / 'source.json').write_text(json.dumps({
            'before': before, 'after': file_info(args.header),
            'scope': 'CI checkout only; no production default change'}, indent=2), encoding='utf-8')
    else:
        evidence = manifest(args.directory, args.variants)
        (args.directory / 'manifest.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
        print(f'Validated {len(evidence)} required codegen artifacts')


if __name__ == '__main__':
    main()
