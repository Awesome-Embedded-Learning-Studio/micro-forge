// Block-mode runner: per rip compile (mf_wasm_block2) + run, GPR bridging
// in JS; interpreter fallback for non-lowerable prefixes and block ends.
// usage: node block-bench.mjs engine.wasm --kernel F --disk F [--instructions N]
//        [--mode interp|blocks] [--chunk N]
import { readFileSync } from "node:fs";
import { loadEngine, decodeExit, EXIT_KIND } from "/home/charliechen/micro-forge/wasm/harness/engine.mjs";

const options = {
  kernel: process.env.MF_KERNEL_BIN, disk: process.env.MF_DISK_IMG,
  instructions: "40000000", chunk: "2000000", mode: "blocks",
};
const artifacts = [];
for (let i = 2; i < process.argv.length; ++i) {
  const arg = process.argv[i];
  if (arg.startsWith("--")) {
    const key = arg.slice(2);
    if (!Object.hasOwn(options, key) || !process.argv[i + 1]) throw new Error("bad option " + arg);
    options[key] = process.argv[++i];
  } else artifacts.push(arg);
}
if (artifacts.length !== 1 || !options.kernel || !options.disk) throw new Error("usage");

const e = await loadEngine(artifacts[0]);
const kernel = readFileSync(options.kernel);
const disk = readFileSync(options.disk);
const k = e.malloc(kernel.length), d = e.malloc(disk.length);
new Uint8Array(e.memory.buffer, k, kernel.length).set(kernel);
new Uint8Array(e.memory.buffer, d, disk.length).set(disk);
if (e.mf_boot_cinux(k, kernel.length, d, disk.length) !== 1) throw new Error("boot refused");
e.free(k); e.free(d);

// --- block machinery -------------------------------------------------
const outLen = e.malloc(4), outInsns = e.malloc(4);
const cache = new Map(); // rip(Number) -> {run, mem, count} | null (refused)
const ripNow = () => BigInt(e.mf_rip_high()) << 32n | BigInt(e.mf_rip_low() >>> 0);

async function compile(rip) {
  const lo = Number(rip & 0xFFFFFFFFn) >>> 0, hi = Number(rip >> 32n) >>> 0;
  const ptr = e.mf_wasm_block2(lo, hi, 16, outLen, outInsns);
  const len = new DataView(e.memory.buffer).getUint32(outLen, true);
  if (ptr === 0) return null;
  const count = new DataView(e.memory.buffer).getUint32(outInsns, true);
  const bytes = new Uint8Array(e.memory.buffer, ptr, len).slice();
  e.free(ptr);
  if (count === 0) return null; // decode stopped before any instruction
  const { instance } = await WebAssembly.instantiate(bytes, {});
  return {
    run: instance.exports.block,
    mem: new DataView(instance.exports.memory.buffer),
    count,
  };
}

async function runBlocks(budget) {
  let retired = 0n;
  let compiled = 0, refused = 0, execs = 0;
  while (retired < budget) {
    const rip = ripNow();
    const key = rip; // BigInt key: Number loses precision above 2^53
    let block = cache.get(key);
    if (block === undefined) {
      block = await compile(rip);
      cache.set(key, block);
      if (block) ++compiled; else ++refused;
    }
    if (block && BigInt(e.mf_event_budget()) >= BigInt(block.count)) {
      const m = block.mem;
      for (let i = 0; i < 16; ++i) m.setBigUint64(i * 8, e.mf_gpr_get(i), true);
      m.setUint32(256, e.mf_flags_get(), true); // flags cell behind the GPRs
      const next = block.run();
      for (let i = 0; i < 16; ++i) e.mf_gpr_set(i, m.getBigUint64(i * 8, true));
      e.mf_flags_set(m.getUint32(256, true));
      e.mf_set_rip(next);
      e.mf_retire(block.count); // scheduler + interrupt accounting
      retired += BigInt(block.count);
      ++execs;
    } else {
      // one interpreter step: rep-prefixed insns retire one per iteration,
      // so the actual count comes from mf_icount, not the step itself
      const before = e.mf_icount();
      const exit = decodeExit(e.mf_run(1n));
      retired += e.mf_icount() - before;
      if (exit.kind !== EXIT_KIND.RUNNING) return { retired, exit, compiled, refused, execs };
    }
  }
  return { retired, exit: { kind: 0, code: 0 }, compiled, refused, execs };
}

const budget = BigInt(options.instructions);
const start = process.hrtime.bigint();
let result;
if (options.mode === "blocks") {
  result = await runBlocks(budget);
} else {
  const chunk = BigInt(options.chunk);
  let exit = { kind: 0, code: 0 };
  while (e.mf_icount() < budget) {
    const remain = budget - e.mf_icount();
    exit = decodeExit(e.mf_run(remain < chunk ? remain : chunk));
    if (exit.kind !== EXIT_KIND.RUNNING) break;
  }
  result = { retired: e.mf_icount(), exit, compiled: 0, refused: 0, execs: 0 };
}
const seconds = Number(process.hrtime.bigint() - start) / 1e9;
console.log(JSON.stringify({
  mode: options.mode, seconds: seconds.toFixed(3),
  mips: (Number(result.retired) / seconds / 1e6).toFixed(2),
  icount: result.retired.toString(),
  exit: result.exit,
  serial: e.mf_serial_tx().toString(),
  compiled: result.compiled, refused: result.refused, blockExecs: result.execs,
}));
