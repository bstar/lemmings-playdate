#!/usr/bin/env node
/*
 * Validation-only register tracer for an independently supplied LemmingsJS
 * bundle. The bundle is loaded at runtime and is never redistributed.
 */
const fs = require("fs");
const vm = require("vm");

function fail(message) { process.stderr.write(`audio-trace: ${message}\n`); process.exit(2); }
const [bundlePath, rawPath, outputPath, trackArg = "music:0", stepsArg = "1000"] = process.argv.slice(2);
if (!bundlePath || !rawPath || !outputPath) fail("bundle raw-adlib output.lpr [music:N|sfx:N] [steps]");

const code = fs.readFileSync(bundlePath, "utf8");
vm.runInThisContext(code + "\nglobalThis.Lemmings=Lemmings;");
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

const steps = Number(stepsArg);
const records = [];
for (let step = 0; step < steps; ++step) {
  player.read((register, value) => records.push([step, register, value]));
}
const out = Buffer.alloc(8 + records.length * 6);
out.write("LPR1", 0); out.writeUInt32LE(steps, 4);
records.forEach(([step, register, value], i) => {
  out.writeUInt32LE(step, 8 + i * 6);
  out[12 + i * 6] = register; out[13 + i * 6] = value;
});
fs.writeFileSync(outputPath, out);
process.stdout.write(`${trackArg}: ${steps} steps, ${records.length} writes -> ${outputPath}\n`);
