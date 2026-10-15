// Bot vs bot: our engine (C++ compiled to WebAssembly) against HaviEngine's Experimental bot.
// Our engine is also the referee, using the rules in engine-rules-spec.md.
//
//   node arena/run_arena.mjs [games=20] [ourDets=32] [ourNodes=20000] [haviMs=1000] [seed=1] [shipped|fixed]
//
// "fixed" uses HaviEngine with a one-line fix to its MCTS selection sign (see README).
//
// Seats alternate every game so neither bot keeps the seat advantage.

import fs from "fs";
import vm from "vm";
import path from "path";
import { createRequire } from "module";
import { fileURLToPath } from "url";

const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
const { haviChoose } = require("./havi_adapter.js");

const [games = 20, dets = 32, nodes = 20000, haviMs = 1000, seed0 = 1] = process.argv.slice(2, 7).map(Number);
const version = process.argv[7] === "fixed" ? "fixed" : "shipped";

// HaviEngine bundle (IIFE defining a global HaviEngine).
const ctx = { console, Date, Math };
vm.createContext(ctx);
const bundle = version === "fixed" ? "havi-bundle-signfix.js" : "havi-bundle.js";
vm.runInContext(fs.readFileSync(path.join(here, bundle), "utf8") + "\nthis.HaviEngine = HaviEngine;", ctx);
const H = ctx.HaviEngine;

async function makeEngine(mod) {
  let inst;
  const mem = () => new DataView(inst.exports.memory.buffer);
  const wasi = {
    fd_write: (fd, iovs, n, nw) => { mem().setUint32(nw, 0, true); return 0; },
    fd_close: () => 0, fd_seek: () => 0, fd_fdstat_get: () => 8, fd_prestat_get: () => 8, fd_prestat_dir_name: () => 8,
    proc_exit: (c) => { throw new Error("engine exited " + c); },
    environ_sizes_get: (a, b) => { mem().setUint32(a, 0, true); mem().setUint32(b, 0, true); return 0; },
    environ_get: () => 0,
  };
  inst = await WebAssembly.instantiate(mod, { wasi_snapshot_preview1: new Proxy(wasi, { get: (t, k) => (k in t ? t[k] : () => 52) }) });
  if (inst.exports._initialize) inst.exports._initialize();
  const enc = new TextEncoder(), dec = new TextDecoder();
  return (s) => {
    const b = enc.encode(s + "\0"), p = inst.exports.sb_alloc(b.length);
    new Uint8Array(inst.exports.memory.buffer, p, b.length).set(b);
    const r = inst.exports.sb_command(p);
    inst.exports.sb_free(p);
    const u = new Uint8Array(inst.exports.memory.buffer);
    let e = r; while (u[e]) e++;
    return JSON.parse(dec.decode(u.subarray(r, e)));
  };
}

const mod = await WebAssembly.compile(fs.readFileSync(path.join(here, "..", "wasm", "sbengine.wasm")));
const stats = {};
const results = [];
const t0 = Date.now();
for (let g = 0; g < games; g++) {
  const cmd = await makeEngine(mod);
  const ourSeat = g % 2 === 0 ? 1 : 2;
  let S = cmd(`arena ${seed0 * 100003 + g}`);
  let handsStart = [30, 30], handNet = [];
  while (S.phase !== "gameover") {
    if (S.phase === "handover") {
      handNet.push((S.pools[ourSeat - 1] - handsStart[ourSeat - 1]) - (S.pools[2 - ourSeat] - handsStart[2 - ourSeat]));
      handsStart = S.pools.slice();
      S = cmd("next");
      continue;
    }
    const who = S.toMove;
    let move;
    if (who === ourSeat) {
      const r = cmd(`botchoice ${dets} ${nodes} ${g * 1000 + S.history.length}`);
      if (!r.ok) throw new Error(r.error);
      move = r.move;
    } else {
      move = haviChoose(H, cmd(`state ${who}`), { limitMs: haviMs, stats });
    }
    const next = cmd(`move ${move}`);
    if (!next.ok) throw new Error(`illegal move ${move} by seat ${who}: ${next.error}`);
    S = next;
  }
  handNet.push((S.pools[ourSeat - 1] - handsStart[ourSeat - 1]) - (S.pools[2 - ourSeat] - handsStart[2 - ourSeat]));
  const won = S.winner === ourSeat;
  results.push({ won, ourSeat, hands: S.handNumber, margin: S.pools[ourSeat - 1] - S.pools[2 - ourSeat], handNet });
  console.log(`game ${g + 1}: ${won ? "our engine" : "HaviEngine"} wins (our seat ${ourSeat}), ${S.handNumber} hands, pools ${S.pools.join("-")}`);
}
const wins = results.filter((r) => r.won).length;
const allHands = results.flatMap((r) => r.handNet);
const mean = allHands.reduce((a, b) => a + b, 0) / allHands.length;
const sd = Math.sqrt(allHands.reduce((a, b) => a + (b - mean) ** 2, 0) / allHands.length);
console.log(`\nour engine ${wins}-${games - wins} HaviEngine over ${games} games (${((100 * wins) / games).toFixed(0)}%)`);
console.log(`net chips per hand for our engine: ${mean >= 0 ? "+" : ""}${mean.toFixed(2)} (se ${(sd / Math.sqrt(allHands.length)).toFixed(2)}, ${allHands.length} hands)`);
console.log(`HaviEngine fallbacks (no usable move): ${stats.fallback || 0}, mapping failures: ${stats.mapFail || 0}`);
console.log(`settings: ours ${dets} deals x ${nodes} nodes, HaviEngine (${version}) ${haviMs} ms per move; ${((Date.now() - t0) / 1000).toFixed(0)} s`);
