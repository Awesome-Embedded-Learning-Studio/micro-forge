import { describe, expect, it } from 'vitest';
import { readFile } from 'node:fs/promises';
import { bootWithPointers, injectSerial, loadEngine, readConsole, run, uploadImage } from '../src/engine';

const FINAL = '/home/charliechen/micro-forge/.claude/final';

describe('final bundle', () => {
  it('boots .claude/final images into a working shell', async () => {
    const wasm = await readFile(new URL('../public/engine.wasm', import.meta.url));
    const engine = await loadEngine({ bytes: wasm.buffer.slice(0) as ArrayBuffer });
    const kernel = await readFile(`${FINAL}/kernel.bin`).then((b) => new Uint8Array(b)).catch(() => null);
    const disk = await readFile(`${FINAL}/ext2-shell.img`).then((b) => new Uint8Array(b)).catch(() => null);
    if (!kernel || !disk) {
      // Host artifacts (gitignored): skip quietly when this checkout has none.
      return;
    }
    expect(engine.mf_abi_version()).toBe(1);
    const kp = uploadImage(engine, kernel);
    const dp = uploadImage(engine, disk);
    bootWithPointers(engine, kp, kernel.length, dp, disk.length);
    let printed = 0;
    let text = '';
    const pump = (frames: number) => {
      for (let i = 0; i < frames; ++i) {
        run(engine, 500_000n);
        const d = readConsole(engine, printed);
        printed = d.printed;
        text += d.text;
      }
    };
    pump(200);
    expect(text).toContain('Cinux up');
    expect(text).toContain('/ #');
    injectSerial(engine, 'echo final-bundle-ok\r');
    pump(120);
    expect(text).toContain('final-bundle-ok');
    console.log(`OK console=${text.length}B tail=${JSON.stringify(text.slice(-40))}`);
  }, 300_000);
});
