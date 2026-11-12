<script setup lang="ts">
import { computed, onBeforeUnmount, ref, shallowRef } from 'vue';
import TermConsole from './TermConsole.vue';
import {
  bootWithPointers,
  compileBlock,
  ExitKind,
  icount,
  injectSerial,
  loadEngine,
  readConsole,
  run,
  uploadImage,
  type EngineExports,
} from './engine';

const BOOT_BUDGET = 400_000_000;
// Full-speed frame: the O3 wasm engine retires ~30 Mips, and one frame runs
// 500K instructions (~16 ms) — enough to saturate 60 Hz without starving
// the UI thread for longer than a frame.
const TURN_BUDGET = 500_000n;
const STALL_TURNS = 40;

const term = ref<InstanceType<typeof TermConsole> | null>(null);
const status = ref('选择镜像后引导');
const booting = ref(false);
const running = ref(false);
const kernelFile = ref<File | null>(null);
const diskFile = ref<File | null>(null);
// Plan-B prototype: compile the block at the current RIP into a wasm
// module and run it in its own linear memory. It proves the C++ -> V8
// pipeline end to end; it is NOT guest acceleration (the block's memory
// is separate from the machine's registers until a bridge exists).
const jitInfo = ref('');
const jitRipLo = ref(0x300000);
const jitRipHi = ref(0);

const tryJitBlock = async (): Promise<void> => {
  const e = engine.value;
  if (!e) return;
  const program = new Uint8Array([
    0x48, 0xc7, 0xc0, 0x05, 0x00, 0x00, 0x00, // mov rax,5
    0x48, 0x83, 0xc0, 0x07,                   // add rax,7
    0xc3,                                     // ret
  ]);
  const src = e.malloc(program.length);
  new Uint8Array(e.memory.buffer, src, program.length).set(program);
  e.mf_write_guest(jitRipLo.value, src, program.length);
  e.free(src);
  const block = await compileBlock(e, jitRipLo.value, jitRipHi.value, 8);
  if (!block) {
    jitInfo.value = '该地址的指令不在模板集内，翻译不了（原型只覆盖纯寄存器块）';
    return;
  }
  block.run();
  const rax = new DataView(block.memory.buffer).getBigInt64(0, true);
  jitInfo.value = `方案乙原型: guest 块 → wasm 模块 → V8 编译执行, rax = ${rax} (期望 12)`;
};

const engine = shallowRef<EngineExports | null>(null);
let printed = 0;
let busy = false;
let turnHandle = 0;
let stallTurns = 0;

const engineReady = computed(() => Boolean(kernelFile.value && diskFile.value));

/** Terminal input lands here: bytes go straight to the serial line. */
const sendByte = (byte: number): void => {
  const active = engine.value;
  if (active && running.value) injectSerial(active, String.fromCharCode(byte));
};

const sendText = (text: string): void => {
  const active = engine.value;
  if (active && running.value) injectSerial(active, text.replace(/\r?\n/g, '\r'));
};

const readFileBytes = (file: File): Promise<Uint8Array> =>
  new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(new Uint8Array(reader.result as ArrayBuffer));
    reader.onerror = () => reject(reader.error ?? new Error('读取失败'));
    reader.readAsArrayBuffer(file);
  });

const pickKernel = (event: Event): void => {
  kernelFile.value = (event.target as HTMLInputElement).files?.[0] ?? null;
};

const pickDisk = (event: Event): void => {
  diskFile.value = (event.target as HTMLInputElement).files?.[0] ?? null;
};

