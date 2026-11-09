// wasm-side ips reading (batch-5 notes data; not the host baseline table).
// Loads the bench ALU guest into the artifact and times mf_run.
//
// Usage: node wasm/harness/ips.mjs [engine.wasm] [guest-int-alu.bin] [trips]

import { readFileSync } from "node:fs";
import { loadEngine, decodeExit, pokeGuest, EXIT_KIND } from "./engine.mjs";

const wasmPath = process.argv[2] ?? "build-wasm/engine.wasm";
const guestPath = process.argv[3] ?? "build-rel/bench/guest-int-alu.bin";
const trips = BigInt(process.argv[4] ?? "8000000");

const CODE_BASE = 0x100000;
const STACK_TOP = 0x90000;
const LOOP_CELL = 0x80000;
const REPS = 11;

const e = await loadEngine(wasmPath);

function runOnce() {
  const guest = readFileSync(guestPath);
  pokeGuest(e, CODE_BASE, guest);
  const view = new DataView(e.memory.buffer);
  view.setBigUint64(LOOP_CELL, trips, true);
  e.mf_reset(CODE_BASE, STACK_TOP);
  const t0 = process.hrtime.bigint();
  const { kind } = decodeExit(e.mf_run((BigInt(1) << 62n) - 1n));
  const t1 = process.hrtime.bigint();
  if (kind !== EXIT_KIND.EXIT) throw new Error(`guest did not exit cleanly: kind ${kind}`);
  return { insns: e.mf_icount(), secs: Number(t1 - t0) / 1e9 };
}

runOnce(); // warmup
const rates = [];
for (let i = 0; i < REPS; i++) {
  const { insns, secs } = runOnce();
  rates.push(Number(insns) / secs);
}
rates.sort((a, b) => a - b);
const median = rates[(rates.length / 2) | 0];
console.log(`wasm int-alu median ${Math.round(median).toLocaleString()} ips (${REPS} reps)`);
