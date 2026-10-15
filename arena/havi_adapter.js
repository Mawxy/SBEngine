// Plays HaviEngine's live "Experimental" bot (MCTS v2) from a Spectacular Bid session state.
// Shared by the GUI's worker and the Node arena runner. No DOM, no imports:
// pass in the bundled HaviEngine module object (H) and a session state JSON for the bot's seat.
//
// Faithful to how HaviEngine plays in its GameRoom:
//   - one determinization of the hidden tiles per decision, MCTS with C = sqrt(2),
//     DEFAULT_WEIGHTS, 1000 ms limit, SPRT early stop
//   - bidding through its MCTS bidding tree
//   - free lead after winning the bid: the first tile in its hand (deal order), modeled as a random tile
// The referee is our engine (rules from engine-rules-spec.md), so forced leads come from our rules.

function haviTile(code) {
  const hi = +code[0], lo = +code[1];
  return { top: lo, bottom: hi, id: `${lo}-${hi}` };
}

function haviPiece(p) {
  const hi = +p.t[0], lo = +p.t[1];
  return { top: p.in, bottom: p.out, id: `${lo}-${hi}` };
}

// Our layout (center/spinner + arms in attachment order) -> HaviEngine boardState + boardEnds.
// Arm 0 = left, 1 = right, 2 = up, 3 = down.
function haviBoard(L) {
  const bs = { spinner: null, spinnerReady: false, firstTile: null, leftArm: [], rightArm: [], upArm: [], downArm: [] };
  const ends = [null, null, null, null];
  if (!L.center && !L.spinner) return { bs, ends };
  const arms = L.arms.map((a) => a.map(haviPiece));
  bs.leftArm = arms[0] || []; bs.rightArm = arms[1] || []; bs.upArm = arms[2] || []; bs.downArm = arms[3] || [];
  const last = (a, dflt) => (a.length ? a[a.length - 1].bottom : dflt);
  if (L.spinner) {
    const v = +L.spinner[0];
    bs.spinner = { top: v, bottom: v, id: `${v}-${v}` };
    bs.spinnerReady = bs.leftArm.length > 0 && bs.rightArm.length > 0;
    ends[0] = last(bs.leftArm, v);
    ends[1] = last(bs.rightArm, v);
    if (bs.spinnerReady) { ends[2] = last(bs.upArm, v); ends[3] = last(bs.downArm, v); }
  } else {
    const hi = +L.center[0], lo = +L.center[1];
    bs.firstTile = { top: hi, bottom: lo, id: `${lo}-${hi}` };
    ends[0] = last(bs.leftArm, hi);
    ends[1] = last(bs.rightArm, lo);
  }
  return { bs, ends };
}

// HaviEngine move {tile, end} -> our notation.
function haviToNotation(st, mv) {
  const t = mv.tile;
  const code = `${Math.max(t.top, t.bottom)}${Math.min(t.top, t.bottom)}`;
  const L = st.layout;
  if (!L.center && !L.spinner) return code;
  const idx = { left: 0, right: 1, up: 2, down: 3 }[mv.end] ?? 0;
  const end = idx < L.arms.length ? L.ends.find((e) => e.arm === idx) : L.ends.find((e) => e.spinner);
  return end ? code + end.code : code;
}

function haviRunSearch(H, state, limitMs) {
  const search = H.createSearch(state, { explorationConstant: 1.41421356, weights: { ...H.DEFAULT_WEIGHTS } });
  if (search.done) return search.best();
  const t0 = Date.now();
  for (;;) {
    search.step(100);
    const [a, b] = search.topTwo();
    if (a && b) {
      const r = H.evaluateSPRT(a, b);
      if (r.decision === "CONVERGED") return r.winner.move;
    }
    if (Date.now() - t0 >= limitMs) break;
  }
  return search.best();
}

// Returns a move in our notation. `stats` (optional) counts fallbacks.
function haviChoose(H, st, opts = {}) {
  const limitMs = opts.limitMs ?? 1000;
  const stats = opts.stats || {};
  const me = st.me, opp = 3 - me;
  // HaviEngine scores a race to 150 points; in chips that is what each side has won so far.
  const botScore = (30 - st.pools[opp - 1]) * 5, oppScore = (30 - st.pools[me - 1]) * 5;
  const { bs, ends } = haviBoard(st.layout);
  const base = {
    botHand: st.myHand.map(haviTile), oppHandSize: st.oppCount, boardState: bs, boardEnds: ends,
    boneyardSize: st.boneyard, botScore, oppScore, targetScore: 150, passHistoryOpponent: [],
  };

  if (st.phase === "bidding") {
    const current = st.bid.by && st.bid.by !== me ? st.bid.current : 0;
    const state = H.buildDeterminizedStateV2({
      ...base, contract: { amount: current, holderIndex: current > 0 ? 1 : null, resolved: false },
      botIsDominos: st.jockey === me, myPlayerIndex: 0,
    });
    const best = haviRunSearch(H, state, limitMs);
    const want = best && best.type === "BID" ? Math.min(best.amount, st.bid.maxMe) : 0;
    if (want > current) return "b" + want;
    return st.bid.by ? "a" : "b0";
  }

  const plays = st.legal.filter((l) => l.kind === "play");
  if (!plays.length || st.legal.length === 1) return st.legal[0].n;  // draw, pass or forced
  if (!st.layout.center && !st.layout.spinner) return plays[Math.floor(Math.random() * plays.length)].n;  // free lead

  const best = haviRunSearch(H, H.buildDeterminizedStateV2(base), limitMs);
  if (best && best.tile) {
    const n = haviToNotation(st, best);
    if (plays.some((p) => p.n === n)) return n;
    stats.mapFail = (stats.mapFail || 0) + 1;
  }
  // HaviEngine's GameRoom would draw here even while holding a playable tile, which our rules forbid.
  stats.fallback = (stats.fallback || 0) + 1;
  return plays[0].n;
}

if (typeof module !== "undefined") module.exports = { haviChoose, haviBoard, haviToNotation };
