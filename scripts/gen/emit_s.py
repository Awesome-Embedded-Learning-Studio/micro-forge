#!/usr/bin/env python3
"""One-shot bootstrap: rewrites jit_templates.S from the hand-written arrays.

This is a migration aid, not part of the build. It reads the original
jit_templates*.hpp arrays, fills each hole with a distinguishable
placeholder, disassembles to confirm the instruction stream, swaps the
placeholders for hole magics, and emits assembly. The assembler then owns
every offset.
"""
import re
import sys


exec(open('scripts/gen/_convert_lib.py').read().split("for path in sys.argv")[0])


def snake(name):
    return re.sub(r'(?<!^)(?=[A-Z])', '_', name[1:]).lower()


def main(sources, out_path):
    out = [
        '/* JIT machine-code templates. Hole magics mark patch sites;',
        ' * scripts/gen/gen_jit_templates.py assembles this file, scans the',
        ' * magics and emits jit_templates_generated.hpp with the offsets.',
        ' * Never hand-write byte arrays: the assembler owns every offset. */',
        '.intel_syntax noprefix',
        '.section .text',
        '',
    ]
    count = 0
    for path in sources:
        for name, vals, holes in parse(path):
            out.append(f'.globl tpl_{snake(name)}')
            out.append(f'tpl_{snake(name)}:')
            out.extend(f'    {line}' for line in convert(name, vals, holes))
            out.append(f'.globl tpl_{snake(name)}_end')
            out.append(f'tpl_{snake(name)}_end:')
            out.append('')
            count += 1
    open(out_path, 'w').write('\n'.join(out))
    print(f'wrote {count} templates to {out_path}')


if __name__ == '__main__':
    main(['include/micro_forge/base/jit_templates_mem.hpp',
          'include/micro_forge/base/jit_templates.hpp'],
         'src/arch/x86_64/jit_templates.S')
