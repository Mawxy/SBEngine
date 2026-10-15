#pragma once
// Learned models the engine can use, and the process-wide set in use.
//
//   eval  NNUE evaluation at the search horizon (nnue.h)
//   bid   bidding model: my 7 tiles -> value of the lead in chips
//   info  information-set value network: what one player can see -> chips that player
//         nets over the rest of the hand. Blended into each root move's PIMC value,
//         which corrects PIMC's habit of searching as if every tile were visible.
//
// SearchParams take their defaults from models(), so a front end (the Windows app)
// loads trained files once at startup and every search uses them.

#include <string>

#include "mlp.h"
#include "nnue.h"
#include "view.h"

namespace sb {

constexpr int kBidInputs = 28;
constexpr int kBidH1 = 32, kBidH2 = 16;
int bid_features(TileMask hand, int* out);

// Observer = v.me():
//   0..27 my tiles | 28..55 tiles on the table | 56..83 tiles the opponent cannot hold
//   84..149 open ends (same encoding as the NNUE) | 150..164 boneyard size
//   165..179 opponent tile count | 180 my turn | 181 opponent's turn
constexpr int kInfoInputs = 182;
constexpr int kInfoH1 = 128, kInfoH2 = 32;
int info_features(const View& v, int* out);

struct Models {
    const NNWeights* eval = nullptr;
    const Mlp* bid = nullptr;
    const Mlp* info = nullptr;
    float info_weight = 0;  // blend: (1 - w) * PIMC + w * info network
    std::string description;
};

Models& models();

// Loads eval.bin, bid.bin and info.bin from a folder (any that exist and are valid)
// into models(). Returns a one-line summary.
std::string load_models_from(const std::string& dir);

// Copies an evaluation-shaped Mlp (165 -> 128 -> 32) into NNWeights.
bool mlp_to_nnue(const Mlp& m, NNWeights& out);

}  // namespace sb
