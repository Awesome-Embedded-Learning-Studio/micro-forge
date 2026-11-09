// Shared engine loader for the Node harnesses (batch-5 review F4: the
// instantiation stubs, the reactor _initialize contract, and the version
// decoding lived in three copies that had already drifted).
//
// The browser harness (selftest.html) keeps an inline copy because it
// runs without a bundler; its header notes the sync obligation.

import { readFileSync } from "node:fs";

/// Exit-status kinds, pinned by static_asserts in src/wasm/exports.cpp.
export const EXIT_KIND = Object.freeze({
  RUNNING: 0, HALT: 1, EXIT: 2, DEBUG_EXIT: 3,
  DECODE_ERROR: 4, UNSUPPORTED: 5, FAULT: 6,
});

/// SnapshotError ordinals (kernel/snapshot.hpp) as mf_restore returns
/// them (+1; 0 is success).
export const RESTORE_ERROR = Object.freeze({
  OK: 0, BAD_MAGIC: 1, FORMAT_MISMATCH: 2, ENGINE_MISMATCH: 3, TRUNCATED: 4,
});

/// Loads an engine artifact and runs the reactor contract.
/// @returns the exports object (memory on .memory)
export async function loadEngine(path) {
  const wasm = readFileSync(path);
  const { instance } = await WebAssembly.instantiate(wasm, {
    env: { emscripten_notify_memory_growth: () => {} },
    wasi_snapshot_preview1: {
      fd_close: () => 0,
      fd_write: () => 0,
      fd_seek: () => 0,
      environ_sizes_get: () => 0,
      environ_get: () => 0,
    },
    // Minimal WASI stubs: the reactor runtime declares them; the engine
    // never performs console IO through them ("success", no side effects).
    wasi_snapshot_preview1: {
      fd_close: () => 0,
      fd_seek: () => 0,
      fd_write: () => 0,
      environ_sizes_get: () => 0,
      environ_get: () => 0,
    },
  });
  const e = instance.exports;
  // Reactor contract: global constructors run on _initialize, which the
  // embedder invokes once after instantiation (before any export call).
  if (typeof e._initialize === "function") e._initialize();
  return e;
}

/// Decodes the packed exit from mf_run.
export function decodeExit(packed) {
  return { kind: Number(packed & 0xffn), code: Number(packed >> 8n) };
}

/// The artifact's memory layout, derived entirely from its own exports.
export function layout(e) {
  return {
    ramSize: e.mf_guest_ram_size(),
    lfbBase: e.mf_lfb_base(),
    lfbSize: e.mf_lfb_size(),
    windowEnd: e.mf_guest_ram_size() + e.mf_lfb_size(),
  };
}

/// Writes bytes into guest memory at a linear offset.
export function pokeGuest(e, offset, bytes) {
  new Uint8Array(e.memory.buffer, offset, bytes.length).set(bytes);
}
