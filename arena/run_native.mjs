// Bot vs bot with the native engine (sbcli), so the trained models can play.
// Our engine is also the referee, using the rules in engine-rules-spec.md.
//
//   node arena/run_native.mjs --sbcli PATH [--models DIR] [--games 20] [--dets 32] [--nodes 20000]
//                             [--havi-ms 1000] [--seed 1] [--threads N] [--havi 5d092f9|shipped|fixed]
//
// --models   folder with eval.bin / bid.bin / info.bin (e.g. training/promoted); omit for the untrained engine
// --havi     5d092f9 (default): current HaviEngine with havi_adapter_v2.js
//            shipped / fixed:   the old 3a45a1c bundles with havi_adapter.js
// --ours random  replaces our engine with a random legal mover (sanity check for the HaviEngine harness)
// Seats alternate every game so neither bot keeps the seat advantage.

import fs from "fs";
import vm from "vm";
import path from "path";
import readline from "readline";
import { spawn } from "child_process";
import { createRequire } from "module";
import { fileURLToPath } from "url";

const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);

const args = {};
for (let i = 2; i < process.argv.length; i += 2) args[process.argv[i].replace(/^--/, "")] = process.argv[i + 1];
const games = +(args.games ?? 20), dets = +(args.dets ?? 32), nodes = +(args.nodes ?? 20000);
const haviMs = +(args["havi-ms"] ?? 1000), seed0 = +(args.seed ?? 1);
const havi = args.havi ?? "5d092f9";
if (!args.sbcli) { console.error("--sbcli PATH is required"); process.exit(1); }

const bundle = { "5d092f9": "havi-bundle-5d092f9.js", shipped: "havi-bundle.js", fixed: "havi-bundle-signfix.js" }[havi];
const adapter = require(havi === "5d092f9" ? "./havi_adapter_v2.js" : "./havi_adapter.js");
const ctx = { console, Date, Math };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(here, bundle), "utf8") + "\nthis.HaviEngine = HaviEngine;", ctx);
const H = ctx.HaviEngine;
if (adapter.haviInit) adapter.haviInit(H, args.met ? JSON.parse(fs.readFileSync(args.met, "utf8")) : null);

// Native engine over stdin/stdout: one command per line, one JSON reply per line.
const env = { ...process.env };
if (args.threads) env.SB_THREADS = args.threads;
const proc = spawn(args.sbcli, args.models ? [args.models] : [], { env, stdio: ["pipe", "pipe", "pipe"] });
let modelsLine = "none (untrained)";
let errBuf = "";
proc.stderr.on("data", (d) => { errBuf += d; const m = /Engine models: (.*)\n/.exec(errBuf); if (m) modelsLine = m[1]; });
const rl = readline.createInterface({ input: proc.stdout });
const waiting = [];
rl.on("line", (l) => waiting.shift()(JSON.parse(l)));
const cmd = (s) => new Promise((res) => { waiting.push(res); proc.stdin.write(s + "\n"); });

const stats = {};
const results = [];
const t0 = Date.now();
await new Promise((r) => setTimeout(r, 300));
console.log(`engine models: ${modelsLine}`);
for (let g = 0; g < games; g++) {
  const ourSeat = g % 2 === 0 ? 1 : 2;
  let S = await cmd(`arena ${seed0 * 100003 + g}`);
  let handsStart = [30, 30], handNet = [];
  while (S.phase !== "gameover") {
    if (S.phase === "handover") {
      handNet.push((S.pools[ourSeat - 1] - handsStart[ourSeat - 1]) - (S.pools[2 - ourSeat] - handsStart[2 - ourSeat]));
      handsStart = S.pools.slice();
      S = await cmd("next");
      continue;
    }
    const who = S.toMove;
    let move;
    if (who === ourSeat && args.ours === "random") {
      const st = await cmd(`state ${who}`);
      move = st.phase === "bidding" ? (st.bid.by ? "a" : "b0") : st.legal[Math.floor(Math.random() * st.legal.length)].n;
    } else if (who === ourSeat) {
      const r = await cmd(`botchoice ${dets} ${nodes} ${g * 1000 + S.history.length}`);
      if (!r.ok) throw new Error(r.error);
      move = r.move;
    } else {
      move = adapter.haviChoose(H, await cmd(`state ${who}`), { limitMs: haviMs, stats });
    }
    const next = await cmd(`move ${move}`);
    if (!next.ok) throw new Error(`illegal move ${move} by seat ${who}: ${next.error}`);
    S = next;
  }
  handNet.push((S.pools[ourSeat - 1] - handsStart[ourSeat - 1]) - (S.pools[2 - ourSeat] - handsStart[2 - ourSeat]));
  const won = S.winner === ourSeat;
  results.push({ won, ourSeat, hands: S.handNumber, margin: S.pools[ourSeat - 1] - S.pools[2 - ourSeat], handNet });
  const w = results.filter((r) => r.won).length;
  console.log(`game ${g + 1}: ${won ? "SB Engine" : "HaviEngine"} wins (our seat ${ourSeat}), ${S.handNumber} hands, pools ${S.pools.join("-")}  [${w}-${results.length - w}]`);
}
proc.stdin.write("quit\n");
const wins = results.filter((r) => r.won).length;
const allHands = results.flatMap((r) => r.handNet);
const mean = allHands.reduce((a, b) => a + b, 0) / allHands.length;
const sd = Math.sqrt(allHands.reduce((a, b) => a + (b - mean) ** 2, 0) / allHands.length);
console.log(`\nSB Engine ${wins}-${games - wins} HaviEngine over ${games} games (${((100 * wins) / games).toFixed(0)}%)`);
console.log(`net chips per hand for SB Engine: ${mean >= 0 ? "+" : ""}${mean.toFixed(2)} (se ${(sd / Math.sqrt(allHands.length)).toFixed(2)}, ${allHands.length} hands)`);
console.log(`HaviEngine fallbacks (no usable move): ${stats.fallback || 0}, mapping failures: ${stats.mapFail || 0}`);
console.log(`settings: SB Engine ${dets} deals x ${nodes} nodes, models: ${modelsLine}; HaviEngine ${havi} ${haviMs} ms per move; ${((Date.now() - t0) / 1000).toFixed(0)} s`);
