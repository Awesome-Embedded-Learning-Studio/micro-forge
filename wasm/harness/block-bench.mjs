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
const outLen = e.malloc(4), outInsns = e.malloc(4), outMasks = e.malloc(8);
const cache = new Map(); // rip(BigInt) -> {run,...} | null; LRU-bounded below
const refusedHits = new Map(); // rip(Number key hex) -> executions (weight)
const CACHE_MAX = 30000;
function cachePut(key, block) {
  cache.set(key, block);
  if (cache.size > CACHE_MAX) {
    // Map keeps insertion order: evict the oldest third
    const drop = cache.size - Math.floor(CACHE_MAX * 2 / 3);
    let n = 0;
    for (const k of cache.keys()) {
      cache.delete(k);
      if (++n >= drop) break;
    }
  }
}
const ripNow = () => BigInt(e.mf_rip_high()) << 32n | BigInt(e.mf_rip_low() >>> 0);

async function compile(rip) {
  const lo = Number(rip & 0xFFFFFFFFn) >>> 0, hi = Number(rip >> 32n) >>> 0;
  const ptr = e.mf_wasm_block2(lo, hi, 16, outLen, outInsns, outMasks);
  const len = new DataView(e.memory.buffer).getUint32(outLen, true);
  if (ptr === 0) return null;
  const view = new DataView(e.memory.buffer);
  const count = view.getUint32(outInsns, true);
  const used = view.getUint32(outMasks, true) & 0x7FFFFFFF;
  const dirty = view.getUint32(outMasks + 4, true) & 0x7FFFFFFF;
  const flagsUsed = (view.getUint32(outMasks, true) & 0x80000000) !== 0;
  const flagsDirty = (view.getUint32(outMasks + 4, true) & 0x80000000) !== 0;
  const bytes = new Uint8Array(e.memory.buffer, ptr, len).slice();
  e.free(ptr);
  if (count === 0) return null; // decode stopped before any instruction
  const { instance } = await WebAssembly.instantiate(bytes, { e: { memory: e.memory } });
  return { run: instance.exports.block, count, used, dirty, flagsUsed, flagsDirty };
}

async function runBlocks(budget) {
  let retired = 0n;
  let compiled = 0, refused = 0, execs = 0;
  while (retired < budget) {
    const rip = ripNow();
    const key = rip; // BigInt key: Number loses precision above 2^53
    let block = cache.get(key);
    if (block !== null && block !== undefined && block.epoch !== e.mf_code_epoch()) {
      cache.delete(key); // self-modifying code rewrote these bytes
      block = undefined;
    }
    if (block === undefined) {
      block = await compile(rip);
      if (block) block.epoch = e.mf_code_epoch();
      cachePut(key, block);
      if (block) ++compiled; else ++refused;
    }
    if (block && (budgetOf(e) >= BigInt(block.count))) {
      // mirror bridge: one C call each way, no BigInt crossing the boundary
      e.mf_gprs_flush(block.used | 0x80000000); // flags always: handoff commits them
      const next = block.run();
      const bridge = e.mf_gpr_bridge_base();
      const view = new DataView(e.memory.buffer);
      const handed = view.getUint32(bridge + 292, true) === 1;
      // handoff wrote a prefix dirty mask (may exceed the static liveness mask)
      const hd = handed ? view.getUint32(bridge + 300, true) : 0;
      e.mf_gprs_commit(block.dirty | hd | 0x80000000);
      // drain the guest store log: block stores bypass note_store, so the
      // interpreter decode cache must be invalidated from the runner side
      const nStores = view.getUint32(bridge + 296, true);
      for (let i = 0; i < nStores; ++i) e.mf_note_store(view.getBigUint64(bridge + 304 + i * 8, true));
      if (nStores) view.setUint32(bridge + 296, 0, true);
      if (handed) {
        // handoff: mid-block refusal or a ret terminator hands the rest
        // back; done instructions ran, the runner takes one interp step.
        const done = view.getUint32(bridge + 288, true);
        e.mf_set_rip(next);
        e.mf_retire(done);
        const before = e.mf_icount();
        const exit = decodeExit(e.mf_run(1n));
        retired += BigInt(done) + (e.mf_icount() - before);
        if (exit.kind !== EXIT_KIND.RUNNING) return { retired, exit, compiled, refused, execs };
      } else {
        e.mf_set_rip(next);
        e.mf_retire(block.count); // scheduler + interrupt accounting
        retired += BigInt(block.count);
      }
      ++execs;
    } else {
      refusedHits.set(key, (refusedHits.get(key) ?? 0) + 1);
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

const budgetOf = (e) => {
    const raw = e.mf_event_budget(); // i64 -> signed BigInt: UINT64_MAX reads -1n
    return raw < 0n ? raw + (1n << 64n) : raw;
};

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
  serialText: process.env.MF_DUMP_SERIAL ? Buffer.from(Number(e.mf_serial_tx())).toString("latin1") : undefined,
  compiled: result.compiled, refused: result.refused, blockExecs: result.execs,
}));
const top = [...refusedHits.entries()].sort((x, y) => y[1] - x[1]).slice(0, 12);
console.error("top refused (weight, rip): " + top.map(([r, n]) => `${n}@0x${(r < 0n ? r + (1n << 64n) : r).toString(16)}`).join(" "));
