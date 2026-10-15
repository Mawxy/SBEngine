# Spectacular Bid Engine

C++20 engine for Spectacular Bid. Rules are defined in `../engine-rules-spec.md`.

## How it finds the best move

The search takes after Stockfish, adapted for hidden tiles.

Stockfish searches one fully known position. In Spectacular Bid the opponent's hand and the boneyard order are hidden, so the engine uses **Perfect Information Monte Carlo (PIMC)**, the approach used by strong bridge and skat engines:

1. **Sample.** Deal out many versions of the hidden tiles (determinizations) that are consistent with everything the player has seen and inferred.
2. **Search each one, Stockfish-style.** Run a negamax alpha-beta search with:
   - iterative deepening
   - a transposition table (Zobrist-style hash; solved subtrees are reused at any depth)
   - move ordering: transposition-table move, then scoring plays, then heavy tiles
   - a static evaluation at the horizon (pip and tile-count difference, tunable)
   - draws and passes that cost no depth, like Stockfish's extensions for forced moves
3. **Average.** Each root move's value is averaged across samples, with a standard error.

**Evaluation:** net chips from now to the end of the current hand. Inside the search values are from the mover's side (negamax); everything the engine reports is from **Seat 1's side: + favors Seat 1, − favors Seat 2**, like chess engines reporting from White's side. The search adds a configurable bonus for dominoing (the jockey seat) and for winning the game.

**Known PIMC limitation:** inside one sample the search "knows" the opponent's tiles and the draw order (strategy fusion). Averaging over samples is the standard fix; an information-set search can replace this later.

### Inference about the opponent
- Drawing or passing proves the opponent held nothing matching the open ends at that moment.
- Forced leads (first hand, or after a block) cap which tiles the opponent can hold.
- Every sample honors these constraints. Self-play checks that inference never rules out a tile the opponent actually holds.

### Bidding
`analyze_bid` estimates the hand's value if I lead and if the opponent leads. It then computes a walk-away bid: the highest bid where winning the lead still beats conceding at the previous bid, using each side's cost (jockey credit included).

## Ways to use it
| | Browser | Windows app |
|---|---|---|
| Analyze a game at the table (your seat, you enter moves) | yes | yes |
| Play against the engine (it deals, hides its tiles; hints optional) | yes | yes |
| Bot vs bot: SB Engine vs HaviEngine, watch or run a series | yes | yes |
| Chess-style eval bar (toggle; sees both hands in bot games) | yes | yes |
| Engine runs | WebAssembly in the page | native, all cores |

- **Browser:** open `gui/spectacular-bid.html` (works offline) or the published link.
- **Windows:** run `desktop/SpectacularBid.exe`. It opens the same GUI in an Edge app window and sends every search to the native engine. It closes itself about 20 seconds after you close the window. Nothing to install.
- **Bot vs bot series:** from the New game screen, pick a game count and press Run series (no need to watch a game first). In the Windows app our side is the native engine with the trained models.
- **Batch bot vs bot (Node 18+), native engine with trained models:** build `sbcli`, then `node arena/run_native.mjs --sbcli build/sbcli --models <path>/training/promoted --games 40` (`--havi current` by default; `shipped` / `fixed` for the old bot; `--ours random` as a harness sanity check).
- **Batch bot vs bot, WebAssembly engine (untrained):** `node arena/run_arena.mjs 20 32 20000 1000 1 shipped` (games, our deals, our nodes, HaviEngine ms per move, seed, `shipped` or `fixed`).

