var HaviEngine = (() => {
  var __defProp = Object.defineProperty;
  var __getOwnPropDesc = Object.getOwnPropertyDescriptor;
  var __getOwnPropNames = Object.getOwnPropertyNames;
  var __hasOwnProp = Object.prototype.hasOwnProperty;
  var __esm = (fn, res) => function __init() {
    return fn && (res = (0, fn[__getOwnPropNames(fn)[0]])(fn = 0)), res;
  };
  var __export = (target, all) => {
    for (var name in all)
      __defProp(target, name, { get: all[name], enumerable: true });
  };
  var __copyProps = (to, from, except, desc) => {
    if (from && typeof from === "object" || typeof from === "function") {
      for (let key of __getOwnPropNames(from))
        if (!__hasOwnProp.call(to, key) && key !== except)
          __defProp(to, key, { get: () => from[key], enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable });
    }
    return to;
  };
  var __toCommonJS = (mod) => __copyProps(__defProp({}, "__esModule", { value: true }), mod);

  // ../mnt/DominoBot/spectacular-bid/src/lib/gameUtils.js
  function canPlayTile(tile, boardEnds) {
    if (!boardEnds || boardEnds.length === 0) return true;
    const active = boardEnds.filter((e) => e !== null && e !== void 0);
    if (active.length === 0) return true;
    return active.some((e) => tile.top === e || tile.bottom === e);
  }
  function getPlayableEnds(tile, boardEnds) {
    if (!boardEnds || boardEnds.filter((e) => e !== null && e !== void 0).length === 0) return ["any"];
    const [leftEnd, rightEnd, upEnd, downEnd] = boardEnds;
    const ends = [];
    const matches = (e) => e !== null && e !== void 0 && (tile.top === e || tile.bottom === e);
    if (matches(leftEnd)) ends.push("left");
    if (matches(rightEnd)) ends.push("right");
    if (matches(upEnd)) ends.push("up");
    if (matches(downEnd)) ends.push("down");
    return ends;
  }
  function armEndScore(arm) {
    if (!arm || arm.length === 0) return null;
    const last = arm[arm.length - 1];
    return last.top === last.bottom ? last.bottom * 2 : last.bottom;
  }
  function calculatePipSum(bs) {
    if (!bs) return 0;
    if (!bs.spinner && !bs.firstTile) return 0;
    if (!bs.spinner) {
      const leftScore = bs.leftArm.length > 0 ? armEndScore(bs.leftArm) : bs.firstTile.top;
      const rightScore = bs.rightArm.length > 0 ? armEndScore(bs.rightArm) : bs.firstTile.bottom;
      return leftScore + rightScore;
    }
    const sv = bs.spinner.top;
    const hasLeft = bs.leftArm.length > 0;
    const hasRight = bs.rightArm.length > 0;
    const bothArmed = hasLeft && hasRight;
    let sum = 0;
    if (!hasLeft && !hasRight) {
      sum = sv * 2;
    } else {
      sum += hasLeft ? armEndScore(bs.leftArm) : sv * 2;
      sum += hasRight ? armEndScore(bs.rightArm) : sv * 2;
    }
    if (bothArmed) {
      if (bs.upArm.length > 0) sum += armEndScore(bs.upArm);
      if (bs.downArm.length > 0) sum += armEndScore(bs.downArm);
    }
    return sum;
  }
  function calculateScoreFromBoardState(bs) {
    const sum = calculatePipSum(bs);
    return sum % 5 === 0 && sum > 0 ? sum : 0;
  }
  function applyTileToBoard(bs, be, tile, end) {
    const newBs = {
      spinner: bs.spinner,
      spinnerReady: bs.spinnerReady,
      firstTile: bs.firstTile,
      leftArm: [...bs.leftArm],
      rightArm: [...bs.rightArm],
      upArm: [...bs.upArm],
      downArm: [...bs.downArm]
    };
    const newBe = [...be];
    const isDouble = tile.top === tile.bottom;
    const isEmpty = !newBs.spinner && !newBs.firstTile && newBs.leftArm.length === 0 && newBs.rightArm.length === 0;
    const flip = (arr) => arr.map((t) => ({ ...t, top: t.bottom, bottom: t.top }));
    if (isEmpty || end === "any") {
      if (isDouble) {
        newBs.spinner = tile;
        newBe[0] = tile.top;
        newBe[1] = tile.top;
      } else {
        newBs.firstTile = { ...tile };
        newBe[0] = tile.top;
        newBe[1] = tile.bottom;
      }
      return { newBs, newBe };
    }
    const endIdx = { left: 0, right: 1, up: 2, down: 3 }[end] ?? 0;
    const currentEndVal = newBe[endIdx];
    const orientedTile = tile.top === currentEndVal ? { ...tile } : { ...tile, top: tile.bottom, bottom: tile.top };
    const newOpenEnd = orientedTile.bottom;
    if (isDouble && !newBs.spinner) {
      newBs.spinner = tile;
      const oldLeft = [...newBs.leftArm];
      const oldRight = [...newBs.rightArm];
      const ft = newBs.firstTile;
      newBs.firstTile = null;
      newBs.spinnerReady = false;
      if (end === "left") {
        newBs.leftArm = [];
        newBs.rightArm = [
          ...flip([...oldLeft].reverse()),
          ...ft ? [ft] : [],
          ...oldRight
        ];
        newBe[0] = tile.top;
      } else {
        newBs.rightArm = [];
        newBs.leftArm = [
          ...flip([...oldRight].reverse()),
          ...ft ? [{ ...ft, top: ft.bottom, bottom: ft.top }] : [],
          ...oldLeft
        ];
        newBe[1] = tile.top;
      }
    } else {
      if (end === "left") {
        newBs.leftArm.push(orientedTile);
        newBe[0] = newOpenEnd;
      } else if (end === "right") {
        newBs.rightArm.push(orientedTile);
        newBe[1] = newOpenEnd;
      } else if (end === "up") {
        newBs.upArm.push(orientedTile);
        newBe[2] = newOpenEnd;
      } else if (end === "down") {
        newBs.downArm.push(orientedTile);
        newBe[3] = newOpenEnd;
      }
      if (newBs.spinner && !newBs.spinnerReady && newBs.leftArm.length > 0 && newBs.rightArm.length > 0) {
        newBs.spinnerReady = true;
        newBe[2] = newBs.spinner.top;
        newBe[3] = newBs.spinner.top;
      }
    }
    return { newBs, newBe };
  }
  function countPips(hand) {
    return hand.reduce((sum, tile) => sum + tile.top + tile.bottom, 0);
  }
  function maxSafeBid(oppScore, targetScore = 150, bidderIsDominos = false) {
    if (bidderIsDominos) {
      const max2 = Math.floor((targetScore - oppScore + 14) / 5);
      return Math.max(0, max2);
    }
    const max = Math.ceil((targetScore - oppScore) / 5) - 1;
    return Math.max(0, max);
  }
  var init_gameUtils = __esm({
    "../mnt/DominoBot/spectacular-bid/src/lib/gameUtils.js"() {
    }
  });

  // entry.js
  var entry_exports = {};
  __export(entry_exports, {
    DEFAULT_WEIGHTS: () => DEFAULT_WEIGHTS,
    buildBaseline: () => buildBaseline,
    buildDeterminizedStateV2: () => buildDeterminizedStateV2,
    createSearch: () => createSearch,
    deriveSecond: () => deriveSecond,
    evaluateSPRT: () => evaluateSPRT,
    setMatchEquity: () => setMatchEquity
  });

  // ../mnt/DominoBot/spectacular-bid/src/lib/mcts/engine.js
  init_gameUtils();

  // ../mnt/DominoBot/spectacular-bid/src/lib/mctsEngine.js
  init_gameUtils();
  var DEFAULT_WEIGHTS = {
    heavyDouble: 2,
    // multiplier applied to double tiles during weighted rollout
    diversity: 0.5,
    // bonus weight for keeping many suits in hand
    aggression: 1,
    // scales how much score/pip advantage influences evaluation
    cutoffDepth: 30
    // rollout depth limit before early evaluation
  };

  // ../mnt/DominoBot/spectacular-bid/src/lib/mcts/determinization.js
  function fullTileSet() {
    const tiles = [];
    for (let i = 0; i <= 6; i++) {
      for (let j = i; j <= 6; j++) {
        tiles.push({ top: i, bottom: j, id: `${i}-${j}` });
      }
    }
    return tiles;
  }
  function cloneBoardState(bs) {
    if (!bs) return emptyBoardState();
    const cloneArm = (a) => Array.isArray(a) ? a.map((t) => ({ ...t })) : [];
    return {
      spinner: bs.spinner ? { ...bs.spinner } : null,
      spinnerReady: !!bs.spinnerReady,
      firstTile: bs.firstTile ? { ...bs.firstTile } : null,
      leftArm: cloneArm(bs.leftArm),
      rightArm: cloneArm(bs.rightArm),
      upArm: cloneArm(bs.upArm),
      downArm: cloneArm(bs.downArm)
    };
  }
  function emptyBoardState() {
    return { spinner: null, spinnerReady: false, firstTile: null, leftArm: [], rightArm: [], upArm: [], downArm: [] };
  }
  function collectBoardTileIds(bs, set) {
    if (!bs) return;
    if (bs.spinner) set.add(bs.spinner.id);
    if (bs.firstTile) set.add(bs.firstTile.id);
    for (const arm of [bs.leftArm, bs.rightArm, bs.upArm, bs.downArm]) {
      if (Array.isArray(arm)) for (const t of arm) set.add(t.id);
    }
  }
  function filterOpponentEligible(unseen, { passHistoryOpponent = [], bidHistory = [] } = {}) {
    let eligible = unseen;
    const blockedSuits = new Set(passHistoryOpponent);
    if (blockedSuits.size > 0) {
      const filtered = eligible.filter(
        (t) => !blockedSuits.has(t.top) && !blockedSuits.has(t.bottom)
      );
      if (filtered.length > 0) eligible = filtered;
    }
    const passedHighBid = bidHistory.some(
      (h) => h.actor === 1 && h.action === "PASS" && (h.amount ?? 0) >= 4
    );
    if (passedHighBid) {
      const filtered = eligible.filter((t) => !(t.top === t.bottom && t.top >= 5));
      if (filtered.length >= 5) eligible = filtered;
    }
    return eligible;
  }
  function shuffleInPlace(arr) {
    for (let i = arr.length - 1; i > 0; i--) {
      const j = Math.floor(Math.random() * (i + 1));
      [arr[i], arr[j]] = [arr[j], arr[i]];
    }
    return arr;
  }
  function buildDeterminizedStateV2({
    botHand,
    oppHandSize,
    boardState,
    boardEnds,
    boneyardSize,
    botScore,
    oppScore,
    targetScore,
    passHistoryOpponent = [],
    bidHistory = [],
    turnIndex = 0,
    contract = null,
    myPlayerIndex = 0,
    botIsDominos = false
  }) {
    const seenIds = /* @__PURE__ */ new Set();
    for (const t of botHand) seenIds.add(t.id);
    collectBoardTileIds(boardState, seenIds);
    const allTiles = fullTileSet();
    const unseen = allTiles.filter((t) => !seenIds.has(t.id));
    const oppEligible = filterOpponentEligible(unseen, { passHistoryOpponent, bidHistory });
    const pool = oppEligible.length >= oppHandSize ? oppEligible : unseen;
    const shuffled = shuffleInPlace(pool.map((t) => ({ ...t })));
    const oppHand = shuffled.slice(0, oppHandSize);
    const newBoneyard = shuffled.slice(oppHandSize, oppHandSize + boneyardSize);
    return {
      botHand: botHand.map((t) => ({ ...t })),
      oppHand,
      boardState: cloneBoardState(boardState),
      boardEnds: [...boardEnds],
      boneyard: newBoneyard,
      botScore,
      oppScore,
      targetScore,
      currentPlayer: 0,
      passHistory: { opponent: [...passHistoryOpponent] },
      bidHistory: [...bidHistory],
      turnIndex,
      contract,
      phase: contract ? "bidding" : "playing",
      myPlayerIndex,
      botIsDominos
    };
  }

  // ../mnt/DominoBot/spectacular-bid/src/lib/mcts/evaluation.js
  init_gameUtils();

  // ../mnt/DominoBot/spectacular-bid/src/lib/met.js
  var AWAY = 30;
  var SIZE = AWAY + 1;
  var idx = (a1, a2) => a1 * SIZE + a2;
  function buildBaseline() {
    const g = new Array(SIZE * SIZE).fill(0);
    for (let a1 = 0; a1 <= AWAY; a1++) {
      for (let a2 = 0; a2 <= AWAY; a2++) {
        if (a1 === 0) g[idx(a1, a2)] = a2 === 0 ? 0 : 1;
        else if (a2 === 0) g[idx(a1, a2)] = 0;
        else g[idx(a1, a2)] = 0.5 * g[idx(a1 - 1, a2)] + 0.5 * g[idx(a1, a2 - 1)];
      }
    }
    return g;
  }
  function deriveSecond(gridFirst) {
    const g = new Array(SIZE * SIZE).fill(0);
    for (let a1 = 0; a1 <= AWAY; a1++) {
      for (let a2 = 0; a2 <= AWAY; a2++) {
        if (a1 === 0) g[idx(a1, a2)] = a2 === 0 ? 0 : 1;
        else if (a2 === 0) g[idx(a1, a2)] = 0;
        else g[idx(a1, a2)] = 1 - gridFirst[idx(a2, a1)];
      }
    }
    return g;
  }
  var MAX_HANDS = 2 ** 20;

  // ../mnt/DominoBot/spectacular-bid/src/lib/metStore.js
  var MET = null;
  function setMatchEquity(grids) {
    MET = grids && grids.first && grids.second ? grids : null;
  }
  var away = (score, target) => Math.min(AWAY, Math.max(0, Math.ceil((target - score) / 5)));
  function metEquity(botScore, oppScore, target, botLeadsNext) {
    if (!MET) return null;
    const a = away(botScore, target), b = away(oppScore, target);
    if (a === 0) return 1;
    if (b === 0) return 0;
    const k = idx(a, b);
    if (botLeadsNext === true) return MET.first[k];
    if (botLeadsNext === false) return MET.second[k];
    return 0.5 * (MET.first[k] + MET.second[k]);
  }

  // ../mnt/DominoBot/spectacular-bid/src/lib/mcts/evaluation.js
  var K_SCALE = 50;
  var DOMINO_BONUS = 10;
  var DOMINO_BONUS_CAP = 145;
  function dynamicAlpha(myMatchScore, oppMatchScore, targetScore) {
    const progress = Math.max(myMatchScore, oppMatchScore) / Math.max(1, targetScore);
    if (progress < 0.4) return 0.35;
    if (progress < 0.75) return 0.6;
    return 0.9;
  }
  function rescaledUtility(myPoints, oppPoints) {
    const D = myPoints - oppPoints;
    const uRaw = Math.tanh(D / K_SCALE);
    return (uRaw + 1) / 2;
  }
  function estimateWinProbability(state) {
    const botChips = Math.floor(state.botScore / 5);
    const oppChips = Math.floor(state.oppScore / 5);
    const targetChips = Math.max(1, Math.floor(state.targetScore / 5));
    const chipAdv = botChips - oppChips;
    const botPips = countPips(state.botHand);
    const oppPips = countPips(state.oppHand);
    const pipAdv = oppPips - botPips;
    const progress = Math.max(botChips, oppChips) / targetChips;
    const chipWeight = 6 * (1.1 - progress);
    const z = chipAdv / chipWeight + pipAdv / 40;
    return Math.max(0, Math.min(1, 1 / (1 + Math.exp(-z))));
  }
  function evaluateRollout(state) {
    if (state.botScore >= state.targetScore) return 1;
    if (state.oppScore >= state.targetScore) return 0;
    if (state.handOver) {
      const lead = state.nextLeader === 0 ? true : state.nextLeader === 1 ? false : null;
      const eq = metEquity(state.botScore, state.oppScore, state.targetScore, lead);
      if (eq != null) return eq;
    }
    const dominoed = state.winner != null && (state.botHand.length === 0 || state.oppHand.length === 0);
    let botScore = state.botScore;
    let oppScore = state.oppScore;
    if (dominoed) {
      if (state.winner === 0) botScore = Math.min(botScore + DOMINO_BONUS, DOMINO_BONUS_CAP);
      else oppScore = Math.min(oppScore + DOMINO_BONUS, DOMINO_BONUS_CAP);
    }
    const alpha = dynamicAlpha(botScore, oppScore, state.targetScore);
    const W = estimateWinProbability({ ...state, botScore, oppScore });
    const U = rescaledUtility(botScore, oppScore);
    return alpha * W + (1 - alpha) * U;
  }

  // ../mnt/DominoBot/spectacular-bid/src/lib/mcts/bidding.js
  init_gameUtils();
  function handStrengthForBid(hand) {
    let s = 0;
    const suitCount = new Array(7).fill(0);
    for (const t of hand) {
      s += t.top + t.bottom;
      if (t.top === t.bottom) s += 4;
      suitCount[t.top]++;
      if (t.top !== t.bottom) suitCount[t.bottom]++;
    }
    if (suitCount.some((c) => c >= 4)) s += 6;
    return s;
  }
  function getBiddingActions(state, opts = {}) {
    const currentBid = state.contract?.amount ?? 0;
    const isDominos = opts.isDominosPlayer ?? false;
    const oppScore = opts.oppScore ?? state.oppScore ?? 0;
    const targetScore = opts.targetScore ?? state.targetScore ?? 150;
    const cap = maxSafeBid(oppScore, targetScore, isDominos);
    const actions = [{ type: "PASS" }];
    const floor = Math.max(currentBid + 1, 3);
    const cands = /* @__PURE__ */ new Set([floor, floor + 1, floor + 2, cap]);
    for (const n of [...cands].sort((a, b) => a - b)) {
      if (n >= floor && n <= cap) actions.push({ type: "BID", amount: n });
    }
    return actions;
  }
  function dominosIndex(state) {
    const myIdx = state.myPlayerIndex ?? 0;
    return state.botIsDominos ? myIdx : 1 - myIdx;
  }
  function applyBiddingAction(state, action, actorIndex) {
    const priorBid = state.contract?.amount ?? 0;
    if (action.type === "BID" && action.amount < Math.max(priorBid + 1, 3)) action = { type: "PASS" };
    if (action.type === "PASS") {
      if (priorBid > 0) {
        return {
          ...state,
          phase: "bidding_terminal",
          contract: {
            amount: priorBid,
            holderIndex: state.contract.holderIndex,
            winnerIndex: actorIndex,
            // acceptor (the passer)
            resolved: true
          },
          currentPlayer: actorIndex === 0 ? 1 : 0
        };
      }
      const domIdx = dominosIndex(state);
      if (actorIndex !== domIdx && !state.contract?.openerPassed) {
        return {
          ...state,
          phase: "bidding",
          contract: { amount: 0, holderIndex: null, resolved: false, openerPassed: true },
          currentPlayer: domIdx
        };
      }
      return {
        ...state,
        phase: "bidding_terminal",
        contract: { amount: 0, holderIndex: null, winnerIndex: null, resolved: true },
        currentPlayer: domIdx
      };
    }
    return {
      ...state,
      phase: "bidding",
      contract: { amount: action.amount, holderIndex: actorIndex, resolved: false, openerPassed: !!state.contract?.openerPassed },
      bidHistory: [...state.bidHistory || [], { actor: actorIndex, action: "BID", amount: action.amount }],
      currentPlayer: actorIndex === 0 ? 1 : 0
    };
  }
  function biddingRolloutPolicy(state, hand, opts = {}) {
    const currentBid = state.contract?.amount ?? 0;
    const isDominos = opts.isDominosPlayer ?? false;
    const oppScore = opts.oppScore ?? state.oppScore ?? 0;
    const targetScore = opts.targetScore ?? state.targetScore ?? 150;
    const cap = maxSafeBid(oppScore, targetScore, isDominos);
    if (cap < 3) return { type: "PASS" };
    const nextBid = Math.max(currentBid + 1, 3);
    if (nextBid > cap) return { type: "PASS" };
    const strength = handStrengthForBid(hand);
    const threshold = 45 + nextBid * 3 - (isDominos ? 10 : 0);
    const holdsKeyDouble = hand.some((t) => t.top === t.bottom && t.top >= 5);
    const bluff = Math.random() < 0.04;
    if (strength >= threshold || bluff && holdsKeyDouble) {
      return { type: "BID", amount: nextBid };
    }
    return { type: "PASS" };
  }
  function evaluateBiddingTerminal(state) {
    const c = state.contract;
    if (!c || !c.resolved) return null;
    if (c.amount === 0 || c.winnerIndex == null) {
      return { botContractPts: 0, oppContractPts: 0 };
    }
    const myIdx = state.myPlayerIndex ?? 0;
    const bidderIsBot = c.holderIndex === myIdx;
    const bidderIsDominos = bidderIsBot ? !!state.botIsDominos : !state.botIsDominos;
    const gross = c.amount * 5;
    const pts = bidderIsDominos ? Math.max(0, gross - 15) : gross;
    const acceptorIsBot = c.winnerIndex === myIdx;
    return acceptorIsBot ? { botContractPts: pts, oppContractPts: 0 } : { botContractPts: 0, oppContractPts: pts };
  }

  // ../mnt/DominoBot/spectacular-bid/src/lib/mcts/engine.js
  var SQRT2 = 1.41421356;
  function cloneState(state) {
    return {
      botHand: state.botHand.map((t) => ({ ...t })),
      oppHand: state.oppHand.map((t) => ({ ...t })),
      boneyard: (state.boneyard || []).map((t) => ({ ...t })),
      boardState: cloneBoardState(state.boardState),
      boardEnds: [...state.boardEnds],
      botScore: state.botScore,
      oppScore: state.oppScore,
      targetScore: state.targetScore,
      currentPlayer: state.currentPlayer,
      passHistory: { opponent: [...state.passHistory?.opponent || []] },
      bidHistory: [...state.bidHistory || []],
      turnIndex: state.turnIndex || 0,
      contract: state.contract ? { ...state.contract } : null,
      phase: state.phase || "playing",
      myPlayerIndex: state.myPlayerIndex ?? 0,
      botIsDominos: !!state.botIsDominos,
      terminal: state.terminal || false,
      winner: state.winner
    };
  }
  function getValidPlayMoves(state) {
    const hand = state.currentPlayer === 0 ? state.botHand : state.oppHand;
    const moves = [];
    for (const tile of hand) {
      const ends = getPlayableEnds(tile, state.boardEnds);
      for (const end of ends) {
        const actualEnd = end === "any" ? "right" : end;
        moves.push({ type: "PLAY", tile, end: actualEnd });
      }
    }
    return moves;
  }
  function getActions(state) {
    if (state.phase === "bidding") {
      const isBot = state.currentPlayer === 0;
      return getBiddingActions(state, {
        isDominosPlayer: isBot ? !!state.botIsDominos : !state.botIsDominos,
        oppScore: isBot ? state.oppScore : state.botScore,
        targetScore: state.targetScore
      });
    }
    const moves = getValidPlayMoves(state);
    if (moves.length > 0) return moves;
    return [state.boneyard.length > 0 ? { type: "DRAW" } : { type: "PASS_TURN" }];
  }
  function isForcedTurnAction(a) {
    return a && (a.type === "DRAW" || a.type === "PASS_TURN");
  }
  function applyMoveToState(state, move) {
    const ns = cloneState(state);
    const isBot = ns.currentPlayer === 0;
    const hand = isBot ? ns.botHand : ns.oppHand;
    const newHand = hand.filter((t) => t.id !== move.tile.id);
    if (isBot) ns.botHand = newHand;
    else ns.oppHand = newHand;
    const { newBs, newBe } = applyTileToBoard(ns.boardState, ns.boardEnds, move.tile, move.end);
    ns.boardState = newBs;
    ns.boardEnds = newBe;
    const scoreEarned = calculateScoreFromBoardState(newBs);
    if (scoreEarned > 0) {
      if (isBot) ns.botScore += scoreEarned;
      else ns.oppScore += scoreEarned;
    }
    if (newHand.length === 0) {
      ns.terminal = true;
      ns.winner = ns.currentPlayer;
    } else if (isBot ? ns.botScore >= ns.targetScore : ns.oppScore >= ns.targetScore) {
      ns.terminal = true;
      ns.winner = ns.currentPlayer;
    }
    ns.currentPlayer = isBot ? 1 : 0;
    ns.turnIndex = (ns.turnIndex || 0) + 1;
    return ns;
  }
  function applyAction(state, action) {
    if (action.type === "PLAY") return applyMoveToState(state, action);
    if (action.type === "DRAW") {
      const ns = cloneState(state);
      const drawn = ns.boneyard.pop();
      if (ns.currentPlayer === 0) ns.botHand.push(drawn);
      else ns.oppHand.push(drawn);
      return ns;
    }
    if (action.type === "PASS_TURN") {
      const ns = cloneState(state);
      ns.currentPlayer = ns.currentPlayer === 0 ? 1 : 0;
      return ns;
    }
    return resolveContract(applyBiddingAction(state, action, state.currentPlayer));
  }
  function resolveContract(s) {
    if (s.phase !== "bidding_terminal") return s;
    const ev = evaluateBiddingTerminal(s);
    if (ev) {
      s.botScore += ev.botContractPts || 0;
      s.oppScore += ev.oppContractPts || 0;
    }
    s.phase = "playing";
    return s;
  }
  function isTerminal(state) {
    if (state.terminal) return true;
    if (state.botScore >= state.targetScore || state.oppScore >= state.targetScore) return true;
    if (state.phase === "bidding_terminal") return true;
    if (state.boneyard.length === 0) {
      const botBlocked = !state.botHand.some((t) => canPlayTile(t, state.boardEnds));
      const oppBlocked = !state.oppHand.some((t) => canPlayTile(t, state.boardEnds));
      if (botBlocked && oppBlocked && state.botHand.length > 0 && state.oppHand.length > 0) return true;
    }
    return false;
  }
  function settleEndOfRound(s) {
    const targetWin = s.botScore >= s.targetScore || s.oppScore >= s.targetScore;
    if (targetWin) return;
    const dominoed = s.winner != null && (s.botHand.length === 0 || s.oppHand.length === 0);
    const blocked = isTerminal(s) && !dominoed && s.botHand.length > 0 && s.oppHand.length > 0;
    s.nextLeader = null;
    if (dominoed && s.winner != null) {
      s.nextLeader = s.winner;
      const loserHand = s.winner === 0 ? s.oppHand : s.botHand;
      const pts = Math.round(countPips(loserHand) / 5) * 5;
      if (pts > 0) {
        if (s.winner === 0) s.botScore += pts;
        else s.oppScore += pts;
      }
    } else if (blocked) {
      const botPips = countPips(s.botHand);
      const oppPips = countPips(s.oppHand);
      if (botPips < oppPips) {
        s.botScore += Math.round(oppPips / 5) * 5;
        s.nextLeader = 0;
      } else if (oppPips < botPips) {
        s.oppScore += Math.round(botPips / 5) * 5;
        s.nextLeader = 1;
      }
    }
    if (s.botScore >= s.targetScore) s.winner = 0;
    else if (s.oppScore >= s.targetScore) s.winner = 1;
  }
  function weightedRandomPlayMove(state, weights) {
    const moves = getValidPlayMoves(state);
    if (moves.length === 0) return null;
    if (moves.length === 1) return moves[0];
    const hand = state.currentPlayer === 0 ? state.botHand : state.oppHand;
    let total = 0;
    const weightsArr = new Array(moves.length);
    for (let i = 0; i < moves.length; i++) {
      const m = moves[i];
      let w = 1;
      if (m.tile.top === m.tile.bottom) w *= weights.heavyDouble;
      const remaining = hand.filter((t) => t.id !== m.tile.id);
      const suits = /* @__PURE__ */ new Set();
      for (const t of remaining) {
        suits.add(t.top);
        suits.add(t.bottom);
      }
      w *= 1 + weights.diversity * (suits.size / 7);
      w = Math.max(0.01, w);
      weightsArr[i] = w;
      total += w;
    }
    let r = Math.random() * total;
    for (let i = 0; i < moves.length; i++) {
      r -= weightsArr[i];
      if (r <= 0) return moves[i];
    }
    return moves[moves.length - 1];
  }
  function findImmediateWinMove(state) {
    const moves = getValidPlayMoves(state);
    if (moves.length === 0) return null;
    const isBot = state.currentPlayer === 0;
    const hand = isBot ? state.botHand : state.oppHand;
    if (hand.length === 1) return moves[0];
    const currentScore = isBot ? state.botScore : state.oppScore;
    for (const m of moves) {
      const { newBs } = applyTileToBoard(state.boardState, state.boardEnds, m.tile, m.end);
      const scoreEarned = calculateScoreFromBoardState(newBs);
      if (currentScore + scoreEarned >= state.targetScore) return m;
    }
    return null;
  }
  function simulate(state, weights) {
    let s = cloneState(state);
    if (s.phase === "bidding") {
      let depth2 = 0;
      while (s.phase === "bidding" && depth2 < 12 && !isTerminal(s)) {
        const isBot = s.currentPlayer === 0;
        const hand = isBot ? s.botHand : s.oppHand;
        const action = biddingRolloutPolicy(s, hand, {
          isDominosPlayer: isBot ? !!s.botIsDominos : !s.botIsDominos,
          oppScore: isBot ? s.oppScore : s.botScore,
          targetScore: s.targetScore
        });
        s = applyBiddingAction(s, action, s.currentPlayer);
        depth2++;
      }
    }
    s = resolveContract(s);
    const maxDepth = 200;
    let depth = 0;
    while (!isTerminal(s) && depth < maxDepth) {
      let move = findImmediateWinMove(s) || weightedRandomPlayMove(s, weights);
      if (!move) {
        if (s.boneyard.length > 0) {
          const drawn = s.boneyard.pop();
          if (s.currentPlayer === 0) s.botHand.push(drawn);
          else s.oppHand.push(drawn);
        } else {
          s.currentPlayer = s.currentPlayer === 0 ? 1 : 0;
        }
        depth++;
        continue;
      }
      s = applyMoveToState(s, move);
      depth++;
    }
    settleEndOfRound(s);
    s.handOver = isTerminal(s);
    return evaluateRollout(s);
  }
  function isExactSolvable(state) {
    if (!state || state.phase === "bidding") return false;
    if ((state.boneyard || []).length > 0) return false;
    const nBot = state.botHand.length;
    const nOpp = state.oppHand.length;
    return nOpp <= 3 && nBot <= 5 && nBot + nOpp <= 7;
  }
  function settledValue(state) {
    const s = cloneState(state);
    settleEndOfRound(s);
    s.handOver = true;
    return evaluateRollout(s);
  }
  function solveExact(state, alpha, beta, depth) {
    if (state.botScore >= state.targetScore || state.oppScore >= state.targetScore) {
      return evaluateRollout(state);
    }
    const moves = getValidPlayMoves(state);
    if (moves.length === 0) {
      if ((state.boneyard || []).length === 0 && !state.botHand.some((t) => canPlayTile(t, state.boardEnds)) && !state.oppHand.some((t) => canPlayTile(t, state.boardEnds))) {
        return settledValue(state);
      }
      const passed = cloneState(state);
      passed.currentPlayer = passed.currentPlayer === 0 ? 1 : 0;
      return solveExact(passed, alpha, beta, depth + 1);
    }
    if (depth > 60) return settledValue(state);
    const maximizing = state.currentPlayer === 0;
    let value = maximizing ? -Infinity : Infinity;
    for (const m of moves) {
      const ns = applyMoveToState(state, m);
      const v = ns.terminal ? settledValue(ns) : solveExact(ns, alpha, beta, depth + 1);
      if (maximizing) {
        if (v > value) value = v;
        if (value > alpha) alpha = value;
        if (alpha >= beta) return beta;
      } else {
        if (v < value) value = v;
        if (value < beta) beta = value;
        if (beta <= alpha) return alpha;
      }
    }
    return value;
  }
  function exactBlockCandidate(rootState) {
    if (!rootState || rootState.phase === "bidding") return null;
    if ((rootState.boneyard || []).length > 0) return null;
    let bestBlock = null;
    for (const m of getValidPlayMoves(rootState)) {
      const ns = applyAction(rootState, m);
      if (ns.terminal || ns.boneyard.length > 0) continue;
      const botBlocked = !ns.botHand.some((t) => canPlayTile(t, ns.boardEnds));
      const oppBlocked = !ns.oppHand.some((t) => canPlayTile(t, ns.boardEnds));
      if (!botBlocked || !oppBlocked) continue;
      const value = settledValue(ns);
      if (!bestBlock || value > bestBlock.value) bestBlock = { move: m, value };
    }
    return bestBlock;
  }
  var MCTSNode = class {
    constructor(state, parent = null, move = null) {
      this.state = state;
      this.parent = parent;
      this.move = move;
      this.children = [];
      this.visits = 0;
      this.reward = 0;
      this.sumSqReward = 0;
      this.untriedMoves = isTerminal(state) ? [] : getActions(state);
    }
    ucb1(C) {
      if (this.visits === 0) return Infinity;
      const chooser = this.parent.state.currentPlayer;
      const mean = this.reward / this.visits;
      const exploit = chooser === 1 ? 1 - mean : mean;
      return exploit + C * Math.sqrt(Math.log(this.parent.visits) / this.visits);
    }
  };
  function selectChild(node, C) {
    let best = null;
    let bestScore = -Infinity;
    for (const child of node.children) {
      const score = child.ucb1(C);
      if (score > bestScore) {
        bestScore = score;
        best = child;
      }
    }
    return best;
  }
  function backpropagate(node, result) {
    let current = node;
    while (current) {
      current.visits++;
      current.reward += result;
      current.sumSqReward += result * result;
      current = current.parent;
    }
  }
  function createSearch(rootState, opts = {}) {
    const weights = { ...DEFAULT_WEIGHTS, ...opts.weights || {} };
    const C = opts.explorationConstant || SQRT2;
    const root = new MCTSNode(rootState);
    let iterations = 0;
    if (isExactSolvable(rootState) && rootState.currentPlayer === 0) {
      const moves = getValidPlayMoves(rootState);
      if (moves.length > 1) {
        const ranking = moves.map((m) => {
          const ns = applyAction(rootState, m);
          const v = ns.terminal ? settledValue(ns) : solveExact(ns, -Infinity, Infinity, 0);
          return { move: m, value: v };
        }).sort((a, b) => b.value - a.value);
        const fakeRoot = {
          children: ranking.map((r) => ({ move: r.move, visits: 1, reward: r.value }))
        };
        return {
          done: true,
          step() {
          },
          best: () => ranking[0].move,
          topTwo: () => [],
          iterations: () => 0,
          root: () => fakeRoot
        };
      }
    }
    if (root.untriedMoves.length === 1 && root.children.length === 0) {
      const only = root.untriedMoves[0];
      return {
        done: true,
        step() {
        },
        best: () => isForcedTurnAction(only) ? null : only,
        topTwo: () => [],
        iterations: () => 0,
        root: () => root
      };
    }
    if (root.untriedMoves.length === 0) {
      return {
        done: true,
        step() {
        },
        best: () => null,
        topTwo: () => [],
        iterations: () => 0,
        root: () => root
      };
    }
    function step(k = 100) {
      for (let i = 0; i < k; i++) {
        iterations++;
        let node = root;
        while (node.untriedMoves.length === 0 && node.children.length > 0) {
          node = selectChild(node, C);
        }
        if (node.untriedMoves.length > 0 && !isTerminal(node.state)) {
          const move = node.untriedMoves.pop();
          const ns = applyAction(node.state, move);
          const child = new MCTSNode(ns, node, move);
          node.children.push(child);
          node = child;
        }
        const result = simulate(node.state, weights);
        backpropagate(node, result);
      }
    }
    function topTwo() {
      const sorted = [...root.children].sort((a, b) => b.visits - a.visits);
      return [sorted[0], sorted[1]];
    }
    function best() {
      if (root.children.length === 0) return root.untriedMoves[0] || null;
      const block = exactBlockCandidate(rootState);
      if (block) {
        let bestVis = -1;
        let bestNode0 = null;
        for (const c of root.children) {
          if (c.visits > bestVis) {
            bestVis = c.visits;
            bestNode0 = c;
          }
        }
        if (bestNode0 && bestNode0.visits > 0 && block.value > bestNode0.reward / bestNode0.visits) {
          return block.move;
        }
      }
      let bestNode = null;
      let bestVisits = -1;
      for (const c of root.children) {
        if (c.visits > bestVisits) {
          bestVisits = c.visits;
          bestNode = c;
        }
      }
      return bestNode ? bestNode.move : null;
    }
    return { done: false, step, best, topTwo, iterations: () => iterations, root: () => root };
  }

  // ../mnt/DominoBot/spectacular-bid/src/lib/mcts/sprt.js
  var SPRT_ALPHA = 0.05;
  var SPRT_BETA = 0.05;
  var SPRT_DELTA = 0.05;
  var SPRT_A_BOUND = Math.log((1 - SPRT_BETA) / SPRT_ALPHA);
  var SPRT_B_BOUND = Math.log(SPRT_BETA / (1 - SPRT_ALPHA));
  function nodeMean(node) {
    return node.visits > 0 ? node.reward / node.visits : 0;
  }
  function nodeVariance(node) {
    if (node.visits <= 1) return 0;
    const mean = node.reward / node.visits;
    const v = node.sumSqReward / node.visits - mean * mean;
    return v > 0 ? v : 0;
  }
  function evaluateSPRT(nodeA, nodeB) {
    if (!nodeA || !nodeB || nodeA.visits < 2 || nodeB.visits < 2) {
      return { decision: "CONTINUE", winner: null };
    }
    const meanA = nodeMean(nodeA);
    const meanB = nodeMean(nodeB);
    const varA = nodeVariance(nodeA);
    const varB = nodeVariance(nodeB);
    const dHat = meanA - meanB;
    const varTheta = varA / nodeA.visits + varB / nodeB.visits;
    if (varTheta <= 1e-9) {
      return { decision: "CONTINUE", winner: null };
    }
    const llr = 2 * SPRT_DELTA * dHat / varTheta;
    if (llr >= SPRT_A_BOUND) return { decision: "CONVERGED", winner: nodeA };
    if (llr <= SPRT_B_BOUND) return { decision: "INCONCLUSIVE", winner: nodeA };
    return { decision: "CONTINUE", winner: null };
  }
  return __toCommonJS(entry_exports);
})();
