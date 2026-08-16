#!/usr/bin/env node
/*
 * Private validation renderer for the DOS sound image. This deliberately loads
 * an external research implementation at runtime; none of that implementation
 * is redistributed here. Production assets are generated, never committed.
 */
const fs = require("fs");
const vm = require("vm");

function fail(message) { process.stderr.write(`audio-render: ${message}\n`); process.exit(2); }
const [bundlePath, rawPath, outputPath, trackArg = "music:0", secondsArg = "90"] = process.argv.slice(2);
if (!bundlePath || !rawPath || !outputPath) fail("bundle raw-adlib output.wav [music:N|sfx:N] [seconds]");

const code = fs.readFileSync(bundlePath, "utf8");
vm.runInThisContext(code + "\nglobalThis.Lemmings=Lemmings;globalThis.DBOPL=DBOPL;");
const raw = new Uint8Array(fs.readFileSync(rawPath));
const config = {
  version: 1, adlibChannelConfigPosition: 1452, dataOffset: 2215,
  frequenciesOffset: 2343, octavesOffset: 2727, frequenciesCountOffset: 2823,
  instructionsOffset: 2926, soundIndexTablePosition: 21989,
  soundDataOffset: 21731, numberOfTracks: 21
};
const player = new Lemmings.SoundImagePlayer(raw, config);
const [kind, indexText] = trackArg.split(":");
if (kind === "music") player.initMusic(Number(indexText));
else if (kind === "sfx") player.initSound(Number(indexText));
else fail("track must be music:N or sfx:N");
const rate = 22050;
const samplesPerTick = Math.round(rate / player.getSamplingInterval());
const opl = new DBOPL.OPL(rate, 2);
const sampleCount = Math.floor(Number(secondsArg) * rate);
const pcm = Buffer.alloc(sampleCount * 2);
let position = 0;
while (position < sampleCount) {
  player.read((register, value) => opl.write(register, value));
  const count = Math.min(samplesPerTick, sampleCount - position);
  const stereo = opl.generate(count);
  for (let i = 0; i < count; ++i) {
    let value = Math.round((stereo[i * 2] + stereo[i * 2 + 1]) / 2);
    value = Math.max(-32768, Math.min(32767, value));
    pcm.writeInt16LE(value, (position + i) * 2);
  }
  position += count;
}

const wav = Buffer.alloc(44);
wav.write("RIFF", 0); wav.writeUInt32LE(36 + pcm.length, 4); wav.write("WAVEfmt ", 8);
wav.writeUInt32LE(16, 16); wav.writeUInt16LE(1, 20); wav.writeUInt16LE(1, 22);
wav.writeUInt32LE(rate, 24); wav.writeUInt32LE(rate * 2, 28);
wav.writeUInt16LE(2, 32); wav.writeUInt16LE(16, 34); wav.write("data", 36);
wav.writeUInt32LE(pcm.length, 40);
fs.writeFileSync(outputPath, Buffer.concat([wav, pcm]));
process.stdout.write(`${trackArg}: ${secondsArg}s, ${sampleCount} samples -> ${outputPath}\n`);
