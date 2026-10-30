# Spectacular Bid Notation

A short way to write down Spectacular Bid moves and games, modeled on chess notation. You can use it to keep score at the table, share a game, or enter moves into the engine.

There are three parts:
- **Moves**: how to write one action, like `64` or `41@R`.
- **Game records** (`.sbg` files): a whole game, hand by hand.
- **Position strings**: one line that describes a single position.

## Seats
The players are **Seat 1** and **Seat 2**. The rules always decide who acts next, so a move never names the player.

## Tiles
A tile is two digits with the **higher number first**:

| Tile | Written |
|---|---|
| [6-4] | `64` |
| [6-6] | `66` |
| [3-0] | `30` |
| [0-0] | `00` |

When you type moves into the engine, `6-4`, `46` and `[6-4]` are all accepted, and capital letters are fine.

## The layout, in brief
- Until a double is played, the layout is a line with two open ends.
- The **first double** becomes the **spinner**. Tiles can be added on all four of its sides, so it can grow up to four arms. The spinner counts both of its pips until two arms are attached to it.
- Any later double is laid crosswise at the end of an arm. That end is called a **double end**, and it counts both pips.
- The count is the sum of all open ends. A move scores when the count is a multiple of 5.

## Directions
Every arm of the layout is named by the way it points **from the recording player's seat** (the `View` seat, which is the bottom of the app's board):

| Letter | Arm |
|---|---|
| `L` | left |
| `R` | right |
| `U` | up (away from you) |
| `D` | down (toward you) |
| `S` | the spinner's next open side |

- A line has a left end and a right end.
- When the first double lands on an end of the line, it becomes the spinner right there. The line stays where it is, so it is now the spinner's arm on the other side (a double played on the right end leaves the line as the spinner's left arm).
- The spinner's long sides (`L`, `R`) fill before its short ends (`U`, `D`).
- An arm keeps its letter for the rest of the hand, however many tiles it grows by, and a double laid across its end doesn't change it.

