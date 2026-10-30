# Spectacular Bid Engine

A Stockfish-style engine for **Spectacular Bid**, a two-player All Fives dominoes game with bidding for the lead (rulebook by Christopher and Nora Haviland). It suggests the best move and gives an evaluation at any point in a game. It comes with a browser GUI, a Windows app, play against the engine, bot vs bot against HaviEngine, and a self-training pipeline.

Written in C++20. How to write moves and games: [docs/NOTATION.md](docs/NOTATION.md).

## Rules the engine plays
These settle points the printed rules leave open.
- Each player starts with 30 chips (1 chip = 5 points). Scores and bids are paid from your own pool, and chips you win go to a trophy pile that is never spent. You lose the moment your pool reaches 0, even mid-hand.
- Every placement scores when the open ends add up to a multiple of 5, the lead included. The first double is the only spinner.
- **Domino:** the player who goes out wins the opponent's leftover pips, rounded to the nearest 5, and becomes the **jockey**.
- **Blocked hand:** the lower pip count wins the opponent's total pips, rounded to the nearest 5. A tie scores nothing.
- The first hand, and any hand after a blocked hand, has no auction: the highest double leads.
- **Auction (after a domino):** the player who lost the hand opens with any bid, including 0, which passes and lets the jockey lead for free. Each raise is at least 1 chip, up to what you can afford. The jockey has a 3-chip credit and pays only the amount above 3. The high bidder pays the opponent and leads any tile.
- Still unconfirmed with the authors: the domino payout rounds to the nearest 5, and if a first hand has no doubles the heaviest tile leads.

## How it finds the best move
The search takes after Stockfish, adapted for hidden tiles. Stockfish searches one fully known position. In Spectacular Bid the opponent's hand and the boneyard order are hidden, so the engine uses **Perfect Information Monte Carlo (PIMC)**, the approach used by strong bridge and skat engines:

1. **Sample.** Deal out many versions of the hidden tiles that are consistent with everything the player has seen and inferred.
2. **Search each one, Stockfish-style.** Negamax alpha-beta with:
   - iterative deepening
   - a transposition table (solved subtrees are reused at any depth)
   - move ordering: transposition-table move, then scoring plays, then heavy tiles
   - an evaluation at the horizon (a trained NNUE network if one is loaded, otherwise pip and tile-count difference)
   - draws and passes that cost no depth, like Stockfish's extensions for forced moves
3. **Average.** Each move's value is averaged across samples, with a standard error. If a trained information-set network is loaded, it is blended in to correct for PIMC seeing every tile within a sample.

**Evaluation:** net chips from now to the end of the current hand. Everything the engine reports is from **Seat 1's side: + favors Seat 1, - favors Seat 2**, the way chess engines report from White's side.

**Inference about the opponent:**
- Drawing or passing proves the opponent held nothing matching the open ends at that moment.
- A forced lead (first hand, or after a block) caps which tiles the opponent can hold.
- Every sample honors these constraints. Self-play checks that inference never rules out a tile the opponent actually holds.

**Bidding:** `analyze_bid` estimates the hand's value if I lead and if the opponent leads, then computes a walk-away bid: the highest bid where winning the lead still beats conceding at the previous bid, counting each side's cost (jockey credit included). A trained bidding model replaces the raw lead estimate when loaded.

**Hand outcome:** along with chips, every move and position gets P(go out), P(win the hand) and P(opponent goes out). Each sample follows its search's best line to the end of the hand (transposition-table moves, then greedy play past the horizon) and records how the hand ended; averaging over samples gives the probabilities. They assume chip-maximizing play and share PIMC's optimism. In a 300-game self-play check (about 8,500 positions) the mover's-view numbers were well calibrated in the middle and a little overconfident at the extremes (predicted 7% happened 13% of the time, predicted 91% happened 86%).

**Game odds:** chips only matter for winning the game, so every move and position also gets P(win the game). Pools only go down (chips won go to a trophy pile that is never spent), so the game is a walk from 30-30 toward 0. `tools/sbequity.cpp` plays self-play hands at 30-30, records what each seat paid and who ends up jockey, and solves the whole game by dynamic programming into a **match equity table**: P(win) from the start of a hand for every pair of pools and jockey state (`src/met.h`, built in as `src/met_table.cpp`). Each search sample's line ends in a pool state, and the table turns that into a game win chance; averaging over samples gives the move's game odds. A `met.txt` in the models folder replaces the built-in table. For now the engine still *chooses* moves by chips; game odds are reported, not yet played for.

