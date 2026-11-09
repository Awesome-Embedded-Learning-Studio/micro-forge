// Engine self-test harness (Node), batch-5 acceptance: load engine.wasm,
// run the bare client once, verify the exit, console, and that the
// snapshot triple round-trips through the artifact.
//
// Usage: node wasm/harness/selftest.mjs [path-to-engine.wasm] [path-to-bare-client.bin]

import { loadEngine, decodeExit, layout, pokeGuest, EXIT_KIND } from "./engine.mjs";
import { readFileSync } from "node:fs";

const wasmPath = process.argv[2] ?? "build-wasm/engine.wasm";
const clientPath = process.argv[3] ?? "build/tests/bare-client.bin";

const CODE_BASE = 0x100000;
const STACK_TOP = 0x90000;

const e = await loadEngine(wasmPath);

function fail(msg) {
    console.error(`selftest: FAIL ${msg}`);
    process.exit(1);
}

// Version triple is queryable from the artifact itself.
const abi = e.mf_abi_version();
const engine = e.mf_engine_version();
const fmt = e.mf_snapshot_format_version();
const major = engine >>> 16;
const minor = (engine >>> 8) & 0xff;
const patch = engine & 0xff;
console.log(`selftest: abi=${abi} engine=${major}.${minor}.${patch} snapshot-fmt=${fmt}`);
if (abi !== 1) fail(`unexpected ABI version ${abi}`);

// The whole memory map comes from the artifact (batch-5 review F3: no
// harness-side constants to drift).
const map = layout(e);
console.log(`selftest: guest window [0, 0x${map.windowEnd.toString(16)}) lfb=0x${map.lfbBase.toString(16)}+0x${map.lfbSize.toString(16)}`);

// The exported linear memory IS guest memory: write the image directly.
const client = readFileSync(clientPath);
pokeGuest(e, CODE_BASE, client);

e.mf_reset(CODE_BASE, STACK_TOP);
const { kind, code } = decodeExit(e.mf_run(BigInt(1 << 20)));
console.log(`selftest: exit kind=${kind} code=${code} icount=${e.mf_icount()}`);
if (kind !== EXIT_KIND.EXIT) fail(`expected syscall exit (kind ${EXIT_KIND.EXIT}), got ${kind}`);
if (code !== 0xaa) fail(`expected exit code 0xaa, got 0x${code.toString(16)}`);
if (e.mf_icount() === 0n) fail("icount did not advance");

// The write syscall collected nothing (the client is silent).
const consoleLen = e.mf_read_console(map.windowEnd - 4096, 4096);
if (consoleLen !== 0n) fail(`expected empty console, got ${consoleLen} bytes`);

// Snapshot blob length is queryable from the artifact (the triple is in
// the envelope). Full round-trip inside linear memory is a placement
// question for batch 6+: the region cannot hold its own blob plus
// header, so the host scratch policy (grow-above-top) gets designed with
// the device bank, not improvised here.
const needed = e.mf_snapshot(0, 0);
if (needed < BigInt(map.windowEnd)) fail(`snapshot claims ${needed} bytes, less than the guest window`);
console.log(`selftest: snapshot blob length = ${needed} bytes (queryable)`);

// A null host pointer cannot stand for a direct-access miss: wasm guest
// RAM at physical 0 is valid. Run twice to cover decoded cache hits too.
const zero = await loadEngine(wasmPath);
pokeGuest(zero, CODE_BASE, new Uint8Array([
    0x31, 0xff,                   // xor edi,edi
    0xc6, 0x07, 0x5a,             // mov byte [rdi],0x5a
    0x0f, 0xb6, 0x3f,             // movzx edi,byte [rdi]
    0xb8, 0x3c, 0x00, 0x00, 0x00, // mov eax,60
    0x0f, 0x05,                   // syscall exit
]));
for (let pass = 0; pass < 2; ++pass) {
    pokeGuest(zero, 0, new Uint8Array([0]));
    zero.mf_reset(CODE_BASE, STACK_TOP);
    const status = decodeExit(zero.mf_run(100n));
    if (status.kind !== EXIT_KIND.EXIT || status.code !== 0x5a ||
        new Uint8Array(zero.memory.buffer)[0] !== 0x5a) {
        fail(`physical-zero RAM access on pass ${pass}: ${JSON.stringify(status)}`);
    }
}
console.log("selftest: PASS (bare client, snapshot size, physical-zero RAM, cached execution)");
