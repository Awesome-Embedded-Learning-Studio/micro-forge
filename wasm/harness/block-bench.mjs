// Block-mode runner: per rip compile (mf_wasm_block2) + run, GPR bridging
// in JS; interpreter fallback for non-lowerable prefixes and block ends.
// usage: node block-bench.mjs engine.wasm --kernel F --disk F [--instructions N]
//        [--mode interp|blocks] [--chunk N]
// CODEX-TIMING: [--timing 1024 --timing-out /tmp/counts.json] for counts/compile;
//              --timing 1 clocks every phase (much slower; approximate phase costs).
import { readFileSync, writeFileSync } from "node:fs"; // CODEX-TIMING: optional report file
import { performance } from "node:perf_hooks"; // CODEX-TIMING: monotonic sample clock
import { loadEngine, decodeExit, EXIT_KIND } from "/home/charliechen/micro-forge/wasm/harness/engine.mjs";

const options = {
  kernel: process.env.MF_KERNEL_BIN, disk: process.env.MF_DISK_IMG,
  instructions: "40000000", chunk: "2000000", mode: "blocks",
  timing: "0", "timing-out": "", // CODEX-TIMING: 0 = original loop; N = mean sample interval
  runner: "1", // 1 = engine-side scheduler (C++), 0 = JS loop
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
const bridge0 = e.mf_gpr_bridge_base(); // mirror base is fixed for the engine
let view0 = new DataView(e.memory.buffer);
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

// 零进展 handoff 的重探间隔：跳过这么多轮后重跑一次块，看拒绝条件
// 是否已解除（内核态↔用户态切换）。64 是折中：重探成本 1/64，恢复
// 延迟平均 32 次调度循环（仍是纯解释器速度，无正确性影响）。
const SKIP_PROBE = 64;

// 无块/skip/预算不足时给解释器的短批次：mf_run 逐条处理设备/中断/异常
// 且自带预算截断，批次只是隔多少条再回 JS 查一次块机会。32 是 codex
// 固定 CPU 对照的保守选择（8/32/64 递增收益，32 起波动加大）。
const FALLBACK_BATCH = 32n;

// 引擎侧调度（src/wasm/block_runner.cpp）：JS 只处理 miss 编译与表注册，
// 调度循环（缓存探测/epoch 校验/桥/回退批次）全在引擎模块内。
// --runner 0 退回本文件的 JS 调度循环（A/B 对照用）。
const RUNNER_BURST = 4096;

// CODEX-TIMING: compilation is infrequent, so time every attempt exactly.
// Hot operations use random iteration samples (or all calls with --timing 1).
// The separate diagnostic loop keeps --timing 0 on the original hot path.
const timingInterval = Number(options.timing);
if (!Number.isSafeInteger(timingInterval) || timingInterval < 0) throw new Error("bad --timing");
if (timingInterval && options.mode !== "blocks") throw new Error("--timing requires --mode blocks"); // CODEX-TIMING
const timing = timingInterval ? {
  interval: timingInterval, loops: 0, prepares: 0, handoffs: 0,
  refusedSteps: 0, eventSteps: 0, blockInstructions: 0,
  staticBlockInstructions: 0, handoffInstructions: 0, interpreterInstructions: 0,
  zeroDoneHandoffs: 0,
  epochInvalidations: 0, evictions: 0, evictedBlocks: 0,
  dataViewRebuilds: 0, moduleBytes: 0, compileAttempts: 0,
  compileWallMs: 0, lowerMs: 0, instantiateMs: 0, compileBookkeepingMs: 0,
  firstInstantiateMs: 0, repeatInstantiateMs: 0, refusedLowerMs: 0,
  compileJsMs: 0, compileCounts: {}, refusalReasons: {}, sampled: {},
} : null;
const compiledRips = new Set(), refusedRips = new Set(); // CODEX-TIMING
function recordSample(name, since) { // CODEX-TIMING
  const ms = performance.now() - since;
  const s = timing.sampled[name] ??= { samples: 0, totalMs: 0, maxMs: 0 };
  ++s.samples; s.totalMs += ms; s.maxMs = Math.max(s.maxMs, ms);
}

async function compile(rip) {
  const lo = Number(rip & 0xFFFFFFFFn) >>> 0, hi = Number(rip >> 32n) >>> 0;
  const lowerStart = timing ? performance.now() : 0; // CODEX-TIMING
  const ptr = e.mf_wasm_block2(lo, hi, 16, outLen, outInsns, outMasks);
  const lowerMs = timing ? performance.now() - lowerStart : 0; // CODEX-TIMING
  if (timing) { ++timing.compileAttempts; timing.lowerMs += lowerMs; } // CODEX-TIMING
  const len = new DataView(e.memory.buffer).getUint32(outLen, true);
  if (ptr === 0) {
    // CODEX-TIMING: include unsuccessful lowering and its reason.
    if (timing) {
      refusedRips.add(rip);
      timing.refusedLowerMs += lowerMs;
      timing.refusalReasons[len] = (timing.refusalReasons[len] ?? 0) + 1;
    }
    return null;
  }
  const view = new DataView(e.memory.buffer);
  const count = view.getUint32(outInsns, true);
  const used = view.getUint32(outMasks, true) & 0x7FFFFFFF;
  const dirty = view.getUint32(outMasks + 4, true) & 0x7FFFFFFF;
  const flagsUsed = (view.getUint32(outMasks, true) & 0x80000000) !== 0;
  const flagsDirty = (view.getUint32(outMasks + 4, true) & 0x80000000) !== 0;
  const bytes = new Uint8Array(e.memory.buffer, ptr, len).slice();
  e.free(ptr);
  if (count === 0) return null; // decode stopped before any instruction
  const repeatedRip = timing && compiledRips.has(rip); // CODEX-TIMING
  const instantiateStart = timing ? performance.now() : 0; // CODEX-TIMING
  // 同步 Module+Instance：省掉逐块异步实例化的唤醒/调度开销（codex perf
  // A/B 实测 40M 省 3-5s；块都很小，同步编译毫无压力）。
  const instance = new WebAssembly.Instance(new WebAssembly.Module(bytes), { e: { memory: e.memory } });
  // CODEX-TIMING: this is wall time including async compilation/wakeup, not main-thread CPU.
  if (timing) {
    const ms = performance.now() - instantiateStart;
    timing.instantiateMs += ms;
    if (repeatedRip) timing.repeatInstantiateMs += ms; else timing.firstInstantiateMs += ms;
    timing.moduleBytes += len;
    compiledRips.add(rip);
    timing.compileCounts[count] = (timing.compileCounts[count] ?? 0) + 1;
  }
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
    if (block) {
      // 零进展抑制：handoff 且 done=0 的块（编译成功但运行时守卫拒绝，
      // 如用户态 cpl!=0）什么也没退休、什么也没提交，重跑纯属浪费。
      // 连续跳过 SKIP_PROBE 次后重探一次，条件恢复（回内核态）就自动
      // 恢复块执行；跳过期间直接走解释器单步，语义与重跑完全一致。
      if (block.skipLeft > 0) {
        --block.skipLeft;
      } else
      // bridge prepare = flush + event budget in one crossing; flags always
      // flush because a handoff commits them. i64 -> signed BigInt: normalize.
      {
      const raw = e.mf_bridge_prepare(block.used | 0x80000000);
      const budget = raw < 0n ? raw + (1n << 64n) : raw;
      if (budget >= BigInt(block.count)) {
        // run the block: finish = commit + store drain + rip + retire
        // in one crossing
      const next = block.run();
      const bridge = bridge0;
      if (view0.buffer !== e.memory.buffer) view0 = new DataView(e.memory.buffer);
      const view = view0;
      const handed = view.getUint32(bridge + 292, true) === 1;
      // handoff wrote a prefix dirty mask (may exceed the static liveness mask)
      const hd = handed ? view.getUint32(bridge + 300, true) : 0;
      if (handed) {
        // handoff: mid-block refusal or a ret terminator hands the rest
        // back; done instructions ran, the runner takes one interp step.
        const done = view.getUint32(bridge + 288, true);
        if (done === 0) block.skipLeft = SKIP_PROBE; // CODEX-SKIP: arm suppression
        // bridge finish = commit + store-log drain + rip + retire in one
        // crossing (block stores bypass note_store, so the decode cache
        // needs the drained entries before any block reuse)
        e.mf_bridge_finish(next, done, block.dirty | hd | 0x80000000);
        const before = e.mf_icount();
        const exit = decodeExit(e.mf_run(1n));
        retired += BigInt(done) + (e.mf_icount() - before);
        if (exit.kind !== EXIT_KIND.RUNNING) return { retired, exit, compiled, refused, execs };
      } else {
        e.mf_bridge_finish(next, block.count, block.dirty | 0x80000000);
        retired += BigInt(block.count);
      }
        ++execs;
        continue;
      }
      } // else branch: not suppressed — full prepare/run/finish path
    }
    // no block, or the event budget would outlive it: short interpreter
    // batch. mf_run itself handles devices/IRQs/exceptions per instruction
    // and caps at its argument, so the only cost of a batch is re-checking
    // for a block opportunity every FALLBACK_BATCH insns instead of every
    // one — measured well worth it (avg block retires < 2 insns anyway).
    // rep-prefixed insns retire one per iteration, so the actual count comes
    // from mf_icount, not the step itself
    refusedHits.set(key, (refusedHits.get(key) ?? 0) + 1);
    const before = e.mf_icount();
    const remain = budget - retired;
    const exit = decodeExit(e.mf_run(remain >= FALLBACK_BATCH ? FALLBACK_BATCH : remain < 1n ? 1n : remain));
    retired += e.mf_icount() - before;
    if (exit.kind !== EXIT_KIND.RUNNING) return { retired, exit, compiled, refused, execs };
  }
  return { retired, exit: { kind: 0, code: 0 }, compiled, refused, execs };
}

