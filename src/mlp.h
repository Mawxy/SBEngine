#pragma once
// Small fully connected network with sparse binary inputs, used for all learned parts
// of the engine (the NNUE evaluation, the bidding model and the information-set value
// network). in -> h1 -> h2 -> 1, clipped ReLU (0..1) between layers.
//
// Weight layout matches NNWeights (w1[in][h1], b1, w2[h1][h2], b2, w3[h2], b3), so an
// evaluation-sized Mlp converts to NNWeights with a single copy.
//
// File format: "SBNN" | uint32 version=2 | int32 in, h1, h2 | float meta | float weights...
// `meta` carries a model setting (the info network's blend weight). Older eval files
// that are just raw NNWeights are still accepted by nn_load.

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace sb {

class Mlp {
public:
    int in = 0, h1 = 0, h2 = 0;
    float meta = 0;
    std::vector<float> w;  // all parameters, flat

    Mlp() = default;
    Mlp(int in_, int h1_, int h2_) { resize(in_, h1_, h2_); }
    void resize(int in_, int h1_, int h2_);
    void init_random(std::mt19937_64& rng, float out_bias);

    size_t n_params() const { return w.size(); }
    float* w1() { return w.data(); }
    float* b1() { return w1() + static_cast<size_t>(in) * h1; }
    float* w2() { return b1() + h1; }
    float* b2() { return w2() + static_cast<size_t>(h1) * h2; }
    float* w3() { return b2() + h2; }
    float* b3() { return w3() + h2; }
    const float* w1() const { return w.data(); }
    const float* b1() const { return w1() + static_cast<size_t>(in) * h1; }
    const float* w2() const { return b1() + h1; }
    const float* b2() const { return w2() + static_cast<size_t>(h1) * h2; }
    const float* w3() const { return b2() + h2; }
    const float* b3() const { return w3() + h2; }

    // Output for the active input indices.
    float eval(const uint8_t* idx, int n) const;
    float eval(const int* idx, int n) const;

    bool save(const std::string& path) const;
    bool load(const std::string& path);
};

// One training example: active inputs and a target.
struct MlpSample {
    uint8_t n = 0;
    uint8_t idx[103];
    float target = 0;
};
static_assert(sizeof(MlpSample) == 108, "compact sample");

struct TrainOptions {
    int epochs = 6;
    float lr = 1e-3f;
    int batch = 1024;
    int threads = 1;
    float holdout = 0.05f;
    uint64_t seed = 1;
    bool verbose = true;
};

struct TrainResult {
    double holdout_mse = 0;
    double holdout_var = 0;  // error of always predicting the mean
    size_t train_n = 0, holdout_n = 0;
};

// Minibatch Adam on mean squared error. Shuffles `data` in place.
TrainResult train_mlp(Mlp& m, std::vector<MlpSample>& data, const TrainOptions& opt);

}  // namespace sb
