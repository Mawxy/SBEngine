#pragma once
// NNUE-style evaluation network.
//
// Like Stockfish's NNUE, the network replaces the hand-written evaluation at the
// leaves of the alpha-beta search. Inside one determinization every tile is known,
// so the network sees the full position from the side to move's point of view and
// predicts the net chips that side will win over the rest of the hand.
//
// Inputs are sparse and binary (about 30 of 165 active), so the first layer is a sum
// of the active weight rows, as in NNUE's accumulator:
//   0..27     tiles in the mover's hand
//   28..55    tiles in the opponent's hand
//   56..83    tiles on the table
//   84..111   plain open ends per pip (1, 2, 3, 4+ ends showing that pip)
//   112..118  a crosswise double at an open end, per pip
//   119..125  spinner can still take a tile, per pip
//   126..132  spinner still counts in the score (fewer than 2 arms), per pip
//   133       empty table (lead)
//   134..138  table count mod 5
//   139..149  table count / 5 (capped at 10)
//   150..164  boneyard size (0..14)
// Network: 165 -> 128 -> 32 -> 1, clipped ReLU (0..1) between layers, output in chips.

#include <string>

#include "game.h"

namespace sb {

constexpr int kNNInputs = 165;
constexpr int kNNH1 = 128;
constexpr int kNNH2 = 32;
constexpr int kNNMaxActive = 64;

struct NNWeights {
    float w1[kNNInputs][kNNH1];
    float b1[kNNH1];
    float w2[kNNH1][kNNH2];
    float b2[kNNH2];
    float w3[kNNH2];
    float b3;
};

// Open-end features (66 slots starting at `base`; see the table above, 84..149).
int layout_features(const Layout& l, int base, int* out);

// Active input indices for the side to move. Returns how many.
int nn_features(const Game& g, int* out);

// Network output in chips for the side to move.
float nn_eval(const NNWeights& w, const int* idx, int n);

// Weights compiled into the engine (nullptr if this build has none).
const NNWeights* nn_builtin();

bool nn_save(const NNWeights& w, const std::string& path);
bool nn_load(NNWeights& w, const std::string& path);

}  // namespace sb
