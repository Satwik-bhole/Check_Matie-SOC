#include "nnue.h"
#include "nnue.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <cassert>

NNUE::NNUE() : output_bias_(0.0f), weights_loaded(false) {
    input_weights_.resize(768 * 256, 0.0f);
    input_biases_.resize(256, 0.0f);
    hidden_weights_.resize(256 * 32, 0.0f);
    hidden_biases_.resize(32, 0.0f);
    output_weights_.resize(32, 0.0f);
}

bool NNUE::load_weights(const std::string& filename) {
    weights_loaded = false;  // Reset loading status
    
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Could not open weights file: " << filename << std::endl;
        return false;
    }

    // Read weights in the order exported by Python
    file.read(reinterpret_cast<char*>(input_weights_.data()), input_weights_.size() * sizeof(float));
    file.read(reinterpret_cast<char*>(input_biases_.data()), input_biases_.size() * sizeof(float));
    file.read(reinterpret_cast<char*>(hidden_weights_.data()), hidden_weights_.size() * sizeof(float));
    file.read(reinterpret_cast<char*>(hidden_biases_.data()), hidden_biases_.size() * sizeof(float));
    file.read(reinterpret_cast<char*>(output_weights_.data()), output_weights_.size() * sizeof(float));
    file.read(reinterpret_cast<char*>(&output_bias_), sizeof(float));

    if (file.fail()) {
        std::cerr << "Error reading weights file." << std::endl;
        return false;
    }
    std::cout << "Loaded weights: "
              << input_weights_[0] << ", " 
              << input_weights_[768*256-1] << "\n"; 
    return true;
    weights_loaded = true;
    return true;
}

void NNUE::extract_features(const chess::Board& board, std::vector<float>& features) const {
    features.assign(768, 0.0f);
    for (int sq = 0; sq < 64; sq++) {
        chess::Square square(sq);
        chess::Piece piece = board.at(square);
        
        // Skip empty squares
        if (piece == chess::Piece::NONE || piece.type() == chess::PieceType::NONE) {
            continue;
        }

        int color_idx = (piece.color() == chess::Color::WHITE) ? 0 : 1;
        int pt_idx = static_cast<int>(piece.type()) - 1; // PieceType: PAWN=1 -> index 0
        
        // Validate piece type index
        if (pt_idx < 0 || pt_idx > 5) {
            // Skip invalid pieces but don't log to avoid spamming
            continue;
        }
        
        int feat_idx = sq * 12 + (pt_idx + 6 * color_idx);
        
        // Validate feature index
        if (feat_idx < 0 || feat_idx >= 768) {
            // Skip invalid indices
            continue;
        }
        if (feat_idx >= 768) {
        std::cerr << "Invalid feature index: " << feat_idx 
                  << " at square " << sq << "\n";
    }
        features[feat_idx] = 1.0f;
    }
}

float NNUE::relu(float x) const {
    return (x > 0) ? x : 0;
}

float NNUE::forward(const std::vector<float>& features) const {
    // Input layer: 768 -> 256
    std::vector<float> layer1(256, 0.0f);
    for (int j = 0; j < 256; j++) {
        float sum = input_biases_[j];
        for (int i = 0; i < 768; i++) {
            sum += features[i] * input_weights_[j * 768 + i];
        }
        layer1[j] = relu(sum);
    }

    // Hidden layer: 256 -> 32
    std::vector<float> layer2(32, 0.0f);
    for (int j = 0; j < 32; j++) {
        float sum = hidden_biases_[j];
        for (int i = 0; i < 256; i++) {
            sum += layer1[i] * hidden_weights_[j * 256 + i];
        }
        layer2[j] = relu(sum);
    }

    // Output layer: 32 -> 1
    float output = output_bias_;
    for (int i = 0; i < 32; i++) {
        output += layer2[i] * output_weights_[i];
    }
    return output;
}

float NNUE::evaluate(const chess::Board& board) const {
    std::vector<float> features;
    extract_features(board, features);
    float score = forward(features);
    return (board.sideToMove() == chess::Color::WHITE) ? score : -score;
}