// CODEX-TIMING: diagnostic copy of runBlocks. Accounting uses actual `done`
// on handoff, and mf_icount deltas for REP; static block.count is separate.
async function runBlocksTimed(budget) {
  let retired = 0n, compiled = 0, refused = 0, execs = 0;
  let countdown = 1, rng = 0x12345678;
  while (retired < budget) {
    const sampled = --countdown === 0;
    if (sampled) {
      rng ^= rng << 13; rng ^= rng >>> 17; rng ^= rng << 5;
      countdown = 1 + (rng >>> 0) % Math.max(1, 2 * timingInterval - 1);
    }
    let stamp = sampled ? performance.now() : 0;
    const rip = ripNow(), key = rip;
    let block = cache.get(key);
    if (block !== null && block !== undefined && block.epoch !== e.mf_code_epoch()) {
      cache.delete(key); block = undefined; ++timing.epochInvalidations;
    }
    ++timing.loops;
    if (sampled) recordSample("dispatch", stamp);
    if (block === undefined) {
      const compileStart = performance.now();
      block = await compile(rip);
      timing.compileWallMs += performance.now() - compileStart;
      const bookkeepingStart = performance.now();
      if (block) block.epoch = e.mf_code_epoch();
      // Observe the same eviction policy as cachePut, without changing it.
      if (cache.size === CACHE_MAX) {
        let seen = 0;
        for (const old of cache.values()) {
          ++timing.evictions; if (old) ++timing.evictedBlocks;
          if (++seen >= CACHE_MAX + 1 - Math.floor(CACHE_MAX * 2 / 3)) break;
        }
      }
      cachePut(key, block);
      timing.compileBookkeepingMs += performance.now() - bookkeepingStart;
      if (block) ++compiled; else ++refused;
    }
    if (block) {
      stamp = sampled ? performance.now() : 0;
      const raw = e.mf_bridge_prepare(block.used | 0x80000000);
      ++timing.prepares;
      if (sampled) recordSample("bridgePrepare", stamp);
      stamp = sampled ? performance.now() : 0;
      const remaining = raw < 0n ? raw + (1n << 64n) : raw;
      const fits = remaining >= BigInt(block.count);
      if (sampled) recordSample("budgetGate", stamp);
      if (fits) {
        stamp = sampled ? performance.now() : 0;
        const next = block.run();
        if (sampled) recordSample("blockRun", stamp);
        stamp = sampled ? performance.now() : 0;
        if (view0.buffer !== e.memory.buffer) {
          view0 = new DataView(e.memory.buffer); ++timing.dataViewRebuilds;
        }
        const handed = view0.getUint32(bridge0 + 292, true) === 1;
        const hd = handed ? view0.getUint32(bridge0 + 300, true) : 0;
        const done = handed ? view0.getUint32(bridge0 + 288, true) : block.count;
        if (sampled) recordSample("mirrorRead", stamp);
        stamp = sampled ? performance.now() : 0;
        e.mf_bridge_finish(next, done, block.dirty | hd | 0x80000000);
        if (sampled) recordSample("bridgeFinish", stamp);
        ++execs;
        timing.staticBlockInstructions += block.count;
        timing.blockInstructions += done;
        retired += BigInt(done);
        if (handed) {
          ++timing.handoffs; timing.handoffInstructions += done;
          if (done === 0) ++timing.zeroDoneHandoffs;
          stamp = sampled ? performance.now() : 0;
          const before = e.mf_icount();
          if (sampled) recordSample("icountRead", stamp);
          stamp = sampled ? performance.now() : 0;
          const packed = e.mf_run(1n);
          if (sampled) recordSample("handoffRun", stamp);
          stamp = sampled ? performance.now() : 0;
          const exit = decodeExit(packed);
          if (sampled) recordSample("decodeExit", stamp);
          stamp = sampled ? performance.now() : 0;
          const after = e.mf_icount();
          if (sampled) recordSample("icountRead", stamp);
          const delta = after - before;
          retired += delta; timing.interpreterInstructions += Number(delta);
          if (exit.kind !== EXIT_KIND.RUNNING) return { retired, exit, compiled, refused, execs };
        }
        continue;
      }
      ++timing.eventSteps;
    } else ++timing.refusedSteps;
    stamp = sampled ? performance.now() : 0;
    refusedHits.set(key, (refusedHits.get(key) ?? 0) + 1);
    if (sampled) recordSample("refusedHitsMap", stamp);
    stamp = sampled ? performance.now() : 0;
    const before = e.mf_icount();
    if (sampled) recordSample("icountRead", stamp);
    stamp = sampled ? performance.now() : 0;
    const packed = e.mf_run(1n);
    if (sampled) recordSample(block ? "eventRun" : "refusedRun", stamp);
    stamp = sampled ? performance.now() : 0;
    const exit = decodeExit(packed);
    if (sampled) recordSample("decodeExit", stamp);
    stamp = sampled ? performance.now() : 0;
    const after = e.mf_icount();
    if (sampled) recordSample("icountRead", stamp);
    const delta = after - before;
    retired += delta; timing.interpreterInstructions += Number(delta);
    if (exit.kind !== EXIT_KIND.RUNNING) return { retired, exit, compiled, refused, execs };
  }
  return { retired, exit: { kind: 0, code: 0 }, compiled, refused, execs };
}

