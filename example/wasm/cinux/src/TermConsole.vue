<script setup lang="ts">
import { ref } from 'vue';

/**
 * Terminal console with lazy history.
 *
 * All output lives in a JS string (cheap and never discarded); the DOM
 * mounts only the newest WINDOW bytes. Scrolling to the top prepends one
 * more page of history, so a long session cannot grow the DOM unbounded.
 * While the user is scrolled up, fresh output goes to a backlog badge and
 * is flushed when they come back down.
 */
const props = defineProps<{
  /** Called for each keystroke byte (already mapped, e.g. ^C = 0x03). */
  onByte: (byte: number) => void;
  /** Called for pasted text. */
  onText: (text: string) => void;
}>();

const PAGE = 32768; // history bytes prepended per top-edge load
const TOP_EDGE = 64; // scrollTop within this many px triggers a load
const WINDOW = 131072; // bytes kept mounted in the DOM

const body = ref<HTMLDivElement | null>(null);
const older = ref(0); // history bytes not yet mounted
const backlog = ref(0); // fresh bytes held back while scrolled up

let history = '';
let domStart = 0; // history index of the first mounted byte
let pinned = true;

/** Remounts the tail window; used when domStart moves a long way. */
const mountTail = (): void => {
  const el = body.value;
  if (!el) return;
  el.textContent = history.slice(domStart);
  if (pinned) el.scrollTop = el.scrollHeight;
};

const push = (text: string): void => {
  if (text === '') return;
  history += text;
  const el = body.value;
  if (!el) return;
  if (!pinned) {
    backlog.value += text.length;
    return;
  }
  el.append(text);
  if (history.length - domStart > WINDOW + PAGE) {
    // Unmount the oldest pages; the history string keeps everything.
    domStart = history.length - WINDOW;
    mountTail();
    return;
  }
  el.scrollTop = el.scrollHeight;
};

const flushBacklog = (): void => {
  const el = body.value;
  if (!el || backlog.value === 0) return;
  el.append(history.slice(history.length - backlog.value));
  backlog.value = 0;
  el.scrollTop = el.scrollHeight;
};

const jumpToLatest = (): void => {
  pinned = true;
  backlog.value = 0;
  if (history.length - domStart > WINDOW) {
    domStart = history.length - WINDOW;
    mountTail();
    return;
  }
  flushBacklog();
};

const onScroll = (): void => {
  const el = body.value;
  if (!el) return;
  const wasPinned = pinned;
  pinned = el.scrollTop + el.clientHeight >= el.scrollHeight - 4;
  if (pinned && !wasPinned) {
    flushBacklog();
    return;
  }
  if (el.scrollTop <= TOP_EDGE && domStart > 0) {
    const from = Math.max(0, domStart - PAGE);
    const page = history.slice(from, domStart);
    domStart = from;
    older.value = domStart;
    const before = el.scrollHeight;
    el.insertBefore(document.createTextNode(page), el.firstChild);
    el.scrollTop += el.scrollHeight - before; // keep the viewport steady
  }
};

/** Maps a browser key event to the byte the guest tty should see. */
const keyToByte = (event: KeyboardEvent): number | null => {
  if (event.ctrlKey && !event.altKey && event.key.length === 1) {
    const code = event.key.toUpperCase().charCodeAt(0);
    if (code >= 0x40 && code <= 0x5f) return code & 0x1f; // ^C, ^U, ^D ...
  }
  if (event.metaKey || event.altKey) return null;
  switch (event.key) {
    case 'Enter':
      return 0x0d; // the shell reads CR
    case 'Backspace':
      return 0x7f;
    case 'Tab':
      return 0x09;
    case 'Escape':
      return 0x1b;
    default:
      break;
  }
  if (event.key.length === 1) return event.key.charCodeAt(0);
  return null; // arrows and friends need ANSI sequences; ignored for now
};

const onKeydown = (event: KeyboardEvent): void => {
  if (event.key === 'F5' || (event.ctrlKey && event.key === 'r')) return;
  const byte = keyToByte(event);
  if (byte === null) return;
  event.preventDefault();
  props.onByte(byte);
};

const onPaste = (event: ClipboardEvent): void => {
  const text = event.clipboardData?.getData('text');
  if (!text) return;
  event.preventDefault();
  props.onText(text);
};

const focus = (): void => {
  body.value?.focus();
};

defineExpose({ push, focus });
</script>

<template>
  <div class="term-wrap">
    <span v-if="older > 0" class="term-older">↑ 上滚加载更早输出（还有 {{ older }} 字节）</span>
    <div
      ref="body"
      class="term"
      tabindex="0"
      title="点击终端后直接打字，与真实串口一致"
      @scroll="onScroll"
      @keydown="onKeydown"
      @paste="onPaste"
    ></div>
    <button v-if="backlog > 0" class="term-jump" @click="jumpToLatest">
      ↓ {{ backlog }} 字节新输出
    </button>
  </div>
</template>

<style scoped>
.term-wrap {
  position: relative;
  flex: 1;
  min-height: 0;
  display: flex;
  margin: 12px 18px;
}

.term {
  flex: 1;
  min-height: 0;
  padding: 12px;
  overflow-y: auto;
  background: var(--panel);
  border: 1px solid var(--line);
  border-radius: 8px;
  white-space: pre-wrap;
  word-break: break-all;
  line-height: 1.35;
  font-size: 13px;
  cursor: text;
  outline: none;
}

.term:focus {
  border-color: var(--accent);
  box-shadow: 0 0 0 2px rgba(143, 211, 255, 0.25);
}

.term-older {
  position: absolute;
  top: 8px;
  left: 50%;
  transform: translateX(-50%);
  z-index: 2;
  font-size: 11px;
  color: var(--dim);
  background: var(--bg);
  border: 1px solid var(--line);
  border-radius: 10px;
  padding: 2px 10px;
  pointer-events: none;
}

.term-jump {
  position: absolute;
  bottom: 14px;
  right: 18px;
  z-index: 2;
  font-size: 12px;
  padding: 5px 12px;
  border: 1px solid #2f6f8f;
  border-radius: 14px;
  background: #12303f;
  color: #bfe7ff;
  cursor: pointer;
}
</style>
