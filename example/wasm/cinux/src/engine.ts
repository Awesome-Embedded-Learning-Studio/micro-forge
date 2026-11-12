/** The wasm engine ABI (src/wasm/exports.cpp), typed for the page. */
export interface EngineExports {
  memory: WebAssembly.Memory;
  _initialize: () => void;
  mf_abi_version: () => number;
  mf_engine_version: () => number;
  mf_guest_ram_size: () => number;
  mf_boot_cinux: (kernelPtr: number, kernelLen: number, diskPtr: number, diskLen: number) => number;
  mf_run: (maxInstructions: bigint) => bigint;
  mf_icount: () => bigint;
  mf_read_console: (dst: number, cap: number) => bigint;
  mf_inject_serial: (byte: number) => void;
  mf_instance_create: (ramSize: number, window: number) => number;
  mf_write_guest: (paddr: number, src: number, len: number) => number;
  mf_wasm_block: (
    ripLo: number,
    ripHi: number,
    maxInsns: number,
    outLen: number,
  ) => number;
  mf_instance_destroy: (handle: number) => void;
  mf_instance_use: (handle: number) => number;
  malloc: (size: number) => number;
  free: (ptr: number) => void;
}

export interface EngineLoadOptions {
  /** URL of the standalone wasm artifact (default ./engine.wasm). */
  url?: string;
  /** Pre-fetched module bytes (node tests bypass fetch this way). */
  bytes?: ArrayBuffer;
  /** Progress callback while the module downloads. */
  onProgress?: (loaded: number, total: number) => void;
}

const REACTOR_IMPORTS: WebAssembly.Imports = {
  env: { emscripten_notify_memory_growth: () => {} },
  wasi_snapshot_preview1: {
        fd_close: () => 0,
        fd_write: () => 0,
        fd_seek: () => 0,
        environ_sizes_get: () => 0,
        environ_get: () => 0,
      },
};

/** Loads the standalone engine and runs the reactor constructors. */
export async function loadEngine(options: EngineLoadOptions = {}): Promise<EngineExports> {
  if (options.bytes) {
    const { instance } = await WebAssembly.instantiate(options.bytes, REACTOR_IMPORTS);
    const exports = instance.exports as unknown as EngineExports;
    exports._initialize();
    return exports;
  }
  // Vite serves public/engine.wasm at the base root (dev and dist alike).
  const url = options.url ?? new URL(`${import.meta.env.BASE_URL}engine.wasm`, location.href).toString();
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`engine.wasm: HTTP ${response.status}`);
  }
  let bytes: ArrayBuffer;
  const total = Number(response.headers.get('content-length') ?? '0');
  if (options.onProgress && response.body && total > 0) {
    const reader = response.body.getReader();
    const chunks: Uint8Array[] = [];
    let loaded = 0;
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      chunks.push(value);
      loaded += value.byteLength;
      options.onProgress(loaded, total);
    }
    const merged = new Uint8Array(loaded);
    let offset = 0;
    for (const chunk of chunks) {
      merged.set(chunk, offset);
      offset += chunk.byteLength;
    }
    bytes = merged.buffer;
  } else {
    bytes = await response.arrayBuffer();
  }
  const { instance } = await WebAssembly.instantiate(bytes, REACTOR_IMPORTS);
  const exports = instance.exports as unknown as EngineExports;
  // Reactor contract: static constructors (the Machine singleton) run here.
  exports._initialize();
  return exports;
}

/** Cinux 镜像文件的双人组。 */
export interface CinuxImages {
  kernel: Uint8Array;
  disk: Uint8Array;
}

/** Copies @p bytes into the wasm heap and returns the pointer. */
function upload(engine: EngineExports, bytes: Uint8Array): number {
  const ptr = engine.malloc(bytes.length);
  if (ptr === 0) {
    throw new Error(`malloc(${bytes.length}) failed`);
  }
  new Uint8Array(engine.memory.buffer, ptr, bytes.length).set(bytes);
  return ptr;
}

/**
 * Boots the kernel with its ext2 volume. Throws when the boot shim refuses
 * the image (bad header, oversized kernel).
 */
/**
 * Uploads one image into the engine heap and returns the wasm pointer. The
 * caller owns it and must engine.free() it (or hand it to bootWithPointers).
 */
export function uploadImage(engine: EngineExports, bytes: Uint8Array): number {
  return upload(engine, bytes);
}

/**
 * Boots with images already in the engine heap. Kept separate from
 * bootCinux so the page can yield between the two uploads: moving ~600 MiB
 * of disk image on one turn freezes the tab.
 */
export function bootWithPointers(
  engine: EngineExports,
  kernelPtr: number,
  kernelLen: number,
  diskPtr: number,
  diskLen: number,
): void {
  try {
    const ok = engine.mf_boot_cinux(kernelPtr, kernelLen, diskPtr, diskLen);
    if (ok !== 1) {
      throw new Error('boot_cinux refused the image');
    }
  } finally {
    engine.free(kernelPtr);
    engine.free(diskPtr);
  }
}

