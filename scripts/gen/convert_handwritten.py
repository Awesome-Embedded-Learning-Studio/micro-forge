import re, subprocess, tempfile, pathlib, sys

MAGIC32 = {
 'base32':'0x6B415365','disp32':'0x6B415367','width32':'0x6B415369','slot32':'0x6B41536A',
 'src32':'0x6B41536B','mnem32':'0x6B41536C','cc32':'0x6B41536D','op32':'0x6B41536E',
 'imm32':'0x6B41536F','idx32':'0x6B415370','dst32':'0x6B415372','abs32':'0x6B415373',
 'rsp32':'0x6B415374','reg32':'0x6B415375','srcw32':'0x6B415376','zx32':'0x6B415377',
 'shift32':'0x6B415378',
}
MAGIC64 = {
 'cpu64':'0x1B4153A5000000C1','helper64':'0x2B4153A5000000C2','imm64':'0x3B4153A5000000C3',
 'abs64':'0x4B4153A5000000C4','my_rip64':'0x5B4153A5000000C5','ret_addr64':'0x6B4153A5000000C6',
 'target64':'0x7B4153A5000000C7',
}
MAGIC8 = {'shift8':'0x5A'}

def size_of(kind):
    if kind in MAGIC64: return 8
    if kind in MAGIC8: return 1
    return 4

def parse(path):
    src = open(path).read()
    pat = re.compile(r'constexpr std::array<std::uint8_t, (\d+)> (k\w+) = \{(.*?)\};\n((?://[^\n]*\n)*)', re.S)
    out = []
    for m in pat.finditer(src):
        size, name, body, comment = int(m.group(1)), m.group(2), m.group(3), m.group(4)
        body_nc = re.sub(r'//[^\n]*', '', body)
        tokens = re.findall(r'0x[0-9A-Fa-f]+|\b0\b', body_nc)
        vals = [int(t, 16) if t.startswith('0x') else 0 for t in tokens]
        assert len(vals) == size, f'{name}: {len(vals)} != {size}'
        holes = []
        # 注释形如 "Holes: base32@5, disp32@12, cpu64@18, width32@27, ..."
        for hm in re.finditer(r'(\w+)@(\d+)', comment):
            kind, off = hm.group(1), int(hm.group(2))
            holes.append((kind, off, size_of(kind)))
        out.append((name, vals, holes))
    return out

def placeholder_bytes(sz, i):
    return bytes([0x20 + i]) * sz

def fill(vals, holes):
    buf = bytearray(vals)
    for i, (kind, off, sz) in enumerate(holes):
        buf[off:off+sz] = placeholder_bytes(sz, i)
    return bytes(buf)

def disasm(blob):
    with tempfile.TemporaryDirectory() as d:
        p = pathlib.Path(d)/'t.bin'; p.write_bytes(blob)
        out = subprocess.run(['objdump','-D','-b','binary','-m','i386:x86-64','-M','intel','--no-show-raw-insn',str(p)],
                             capture_output=True, text=True).stdout
    lines = []
    for line in out.splitlines():
        m = re.match(r'^\s+[0-9a-f]+:\s+(.*)$', line)
        if m:
            text = m.group(1).strip()
            if text and not text.startswith('.'):
                lines.append(text)
    return lines

def convert(name, vals, holes):
    blob = fill(vals, holes)
    lines = disasm(blob)
    asm = []
    for text in lines:
        for i, (kind, off, sz) in enumerate(holes):
            needle = hex(int.from_bytes(placeholder_bytes(sz, i), 'little'))
            magic = MAGIC64.get(kind) or MAGIC32.get(kind) or MAGIC8.get(kind)
            if needle in text:
                text = text.replace(needle, magic)
        text = text.replace('*1+', '+').replace('*1-', '-').replace('riz*1+', '+').replace('riz', '').replace('++', '+')
        asm.append(text)
    return asm

for path in sys.argv[1:]:
    for name, vals, holes in parse(path):
        print(f'### {name} ({len(vals)}B, {len(holes)} holes)')
        for line in convert(name, vals, holes):
            print('    ' + line)
        print()
