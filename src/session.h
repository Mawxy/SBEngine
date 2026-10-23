#pragma once
// Text command interface to the engine, shared by the browser GUI (WebAssembly),
// the desktop app, and any command-line front end. Similar in spirit to UCI for
// chess engines: the front end sends one command per call and gets JSON back.
//
// Commands (seats are 1 and 2; moves use notation-spec.md):
//   new <my-seat> [pools <a> <b>]     start a game from my seat's point of view
//   hand <7 tiles> [lead <seat>]       start a hand with my tiles; lead = forced leader
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

// Notation for a target on the current layout ("" for a lead).
std::string target_code(const Layout& l, int target);
// Parse a move in notation. drawn = tile for a known draw, else -1.
bool parse_move(const std::string& s, const Layout& l, Action& out, int& drawn, std::string& err);
// Notation for an action played on layout `before`.
std::string move_code(const Layout& before, const Action& a, int drawn = -1);

// Tile chains for drawing the layout. Arm indices match Layout's.
struct Diagram {
    struct Piece {
        int8_t tile;
        int8_t in;   // pip facing the center / spinner
        int8_t out;  // pip facing outward
    };
    int center = -1;   // lead tile before a spinner exists (hi end faces arm 0)
    int spinner = -1;  // spinner tile
    std::vector<Piece> arms[4];
    int n_arms = 0;
    void play(const Layout& before, int tile, int target);
};

class Session {
public:
    std::string command(const std::string& line);

private:
    struct HistMove {
        int seat;
        std::string code;
        int points;
    };
    struct HandRec {
        int number = 0;
        std::string header;
        TileMask my_hand = 0;
        std::vector<HistMove> moves;
        std::string end;
        TileMask revealed = 0;
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
};

}  // namespace sb
