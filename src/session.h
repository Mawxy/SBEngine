#pragma once
// Text command interface to the engine, shared by the browser GUI (WebAssembly),
// the desktop app, and any command-line front end. Similar in spirit to UCI for
// chess engines: the front end sends one command per call and gets JSON back.
//
// Commands (seats are 1 and 2; moves use notation-spec.md):
//   new <my-seat> [pools <a> <b>]     start a game from my seat's point of view
//   hand <7 tiles> [opp <7 tiles>] [lead <seat>]
//                                      start a hand with my tiles; lead = forced leader.
//                                      With opp, the opponent's tiles are known too: the engine
//                                      then also keeps the full game (omniscient eval, the
//                                      opponent's own view) as long as their draws are entered
//                                      with the tile (d52); a plain d ends that for the hand
//   move <move>                        next action by whoever is to act
//   blocked                            end the hand as blocked
//   reveal <tiles>                     opponent's remaining tiles at hand end (settles chips)
//   pools <a> <b>                      sync chip pools
//   undo                               take back the last command
//   state                              current state
//   analyze [dets] [nodes] [seed]      engine analysis of the current position
//   review [dets] [nodes] [seed]       grade the decision at the current position: the
//                                      mover's view, and (games dealt by the app) the
//                                      full view with both hands known
//   peek                               (games dealt by the app) both hands and the boneyard
//   record                             game record (.sbg text)
//   log                                the command log (for replaying elsewhere)
//
// Play against the engine (the session is the referee and deals the tiles):
//   match <my-seat> [seed]             start a game against the engine
//   next                               deal the next hand
//   botchoice [dets] [nodes]           the engine's move for its seat (does not play it)
//   arena [seed]                       bot vs bot: both seats are played by engines
//   state <seat>                       (arena) the game as that seat sees it
//   Moves for both seats use `move`; draws need no tile, the referee knows it.

#include <string>
#include <vector>

#include "search.h"

namespace sb {

std::string tile_code(int t);  // "64"
int parse_tile(const std::string& s);  // "64", "6-4", "46"; -1 if invalid

// Directions on the table, as seen from the recording seat (the bottom of the app's board).
enum Dir : int8_t { kDirL = 0, kDirR = 1, kDirU = 2, kDirD = 3 };
char dir_letter(int d);  // 'L' 'R' 'U' 'D'

// Choices a move can carry that don't change the game, only how the table looks:
// which side of the spinner a new arm goes, and which way round the lead tile lies.
struct PlayHint {
    int8_t dir = -1;     // spinner side for a new arm (-1: next free side)
    bool flip = false;   // lead: low pip on the left
};

// The table as the players see it: tile chains per arm, and each arm's direction.
// Arm indices match Layout's.
struct Diagram {
    struct Piece {
        int8_t tile;
        int8_t in;   // pip facing the center / spinner
        int8_t out;  // pip facing outward
    };
    int center = -1;     // lead tile before a spinner exists
    bool flip = false;   // center drawn low pip on the left (Layout arm 0, the high end, points right)
    int spinner = -1;    // spinner tile
    std::vector<Piece> arms[4];
    int n_arms = 0;
    int8_t dir[4] = {kDirL, kDirR, kDirU, kDirD};  // Layout arm index -> direction
    uint8_t used = 0;    // directions taken around the spinner (bit per Dir)

    // Sides of the spinner a new arm may take now: the long sides (L, R) fill first, then U and D.
    uint8_t spinner_sides() const;
    int next_spinner_dir() const;  // the default side, -1 if none
    int arm_at(int d) const;       // Layout arm index pointing in direction d, -1 if none
    void play(const Layout& before, int tile, int target, PlayHint hint = {});
};

// Notation for a target on the current layout ("" for a lead): "@L", "@R", "@U", "@D".
// A new arm off the spinner is written with the side it takes (hint.dir, else the next free side).
std::string target_code(const Layout& l, const Diagram& g, int target, int spinner_dir = -1);
// Parse a move in notation. drawn = tile for a known draw, else -1. Accepts the direction form
// (41@R, 53@S for the spinner's next side) and the older pip form (41@4, 53@s, 61@6d).
bool parse_move(const std::string& s, const Layout& l, const Diagram& g, Action& out, int& drawn, std::string& err,
                PlayHint* hint = nullptr);
// Notation for an action played on layout `before`.
std::string move_code(const Layout& before, const Diagram& g, const Action& a, int drawn = -1, PlayHint hint = {});
// The same target the engine's move lists use (first of equal arms, spinner as one target).
int canonical_target(const Layout& l, int target);

class Session {
public:
    std::string command(const std::string& line);

private:
    struct HistMove {
        int seat;
        std::string code;
        int points;
        std::string key;  // engine-list form of the same move (equal arms merged, default spinner side)
    };
    struct HandRec {
        int number = 0;
        std::string header;
        TileMask my_hand = 0;
        std::vector<HistMove> moves;
        std::string end;
        TileMask revealed = 0;
        int start_pools[2] = {0, 0};  // chips in each pool when the hand was dealt
        TileMask opp_hand = 0;        // the opponent's starting tiles, when known (table games)
    };

    std::string exec(const std::vector<std::string>& args);  // "" on success, else error
    std::string exec_move(const std::string& code);
    void replay();
    std::string state_json(int seat = 0) const;
    std::string analyze_json(int dets, long long nodes, unsigned long long seed) const;
    std::string review_json(int dets, long long nodes, unsigned long long seed) const;
    std::string peek_json() const;
    std::string record_text() const;
    std::string begin_match_hand();
    std::string exec_match_start(const std::vector<std::string>& args);
    std::string exec_rest(const std::vector<std::string>& args);
    std::string exec_match_move(const std::string& code);

    bool started_ = false;
    int me_ = 0;
    View view_{0};
    Diagram diag_;
    std::vector<std::string> log_;
    std::vector<HandRec> hands_;
    bool settled_ = false;

    // Play-against-engine mode.
    bool match_ = false;
    bool arena_ = false;  // both seats played by bots
    Game game_;
    View bot_view_{1};
    std::mt19937_64 rng_;
    unsigned long long seed_ = 1;

    // Table games where both hands are known: the full game (both hands, the boneyard as a
    // set) and the opponent's own view, kept alongside my view.
    bool full_ok_ = false;
    Game full_;
    bool opp_view_ok_ = false;
    // The game with both hands visible, if there is one now (app-dealt games, or a table game
    // with both hands entered).
    const Game* full_game() const { return match_ ? &game_ : full_ok_ ? &full_ : nullptr; }
};

}  // namespace sb
