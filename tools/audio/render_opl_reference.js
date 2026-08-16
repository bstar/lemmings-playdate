#!/usr/bin/env node
/* Synthesize a clean-interpreter LPR stream with externally supplied DBOPL. */
const fs = require("fs");
const vm = require("vm");

function fail(message) { process.stderr.write(`opl-render: ${message}\n`); process.exit(2); }
const [bundlePath, tracePath, outputPath] = process.argv.slice(2);
if (!bundlePath || !tracePath || !outputPath) fail("bundle input.lpr output.wav");

const code = fs.readFileSync(bundlePath, "utf8");
vm.runInThisContext(code + "\nglobalThis.DBOPL=DBOPL;");
const trace = fs.readFileSync(tracePath);
const magic = trace.toString("ascii", 0, 4);
const headerSize = magic === "LPR3" ? 20 : magic === "LPR2" ? 16 : 0;
if (!headerSize || trace.length < headerSize || (trace.length - headerSize) % 6)
  fail("invalid LPR stream");
const steps = trace.readUInt32LE(4);
const factor = trace.readUInt32LE(8);
const sampleCount = trace.readUInt32LE(12);
const rate = magic === "LPR3" ? trace.readUInt32LE(16) : 22050;
const samplesPerTick = Math.round(rate / (factor / 210));
// DBOPL.OPL routes through MixerChannel, which boosts by 2x and hard-clips to
// Int16. Capture Handler's raw Int32 blocks instead so headroom is preserved.
const handler = new DBOPL.Handler();
handler.Init(rate);
const opl = {
  write(register, value) { handler.WriteReg(register, value); },
  generate(samples) {
    const output = new Int32Array(samples * 2);
    const rawMixer = {
      AddSamples_m32(count, buffer) {
        for (let i = 0; i < count; ++i)
          output[i * 2] = output[i * 2 + 1] = buffer[i];
      },
      AddSamples_s32(count, buffer) {
        for (let i = 0; i < count * 2; ++i) output[i] = buffer[i];
      }
    };
    handler.Generate(rawMixer, samples);
    return output;
  }
};
const pcm = Buffer.alloc(sampleCount * 2);
let record = headerSize, position = 0, peak = 0, clipped = 0;
for (let step = 0; step < steps && position < sampleCount; ++step) {
  while (record < trace.length && trace.readUInt32LE(record) === step) {
    opl.write(trace[record + 4], trace[record + 5]); record += 6;
  }
  const count = Math.min(samplesPerTick, sampleCount - position);
  let generated = 0;
  while (generated < count) {
    // This DBOPL port intentionally caps one generation call at 512 frames.
    const chunk = Math.min(512, count - generated);
    const stereo = opl.generate(chunk);
    for (let i = 0; i < chunk; ++i) {
      // Raw output is 6 dB below the reference wrapper's boosted mixer. This
      // fixed headroom preserves relative dynamics without per-song leveling.
      let value = Math.round((stereo[i * 2] + stereo[i * 2 + 1]) * 0.5);
      peak = Math.max(peak, Math.abs(value));
      if (value < -32768 || value > 32767) ++clipped;
      value = Math.max(-32768, Math.min(32767, value));
      pcm.writeInt16LE(value, (position + generated + i) * 2);
    }
    generated += chunk;
  }
  position += count;
}
if (record !== trace.length || position !== sampleCount) fail("incomplete register stream");
if (clipped) fail(`${clipped} samples exceeded 16-bit range (peak ${peak})`);

const wav = Buffer.alloc(44);
wav.write("RIFF", 0); wav.writeUInt32LE(36 + pcm.length, 4); wav.write("WAVEfmt ", 8);
wav.writeUInt32LE(16, 16); wav.writeUInt16LE(1, 20); wav.writeUInt16LE(1, 22);
wav.writeUInt32LE(rate, 24); wav.writeUInt32LE(rate * 2, 28);
wav.writeUInt16LE(2, 32); wav.writeUInt16LE(16, 34); wav.write("data", 36);
wav.writeUInt32LE(pcm.length, 40);
fs.writeFileSync(outputPath, Buffer.concat([wav, pcm]));
process.stdout.write(`${steps} steps, ${sampleCount} samples, peak ${peak}, clipped ${clipped} -> ${outputPath}\n`);
