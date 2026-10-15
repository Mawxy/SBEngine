#pragma once
// Full game state and rules for Spectacular Bid (see engine-rules-spec.md).
// This is the perfect-information "referee" state. Hidden-information views
// for the search are built on top of it later.

#include <array>
#include <random>
#include <string>
#include <vector>

#include "layout.h"

namespace sb {

enum class Phase : uint8_t { Bidding, Play, HandOver, GameOver };
enum class HandEnd : uint8_t { None, Domino, Blocked };

struct Action {
    enum Kind : uint8_t { Play, Draw, Pass, Bid, Accept };
    Kind kind = Pass;
    int8_t tile = -1;
    int8_t target = -1;
    int16_t bid = 0;

    static Action play(int t, int target) { return {Play, static_cast<int8_t>(t), static_cast<int8_t>(target), 0}; }
    static Action draw() { return {Draw}; }
    static Action pass() { return {Pass}; }
    static Action make_bid(int b) { return {Bid, -1, -1, static_cast<int16_t>(b)}; }
    static Action accept() { return {Accept}; }
    bool operator==(const Action&) const = default;
};

std::string to_string(const Action& a);

struct Rules {
    int start_chips = 30;
    int jockey_credit = 3;
    int hand_size = 7;
};

// Full state for loading a position (used by determinization).
struct Snapshot {
    Phase phase = Phase::Play;
    int to_move = 0;
    int jockey = -1;
    int hand_number = 1;
    std::array<int, 2> pool{30, 30};
    std::array<TileMask, 2> hand{};
    std::vector<int8_t> boneyard;  // draw order, next draw first
    Layout layout;
    int bid = 0;
    int high_bidder = -1;
    int forced_lead = -1;
};

class Game {
public:
    explicit Game(Rules r = {}) : rules_(r) {}

    // Reset chips and deal the first hand.
    void new_game(std::mt19937_64& rng);
    // Deal the next hand (requires phase HandOver).
    void next_hand(std::mt19937_64& rng);
    // Deal from an explicit order: [0,7) -> player 0, [7,14) -> player 1,
    // the rest is the boneyard in draw order.
    void deal(const std::array<int8_t, kNumTiles>& order);

    void legal_actions(std::vector<Action>& out) const;
    std::vector<Action> legal_actions() const;
    void apply(const Action& a);

    Phase phase() const { return phase_; }
    int to_move() const { return to_move_; }
    int winner() const { return winner_; }
    int pool(int p) const { return pool_[p]; }
    int trophy(int p) const { return trophy_[p]; }
    int jockey() const { return jockey_; }
    TileMask hand(int p) const { return hand_[p]; }
    TileMask boneyard_mask() const;
    int boneyard_size() const { return bone_end_ - bone_pos_; }
    const Layout& layout() const { return layout_; }
    int current_bid() const { return bid_; }
    int high_bidder() const { return high_bidder_; }
    int forced_lead() const { return forced_lead_; }
    HandEnd last_hand_end() const { return last_end_; }
    int hand_number() const { return hand_number_; }
    const Rules& rules() const { return rules_; }

    // Chips the player pays if they win the bid at `bid`.
    int bid_cost(int player, int bid) const;
    // Highest bid the player can afford.
    int max_bid(int player) const;
    bool can_play(int player) const;

    std::string to_string() const;

    void load(const Snapshot& s);
    // Skip/resolve bidding without payment: `p` leads any tile.
    void force_leader(int p);
    // Reorder the undrawn boneyard (spectator analysis: both hands known, draw order not).
    void shuffle_boneyard(std::mt19937_64& rng) { portable_shuffle(boneyard_.data() + bone_pos_, bone_end_ - bone_pos_, rng); }
    int last_drawn() const { return last_drawn_; }
    int bone_pos() const { return bone_pos_; }

    // Test hooks.
    void set_pools(int p0, int p1);
    void set_jockey(int j) { jockey_ = j; }

private:
    void begin_hand();
    void pay(int payer, int chips);
    void check_block();
    void resolve_block();

    Rules rules_;
    Phase phase_ = Phase::HandOver;
    int to_move_ = 0;
    int winner_ = -1;
    int jockey_ = -1;  // player who dominoed last hand, or -1
    int hand_number_ = 0;
    std::array<int, 2> pool_{};
    std::array<int, 2> trophy_{};
    std::array<TileMask, 2> hand_{};
    std::array<int8_t, kNumTiles> boneyard_{};
    int bone_pos_ = 0;
    int bone_end_ = 0;
    Layout layout_;
    int bid_ = 0;
    int high_bidder_ = -1;
    int forced_lead_ = -1;
    HandEnd last_end_ = HandEnd::None;
    int last_drawn_ = -1;
};

}  // namespace sb