export function bootCinux(engine: EngineExports, images: CinuxImages): void {
  const kernelPtr = upload(engine, images.kernel);
  const diskPtr = upload(engine, images.disk);
  try {
    const ok = engine.mf_boot_cinux(kernelPtr, images.kernel.length, diskPtr, images.disk.length);
    if (ok !== 1) {
      throw new Error('boot_cinux refused the image');
    }
  } finally {
    engine.free(kernelPtr);
    engine.free(diskPtr);
  }
}

/** Exit kinds of the Cpu (src/arch/x86_64/cpu.hpp). */
export enum ExitKind {
  Running = 0,
  Exit = 2,
  DebugExit = 3,
  DecodeError = 4,
  Unsupported = 5,
  Fault = 6,
}

export interface RunResult {
  kind: ExitKind;
  code: number;
}

/** Runs @p budget instructions and unpacks the ABI status word. */
export function run(engine: EngineExports, budget: bigint): RunResult {
  const packed = engine.mf_run(budget);
  return {
    kind: Number(packed & 0xffn) as ExitKind,
    code: Number(packed >> 8n),
  };
}

/**
 * Reads the whole serial console (the ABI copies from the start) and
 * returns the bytes past @p printed, advancing the mark.
 */
export function readConsole(
  engine: EngineExports,
  printed: number,
): { text: string; printed: number } {
  const cap = 1 << 16;
  const dst = engine.malloc(cap);
  if (dst === 0) {
    throw new Error(`readConsole: malloc(${cap}) failed`);
  }
  try {
    const n = Number(engine.mf_read_console(dst, cap));
    if (n <= printed) {
      return { text: '', printed };
    }
    const view = new Uint8Array(engine.memory.buffer, dst, n).subarray(printed);
    return { text: new TextDecoder('utf-8', { fatal: false }).decode(view), printed: n };
  } finally {
    engine.free(dst);
  }
}

/** Queues one byte on the COM1 receiver (LF becomes CR for the console). */
export function injectSerial(engine: EngineExports, text: string, lineEnd = false): void {
  for (const char of text) {
    engine.mf_inject_serial(char.codePointAt(0) ?? 0);
  }
  if (lineEnd) {
    engine.mf_inject_serial(0x0d);
  }
}

/**
 * Creates an independent machine instance. The default instance (handle 0)
 * is created lazily and never destroyed; every other handle owns a full
 * machine and must be destroyed explicitly.
 */
export function createInstance(
  engine: EngineExports,
  ramSize = 0,
  window = 0,
): number {
  return engine.mf_instance_create(ramSize, window);
}

/** Selects the instance the legacy exports act on (0 = default). */
export function useInstance(engine: EngineExports, handle: number): boolean {
  return engine.mf_instance_use(handle) === 1;
}

/** Destroys an instance (the default instance ignores this). */
export function destroyInstance(engine: EngineExports, handle: number): void {
  engine.mf_instance_destroy(handle);
}

/**
 * One compiled guest block: the wasm module plus its exported memory.
 *
 * The module owns a linear memory holding guest register slots
 * (slot = register * 8). Callers copy GPRs in and out around block().
 */
export interface WasmBlock {
  run: () => void;
  memory: WebAssembly.Memory;
}

/**
 * Compiles the guest block at `rip` into a wasm module and instantiates it.
 *
 * Returns null when the block cannot be lowered (caller falls back to the
 * interpreter). The C++ side returns a malloc'd module which is freed here.
 */
export async function compileBlock(
  engine: EngineExports,
  ripLo: number,
  ripHi: number,
  maxInsns = 16,
): Promise<WasmBlock | null> {
  const outLenPtr = engine.malloc(4);
  if (outLenPtr === 0) {
    return null;
  }
  try {
    const modulePtr = engine.mf_wasm_block(ripLo >>> 0, ripHi >>> 0, maxInsns, outLenPtr);
    const len = new DataView(engine.memory.buffer).getUint32(outLenPtr, true);
    if (modulePtr === 0 || len === 0) {
      return null;
    }
    const bytes = new Uint8Array(engine.memory.buffer, modulePtr, len).slice();
    engine.free(modulePtr);
    const module = await WebAssembly.compile(bytes);
    const instance = await WebAssembly.instantiate(module, {});
    const exports = instance.exports as { block: () => void; memory: WebAssembly.Memory };
    return { run: () => exports.block(), memory: exports.memory };
  } finally {
    engine.free(outLenPtr);
  }
}

/** Retired instruction count as a number (safe below 2^53). */
export function icount(engine: EngineExports): number {
  return Number(engine.mf_icount());
}
