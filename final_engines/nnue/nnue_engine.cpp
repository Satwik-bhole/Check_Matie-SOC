#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <array>
#include <unordered_map>
#include <random>
#include <ctime>
#include <fstream>
#include "chess-library/include/chess.hpp"
#include "nnue.h"
using namespace std;
using namespace chess;

// Configuration constants
const int MATE_SCORE = 1000000;
const int MAX_DEPTH = 15;
const size_t TT_SIZE = 1 << 24;
const int MAX_TIME = 10000;
const int ASPIRATION_WINDOW = 50;

// Time management parameters
const double TIME_FACTOR = 0.75;
const int MAX_PLY = 64;

// Transposition table
enum Bound { EXACT, UPPER, LOWER };

struct TTEntry {
    uint64_t key = 0;
    int depth = 0;
    int score = 0;
    Bound bound = EXACT;
    Move best_move = Move::NO_MOVE;
};

class TranspositionTable {
private:
    vector<TTEntry> entries;
    size_t size;
    
public:
    TranspositionTable(size_t size) : size(size) {
        entries.resize(size);
    }

    void store(uint64_t key, int depth, int score, Bound bound, Move best_move) {
        size_t index = key % size;
        // Always replace strategy
        entries[index] = {key, depth, score, bound, best_move};
    }

    TTEntry* probe(uint64_t key) {
        size_t index=key%size;
        if (entries[index].key == key) {
            return &entries[index];
        }
        return nullptr;
    }
};

// Material values
constexpr array<int, 6> MATERIAL_VALUE = {
    100,   // Pawn
    320,   // Knight
    330,   // Bishop
    500,   // Rook
    900,   // Queen
    0      // King
};

// Piece-square tables - initialized with zeros
constexpr array<array<int, 64>, 6> mg_psqt = {{
    {0}, {0}, {0}, {0}, {0}, {0}  
}};
constexpr array<array<int, 64>, 6> eg_psqt = {{
    {0}, {0}, {0}, {0}, {0}, {0}
}};

//comprehensive evaluation function
/*int evaluate(const Board& board) {
    int material = 0;
    int pawn_structure = 0;
    int king_safety = 0;
    int development = 0;
    int center_control = 0;
    
    // Arrays to track pawn positions
    array<int, 8> white_pawn_files = {0};
    array<int, 8> black_pawn_files = {0};
    
    // Center squares
    constexpr array<Square, 4> CENTER_SQUARES = {
        Square::SQ_D4, Square::SQ_E4, Square::SQ_D5, Square::SQ_E5
    };
    
    for (int sq = 0; sq < 64; ++sq) {
        auto square = static_cast<Square>(sq);
        Piece piece = board.at(square);
        if (piece == Piece::NONE) continue;

        PieceType pt = piece.type();
        int val = MATERIAL_VALUE[static_cast<int>(pt)];
        int lookup_sq = (piece.color() == Color::WHITE) ? sq : 63 - sq;

        // Center control bonus
        if (find(CENTER_SQUARES.begin(), CENTER_SQUARES.end(), square) != CENTER_SQUARES.end()) {
            if (piece.color() == Color::WHITE) center_control += 15;
            else center_control -= 15;
        }

        if (piece.color() == Color::WHITE) {
            material += val;
            if (pt == PieceType::PAWN) {
                white_pawn_files[square.file()]++;
            }
        } else {
            material -= val;
            if (pt == PieceType::PAWN) {
                black_pawn_files[square.file()]++;
            }
        }
    }
    
    // Development bonuses
    const Bitboard WHITE_BACK_RANK = 0x00000000000000FF;
    const Bitboard BLACK_BACK_RANK = 0xFF00000000000000;
    
    // Knights off back rank
    Bitboard white_knights = board.pieces(PieceType::KNIGHT, Color::WHITE);
    development += 15 * (white_knights & ~WHITE_BACK_RANK).count();
    
    Bitboard black_knights = board.pieces(PieceType::KNIGHT, Color::BLACK);
    development -= 15 * (black_knights & ~BLACK_BACK_RANK).count();
    
    // Bishops off back rank
    Bitboard white_bishops = board.pieces(PieceType::BISHOP, Color::WHITE);
    development += 15 * (white_bishops & ~WHITE_BACK_RANK).count();
    
    Bitboard black_bishops = board.pieces(PieceType::BISHOP, Color::BLACK);
    development -= 15 * (black_bishops & ~BLACK_BACK_RANK).count();
    
    // Castling bonuses
    Square white_king = board.kingSq(Color::WHITE);
    if (white_king == Square::SQ_G1 || white_king == Square::SQ_C1) {
        development += 40;
    }
    
    Square black_king = board.kingSq(Color::BLACK);
    if (black_king == Square::SQ_G8 || black_king == Square::SQ_C8) {
        development -= 40;
    }
    
    // Queen safety - penalize early queen moves
    int total_material = abs(material);
    const int OPENING_THRESHOLD = 6000;
    
    if (total_material > OPENING_THRESHOLD) {
        // White queen penalty
        if (auto white_queen = board.pieces(PieceType::QUEEN, Color::WHITE)) {
            Square sq = white_queen.lsb();
            if (int(sq.rank())>=  2) {  // Penalize beyond 2nd rank
                development -= 15 * (sq.rank() - 1);
            }
        }
        
        // Black queen penalty
        if (auto black_queen = board.pieces(PieceType::QUEEN, Color::BLACK)) {
            Square sq = black_queen.lsb();
            if (int(sq.rank()) <= 5) {  // Penalize beyond 7th rank from black's perspective
                development += 15 * (5 - sq.rank());
            }
        }
    }
    
    // Pawn structure evaluation
    for (int file = 0; file < 8; file++) {
        // Doubled pawns penalty
        if (white_pawn_files[file] > 1) pawn_structure -= 15 * (white_pawn_files[file] - 1);
        if (black_pawn_files[file] > 1) pawn_structure += 15 * (black_pawn_files[file] - 1);
        
        // Isolated pawns penalty
        bool white_isolated = (file == 0 || white_pawn_files[file-1] == 0) && 
                             (file == 7 || white_pawn_files[file+1] == 0);
        bool black_isolated = (file == 0 || black_pawn_files[file-1] == 0) && 
                             (file == 7 || black_pawn_files[file+1] == 0);
        
        if (white_isolated && white_pawn_files[file] > 0) pawn_structure -= 20;
        if (black_isolated && black_pawn_files[file] > 0) pawn_structure += 20;
    }
    
    // King safety - penalty for king in center
    if (int(white_king.file()) >= 2 && int(white_king.file()) <= 5) {
        king_safety -= 20 - (7 - white_king.rank()) * 5;
    }
    if (int(black_king.file()) >= 2 && int(black_king.file()) <= 5) {
        king_safety += 20 - black_king.rank() * 5;
    }
    
    // Combine all components
    int score = material + pawn_structure + king_safety + development + center_control;
    
    return (board.sideToMove() == Color::WHITE) ? score : -score;
}*/