## Code
- `src/tile.h`: tile ids, masks, pip helpers, portable shuffle (same deals on every platform)
- `src/layout.h/.cpp`: single-spinner layout, legal targets, All Fives count
- `src/game.h/.cpp`: full referee state (dealing, lead, bidding, play, scoring, hand and game end)
- `src/view.h/.cpp`: one player's view, inference, determinization
- `src/search.h/.cpp`: alpha-beta searcher, PIMC move analysis, bid analysis (multithreaded with `SB_THREADS`)
- `src/session.h/.cpp`: text command interface (like UCI for chess) with notation parsing; every front end talks to the engine through it. Modes: analysis (`new`), play the engine (`match`), bot vs bot (`arena`)
- `tests/test_rules.cpp`: rule scenarios, inference checks, search versus brute-force minimax, random self-play invariants
- `tools/selfplay.cpp`: engine versus greedy baseline on mirrored deals
- `tools/sbcli.cpp`: command-line front end, one command in, one JSON reply out; `sbcli <models_dir>` loads trained models
- `src/nnue.h/.cpp`, `src/nnue_weights.cpp`: NNUE network and the compiled-in weights (none yet)
- `src/mlp.h/.cpp`: small sparse-input network and multithreaded trainer; `src/models.h/.cpp`: bidding and info-set features, the set of models in use, loading
- `tools/sbtrain.cpp`: unattended training (self-play data, training, head-to-head gates, resumable)
- `tools/gendata.cpp`, `tools/train_nnue.cpp`: the first NNUE experiment's standalone tools; `tools/bidstudy.cpp`: bid values over random hands
- `tools/desktop.cpp`: the Windows app (local server + Edge app window)
- `wasm/`: WebAssembly entry points and build script
- `gui/`: browser GUI (`index.template.html`); `build.py` embeds the WebAssembly engine and HaviEngine
- `arena/`: HaviEngine adapters and bundles (current `5d092f9` with `havi_adapter_v2.js`; old `3a45a1c` as shipped and with the sign fix, with `havi_adapter.js`), batch runners
- `desktop/`: Windows build script and the built `SpectacularBid.exe`

## Build
Native (CMake: Visual Studio, MinGW, Linux):

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

Browser page (needs the WASI sysroot; see the script header), then the Windows app (needs `pip install ziglang==0.13.0`):

```
wasm/build_wasm.sh
desktop/build_windows.sh
```

HaviEngine bundles are rebuilt with esbuild from the HaviEngine repo's `src`, as an IIFE with global name `HaviEngine`.
The current bundle (`havi-bundle-5d092f9.js`) exports `createSearch` (`lib/mcts/engine`), `buildDeterminizedStateV2`, `evaluateSPRT`, `DEFAULT_WEIGHTS` (`lib/mctsEngine`), `setMatchEquity` (`lib/metStore`), `buildBaseline` and `deriveSecond` (`lib/met`):

```
npx esbuild entry.js --bundle --format=iife --global-name=HaviEngine --alias:@=<HaviEngine>/src --platform=neutral --outfile=arena/havi-bundle-5d092f9.js
```

After changing the GUI template or an arena file, rebuild the page with `python gui/build.py wasm/sbengine.wasm arena gui/spectacular-bid.html --standalone`, then `desktop/build_windows.sh` for the exe.

## Training on your PC (48 hours)
`desktop/sbtrain.exe` trains three models by self-play and keeps a new generation only if it beats the current best head to head:
- the NNUE evaluation
- the information-set value network
- the bidding model

Start it with `desktop/Start-Training.bat`. Full guide: `desktop/TRAINING.md`. `SpectacularBid.exe` loads the promoted models from `desktop/training/promoted` at startup.

In a 12-minute check on 2 cores, the info-set network was promoted in its first generation: **+0.34 chips per hand** (se 0.13) over 581 mirrored pairs.

## Learning (NNUE)
Same recipe as Stockfish's NNUE: generate positions by self-play, label them, train a network, and use it at the search horizon.

- `tools/gendata.cpp` plays hands with the engine (15% random moves for variety). It records every play-phase position: the network inputs, a perfect-information search label, and the chips the side to move actually won. Example: `gendata gen1.bin 40000 11 20000` makes about 840,000 positions in 30 minutes on 2 cores.
- `tools/train_nnue.cpp` trains the 165 → 128 → 32 → 1 network (Adam, target = 0.75 × search label + 0.25 × result). With `--cpp src/nnue_weights.cpp` it compiles the network into the engine.
- `selfplay ab <net.bin>` plays the network against the hand-written evaluation on mirrored hands with the same search budget.

