# Match equity table

Built from 15500 self-play hands at 30-30 (mirrored for both seats: 10386 with me as jockey, 10386 with the opponent as jockey, 10228 with a forced lead).

Hands: 94.7% end in a domino, 4.8% block, 0.6% end the game from 30-30. Chips paid per hand (both seats): 11.6 after a forced lead, 13.4 with a jockey. Average bid paid: 0.88 chips.

## P(win the game), nobody jockey (forced lead)
Rows: my pool. Columns: opponent's pool.

|  | 30 | 25 | 20 | 15 | 10 | 5 | 3 | 1 |
|---|---|---|---|---|---|---|---|---|
| **30** | 50 | 63 | 76 | 87 | 95 | 99 | 99 | 100 |
| **25** | 37 | 50 | 64 | 78 | 90 | 97 | 99 | 100 |
| **20** | 24 | 36 | 50 | 66 | 82 | 94 | 97 | 99 |
| **15** | 13 | 22 | 34 | 50 | 69 | 87 | 93 | 97 |
| **10** | 5 | 10 | 18 | 31 | 50 | 74 | 83 | 90 |
| **5** | 1 | 3 | 6 | 13 | 26 | 50 | 61 | 69 |
| **3** | 1 | 1 | 3 | 7 | 17 | 39 | 50 | 58 |
| **1** | 0 | 0 | 1 | 3 | 10 | 31 | 42 | 50 |

## Value of being jockey

| Pools | Win % as jockey | nobody | opponent jockey | jockey worth (chips) |
|---|---|---|---|---|
| 30-30 | 53.0 | 50.0 | 47.0 | 1.2 |
| 25-25 | 53.3 | 50.0 | 46.7 | 1.2 |
| 20-20 | 53.7 | 50.0 | 46.3 | 1.2 |
| 15-15 | 54.2 | 50.0 | 45.8 | 1.2 |
| 10-10 | 55.2 | 50.0 | 44.8 | 1.2 |
| 5-5 | 57.6 | 50.0 | 42.4 | 1.4 |
| 20-10 | 84.5 | 82.0 | 78.6 | 0.9 |
| 10-20 | 21.4 | 18.0 | 15.5 | 1.5 |

## Win % per chip of lead (nobody jockey)

| Pools | +1 chip for me |
|---|---|
| 30-30 | +2.6 points |
| 20-20 | +3.2 points |
| 10-10 | +4.5 points |
| 5-5 | +5.5 points |