int evaluate(const chess::Board& board) {
    static NNUE nnue;
    static bool init_tried = false;
    static bool weights_available = false;

    if (!init_tried) {
        weights_available = nnue.load_weights("nnue_model.bin");
        init_tried = true;
        if (weights_available) {
            std::cout << "info string NNUE weights loaded successfully" << std::endl;
        } else {
            std::cout << "info string Failed to load NNUE weights, using fallback" << std::endl;
        }
    }

    if (weights_available && nnue.is_initialized()) {
        float nnue_eval = nnue.evaluate(board);
        return static_cast<int>(std::round(nnue_eval));
    }

    // Fallback: material evaluation
    float material = 0.0f;
    static constexpr float piece_values[] = {0, 100, 320, 330, 500, 900, 0}; // PieceType index
    for (int sq = 0; sq < 64; sq++) {
        auto piece = board.at(chess::Square(sq));
        if (piece != chess::Piece::NONE) {
            float value = piece_values[static_cast<int>(piece.type())];
            material += (piece.color() == chess::Color::WHITE) ? value : -value;
        }
    }
    return static_cast<int>(material );
}
// Comprehensive opening book
class OpeningBook {
    private:
    unordered_map<uint64_t, std::vector<Move>> book_moves;
    mt19937 rng;

public:
    OpeningBook() : rng(std::time(nullptr)) {
        // Opening book database 
        const vector<std::pair<std::string, std::string>> OPENING_BOOK = {
            // 1. Ruy Lopez
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4"},
            {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", "e7e5"},
            {"rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2", "g1f3"},
            {"rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2", "b8c6"},
            {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3", "f1b5"},
            {"r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3", "a7a6"},
            {"r1bqkbnr/1ppp1ppp/p1n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 4", "b5a4"},
            {"r1bqkbnr/1ppp1ppp/p1n5/4p3/B3P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 1 4", "b7b5"},

            // 2. Italian Game
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4"},
            {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", "e7e5"},
            {"rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 1 2", "f1c4"},
            {"rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKC1R b KQkq - 2 2", "b8c6"},
            {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKC1R w KQkq - 3 3", "c2c3"},
            {"r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQKC1R b KQkq - 0 3", "d7d6"},
            {"r1bqkbnr/ppp2ppp/2n1p3/4p3/2B1P3/5N2/PPPP1PPP/RNBQKC1R w KQkq - 0 4", "d2d4"},
            {"r1bqkbnr/ppp2ppp/2n1p3/4p3/2B1P3/3PNN2/PPP2PPP/R1BQKC1R b KQkq - 1 4", "e5d4"},

            // 3. French Defense
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4"},
            {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", "e7e6"},
            {"rnbqkbnr/pppp1ppp/4p3/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "d2d4"},
            {"rnbqkbnr/pppp1ppp/4p3/8/3PP3/8/PPP2PPP/RNBQKBNR b KQkq - 0 2", "d7d5"},
            {"rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/8/PPP2PPP/RNBQKBNR w KQkq - 0 3", "e4d5"},
            {"rnbqkbnr/ppp2ppp/4p3/3P4/3pP3/8/PPP2PPP/RNBQKBNR b KQkq - 0 3", "e6d5"},
            {"rnbqkbnr/ppp3pp/4p3/3p4/3pP3/8/PPP2PPP/RNBQKBNR w KQkq - 0 4", "c2c4"},
            {"rnbqkbnr/ppp3pp/4p3/3p4/2PPP3/8/PP3PPP/RNBQKBNR b KQkq - 0 4", "c7c6"},

            // 4. Caro-Kann Defense
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4"},
            {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", "c7c6"},
            {"rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "d2d4"},
            {"rnbqkbnr/pp1ppppp/8/2p5/3PP3/8/PPP2PPP/RNBQKBNR b KQkq - 0 2", "d7d5"},
            {"rnbqkbnr/pp2pppp/3p4/2p5/3PP3/8/PPP2PPP/RNBQKBNR w KQkq - 0 3", "e4d5"},
            {"rnbqkbnr/pp2pppp/3p4/2p5/3P4/8/PPP2PPP/RNBQKBNR b KQkq - 0 3", "c6d5"},
            {"rnbqkbnr/pp2pppp/8/2p5/3P4/8/PPP2PPP/RNBQKBNR w KQkq - 0 4", "c2c4"},
            {"rnbqkbnr/pp2pppp/8/2p5/2PP4/8/PP3PPP/RNBQKBNR b KQkq - 0 4", "e7e6"},

            // 5. Sicilian Defense
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4"},
            {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", "c7c5"},
            {"rnbqkbnr/1ppppppp/8/p7/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "g1f3"},
            {"rnbqkbnr/1ppppppp/8/p7/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2", "d7d6"},
            {"rnbqkbnr/1ppp1ppp/8/p2p4/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 3", "d2d4"},
            {"rnbqkbnr/1ppp1ppp/8/p2p4/3PP3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 0 3", "c5d4"},
            {"rnbqkbnr/1ppp1ppp/8/p7/3pP3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 4", "f3d4"},
            {"rnbqkbnr/1ppp1ppp/8/p7/3NP3/8/PPPP1PPP/RNBQKB1R b KQkq - 0 4", "g8f6"},

            // 6. Pirc Defense
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4"},
            {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", "d7d6"},
            {"rnbqkbnr/p1pppppp/1p6/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "d2d4"},
            {"rnbqkbnr/p1pppppp/1p6/8/3PP3/8/PPP2PPP/RNBQKBNR b KQkq - 0 2", "g8f6"},
            {"rnbqkbnr/p1pp1ppp/1p6/5n2/3PP3/8/PPP2PPP/RNBQKBNR w KQkq - 1 3", "b1c3"},
            {"rnbqkbnr/p1pp1ppp/1p6/5n2/2NPP3/8/PP3PPP/R1BQKBNR b KQkq - 1 3", "g7g6"},
            {"rnbqkb1r/p1pp1ppp/1pn2n2/5n2/2NPP3/8/PP3PPP/R1BQKBNR w KQkq - 0 4", "f1e2"},
            {"rnbqkb1r/p1pp1ppp/1pn2n2/5n2/2NPP3/4B3/PP3PPP/R2QKBNR b KQkq - 1 4", "f8g7"},

            // 7. Queen's Gambit Declined
            {"rnbqkbnr/ppp1pppp/8/3p4/2P5/8/PP2PPPP/RNBQKBNR w KQkq - 0 2", "d2d4"},
            {"rnbqkbnr/ppp1pppp/8/3p4/2P5/8/PP2PPPP/RNBQKBNR b KQkq - 0 2", "e7e6"},
            {"rnbqkbnr/ppp2ppp/4pn2/3p4/2P5/8/PP2PPPP/RNBQKBNR w KQkq - 0 3", "g1f3"},
            {"rnbqkbnr/ppp2ppp/4pn2/3p4/2P5/8/PP2PPPP/RNBQKBNR b KQkq - 1 3", "c7c6"},
            {"rnbqkbnr/pp2pppp/3p1n2/3p4/2PP4/5N2/PP2PPPP/RNBQKB1R w KQkq - 2 4", "c4c5"}, 
            {"rnbqkbnr/pp2pppp/3p1n2/2Pp4/8/5N2/PP2PPPP/RNBQKB1R b KQkq - 0 4", "b7b6"},
            {"rnbqkbnr/1p2pppp/p2p1n2/2Pp4/8/5N2/P1Q1PPPP/RNB1KB1R w KQkq - 0 5", "b2b4"},
            {"rnbqkbnr/1p2pppp/p2p1n2/2Pp4/1P6/5N2/P2QPPPP/RNB1KB1R b KQkq - 0 5", "a7a5"},

            // 8. Slav Defense
            {"rnbqkbnr/ppp1pppp/8/3p4/2P5/8/PP2PPPP/RNBQKBNR w KQkq - 0 2", "d2d4"},
            {"rnbqkbnr/ppp1pppp/8/3p4/2P5/8/PP2PPPP/RNBQKBNR b KQkq - 0 2", "c7c6"},
            {"rnbqkbnr/pp2pppp/2p5/3p4/2P5/8/PP2PPPP/RNBQKBNR w KQkq - 0 3", "g1f3"},
            {"rnbqkbnr/pp2pppp/2p5/3p4/2P5/5N2/PP2PPPP/RNBQKB1R b KQkq - 1 3", "g8f6"},
            {"rnbqkb1r/pp2pppp/2p2n2/3p4/2P5/5N2/PP2PPPP/RNBQKB1R w KQkq - 2 4", "b1c3"},
            {"rnbqkb1r/pp2pppp/2p2n2/3p4/2P5/2N2N2/PP2PPPP/R1BQKB1R b KQkq - 3 4", "e7e6"},
            {"rnbqkb1r/pp3ppp/2p1pn2/3p4/2P5/2N2N2/PP2PPPP/R1BQKB1R w KQkq - 0 5", "e2e3"},
            {"rnbqkb1r/pp3ppp/2p1pn2/3p4/2P5/2N2N2/PP1P1PPP/R1BQKB1R b KQkq - 0 5", "d5c4"},

            // 9. King's Indian Defense
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "d2d4"},
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1", "g8f6"},
            {"rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 1 2", "c2c4"},
            {"rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2", "g7g6"},
            {"rnbqkb1r/pppppppp/5n2/7p/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 3", "b1c3"},
            {"rnbqkb1r/pppp1ppp/5n2/4p3/2PP4/2N5/PP2PPPP/R1BQKBNR b KQkq - 1 3", "f8g7"},
            {"rnbq1rk1/pppp1ppp/5n2/4p3/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 2 4", "e2e4"},
            {"rnbq1rk1/pppp1ppp/5n2/4p3/2PPP3/2N5/PP3PPP/R1BQKBNR b KQkq - 0 4", "d7d6"},

            // 10. Nimzo-Indian Defense
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "d2d4"},
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1", "g8f6"},
            {"rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 1 2", "c2c4"},
            {"rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2", "e7e6"},
            {"rnbqkb1r/ppp1pppp/5n2/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 3", "b1c3"},
            {"rnbqkb1r/ppp1pppp/5n2/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR b KQkq - 1 3", "b8c6"},
            {"r1bqkb1r/ppp1pppp/2n2n2/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 2 4", "c1g5"},
            {"r1bqkb1r/ppp1pppp/2n2n2/3p2B1/2PP4/2N5/PP2PPPP/R2QKBNR b KQkq - 3 4", "e7e6"},

            // 11. Queen's Indian Defense
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "d2d4"},
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1", "g8f6"},
            {"rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 1 2", "c2c4"},
            {"rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2", "e7e6"},
            {"rnbqkb1r/ppp1pppp/5n2/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 3", "g1f3"},
            {"rnbqkb1r/ppp1pppp/5n2/3p4/2PP4/5N2/PP2PPPP/RNBQKB1R b KQkq - 1 3", "b8c6"},
            {"rnbqkb1r/1pp1pppp/p4n2/3p4/2PP4/5N2/PP2PPPP/RNBQKB1R w KQkq - 2 4", "b1c3"},
            {"rnbqkb1r/1pp1pppp/p4n2/3p4/2PP4/2N2N2/PP2PPPP/RNBQKB1R b KQkq - 3 4", "b7b6"},

            // 12. Grünfeld Defense
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "d2d4"},
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1", "g8f6"},
            {"rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 1 2", "c2c4"},
            {"rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2", "g7g6"},
            {"rnbqkb1r/pppppppp/5n2/7p/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 3", "g1f3"},
            {"rnbqkb1r/pppppp1p/5n2/7p/2PP4/5N2/PP2PPPP/RNBQKB1R b KQkq - 1 3", "d7d5"},
            {"rnbqkb1r/ppp1pp1p/5n2/3p3p/2PP4/5N2/PP2PPPP/RNBQKB1R w KQkq - 0 4", "c4d5"},
            {"rnbqkb1r/ppp1pp1p/5n2/3p3p/2PPr3/5N2/PP2PPPP/RNBQKB1R b KQkq - 0 4", "f6d5"},

            // 13. Modern Defense
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4"},
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1", "g8f6"},
            {"rnbqkb1r/pppppppp/5n2/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2", "d2d4"},
            {"rnbqkb1r/pppppppp/5n2/8/3PP3/8/PPP2PPP/RNBQKBNR b KQkq - 0 2", "g7g6"},
            {"rnbqkb1r/pppppppp/5n2/8/3PP3/5N2/PPP2PPP/RNBQKB1R w KQkq - 1 3", "b1c3"},
            {"rnbqkb1r/pppppppp/5n2/8/3PP3/2N2N2/PPP2PPP/RNBQKB1R b KQkq - 2 3", "f8g7"},
            {"rnbq1rk1/pppppppp/5n2/8/3PP3/2N2N2/PPP2PPP/RNBQKB1R w KQkq - 3 4", "f1e2"},
            {"rnbq1rk1/pppppppp/5n2/8/3PP3/2N2N2/PPP2PPP/RNBQK2R b KQkq - 4 4", "d7d6"},

            // 14. Scandinavian Defense
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4"},
            {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", "d7d5"},
            {"rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "e4d5"},
            {"rnbqkbnr/ppp1pppp/8/3P4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2", "d8d5"},
            {"rnbqkbnr/ppp1pppp/8/3P4/8/8/PPPP1PPP/RNBQKBNR w KQkq - 1 3", "b1c3"},
            {"rnbqkbnr/ppp1pppp/8/3P4/2N5/8/PPPP1PPP/R1BQKBNR b KQkq - 1 3", "d5a5"},
            {"rnbqkbnr/ppp1pppp/8/3P4/2N5/8/PPPP1PPP/R1BQKBNR w KQkq - 2 4", "d2d4"},
            {"rnbqkbnr/ppp1pppp/3p4/8/2N5/8/PPPP1PPP/R1BQKBNR b KQkq - 0 4", "a5e5"},

            // 15. Alekhine's Defense
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4"},
            {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", "g8f6"},
            {"rnbqkb1r/pppppppp/5n2/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2", "e4e5"},
            {"rnbqkb1r/pppppppp/5n2/4P3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2", "f6d5"},
            {"rnbqkb1r/pppppppp/8/3nP3/8/8/PPPP1PPP/RNBQKBNR w KQkq - 1 3", "c2c4"},
            {"rnbqkb1r/pppppppp/8/3nP3/2P5/8/PP1P1PPP/RNBQKBNR b KQkq - 0 3", "d5b6"},
            {"rnbqkb1r/pppppppp/1n6/4P3/2P5/8/PP1P1PPP/RNBQKBNR w KQkq - 1 4", "d2d4"},
            {"rnbqkb1r/pppppppp/1n6/4P3/2PP4/8/PP3PPP/RNBQKBNR b KQkq - 0 4", "d7d6"},

            // 16. English Opening
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "c2c4"},
            {"rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR b KQkq - 0 1", "e7e5"},
            {"rnbqkbnr/pppppppp/8/4p3/2P5/8/PP1PPPPP/RNBQKBNR w KQkq - 0 2", "g1f3"},
            {"rnbqkbnr/pppppppp/8/4p3/2P5/5N2/PP1PPPPP/RNBQKB1R b KQkq - 1 2", "b8c6"},
            {"r1bqkbnr/pppppppp/2n5/4p3/2P5/5N2/PP1PPPPP/RNBQKB1R w KQkq - 2 3", "g2g3"},
            {"r1bqkbnr/pppppppp/2n5/4p3/2P5/2N2N2/PP1PPPPP/R1BQKB1R b KQkq - 2 3", "d7d6"},
            {"r1bqkbnr/ppp1pppp/2n1b3/4p3/2P5/2N2N2/PP1PPPPP/R1BQKB1R w KQkq - 3 4", "f1g2"},
            {"r1bqkbnr/ppp1pppp/2n1b3/4p3/2P5/2N2N2/PP1PP1PP/R1BQKB1R b KQkq - 4 4", "f8e7"},

            // 17. Catalan Opening
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "d2d4"},
            {"rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1", "d7d5"},
            {"rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 2", "c2c4"},
            {"rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2", "e7e6"},
            {"rnbqkbnr/ppp2ppp/4p3/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 3", "g2g3"},
            {"rnbqkbnr/ppp2ppp/4p3/3p4/2PP4/6P1/PP2PP1P/RNBQKBNR b KQkq - 0 3", "g8f6"},
            {"rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/6P1/PP2PP1P/RNBQKBNR w KQkq - 1 4", "f1g2"},
            {"rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/6P1/PP2PPBP/RNBQK1NR b KQkq - 2 4", "f8e7"},

            // 18. Vienna Game
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4"},
            {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1", "e7e5"},
            {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2", "b1c3"},
            {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 1 2", "g8f6"},
            {"rnbqkbnr/pppppppp/8/8/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3", "f1c4"},
            {"rnbqkbnr/pppppppp/8/8/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 2 3", "b8c6"},
            {"r1bqkbnr/pppppppp/2n5/8/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 3 4", "d2d3"},
            {"r1bqkbnr/pppppppp/2n5/8/3PP3/5N2/PPPP1PPP/RNBQK2R b KQkq - 0 4", "f8c5"},

            // 19. Bird's Opening
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "f2f4"},
            {"rnbqkbnr/pppppppp/8/8/5P2/8/PPPPP1PP/RNBQKBNR b KQkq - 0 1", "d7d5"},
            {"rnbqkbnr/ppp1pppp/8/3p4/5P2/8/PPPPP1PP/RNBQKBNR w KQkq - 0 2", "e2e3"},
            {"rnbqkbnr/ppp1pppp/8/3p4/5P2/4P3/PPPP2PP/RNBQKBNR b KQkq - 0 2", "g8f6"},
            {"rnbqkb1r/ppp1pppp/5n2/3p4/5P2/4P3/PPPP2PP/RNBQKBNR w KQkq - 1 3", "g1f3"},
            {"rnbqkb1r/ppp1pppp/5n2/3p4/5P2/4PN2/PPPP2PP/RNBQKB1R b KQkq - 2 3", "c8g4"},
            {"rnbqk2r/ppp1ppbp/5n2/3p4/5P2/4PN2/PPPP2PP/RNBQKB1R w KQkq - 3 4", "f1e2"},
            {"rnbqk2r/ppp1ppbp/5n2/3p4/5P2/4PN2/PPPP2PP/RNBQKB1R b KQkq - 4 4", "e8g8"},

            // 20. Petrov's Defense
            {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4"},
            {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1", "e7e5"},
            {"rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPPPPPP/RNBQKBNR w KQkq - 0 2", "g1f3"},
            {"rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPPPPPP/RNBQKB1R b KQkq - 1 2", "g8f6"},
            {"rnbqkbnr/pppp1ppp/5n2/4p3/4P3/5N2/PPPPPPPP/RNBQKB1R w KQkq - 2 3", "f3e5"},
            {"rnbqkbnr/pppp1ppp/5n2/8/4p3/4N3/PPPPPPPP/RNBQKB1R b KQkq - 0 3", "d7d6"},
            {"rnbqkbnr/ppp2ppp/5n2/3p4/4p3/4N3/PPPPPPPP/RNBQKB1R w KQkq - 0 4", "e3g4"},
            {"rnbqkbnr/ppp2ppp/5n2/3p4/4p3/4N3/PPPPPPPP/RNBQKB1R b KQkq - 1 4", "f6e4"}
        };

        Board temp_board;
        
        // Precompute book positions
        for (const auto& [fen, move_str] : OPENING_BOOK) {
            
            temp_board.setFen(fen);
            Move move = uci::uciToMove(temp_board, move_str);
            if (move != Move::NO_MOVE) {
                uint64_t hash = temp_board.hash();
                book_moves[hash].push_back(move);
            }
        }
    }

    Move get_move(const Board& board) {
        uint64_t current_hash = board.hash();
        auto it = book_moves.find(current_hash);
        if (it != book_moves.end() && !it->second.empty()) {
            return it->second[0]; 
        }
        return Move::NO_MOVE;
    }
};

// move ordering
class MoveOrderer {
private:
    array<array<Move, 2>, 64> killer_moves;
    array<array<int, 64>, 2> history;  // [color][to_square]
    
public:
    MoveOrderer() {
        for (auto& killers : killer_moves) {
            killers[0] = Move::NO_MOVE;
            killers[1] = Move::NO_MOVE;
        }
        for (auto& arr : history) {
            fill(arr.begin(), arr.end(), 0);
        }
    }
    
    void record_killer(int ply, Move move) {
        if (ply < 64 && move != Move::NO_MOVE) {
            if (killer_moves[ply][0] != move) {
                killer_moves[ply][1] = killer_moves[ply][0];
                killer_moves[ply][0] = move;
            }
        }
    }
    
    void record_history(Color color, Move move, int depth) {
        int color_idx = (color == Color::WHITE) ? 0 : 1;
        history[color_idx][move.to().index()] += depth * depth;
    }
    
    int score_move(Board& board, Move move, Move tt_move, int ply) {
        // 1. TT move
        if (move == tt_move) {
            return 1000000;
        }
        
        // 2. Capture ordering - MVV/LVA(Most Valuable Victim - Least Valuable Attacker)
        if (board.isCapture(move)) {
            Piece captured = board.at(move.to());
            Piece attacker = board.at(move.from());
            
            if (captured != Piece::NONE) {
                int victim_val = MATERIAL_VALUE[static_cast<int>(captured.type())];
                int aggressor_val = MATERIAL_VALUE[static_cast<int>(attacker.type())];
                return 900000 + (victim_val * 10) - aggressor_val;
            }
        }
        
        // 3. Killer moves
        if (ply < MAX_PLY) {
            if (move == killer_moves[ply][0]) return 800000;
            if (move == killer_moves[ply][1]) return 750000;
        }
        
        // 4. History heuristic
        int color_idx = (board.sideToMove() == Color::WHITE) ? 0 : 1;
        return history[color_idx][move.to().index()];
    }
};

// quiescence search
int quiescence(Board& board, int alpha, int beta, int& nodes_searched, int qdepth = 0) {
    const int MAX_QDEPTH = 20;  //quiescence depth
    nodes_searched++;
    
    // Evaluate position first
    int stand_pat = evaluate(board);
    
    if (stand_pat >= beta) return beta;
    if (alpha < stand_pat) alpha = stand_pat;
    
    if (qdepth >= MAX_QDEPTH) {
        return stand_pat;
    }
    
    Movelist moves;
    if (board.inCheck()) {
        movegen::legalmoves(moves, board);
    } else {
        movegen::legalmoves<movegen::MoveGenType::CAPTURE>(moves, board);
        Movelist promotions;
        movegen::legalmoves<movegen::MoveGenType::QUIET>(promotions, board);
        
    }
    
    // MVV/LVA ordering(Most Valuable Victim – Least Valuable Attacker)
    vector<pair<int, Move>> scored_moves;
    for (int i = 0; i < moves.size(); i++) {
        int score = 0;
        Piece captured = board.at(moves[i].to());
        
        if (board.isCapture(moves[i]) && captured != Piece::NONE) {
            Piece attacker = board.at(moves[i].from());
            score = MATERIAL_VALUE[static_cast<int>(captured.type())] * 10 - 
                    MATERIAL_VALUE[static_cast<int>(attacker.type())];
        } 
        scored_moves.push_back({score, moves[i]});
    }
    // Sort moves
    sort(scored_moves.begin(), scored_moves.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });
    // search moves
    for (auto& [score, move] : scored_moves) {
        // delta pruning - only for captures when not in check
        if (!board.inCheck() && board.isCapture(move)) {
            Piece captured = board.at(move.to());
            if (captured != Piece::NONE) {
                int capture_value = MATERIAL_VALUE[static_cast<int>(captured.type())];
                if (stand_pat + capture_value + 100 < alpha) {
                    continue;
                }
            }
        }
        board.makeMove(move);
        int eval = -quiescence(board, -beta, -alpha, nodes_searched, qdepth + 1);
        board.unmakeMove(move);
        if (eval >= beta) return beta;
        if (eval > alpha) alpha = eval;
    }
    return alpha;
}

//alpha-beta search 
int alpha_beta(Board& board, int depth, int alpha, int beta, 
               int ply, TranspositionTable& tt, int& nodes_searched, 
               MoveOrderer& move_orderer, bool is_root = false) {
    // check max ply (later used so that it can be used to find least move mate)
    if (ply >= MAX_PLY) {
        return evaluate(board);
    }
    nodes_searched++;
    // TT Lookup
    uint64_t hash_key = board.hash();
    TTEntry* entry = tt.probe(hash_key);
    Move tt_move = (entry) ? entry->best_move : Move::NO_MOVE;
    // TT cut - adjust mate scores for ply(for mates)
    if (!is_root && entry && entry->depth >= depth) {
        int score = entry->score;
        // adjust mate scores for current ply
        if (score > MATE_SCORE - MAX_PLY) score -= ply;
        if (score < -MATE_SCORE + MAX_PLY) score += ply;
        if (entry->bound == EXACT) {
            return score;
        } else if (entry->bound == LOWER) {
            alpha = max(alpha, score);
        } else if (entry->bound == UPPER) {
            beta = min(beta, score);
        }
        if (alpha >= beta) {
            return score;
        }
    }
    bool in_check = board.inCheck();
    if (in_check) {
        depth++;  // extend search when in check
    }
    // Leaf node: use quiescence search(to evaluate if capture is good or not etc)
    if (depth <= 0) {
        return quiescence(board, alpha, beta, nodes_searched);
    }
    // Generate moves
    Movelist moves;
    movegen::legalmoves(moves, board);
    // Terminal state check
    if (moves.size() == 0) {
        if (in_check) {
            return -MATE_SCORE + ply;  // Checkmate
        }
        return 0;  // Stalemate
    }
    // move ordering
    vector<pair<int, Move>> scored_moves;
    for (int i = 0; i < moves.size(); i++) {
        int score = move_orderer.score_move(board, moves[i], tt_move, ply);
        scored_moves.push_back({score, moves[i]});
    }
    // sort moves by score descending
    sort(scored_moves.begin(), scored_moves.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });
    int best_score = -MATE_SCORE - 1;
    Move best_move = scored_moves[0].second;
    int original_alpha = alpha;
    for (int i = 0; i < scored_moves.size(); i++) {
        auto& [score, move] = scored_moves[i];
        board.makeMove(move);
        int eval = -alpha_beta(board, depth - 1, -beta, -alpha, 
                              ply + 1, tt, nodes_searched, move_orderer);
        board.unmakeMove(move);
        if (eval > best_score) {
            best_score = eval;
            best_move = move;
            if (eval > alpha) {
                alpha = eval;
                if (eval >= beta) {
                    // Record killer and history(moves causing alpha-beta cutoff or previous search best moves)
                    move_orderer.record_killer(ply, move);
                    move_orderer.record_history(board.sideToMove(), move, depth);
                    break;
                }
            }
        }
    }
    // Adjust mate scores for storage
    int score_to_store = best_score;
    if(best_score > MATE_SCORE - MAX_PLY) score_to_store += ply;
    if(best_score < -MATE_SCORE + MAX_PLY) score_to_store -= ply;
    // TT Store
    Bound bound;
    if (best_score <= original_alpha) {
        bound = UPPER;
    } else if (best_score >= beta) {
        bound = LOWER;
    } else {
        bound = EXACT;
    }
    tt.store(hash_key, depth, score_to_store, bound, best_move);
    return best_score;
}
Move iterative_deepening(Board& board, TranspositionTable& tt, int max_depth, int max_time) {
    Move best_move = Move::NO_MOVE;
    int best_score = -MATE_SCORE - 1;
    MoveOrderer move_orderer;
    int total_nodes = 0;
    int aspiration_window = ASPIRATION_WINDOW;
    
    auto start_time = chrono::steady_clock::now();
    Move prev_best_move = Move::NO_MOVE;
    int prev_best_score = best_score;
    
    for (int depth = 1; depth <= max_depth; depth++) {
        int nodes_this_depth = 0;
        int alpha = -MATE_SCORE;
        int beta = MATE_SCORE;
        // use aspiration window after first iteration(to narrow down the search range)
        if (depth > 4) {
            alpha = prev_best_score - aspiration_window;
            beta = prev_best_score + aspiration_window;
        }
        // Check time before starting new depth
        auto now = chrono::steady_clock::now();
        auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - start_time).count();
        if (max_time > 0 && elapsed > max_time * TIME_FACTOR) {
            return prev_best_move;
        }
        int score = best_score;
        bool research = false;
        do {
            research = false;
            score = alpha_beta(board, depth, alpha, beta, 0, tt, nodes_this_depth, move_orderer, true);
            if (score <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = max(alpha - aspiration_window, -MATE_SCORE);
                research = true;
            } else if (score >= beta) {
                beta = min(beta + aspiration_window, MATE_SCORE);
                research = true;
            }
        } while (research);
        best_score = score;
        total_nodes += nodes_this_depth;
        TTEntry* entry = tt.probe(board.hash());
        if (entry) {
            best_move = entry->best_move;
        }
        Movelist legal_moves;
        movegen::legalmoves(legal_moves, board);
        bool valid_pv = false;
        for (int i = 0; i < legal_moves.size(); i++) {
            if (legal_moves[i] == best_move) {
                valid_pv = true;
                break;
            }
        }
        if (!valid_pv && legal_moves.size() > 0) 
        { 
            best_move = legal_moves[0]; 
        }
        // Save best move from this depth
        prev_best_move = best_move;
        prev_best_score = best_score;
        // uci protocol for analysing stats/cutchess compatible
        // UCI info output
        if (elapsed == 0) elapsed = 1;  // Prevent division by zero
        cout << "info depth " << depth << " score cp " << best_score;
        if (abs(best_score) > MATE_SCORE - MAX_PLY) {
            // Convert to mate in ply
            int mate_in = (MATE_SCORE - abs(best_score) + 1) / 2;
            cout << " mate " << ((best_score > 0) ? mate_in : -mate_in);
        } else {
            cout << " nodes " << nodes_this_depth << " nps " 
                 << (nodes_this_depth * 1000) / elapsed
                 << " time " << elapsed;
        }
        cout << " pv " << uci::moveToUci(best_move) << endl;
        
        // Check time after completing depth
        now = chrono::steady_clock::now();
        elapsed = chrono::duration_cast<chrono::milliseconds>(now - start_time).count();
        if (max_time > 0 && elapsed > max_time * TIME_FACTOR) {
            return best_move;
        }
        // Adjust aspiration window(higher depth make aspiration window greater)
        if (depth > 4) {
            aspiration_window += ASPIRATION_WINDOW / 2;
        }
    }
    return best_move;
}

int main() {
    Board board;
    board.setFen(chess::constants::STARTPOS);
    size_t tt_size = TT_SIZE;
    TranspositionTable tt(tt_size);
    int max_search_depth = MAX_DEPTH;
    OpeningBook book;
    int allocated_time = MAX_TIME;
    bool nnue_preloaded = false;
    Board temp;
    temp.setFen(chess::constants::STARTPOS);
    evaluate(temp);
    string line;
    while (getline(cin, line)) {
        stringstream ss(line);
        string cmd;
        ss >> cmd;
        //uci protocol 
        if (cmd == "quit") break;
        
        if (cmd == "uci") {
            cout << "id name OptimizedChessEngine\n";
            cout << "id author ChessOptimizer\n";
            cout << "option name Hash type spin default 256 min 1 max 16384\n";
            cout << "option name Depth type spin default " << MAX_DEPTH 
                 << " min 1 max 50\n";
            cout << "uciok" << endl;
        }
        else if (cmd == "isready") {
            if (!nnue_preloaded) {
                Board temp;
                temp.setFen(chess::constants::STARTPOS);
                evaluate(temp);
                nnue_preloaded = true;
            }
            cout << "readyok" << endl;
        }
        else if (cmd == "ucinewgame") {
            board.setFen(chess::constants::STARTPOS);
            tt = TranspositionTable(tt_size);
        }
        else if (cmd == "position") {
            string arg;
            ss >> arg;
            if (arg == "startpos") {
                board.setFen(chess::constants::STARTPOS);
                if (ss >> arg && arg == "moves") {
                    while (ss >> arg) {
                        Move m = uci::uciToMove(board, arg);
                        if (m != Move::NO_MOVE) {
                            board.makeMove(m);
                        }
                    }
                }
            } 
            else if (arg == "fen") {
                string fen;
                while (ss >> arg && arg != "moves") {
                    fen += arg + " ";
                }
                board.setFen(fen);
                if (arg == "moves") {
                    while (ss >> arg) {
                        Move m = uci::uciToMove(board, arg);
                        if (m != Move::NO_MOVE) {
                            board.makeMove(m);
                        }
                    }
                }
            }
        }
        else if (cmd == "go"){
            // Check opening book first
            Move book_move = book.get_move(board);
            if (book_move != Move::NO_MOVE) {
                cout << "bestmove " << uci::moveToUci(book_move) << endl;
                continue;
            }
            string param;
            int depth = max_search_depth;
            allocated_time = MAX_TIME;  // Reset to default
            while (ss >> param) {
                if (param == "depth") {
                    ss >> depth;
                    depth = min(depth, MAX_DEPTH);
                } else if (param == "movetime") {
                    ss >> allocated_time;
                }
            }
            // Start search
            Move best_move = iterative_deepening(board, tt, depth, allocated_time);
            // Final validation
            Movelist legal_moves;
            movegen::legalmoves(legal_moves, board);
            bool valid_move = false;
            for (int i = 0; i < legal_moves.size(); i++) {
                if (legal_moves[i] == best_move) {
                    valid_move = true;
                    break;
                }
            }
            if (!valid_move && legal_moves.size() > 0) {
                best_move = legal_moves[0];
            }
            cout << "bestmove " << uci::moveToUci(best_move) << endl;
        }
        else if (cmd == "setoption") {
            string name, value;
            ss >> name;  // Skip "name"
            ss >> name;
            if (name == "Hash") {
                ss >> value;  // Skip "value"
                ss >> value;
                int mb = stoi(value);
                tt_size = (static_cast<size_t>(mb) * 1024 * 1024) / sizeof(TTEntry);
                tt = TranspositionTable(tt_size);
            }
            else if (name == "Depth") {
                ss >> value;  // Skip "value"
                ss >> value;
                max_search_depth = min(stoi(value), MAX_DEPTH);
            }
        }
    }
}