#include "nnue.h"

#include <algorithm>
#include <cstdio>

namespace sb {

int layout_features(const Layout& l, int base, int* out) {
    // Offsets relative to `base`: 0..27 plain ends, 28..34 double ends, 35..41 spinner open,
    // 42..48 spinner counts, 49 empty, 50..54 count mod 5, 55..65 count / 5.
    int n = 0;
    if (l.empty()) {
        out[n++] = base + 49;
        return n;
    }
    int plain[kNumPips] = {0};
    bool dbl[kNumPips] = {false};
    for (int i = 0; i < l.arm_count(); ++i) {
        const ArmEnd e = l.arm(i);
        if (e.dbl) dbl[e.pip] = true;
        else ++plain[e.pip];
    }
    for (int pip = 0; pip < kNumPips; ++pip) {
        if (plain[pip]) out[n++] = base + pip * 4 + std::min(plain[pip], 4) - 1;
        if (dbl[pip]) out[n++] = base + 28 + pip;
    }
    if (l.spinner_open()) out[n++] = base + 35 + l.spinner_pip();
    if (l.has_spinner() && l.arm_count() < 2) out[n++] = base + 42 + l.spinner_pip();
    const int c = l.count();
    out[n++] = base + 50 + c % 5;
    out[n++] = base + 55 + std::min(c / 5, 10);
    return n;
}

int nn_features(const Game& g, int* out) {
    int n = 0;
    const int p = g.to_move(), o = 1 - p;
    for (TileMask m = g.hand(p); m; m &= m - 1) out[n++] = first_tile(m);
    for (TileMask m = g.hand(o); m; m &= m - 1) out[n++] = 28 + first_tile(m);
    const Layout& l = g.layout();
    for (TileMask m = l.played(); m; m &= m - 1) out[n++] = 56 + first_tile(m);
    n += layout_features(l, 84, out + n);
    out[n++] = 150 + std::min(g.boneyard_size(), 14);
    return n;
}

float nn_eval(const NNWeights& w, const int* idx, int n) {
    float h1[kNNH1];
    for (int k = 0; k < kNNH1; ++k) h1[k] = w.b1[k];
    for (int i = 0; i < n; ++i) {
        const float* row = w.w1[idx[i]];
        for (int k = 0; k < kNNH1; ++k) h1[k] += row[k];
    }
    for (int k = 0; k < kNNH1; ++k) h1[k] = std::min(std::max(h1[k], 0.0f), 1.0f);
    float h2[kNNH2];
    for (int j = 0; j < kNNH2; ++j) h2[j] = w.b2[j];
    for (int k = 0; k < kNNH1; ++k) {
        const float a = h1[k];
        if (a == 0.0f) continue;
        const float* row = w.w2[k];
        for (int j = 0; j < kNNH2; ++j) h2[j] += a * row[j];
    }
    float y = w.b3;
    for (int j = 0; j < kNNH2; ++j) y += w.w3[j] * std::min(std::max(h2[j], 0.0f), 1.0f);
    return y;
}

bool nn_save(const NNWeights& w, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(&w, sizeof w, 1, f) == 1;
    std::fclose(f);
    return ok;
}

bool nn_load(NNWeights& w, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    const bool ok = std::fread(&w, sizeof w, 1, f) == 1;
    std::fclose(f);
    return ok;
}

}  // namespace sb