// CODEX-TIMING: subtract the median empty clock-pair cost from each sample.
// Estimates remain approximate (JIT, GC and timer perturbation); exact wall
// time and exact counts are reported alongside them, without normalization.
let emptyClockMs = 0;
if (timing) {
  const batches = [];
  for (let batch = 0; batch < 9; ++batch) {
    let sum = 0;
    for (let i = 0; i < 20000; ++i) {
      const stamp = performance.now(); sum += performance.now() - stamp;
    }
    batches.push(sum / 20000);
  }
  emptyClockMs = batches.sort((a, b) => a - b)[4];
}

const budgetOf = (e) => {
    const raw = e.mf_event_budget(); // i64 -> signed BigInt: UINT64_MAX reads -1n
    return raw < 0n ? raw + (1n << 64n) : raw;
};

// runner registry bookkeeping: rip -> {block, fn slot} for eviction
const rcache = new Map();
const RCACHE_MAX = 4096;
const rtable = e.__indirect_function_table;
e.mf_runner_reset(rtable.length); // low slots are the engine's own
function rdrop(rip) {
  const old = rcache.get(rip);
  e.mf_runner_remove(Number(rip & 0xFFFFFFFFn) >>> 0, Number(rip >> 32n) >>> 0);
  if (old && old.fn !== undefined) rtable.set(old.fn, null);
  rcache.delete(rip);
}
function revict() {
  let n = rcache.size - Math.floor(RCACHE_MAX * 2 / 3);
  for (const rip of rcache.keys()) {
    if (n-- <= 0) break;
    rdrop(rip);
  }
}
async function runBlocksEngine(budget) {
  let compiled = 0, refused = 0, execs = 0;
  let retired = 0n;
  while (retired < budget) {
    const status = e.mf_runner_run(budget - retired, RUNNER_BURST);
    retired += e.mf_runner_retired();
    execs += e.mf_runner_execs();
    const exit = decodeExit(e.mf_runner_exit());
    if (status === 0 || status === 3) return { retired, exit, compiled, refused, execs };
    if (status === 4) continue; // burst limit: straight back in
    const rip = BigInt(e.mf_runner_miss_hi() >>> 0) << 32n | BigInt(e.mf_runner_miss_lo() >>> 0);
    const lo = Number(rip & 0xFFFFFFFFn) >>> 0, hi = Number(rip >> 32n) >>> 0;
    if (status === 2) rdrop(rip); // stale epoch: compile replaces it
    const block = await compile(rip);
    if (block) {
      ++compiled;
      let slot = e.mf_runner_put(lo, hi, block.count, block.used, block.dirty);
      if (slot < 0) { // registry full: evict a third and retry once
        revict();
        slot = e.mf_runner_put(lo, hi, block.count, block.used, block.dirty);
        if (slot < 0) { ++refused; continue; }
      }
      while (rtable.length <= slot) rtable.grow(1024);
      rtable.set(slot, block.run);
      rcache.set(rip, { block, fn: slot });
    } else {
      ++refused;
      e.mf_runner_refuse(lo, hi);
      rcache.set(rip, { block: null });
      refusedHits.set(rip, (refusedHits.get(rip) ?? 0) + 1);
    }
    if (rcache.size > RCACHE_MAX) revict();
  }
  return { retired, exit: { kind: 0, code: 0 }, compiled, refused, execs };
}

