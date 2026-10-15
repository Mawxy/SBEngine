#include "models.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace sb {

int bid_features(TileMask hand, int* out) {
    int n = 0;
    for (TileMask m = hand; m; m &= m - 1) out[n++] = first_tile(m);
    return n;
}

int info_features(const View& v, int* out) {
    int n = 0;
    for (TileMask m = v.my_hand(); m; m &= m - 1) out[n++] = first_tile(m);
    const Layout& l = v.layout();
    for (TileMask m = l.played(); m; m &= m - 1) out[n++] = 28 + first_tile(m);
    for (TileMask m = v.opp_excluded(); m; m &= m - 1) out[n++] = 56 + first_tile(m);
    n += layout_features(l, 84, out + n);
    out[n++] = 150 + std::min(v.boneyard_size(), 14);
    out[n++] = 165 + std::min(v.opp_count(), 14);
    out[n++] = v.to_move() == v.me() ? 180 : 181;
    return n;
}

Models& models() {
    static Models m = [] {
        Models x;
        x.eval = nn_builtin();
        x.description = x.eval ? "built-in NNUE" : "classic evaluation";
        return x;
    }();
    return m;
}

bool mlp_to_nnue(const Mlp& m, NNWeights& out) {
    if (m.in != kNNInputs || m.h1 != kNNH1 || m.h2 != kNNH2 || m.w.size() * sizeof(float) != sizeof(NNWeights)) return false;
    std::memcpy(&out, m.w.data(), sizeof(NNWeights));
    return true;
}

std::string load_models_from(const std::string& dir) {
    static NNWeights eval_w;
    static Mlp eval_m, bid_m, info_m;
    Models& M = models();
    std::string sum;
    const std::string sep = dir.empty() || dir.back() == '/' || dir.back() == '\\' ? "" : "/";
    if (eval_m.load(dir + sep + "eval.bin") && mlp_to_nnue(eval_m, eval_w)) {
        M.eval = &eval_w;
        sum += "trained NNUE";
    } else if (nn_load(eval_w, dir + sep + "eval.bin")) {
        M.eval = &eval_w;
        sum += "trained NNUE";
    } else {
        sum += M.eval ? "built-in NNUE" : "classic evaluation";
    }
    if (bid_m.load(dir + sep + "bid.bin") && bid_m.in == kBidInputs) {
        M.bid = &bid_m;
        sum += ", learned bidding";
    }
    if (info_m.load(dir + sep + "info.bin") && info_m.in == kInfoInputs) {
        M.info = &info_m;
        M.info_weight = info_m.meta;
        char buf[64];
        std::snprintf(buf, sizeof buf, ", info-set network (weight %.2f)", info_m.meta);
        sum += buf;
    }
    M.description = sum;
    return sum;
}

}  // namespace sb
