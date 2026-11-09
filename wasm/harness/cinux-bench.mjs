// One Cinux sample; only mf_run is timed. For repeated, isolated A/B
// comparisons use cinux-bench.py, which starts a fresh Node per sample.
// node wasm/harness/cinux-bench.mjs engine.wasm \
//   --kernel /path/kernel.bin --disk /path/ext2.img

import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { loadEngine, decodeExit, EXIT_KIND } from "./engine.mjs";

const options = {
  kernel: process.env.MF_KERNEL_BIN,
  disk: process.env.MF_DISK_IMG,
  instructions: "400000000",
  chunk: "2000000",
};
const artifacts = [];
for (let i = 2; i < process.argv.length; ++i) {
  const arg = process.argv[i];
  if (arg.startsWith("--")) {
    const key = arg.slice(2);
    if (!Object.hasOwn(options, key) || !process.argv[i + 1]) {
      throw new Error(`unknown or incomplete option: ${arg}`);
    }
    options[key] = process.argv[++i];
  } else {
    artifacts.push(arg);
  }
}
if (artifacts.length !== 1 || !options.kernel || !options.disk) {
  throw new Error("usage: cinux-bench.mjs engine.wasm --kernel FILE --disk FILE " +
                  "[--instructions 400000000] [--chunk 2000000]");
}
const budget = BigInt(options.instructions);
const chunk = BigInt(options.chunk);
if (budget <= 0n || chunk <= 0n) {
  throw new Error("instructions and chunk must be positive integers");
}
const hash = (bytes) => createHash("sha256").update(bytes).digest("hex");

async function runSample(artifact) {
  const e = await loadEngine(artifact);
  const kernel = readFileSync(options.kernel);
  const disk = readFileSync(options.disk);
  const k = e.malloc(kernel.length);
  const d = e.malloc(disk.length);
  if (!k || !d) throw new Error("image allocation failed");
  new Uint8Array(e.memory.buffer, k, kernel.length).set(kernel);
  new Uint8Array(e.memory.buffer, d, disk.length).set(disk);
  if (e.mf_boot_cinux(k, kernel.length, d, disk.length) !== 1) {
    throw new Error("boot refused the images");
  }
  e.free(k);
  e.free(d);

  let exit;
  const start = process.hrtime.bigint();
  while (e.mf_icount() < budget) {
    const remaining = budget - e.mf_icount();
    exit = decodeExit(e.mf_run(remaining < chunk ? remaining : chunk));
    if (exit.kind !== EXIT_KIND.RUNNING) {
      throw new Error(`guest stopped at ${e.mf_icount()}: ${JSON.stringify(exit)}`);
    }
  }
  const seconds = Number(process.hrtime.bigint() - start) / 1e9;

  // All checks are outside the timed interval. Compare RAM too: a faster
  // run with matching console text alone could still have corrupted data.
  const ptr = e.malloc(65536);
  if (!ptr) throw new Error("console allocation failed");
  const n = Number(e.mf_read_console(ptr, 65536));
  if (n === 65536) throw new Error("console buffer may be truncated");
  const consoleBytes = new Uint8Array(e.memory.buffer, ptr, n);
  const state = {
    icount: e.mf_icount().toString(),
    exit,
    rip: ((BigInt(e.mf_rip_high() >>> 0) << 32n) |
          BigInt(e.mf_rip_low() >>> 0)).toString(16),
    serial: e.mf_serial_tx().toString(),
    consoleHash: hash(consoleBytes),
    ramHash: hash(new Uint8Array(e.memory.buffer, 0,
                                e.mf_guest_ram_size() + e.mf_lfb_size())),
  };
  e.free(ptr);
  return { artifact, seconds, mips: Number(state.icount) / seconds / 1e6, state };
}

console.log(JSON.stringify(await runSample(artifacts[0])));
