#include "mlp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef SB_TRAIN_THREADS
#include <thread>
#endif

namespace sb {

void Mlp::resize(int in_, int h1_, int h2_) {
    in = in_;
    h1 = h1_;
    h2 = h2_;
    w.assign(static_cast<size_t>(in) * h1 + h1 + static_cast<size_t>(h1) * h2 + h2 + h2 + 1, 0.0f);
}

void Mlp::init_random(std::mt19937_64& rng, float out_bias) {
    std::normal_distribution<float> nd(0.0f, 1.0f);
    for (size_t i = 0; i < static_cast<size_t>(in) * h1; ++i) w1()[i] = nd(rng) * 0.08f;
    for (int k = 0; k < h1; ++k) b1()[k] = 0.5f;
    for (size_t i = 0; i < static_cast<size_t>(h1) * h2; ++i) w2()[i] = nd(rng) / std::sqrt(static_cast<float>(h1));
    for (int j = 0; j < h2; ++j) b2()[j] = 0.5f;
    for (int j = 0; j < h2; ++j) w3()[j] = nd(rng);
    *b3() = out_bias;
}

template <class I>
static float forward(const Mlp& m, const I* idx, int n, float* a1, float* a2) {
    const int H1 = m.h1, H2 = m.h2;
    for (int k = 0; k < H1; ++k) a1[k] = m.b1()[k];
    for (int i = 0; i < n; ++i) {
        const float* row = m.w1() + static_cast<size_t>(idx[i]) * H1;
        for (int k = 0; k < H1; ++k) a1[k] += row[k];
    }
    for (int j = 0; j < H2; ++j) a2[j] = m.b2()[j];
    for (int k = 0; k < H1; ++k) {
        const float h = std::min(std::max(a1[k], 0.0f), 1.0f);
        if (h == 0.0f) continue;
        const float* row = m.w2() + static_cast<size_t>(k) * H2;
        for (int j = 0; j < H2; ++j) a2[j] += h * row[j];
    }
    float y = *m.b3();
    for (int j = 0; j < H2; ++j) y += m.w3()[j] * std::min(std::max(a2[j], 0.0f), 1.0f);
    return y;
}

float Mlp::eval(const uint8_t* idx, int n) const {
    float a1[512], a2[128];
    return forward(*this, idx, n, a1, a2);
}

float Mlp::eval(const int* idx, int n) const {
    float a1[512], a2[128];
    return forward(*this, idx, n, a1, a2);
}

bool Mlp::save(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t version = 2;
    const int32_t dims[3] = {in, h1, h2};
    bool ok = std::fwrite("SBNN", 1, 4, f) == 4 && std::fwrite(&version, 4, 1, f) == 1 &&
              std::fwrite(dims, 4, 3, f) == 3 && std::fwrite(&meta, 4, 1, f) == 1 &&
              std::fwrite(w.data(), 4, w.size(), f) == w.size();
    std::fclose(f);
    return ok;
}

bool Mlp::load(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char magic[4];
    uint32_t version = 0;
    int32_t dims[3];
    bool ok = std::fread(magic, 1, 4, f) == 4 && std::memcmp(magic, "SBNN", 4) == 0 && std::fread(&version, 4, 1, f) == 1 &&
              version == 2 && std::fread(dims, 4, 3, f) == 3 && dims[0] > 0 && dims[0] <= 256 && dims[1] > 0 &&
              dims[1] <= 512 && dims[2] > 0 && dims[2] <= 128 && std::fread(&meta, 4, 1, f) == 1;
    if (ok) {
        resize(dims[0], dims[1], dims[2]);
        ok = std::fread(w.data(), 4, w.size(), f) == w.size();
    }
    std::fclose(f);
    return ok;
}

namespace {

// Gradient of the batch slice [lo, hi) accumulated into g (same layout as m.w). Returns sum of squared errors.
double batch_grad(const Mlp& m, const MlpSample* s, size_t lo, size_t hi, float scale, std::vector<float>& g) {
    const int H1 = m.h1, H2 = m.h2;
    float a1[512], a2[128], h1v[512], h2v[128], da1[512], da2[128];
    float* gw1 = g.data();
    float* gb1 = gw1 + static_cast<size_t>(m.in) * H1;
    float* gw2 = gb1 + H1;
    float* gb2 = gw2 + static_cast<size_t>(H1) * H2;
    float* gw3 = gb2 + H2;
    float* gb3 = gw3 + H2;
    double loss = 0;
    for (size_t b = lo; b < hi; ++b) {
        const MlpSample& x = s[b];
        const float y = forward(m, x.idx, x.n, a1, a2);
        for (int k = 0; k < H1; ++k) h1v[k] = std::min(std::max(a1[k], 0.0f), 1.0f);
        for (int j = 0; j < H2; ++j) h2v[j] = std::min(std::max(a2[j], 0.0f), 1.0f);
        const float err = y - x.target;
        loss += static_cast<double>(err) * err;
        const float dy = 2 * err * scale;
        *gb3 += dy;
        for (int j = 0; j < H2; ++j) {
            gw3[j] += dy * h2v[j];
            da2[j] = (a2[j] > 0 && a2[j] < 1) ? dy * m.w3()[j] : 0.0f;
            gb2[j] += da2[j];
        }
        for (int k = 0; k < H1; ++k) {
            const float* w2row = m.w2() + static_cast<size_t>(k) * H2;
            float* g2row = gw2 + static_cast<size_t>(k) * H2;
            float d = 0;
            for (int j = 0; j < H2; ++j) {
                g2row[j] += h1v[k] * da2[j];
                d += w2row[j] * da2[j];
            }
            da1[k] = (a1[k] > 0 && a1[k] < 1) ? d : 0.0f;
            gb1[k] += da1[k];
        }
        for (int i = 0; i < x.n; ++i) {
            float* row = gw1 + static_cast<size_t>(x.idx[i]) * H1;
            for (int k = 0; k < H1; ++k) row[k] += da1[k];
        }
    }
    return loss;
}

}  // namespace

TrainResult train_mlp(Mlp& m, std::vector<MlpSample>& data, const TrainOptions& opt) {
    TrainResult res;
    std::mt19937_64 rng(opt.seed);
    std::shuffle(data.begin(), data.end(), rng);
    const size_t n_hold = std::min(data.size() / 2, std::max<size_t>(500, static_cast<size_t>(data.size() * opt.holdout)));
    const size_t n_train = data.size() - n_hold;
    res.train_n = n_train;
    res.holdout_n = n_hold;
    MlpSample* train = data.data();
    const MlpSample* hold = data.data() + n_train;

    double mean = 0;
    for (size_t i = 0; i < n_hold; ++i) mean += hold[i].target;
    mean /= std::max<size_t>(1, n_hold);
    for (size_t i = 0; i < n_hold; ++i) res.holdout_var += (hold[i].target - mean) * (hold[i].target - mean);
    res.holdout_var /= std::max<size_t>(1, n_hold);

    auto holdout_mse = [&] {
        double e = 0;
        for (size_t i = 0; i < n_hold; ++i) {
            const double d = m.eval(hold[i].idx, hold[i].n) - hold[i].target;
            e += d * d;
        }
        return e / std::max<size_t>(1, n_hold);
    };

    const size_t P = m.n_params();
    std::vector<float> am(P, 0), av(P, 0);
    long long t = 0;
    const int T = std::max(1, opt.threads);
    std::vector<std::vector<float>> grads(T, std::vector<float>(P));

    for (int ep = 0; ep < opt.epochs; ++ep) {
        std::shuffle(train, train + n_train, rng);
        const float lr = opt.lr * (ep < (opt.epochs * 2) / 3 ? 1.0f : 0.3f);
        double loss = 0;
        for (size_t start = 0; start + opt.batch <= n_train; start += opt.batch) {
            const float scale = 1.0f / opt.batch;
            const size_t per = (opt.batch + T - 1) / T;
            std::vector<double> losses(T, 0);
            auto work = [&](int ti) {
                std::fill(grads[ti].begin(), grads[ti].end(), 0.0f);
                const size_t lo = start + ti * per, hi = std::min(start + opt.batch, lo + per);
                if (lo < hi) losses[ti] = batch_grad(m, train, lo, hi, scale, grads[ti]);
            };
#ifdef SB_TRAIN_THREADS
            if (T > 1) {
                std::vector<std::thread> ts;
                for (int ti = 1; ti < T; ++ti) ts.emplace_back(work, ti);
                work(0);
                for (auto& th : ts) th.join();
                for (int ti = 1; ti < T; ++ti)
                    for (size_t i = 0; i < P; ++i) grads[0][i] += grads[ti][i];
            } else {
                work(0);
            }
#else
            for (int ti = 0; ti < T; ++ti) work(ti);
            for (int ti = 1; ti < T; ++ti)
                for (size_t i = 0; i < P; ++i) grads[0][i] += grads[ti][i];
#endif
            for (double l : losses) loss += l;
            ++t;
            const float c1 = 1 - std::pow(0.9f, static_cast<float>(t)), c2 = 1 - std::pow(0.999f, static_cast<float>(t));
            const std::vector<float>& g = grads[0];
            for (size_t i = 0; i < P; ++i) {
                am[i] = 0.9f * am[i] + 0.1f * g[i];
                av[i] = 0.999f * av[i] + 0.001f * g[i] * g[i];
                m.w[i] -= lr * (am[i] / c1) / (std::sqrt(av[i] / c2) + 1e-8f);
            }
        }
        res.holdout_mse = holdout_mse();
        if (opt.verbose) {
            std::printf("    epoch %d/%d  train MSE %.3f  held-out MSE %.3f (baseline %.3f)\n", ep + 1, opt.epochs,
                        loss / std::max<size_t>(1, (n_train / opt.batch) * opt.batch), res.holdout_mse, res.holdout_var);
            std::fflush(stdout);
        }
    }
    if (opt.epochs == 0) res.holdout_mse = holdout_mse();
    return res;
}

}  // namespace sb
