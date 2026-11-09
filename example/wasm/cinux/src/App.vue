<script setup lang="ts">
import { computed, onBeforeUnmount, ref, shallowRef } from 'vue';
import {
  bootCinux,
  ExitKind,
  icount,
  injectSerial,
  loadEngine,
  readConsole,
  run,
  type EngineExports,
} from './engine';

const BOOT_BUDGET = 400_000_000;
// Full-speed frame: the O3 wasm engine retires ~30 Mips, and one frame runs
// 500K instructions (~16 ms) — enough to saturate 60 Hz without starving
// the UI thread for longer than a frame.
const TURN_BUDGET = 500_000n;
const STALL_TURNS = 40;

const screen = ref<HTMLDivElement | null>(null);
const status = ref('选择镜像后引导');
const booting = ref(false);
const running = ref(false);
const command = ref('');
const kernelFile = ref<File | null>(null);
const diskFile = ref<File | null>(null);

const engine = shallowRef<EngineExports | null>(null);
let printed = 0;
let busy = false;
let turnHandle = 0;
let stallTurns = 0;

const engineReady = computed(() => Boolean(kernelFile.value && diskFile.value));

const append = (text: string): void => {
  if (text === '') return;
  const el = screen.value;
  if (!el) return;
  const atBottom = el.scrollTop + el.clientHeight >= el.scrollHeight - 4;
  el.append(text);
  if (atBottom) el.scrollTop = el.scrollHeight;
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
    append(drained.text);
    if (exit.kind !== ExitKind.Running) {
      running.value = false;
      status.value = `guest stopped (kind=${exit.kind}, code=${exit.code})`;
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
  } finally {
    busy = false;
  }
  turnHandle = requestAnimationFrame(pump);
};

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
    const kernel = await readFileBytes(kernelFile.value);
    const disk = await readFileBytes(diskFile.value);
    status.value = `引导中（kernel ${kernel.length}B · disk ${(disk.length / 1048576).toFixed(0)}MiB）…`;
    bootCinux(engine.value, { kernel, disk });
    running.value = true;
    status.value = `引导中 · ${(icount(engine.value) / 1e6).toFixed(1)}M 条指令`;
    pump();
  } catch (error) {
    status.value = `失败：${error instanceof Error ? error.message : String(error)}`;
    running.value = false;
  } finally {
    booting.value = false;
  }
};

const send = (): void => {
  const active = engine.value;
  if (!active || !running.value) return;
  injectSerial(active, command.value, true);
  command.value = '';
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
    <span id="status">{{ status }}</span>
  </header>
  <div ref="screen" id="screen"></div>
  <div id="prompt">
    <span>&gt;</span>
    <input
      v-model="command"
      :disabled="!running"
      type="text"
      placeholder="引导完成后在这里输入命令，回车发送"
      @keydown.enter.prevent="send"
    />
  </div>
</template>