## Moves
| Action | Notation | Example |
|---|---|---|
| Lead the first tile, written as it lies (left pip first) | `<tile>` | `64` (6 on the left), `46` (4 on the left), `55` |
| Play on an arm | `<tile>@<L, R, U or D>` | `41@R` |
| Start a new arm off the spinner | `<tile>@<side>` or `<tile>@S` | `53@U`, `53@S` |
| Draw a tile you can't see (opponent's draw) | `d` | `d` |
| Draw a tile you can see (your own draw) | `d<tile>` | `d32` |
| Pass (can't play, boneyard empty) | `p` | `p` |
| Bid n chips | `b<n>` | `b3` |
| Opening bid of 0 (pass the auction) | `b0` | `b0` |
| Accept the current bid | `a` | `a` |

### Choosing the end
- If a tile fits only one place, you can leave the end off: `64` is enough.
- Typing, you can drop the `@`: `64r` is the same as `64@R`, and `53s` the same as `53@S`. Records write the `@` form.
- `@S` takes the spinner's next open side: left or right while either is free, then up, then down. The engine writes the side itself (`@L`, `@R`, `@U`, `@D`) in records.
- Older records used the number showing on the end (`41@4`, spinner `53@s`, double end `61@6d`). The engine still reads them and writes them back with letters.

### Score annotation
After a move you can add the points it scored, such as `64 +10` or `53@U +15`. Points are the board count, and every 5 points is 1 chip. The engine ignores the annotation when it reads a move, and it adds annotations when it writes a record.

## Bidding
After a hand ends in a domino, the player who went out becomes the **jockey**. Players bid chips for the right to lead the next hand.

- The other player (who lost the hand) **opens**. They can bid any amount they can afford, including `b0`.
- `b0` passes the auction: the jockey leads for free and play begins.
- After any other opening bid, each player either **raises** (`b<n>`, at least 1 higher) or **accepts** (`a`).
- Accepting ends the auction. The high bidder pays the bid to the opponent and leads any tile.
- The jockey gets a 3-chip credit and pays only the amount above 3. For example, a jockey who wins at `b5` pays 2 chips.

Examples, with Seat 1 as jockey and Seat 2 opening:

| Auction | Meaning |
|---|---|
| `b0` | Seat 2 passes. Seat 1 leads free. |
| `b2 a` | Seat 2 bids 2 and Seat 1 accepts. Seat 2 pays 2 chips and leads. |
| `b2 b3 a` | Seat 1 raises to 3 and Seat 2 accepts. Seat 1 leads and pays nothing (3 minus the credit). |
| `b2 b4 b5 a` | Seat 2 wins at 5, pays 5 chips and leads. |

The first hand, and any hand after a blocked hand, has no auction. The player with the highest double must lead it.

## Game records (.sbg)
A record has a few header lines, then one block per hand.

```
[Event "Friday night"]
[Date "2026.10.09"]
[Seat1 "Max"]
[Seat2 "Chris"]
[View "1"]
[Result "1-0"]

#1 lead 1
h1: 55 53 64 41 30 20 10
55 +10 52@L 53@R +5 d d 21@L ...
end domino 1
r2: 41 52

#2 jockey 1
h1: 33 31 21 54 64 22 11
b3 b4 a 33 ...
end blocked
```

### Headers
| Header | Meaning |
|---|---|
| `Event`, `Date` | Optional. The date is written `YYYY.MM.DD`. |
| `Seat1`, `Seat2` | Player names. |
| `View` | Whose hidden tiles the record shows: `1`, `2`, or `both`. |
| `Result` | `1-0` if Seat 1 won the game, `0-1` if Seat 2 won. Leave it out for an unfinished game. |
| `Mode` | How the game was played in the app: `match` (against the engine), `arena` (bot vs bot) or `table` (moves entered at a real table). |
| `Seed` | The deal seed for `match` and `arena` games. With it, a replay rebuilds every hand and draw exactly. |

### Hand lines
| Line | Meaning |
|---|---|
| `#n lead s` | Hand n, no auction. Seat s holds the forced lead. |
| `#n jockey s` | Hand n starts with an auction. Seat s is the jockey. |
| `h1:` / `h2:` | That seat's seven starting tiles. Leave out a hand you never saw. With both hands (and every draw written with its tile, `d52`), the engine follows the whole game: an all-tiles-visible read, grades for both players, and automatic settling. |
| moves | Every action in order, separated by spaces. Draws you couldn't see are `d`. |
| `end domino s` | Seat s went out. |
| `end blocked` | Nobody could play and the boneyard was empty. |
| `r1:` / `r2:` | Tiles that seat revealed at the end of the hand, for scoring. |
| `score a-b, won x-y` | Written by the engine after each hand: chips left in Seat 1's and Seat 2's pools, then the chips each seat won during the hand (bids included). Ignored when a record is read. |

If the game ends during a hand, the end line says so: `end domino 1; game over, seat 1 wins`.

To get a record of a game played in the app, open the game log, press **Record**, then **Copy record** or **Download .sbg**. To replay one, paste it (or open the file) under **Replay a saved game** on the New game screen. Records without a `Seed` replay from the `View` seat only.

## Position strings
A position string describes one position on a single line, for sharing a position to analyze. It works like FEN in chess. The engine does not read position strings yet. For now they are a written format only.

```
<phase><to-move> <pool1>/<pool2> <jockey> <layout> <hand1> <hand2> <boneyard>
```

Example: `P2 27/30 1 [6]2,5 64.31.20.11 6 9`

| Field | Meaning |
|---|---|
| phase and to-move | `P1` or `P2` during play. `B1` or `B2` during an auction, with the current bid added, as in `B2:3`. |
| pools | Chips left in each seat's pool. |
| jockey | `1`, `2`, or `-` for none. |
| layout | `-` if empty. `<6,4>` is a line with ends 6 and 4 before any double. `[5]3,2` is spinner 5 with arm ends 3 and 2. Ends are listed left, right, up, down. A `d` marks a double end, as in `6d`. |
| hand1, hand2 | The tiles, joined with `.`, or just a count if the hand is hidden. |
| boneyard | Number of tiles left in the boneyard. |

A position string has no history, so it loses what the players have shown, such as which numbers a player was missing when they drew. Use a game record when that matters.
