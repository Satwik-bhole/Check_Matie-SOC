#ifndef NNUE_H
#define NNUE_H

#include "chess-library/include/chess.hpp"
#include <vector>
#include <string>

class NNUE {
public:
    NNUE();
    bool load_weights(const std::string& filename);
    float evaluate(const chess::Board& board) const;
    bool is_initialized() const { return weights_loaded; }  // Add this method

private:
    void extract_features(const chess::Board& board, std::vector<float>& features) const;
    float forward(const std::vector<float>& features) const;
    float relu(float x) const;

    // Weights and biases - updated to match training architecture
    std::vector<float> input_weights_;  // 768 x 256
    std::vector<float> input_biases_;   // 256
    std::vector<float> hidden_weights_; // 256 x 32
    std::vector<float> hidden_biases_;  // 32
    std::vector<float> output_weights_; // 32 x 1
    float output_bias_;
    
    bool weights_loaded = false;  // Add this member
};

#endif