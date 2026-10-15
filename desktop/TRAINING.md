# Training the engine on your PC

`sbtrain.exe` improves the engine by playing against itself, the same way Stockfish trains its NNUE. In each cycle it builds a new version of three models:

| Model | What it learns | Where the engine uses it |
|---|---|---|
| **NNUE evaluation** | How good a position is when every tile is known | At the edge of every search |
| **Info-set network** | How good a position is from what one player can actually see | Blended into the value of each move you could make |
| **Bidding model** | What the lead is worth with a given hand | Bid advice and the engine's own bids |

A new version only replaces the current one if it **beats it head to head**. The NNUE and info network are tested over hundreds of mirrored hands, and the bidding model over full games with bidding. It has to be ahead with 95% confidence, so a version that plays worse is very unlikely to be kept.

## Before you start
Windows may show "Windows protected your PC" the first time, because these programs aren't code-signed. Click **More info**, then **Run anyway**.

## 1. Quick test (about 12 minutes)
Double-click **Quick-Test.bat**. It runs a few short cycles in a separate `training-quick` folder and doesn't touch the real training. The window should end with `Training finished`.

## 2. The real run (48 hours)
Double-click **Start-Training.bat** and leave the window open.
- It uses every core but one, at low priority, so the PC stays usable.
- It keeps the PC awake while it runs. The screen can still turn off.
- **To pause,** close the window or press Ctrl+C. Double-click Start-Training.bat again to resume where it left off, with the 48-hour budget counting only time actually spent training.
- **To extend it,** change `--hours 48` in Start-Training.bat to a bigger number and run it again.
- **Disk use** stays under about 2 GB. Old self-play data is deleted as new data arrives.

## 3. Checking progress
- **Training-Status.bat** (or open `training\REPORT.md`) shows which models are in use and how each generation did.
- `training\results.csv` has every head-to-head result. Score is the new model's chips per hand (or chip margin per game, for bidding) against the current best.
- `training\log.txt` is the full running log.

## 4. Using the results
Promoted models are saved in `training\promoted\` (`eval.bin`, `info.bin`, `bid.bin`).
- **SpectacularBid.exe loads them automatically** the next time it starts. The engine panel's status line lists which models are active, for example `trained NNUE, learned bidding, info-set network (weight 0.40)`.
- **For the browser version,** send those three files to Claude so they can be built into the page.

## Folder layout after a run
```
desktop\
  SpectacularBid.exe        the app (loads training\promoted)
  sbtrain.exe               the trainer
  Start-Training.bat  Quick-Test.bat  Training-Status.bat
  training\
    promoted\               current best models (used by the app)
    candidates\             every generation tried
    data\                   recent self-play data (rolling)
    REPORT.md  results.csv  log.txt  state.txt
```

## Round 2 (new recipe)
The first run's NNUE stopped improving after its first generation, because most hands are searched to the end and the NNUE only matters where a search stops early. Round 2 changes what gets trained:
- **NNUE:** trained only on positions where the engine's search actually stopped at its horizon, each labelled with a deeper search, instead of on every position.
- **Info-set network:** twice the size (256-64 instead of 128-32), twice the data window, more training, and about a quarter of each cycle spent testing it. A new network of the larger size has to beat the current one before it's used.
- **Cleaner self-play:** random moves only in the first few plays of a hand, so outcomes reflect real play. The players also search harder (12 deals x 4000 nodes).
- **Bidding:** same as before, with less time.

To run it, double-click **Start-Training-Round2.bat**. It copies the current models from `training\promoted` into a new `training2` folder and trains from there, so the first run's results stay untouched. Pause and resume work the same way. Check progress with **Training-Status-Round2.bat** or `training2\REPORT.md`.

SpectacularBid.exe uses `training2\promoted` once it exists, and `training\promoted` otherwise.

## Command line
```
sbtrain run    [--hours 48] [--dir training] [--threads N] [--cycle-minutes 80] [--from DIR]
sbtrain quick  [--threads N]
sbtrain status [--dir training]
```
`--threads` defaults to the number of cores minus one. A cycle is about 80 minutes plus a few minutes of network training, so 48 hours is roughly 30 to 35 generations of each model.