/** One animation-frame turn: run, drain the console, reschedule. */
const pump = (): void => {
  const active = engine.value;
  if (!active || !running.value) return;
  if (busy) {
    turnHandle = requestAnimationFrame(pump);
    return;
  }
  busy = true;
  try {
    const exit = run(active, TURN_BUDGET);
    const drained = readConsole(active, printed);
    printed = drained.printed;
    term.value?.push(drained.text);
    if (exit.kind !== ExitKind.Running) {
      running.value = false;
      const rip = (exit.code >>> 0).toString(16);
      if (exit.kind === ExitKind.DecodeError) {
        // The engine artifact carries its own decoder; an older
        // public/engine.wasm cannot know instructions the kernel now uses.
        status.value =
          `引擎不认识 0x${rip} 处的指令：engine.wasm 可能比内核旧，` +
          '请重新执行 scripts/build-wasm.sh 并复制产物后刷新页面';
      } else {
        status.value = `guest stopped (kind=${exit.kind}, code=0x${rip})`;
      }
      return;
    }
    if (drained.text === '') {
      stallTurns += 1;
    } else {
      stallTurns = 0;
    }
    if (icount(active) > BOOT_BUDGET && printed === 0 && stallTurns > STALL_TURNS) {
      running.value = false;
      status.value = '内核没有产生串口输出（wasm 下已知问题，见 README）';
      return;
    }
    status.value = `运行中 · ${(icount(active) / 1e6).toFixed(1)}M 条指令`;
  } catch (error) {
    // A wasm trap would otherwise kill the rAF chain and freeze the page.
    running.value = false;
    busy = false;
    status.value = `引擎中断：${error instanceof Error ? error.message : String(error)}`;
    return;
  } finally {
    busy = false;
  }
  turnHandle = requestAnimationFrame(pump);
};

/** One repaint worth of slack for the status line above. */
const yieldToPaint = (): Promise<void> =>
  new Promise<void>((resolve) => {
    requestAnimationFrame(() => resolve());
  });

const boot = async (): Promise<void> => {
  if (!kernelFile.value || !diskFile.value || booting.value) return;
  booting.value = true;
  printed = 0;
  stallTurns = 0;
  status.value = '加载引擎…';
  try {
    engine.value ??= await loadEngine({
      onProgress: (loaded, total) => {
        status.value = `下载引擎 ${Math.round((100 * loaded) / total)}%`;
      },
    });
    status.value = '读取内核镜像…';
    const kernel = await readFileBytes(kernelFile.value);
    status.value = `读取磁盘映像（${(diskFile.value.size / 1048576).toFixed(0)} MiB）…`;
    const disk = await readFileBytes(diskFile.value);
    // Yield between the two uploads: each moves hundreds of MiB and would
    // otherwise freeze the tab with the status line stuck on the old text.
    status.value = `上传内核（${(kernel.length / 1024).toFixed(0)} KiB）…`;
    await yieldToPaint();
    const kernelPtr = uploadImage(engine.value, kernel);
    status.value = `上传磁盘映像（${(disk.length / 1048576).toFixed(0)} MiB）…`;
    await yieldToPaint();
    const diskPtr = uploadImage(engine.value, disk);
    status.value = '加载镜像并设置入口…';
    await yieldToPaint();
    bootWithPointers(engine.value, kernelPtr, kernel.length, diskPtr, disk.length);
    running.value = true;
    status.value = `引导中 · ${(icount(engine.value) / 1e6).toFixed(1)}M 条指令`;
    term.value?.focus(); // terminal takes keystrokes from here on
    pump();
  } catch (error) {
    status.value = `失败：${error instanceof Error ? error.message : String(error)}`;
    running.value = false;
  } finally {
    booting.value = false;
  }
};

onBeforeUnmount(() => {
  cancelAnimationFrame(turnHandle);
  running.value = false;
});
</script>

<template>
  <header>
    <h1>Cinux · wasm 试玩</h1>
    <label class="file">
      选择 kernel.bin
      <input type="file" @change="pickKernel" />
    </label>
    <label class="file">
      选择 ext2.img
      <input type="file" @change="pickDisk" />
    </label>
    <button :disabled="!engineReady || running || booting" @click="boot">引导</button>
    <button
      :disabled="!engineReady"
      @click="tryJitBlock"
      title="方案乙原型：把一段 guest 指令（mov rax,5; add rax,7）翻译成 WebAssembly 模块，交给 V8 编译后执行，验证 C++ 生成器 → 浏览器执行的整条链路。它不是模拟器的加速开关：块的内存和机器寄存器还没打通。"
    >
      试试 wasm 块
    </button>
    <span id="status">{{ status }}</span>
    <span v-if="jitInfo" id="jit-info">{{ jitInfo }}</span>
  </header>
  <TermConsole ref="term" :on-byte="sendByte" :on-text="sendText" />
</template>