**Generation 1 (`nnue/net1.bin`):**
- 840,000 positions, 12 epochs.
- Predicts outcomes far better than the hand-written evaluation (held-out error 16.5 vs 37.5).
- No measurable playing gain yet: +0.02 chips per hand (se 0.15) over 700 mirrored pairs. The search already solves most of each hand exactly, so the horizon evaluation rarely decides a move.
- The built-in evaluation stays hand-written until a network wins its A/B test.

**Most promising next targets:**
- Bidding: see below.
- An information-set value network that judges a position from what the mover can actually see, which addresses PIMC's main weakness.

## Bidding
- **Notation:** `b<n>` bids n, `b0` is the opener's pass, `a` accepts.
- **Measured lead value:** the same deal was played twice, once with each seat leading (500 deals, engine on both sides). The lead was worth **1.9 chips** (se 0.4).
- **The raw estimate was too high:**
  - The raw PIMC estimate averaged 6.5 chips, because in every sampled deal both sides see all the tiles.
  - Fixing the leader's choice to "best tile on average" brought it to 4.9 chips.
- **Calibration:** the engine now shrinks the lead gap by a factor of 0.4 (`SearchParams::bid_calibration`).
  - Calibrated bidding beat raw bidding **115–85 in full games**, with an average bid of 2.4 instead of 3.7.
- **Rule of thumb from 1,200 sampled hands:**
  - Doubles are the strongest signal of a valuable lead. A 10-point lead tile ([5-5] or [6-4]) is next.
  - Total pips hardly matter.

## HaviEngine notes
- **Current version (5d092f9, 2026-10-12):** the sign bug below is fixed (it beats a random mover 8–2 in our harness), it scores by match equity, bids by best mean equity with no SPRT, and searches its free lead. `arena/havi_adapter_v2.js` follows its GameRoom. It uses the baseline match equity table, which is what its GameRoom falls back to when its saved table (on its server) can't be loaded.
- The notes below are about the old version (3a45a1c).
- The adapter (`arena/havi_adapter.js`) plays HaviEngine the way its GameRoom does: MCTS v2 with one determinization per move, 1000 ms with SPRT early stop, its MCTS bidding, and a first-tile free lead. Our engine referees with our rules.
- The board translation was checked on 2,110 positions: HaviEngine's pip count and playable tiles match ours every time.
- **Sign bug in HaviEngine:** `src/lib/mcts/engine.js:440` flips the reward when `currentPlayer === 1`; it should be `=== 0`. As shipped, HaviEngine lost 2–18 to a player choosing random legal moves. With only that line changed it won 12–8. The "with sign fix" option uses that one-line change; the shipped repo is untouched.

## Results so far
- Search matches exact brute-force minimax on 300 random small positions.
- Engine vs greedy baseline (most points now, then heaviest tile): **+2.05 chips per hand** (standard error 0.19) over 300 mirrored deal pairs. That run used 32 samples × 20k nodes, about 23 ms per decision.
- Zero view or inference errors across 5,800 engine decisions.
- SB Engine (Standard: 32 deals × 20k nodes) vs HaviEngine at 1000 ms per move, 20 full games each, seats alternating:
  - HaviEngine as shipped: **19–1**, +5.89 chips per hand (se 0.74)
  - HaviEngine with the sign fix: **16–4**, +3.52 chips per hand (se 1.06)

## Status
- [x] Rules core
- [x] Player view and inference
- [x] PIMC plus alpha-beta search, bid analysis
- [x] Notation and session command interface
- [x] Browser GUI with the engine built in (live play, bidding, game log, record export)
- [x] Desktop app (same GUI, native multithreaded engine)
- [x] Play against the engine
- [x] Bot vs bot against HaviEngine (watch, series, batch runner)
- [ ] Review mode (load a .sbg record and grade each decision)
- [x] NNUE pipeline (data, training, A/B); generation 1 trained
- [x] Bidding calibrated against measured lead value
- [x] 48-hour training program: NNUE generations, info-set network, learned bidding (gated)
- [x] First training run (36 h): NNUE, info network and bidding promoted; NNUE flat after generation 1
- [ ] Round 2 recipe (horizon-position NNUE, 256-64 info network, cleaner self-play): `desktop/Start-Training-Round2.bat`, see `desktop/TRAINING.md`
- [ ] Bake the trained models into the browser version
#   S B E n g i n e  
 