#pragma once
// One player's view of the game: everything that player can see, plus what can be
// inferred about the opponent's hidden tiles. Used for live play, game review and
// self-play. The search never looks at the true hidden tiles; it samples
// determinizations from this view.
//
// Inference model. The opponent's hidden tiles are tracked as groups, each with a
// count and a mask of tiles those slots cannot hold:
//   - Drawing or passing proves the whole hand held no tile matching an open end
//     at that moment, so every current group forbids those tiles.
//   - A tile drawn in the middle of a draw sequence starts unconstrained. If the
//     opponent then draws again, the void rule above covers it.
//   - Forced leads (first hand, or after a blocked hand) bound the opponent's hand:
//     no tile ranks above the forced lead.
// When the opponent plays a tile, it is removed from the most constrained group
// that could hold it. That choice can only loosen constraints, never invent them.

#include <random>
#include <string>
#include <vector>

#include "game.h"

namespace sb {

// Ranking used to pick the forced lead: any double beats any single; higher double
// wins; singles rank by total pips then by higher end.
int lead_key(int t);
TileMask tiles_above_key(int key);

class View {
public:
    explicit View(int me, Rules r = {});

    void new_game();
    // Start a hand with my 7 tiles. If a jockey exists, bidding starts (non-jockey
    // first). Otherwise `leader` is the player holding the forced lead.
    void start_hand(TileMask my_hand, int leader);
    // Record an action by `player`. For my own draws pass the tile I drew.
    void observe(int player, const Action& a, int drawn_tile = -1);
    // End-of-hand settlement when the opponent's tiles are revealed (my domino or a block).
    void settle(TileMask opp_revealed);
    // External sync (chip counts, hand end detected by a referee).
    void set_pools(int p0, int p1);
    void set_jockey(int j) { jockey_ = j; }
    void end_hand(HandEnd e, int jockey);

    // Sample a full game state consistent with this view. `ok` reports whether
    // all inference constraints could be honored.
    Game determinize(std::mt19937_64& rng, bool* ok = nullptr) const;
    // Legal actions for me (only meaningful when it is my turn).
    std::vector<Action> legal_actions() const;

    int me() const { return me_; }
    int opp() const { return 1 - me_; }
    Phase phase() const { return phase_; }
    int to_move() const { return to_move_; }
    int jockey() const { return jockey_; }
    int winner() const { return winner_; }
    int pool(int p) const { return pool_[p]; }
    TileMask my_hand() const { return my_hand_; }
    int opp_count() const;
    int boneyard_size() const { return bone_count_; }
    const Layout& layout() const { return layout_; }
    int current_bid() const { return bid_; }
    int high_bidder() const { return high_bidder_; }
    HandEnd last_hand_end() const { return last_end_; }
    const Rules& rules() const { return rules_; }
    int bid_cost(int player, int bid) const;
    int max_bid(int player) const;

    // Tiles not visible to me (opponent hand + boneyard).
    TileMask unknown() const { return kAllTiles & ~my_hand_ & ~layout_.played(); }
    // Unknown tiles the opponent provably does not hold.
    TileMask opp_excluded() const;
    std::string to_string() const;

private:
    struct Group {
        int count;
        TileMask forbid;
    };
    void pay(int payer, int chips);
    void void_opp(TileMask tiles);
    void add_group(int count, TileMask forbid);
    void remove_from_groups(int t);
    void merge_groups();

    Rules rules_;
    int me_;
    Phase phase_ = Phase::HandOver;
    int to_move_ = 0;
    int jockey_ = -1;
    int winner_ = -1;
    int hand_number_ = 0;
    std::array<int, 2> pool_{};
    TileMask my_hand_ = 0;
    Layout layout_;
    int bone_count_ = 0;
    std::vector<Group> groups_;
    int bid_ = 0;
    int high_bidder_ = -1;
    int forced_lead_ = -1;  // my forced lead tile, if I am the forced leader
    bool opp_forced_ = false;
    int passes_ = 0;
    HandEnd last_end_ = HandEnd::None;
};

}  // namespace sb