**All tiles visible (omniscient read):** with both hands known (games the app deals, or table games where both hands and every draw's tile are entered), the engine also evaluates the position as if all tiles were face up. Draws stay random: the search treats a draw as a chance event and averages over the tiles left in the boneyard (all of them at the first draw on a line, fewer at later ones, with the sampled deals covering the rest). It never plans around which tile comes next.

**Known limitation:** inside one sample the search knows the opponent's tiles and the draw order (strategy fusion). Averaging over samples and the information-set network reduce this but don't remove it.

## Ways to use it
| | Browser | Windows app |
|---|---|---|
| Analyze a game at the table (your seat, you enter moves) | yes | yes |
| Play against the engine (it deals, hides its tiles; hints optional) | yes | yes |
| Bot vs bot: SB Engine vs HaviEngine, watch or run a series | yes | yes |
| Chess-style eval bar (toggle; sees both hands in bot games) | yes | yes |
| Replay a game move by move with the engine's grade of each decision | yes | yes |
| Engine | WebAssembly in the page, untrained | native, all cores, trained models |

- **Browser:** open `gui/spectacular-bid.html` (works offline).
- **Windows:** run `SpectacularBid.exe`. It opens the GUI in an Edge app window and sends every search to the native engine. It closes about 20 seconds after you close the window. Nothing to install. At startup it loads trained models from `training2\promoted` next to the exe if that folder has models, otherwise from `training\promoted`.
- **Replay:** press **Replay** in the game log (or **Replay this game** at game over), or paste a saved `.sbg` on the New game screen. Step with the buttons or arrow keys. At each decision the engine shows the mover's view (what that seat could see) and, for games the app dealt, the full view with both hands known: chips, hand odds, its move list, and what the played move cost. **Analyze whole game** fills in every position and the evaluation graph, and marks inaccuracies, mistakes and blunders in the log.
- **Bot vs bot series:** on the New game screen, pick a game count and press Run series.
- **Batch bot vs bot, native engine (Node 18+):** build `sbcli`, then
  `node arena/run_native.mjs --sbcli build/sbcli --models <folder>/training/promoted --games 40`.
  Options: `--havi 5d092f9` (current HaviEngine, the default), `shipped` or `fixed` (old version); `--dets`, `--nodes`, `--havi-ms`, `--seed`; `--ours random` replaces our engine with a random mover as a harness check. Leave out `--models` for the untrained engine.
- **Batch bot vs bot, WebAssembly engine:** `node arena/run_arena.mjs 20 32 20000 1000 1 shipped` (games, our samples, our nodes, HaviEngine ms per move, seed, HaviEngine version).
- **Match equity table:** `sbequity --hands 30000 --threads 15 --dets 32 --nodes 20000 --dir equity` (resumable; `--build-only` rebuilds from `equity/hands.csv`). Writes `met.txt`, `met_table.cpp` and `REPORT.md`.
- **Command line:** `sbcli [models_folder]` reads one command per line and answers in JSON, like UCI for chess. Main commands: `new <seat>` (analyze from your seat), `match <seat> [seed]` (play the engine), `arena [seed]` (bot vs bot), `move <move>`, `analyze [samples] [nodes]`, `review [samples] [nodes]` (grade the decision at this point, mover's view and full view), `peek` (both hands, app-dealt games), `botchoice`, `next`, `undo`, `state`, `record`. See `src/session.h`.

## Build
**Native** (CMake: Visual Studio, MinGW, Linux). Builds the library, tests and command-line tools:

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

The CMake build searches on one thread. For multithreaded search, compile with `-DSB_THREADS`, as the Windows app build does. Direct compile without CMake also works:
`g++ -std=c++20 -O2 -pthread -Isrc src/*.cpp tools/sbcli.cpp -o sbcli`

**Browser page:** `wasm/build_wasm.sh` (needs clang and the WASI sysroot 24; see the script header). It builds `wasm/sbengine.wasm` and then the page. After changing only the GUI template or an arena file, rebuild just the page:
`python gui/build.py wasm/sbengine.wasm arena gui/spectacular-bid.html --standalone`

**Windows app and trainer:** `desktop/build_windows.sh` (needs `pip install ziglang==0.13.0`). It embeds the built page into `tools/gui_html.h` and writes `desktop/SpectacularBid.exe` and `desktop/sbtrain.exe`. Build the page first. The exes and `tools/gui_html.h` are build output and are not in the repo.

**HaviEngine bundles** are built with esbuild from the HaviEngine repo's `src`, as an IIFE with global name `HaviEngine`. The current bundle exports `createSearch`, `buildDeterminizedStateV2`, `evaluateSPRT`, `DEFAULT_WEIGHTS`, `setMatchEquity`, `buildBaseline` and `deriveSecond`:

```
npx esbuild entry.js --bundle --format=iife --global-name=HaviEngine --alias:@=<HaviEngine>/src --platform=neutral --outfile=arena/havi-bundle-5d092f9.js
```

## Code
| Path | What it is |
|---|---|
| `src/tile.h` | Tile ids, masks, pip helpers, portable shuffle (same deals on every platform) |
| `src/layout.*` | Single-spinner layout, legal targets, All Fives count |
| `src/game.*` | Full referee state: dealing, lead, auction, play, scoring, hand and game end |
| `src/view.*` | One player's view, inference, sampling hidden tiles |
| `src/search.*` | Alpha-beta search, PIMC move analysis, bid analysis |
| `src/session.*` | Text command interface and notation parsing; every front end talks to the engine through it |
| `src/nnue.*`, `src/nnue_weights.cpp` | NNUE network; no weights are compiled in, trained ones load from files |
| `src/mlp.*`, `src/models.*` | Small sparse network and trainer; bidding and info-set features; model loading |
| `tests/test_rules.cpp` | Rule scenarios, inference checks, search vs brute-force minimax, random self-play invariants |
| `tools/sbcli.cpp` | Command-line front end |
| `tools/desktop.cpp` | Windows app (local server + Edge app window) |
| `tools/sbtrain.cpp` | Unattended training: self-play data, training, head-to-head gates, resumable |
| `tools/selfplay.cpp` | Engine vs greedy baseline on mirrored deals; NNUE A/B |
| `tools/gendata.cpp`, `tools/train_nnue.cpp` | First NNUE experiment's data and training tools |
| `tools/bidstudy.cpp` | Bid values over random hands |
| `tools/sbsweep.cpp` | Tests a setting (such as `jockey_value`) in full self-play games, scored by games won |
| `wasm/` | WebAssembly entry points and build script |
| `gui/` | Browser GUI template; `build.py` embeds the engine and HaviEngine into one page |
| `arena/` | HaviEngine bundles and adapters, batch runners |
| `desktop/` | Windows build script, training launchers, `TRAINING.md` |
| `docs/` | Notation guide |

## Training
`sbtrain` trains three models by self-play and keeps a new generation only if it beats the current best head to head:
- the NNUE evaluation used at the search horizon
- the information-set value network
- the bidding model

Launchers: `desktop/Start-Training.bat` (round 1 recipe, `training\`) and `desktop/Start-Training-Round2.bat` (round 2 recipe, `training2\`). Status: `Training-Status.bat` and `Training-Status-Round2.bat`. Full guide: `desktop/TRAINING.md`.

**Setting sweeps:** `desktop/Run-Jockey-Sweep.bat` runs `sbsweep`, which plays each `jockey_value` against the engine with 0 in full games on the same deals, seats swapped, and reports win % in `sweep-jockey\REPORT.md`. Copy `sbsweep.exe` and the `.bat` next to `SpectacularBid.exe`. Stop and resume any time.

**Round 1** (36 hours, 26 cycles): the NNUE was promoted 3 times, the information-set network 4 times (blend weight 0.55), and the bidding model 3 times. Nearly all of the NNUE gain came from generation 1 (+0.10 chips per hand). Later generations were within noise.

**Round 2** (horizon-position NNUE, larger 256-64 information-set network, cleaner self-play): no promotions in 8 cycles. Every candidate tied the round 1 models. The learned models appear to be at their ceiling with the current training signal.

## Results
- Search matches exact brute-force minimax on 300 random small positions.
- Engine vs a greedy baseline (most points now, then heaviest tile): **+2.05 chips per hand** (se 0.19) over 300 mirrored deal pairs.
- Zero inference errors across 5,800 engine decisions.
- The lead is worth about **1.9 chips** (se 0.4), measured by playing each deal twice with each seat leading. The raw PIMC estimate overrates it, so bid decisions shrink it by a factor of 0.4. Calibrated bidding beat raw bidding 115-85 in full games.
- Against HaviEngine (old version, 3a45a1c), 20 games each: **19-1** as shipped, **16-4** with its sign bug fixed.

## HaviEngine
HaviEngine is the Havilands' own bot. The current version (5d092f9, 2026-10-12) scores by match equity, bids by best mean equity, and searches its free lead. `arena/havi_adapter_v2.js` plays it the way its GameRoom does, using its baseline match equity table (its saved table lives on its server). The old version (3a45a1c) had a sign bug at `src/lib/mcts/engine.js:440` that made it lose to a random mover. The "fixed" option applies that one-line change. Board translation between the two engines was checked identical on 2,110 positions.

The `arena/havi-bundle*.js` files are the Havilands' code. Keep this repo private unless they agree otherwise.

## Status
- [x] Rules core, player view and inference
- [x] PIMC plus alpha-beta search, bid analysis
- [x] Notation and command interface
- [x] Browser GUI and Windows app
- [x] Play against the engine
- [x] Bot vs bot against HaviEngine (watch, series, batch)
- [x] Training pipeline; round 1 promoted models, round 2 found no further gain
- [x] Replay and review mode (step through a game or a `.sbg` record, grade each decision)
- [x] Hand outcome probabilities (go out, win the hand)
- [x] Match equity table and game win probability on every move and position
- [ ] Play and bid to maximize game win probability (then test in full games scored by wins)
- [ ] Trained models in the browser version
- [ ] Engine reads position strings and `.sbg` records