const budget = BigInt(options.instructions);
const start = process.hrtime.bigint();
let result;
if (options.mode === "blocks") {
  result = await (timing ? runBlocksTimed(budget)
    : options.runner === "1" ? runBlocksEngine(budget) : runBlocks(budget));
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
// CODEX-TIMING: diagnostics are serialized after stopping the benchmark clock.
if (timing) {
  const steps = timing.handoffs + timing.refusedSteps + timing.eventSteps;
  const calls = {
    dispatch: timing.loops, bridgePrepare: timing.prepares, budgetGate: timing.prepares,
    blockRun: result.execs, mirrorRead: result.execs, bridgeFinish: result.execs,
    icountRead: 2 * steps, handoffRun: timing.handoffs, refusedRun: timing.refusedSteps,
    eventRun: timing.eventSteps, decodeExit: steps,
    refusedHitsMap: timing.refusedSteps + timing.eventSteps,
  };
  for (const [name, s] of Object.entries(timing.sampled)) {
    s.calls = calls[name];
    // Sparse timing branches run cold; extrapolating their nanosecond costs
    // greatly overstates hot-loop costs. Only expose estimates for full timing.
    if (timingInterval === 1) {
      s.estimatedMs = Math.max(0, s.totalMs / s.samples - emptyClockMs) * s.calls;
      s.estimatedNsPerCall = s.estimatedMs * 1e6 / s.calls;
    }
  }
  timing.compileJsMs = timing.compileWallMs - timing.lowerMs - timing.instantiateMs;
  const report = {
    mode: options.mode, instructions: options.instructions, seconds,
    timingCaveat: timingInterval === 1
      ? "Phase estimates subtract empty clock-pair cost; JIT/GC/timer perturbation remains. Use an untimed run for throughput."
      : "Use exact counters and compile times only. Sparse short-call samples are cold-path observations, not absolute phase estimates.",
    timerPairNs: emptyClockMs * 1e6, ...timing,
    compiled: result.compiled, refused: result.refused, blockExecs: result.execs,
    interpreterSteps: steps, uniqueCompiledRips: compiledRips.size,
    uniqueRefusedRips: refusedRips.size, finalCacheSize: cache.size,
    accountedInstructions: timing.blockInstructions + timing.interpreterInstructions,
    engineIcount: e.mf_icount().toString(), retired: result.retired.toString(),
  };
  const json = JSON.stringify(report, null, 2);
  if (options["timing-out"]) writeFileSync(options["timing-out"], json + "\n");
  else console.error("CODEX-TIMING " + json);
}
console.log(JSON.stringify({
  mode: options.mode, seconds: seconds.toFixed(3),
  mips: (Number(result.retired) / seconds / 1e6).toFixed(2),
  icount: result.retired.toString(),
  exit: result.exit,
  serial: e.mf_serial_tx().toString(),
  compiled: result.compiled, refused: result.refused, blockExecs: result.execs,
}));

const top = [...refusedHits.entries()].sort((x, y) => y[1] - x[1]).slice(0, 12);
console.error("top refused (weight, rip): " + top.map(([r, n]) => `${n}@0x${(r < 0n ? r + (1n << 64n) : r).toString(16)}`).join(" "));
