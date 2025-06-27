#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <array>
#include <unordered_map>
#include <random>
#include <ctime>
#include "chess-library/include/chess.hpp"
using namespace std;
using namespace chess;

const int MATE_SCORE = 1000000;
const int MAX_DEPTH = 6;  
const size_t TT_SIZE = 1 << 24;  

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
        entries[index] = {key, depth, score, bound, best_move};
    }

    TTEntry* probe(uint64_t key) {
        size_t index = key % size;
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

// Piece-square tables (Midgame and Endgame)
constexpr array<array<int, 64>, 6> mg_psqt = {{
    // Pawn
    {   0,   0,   0,   0,   0,   0,   0,   0,
       98, 134,  61,  95,  68, 126,  34, -11,
       -6,   7,  26,  31,  65,  56,  25, -20,
      -14,  13,   6,  21,  23,  12,  17, -23,
      -27,  -2,  -5,  12,  17,   6,  10, -25,
      -26,  -4,  -4, -10,   3,   3,  33, -12,
      -35,  -1, -20, -23, -15,  24,  38, -22,
        0,   0,   0,   0,   0,   0,   0,   0 },
    // Knight
    { -167, -89, -34, -49,  61, -97, -15, -107,
       -73, -41,  72,  36,  23,  62,   7,  -17,
       -47,  60,  37,  65,  84, 129,  73,   44,
        -9,  17,  19,  53,  37,  69,  18,   22,
       -13,   4,  16,  13,  28,  19,  21,   -8,
       -23,  -9,  12,  10,  19,  17,  25,  -16,
       -29, -53, -12,  -3,  -1,  18, -14,  -19,
      -105, -21, -58, -33, -17, -28, -19,  -23 },
    // Bishop
    { -29,   4, -82, -37, -25, -42,   7,  -8,
      -26,  16, -18, -13,  30,  59,  18, -47,
      -16,  37,  43,  40,  35,  50,  37,  -2,
       -4,   5,  19,  50,  37,  37,   7,  -2,
       -6,  13,  13,  26,  34,  12,  10,   4,
        0,  15,  15,  15,  14,  27,  18,  10,
        4,  15,  16,   0,   7,  21,  33,   1,
      -33,  -3, -14, -21, -13, -12, -39, -21 },
    // Rook
    {  32,  42,  32,  51, 63,  9,  31,  43,
       27,  32,  58,  62, 80, 67,  26,  44,
       -5,  19,  26,  36, 17, 45,  61,  16,
      -24, -11,   7,  26, 24, 35,  -8, -20,
      -36, -26, -12,  -1,  9, -7,   6, -23,
      -45, -25, -16, -17,  3,  0,  -5, -33,
      -44, -16, -20,  -9, -1, 11,  -6, -71,
      -19, -13,   1,  17, 16,  7, -37, -26 },
    // Queen
    { -28,   0,  29,  12,  59,  44,  43,  45,
      -24, -39,  -5,   1, -16,  57,  28,  54,
      -13, -17,   7,   8,  29,  56,  47,  57,
      -27, -27, -16, -16,  -1,  17,  -2,   1,
       -9, -26,  -9, -10,  -2,  -4,   3,  -3,
      -14,   2, -11,  -2,  -5,   2,  14,   5,
      -35,  -8,  11,   2,   8,  15,  -3,   1,
       -1, -18,  -9,  10, -15, -25, -31, -50 },
    // King (Modified to encourage castling)
    { -65,  23,  16, -150, -150, -34,   2,  13,  // Strong center penalty
       29,  -1, -20,   -7,   -8,  -4, -38, -29,
       -9,  24,   2,  -16,  -20,   6,  22, -22,
      -17, -20, -12,  -27,  -30, -25, -14, -36,
      -49,  -1, -27,  -39,  -46, -44, -33, -51,
      -14, -14, -22,  -46,  -44, -30, -15, -27,
        1,   7,  -8,  -64,  -43, -16,   9,   8,
      -15,  36,  12,  -54,    8, -28,  24,  14 }
}};

constexpr array<array<int, 64>, 6> eg_psqt = {{
    // Pawn
    {   0,   0,   0,   0,   0,   0,   0,   0,
      178, 173, 158, 134, 147, 132, 165, 187,
       94, 100,  85,  67,  56,  53,  82,  84,
       32,  24,  13,   5,  -2,   4,  17,  17,
       13,   9,  -3,  -7,  -7,  -8,   3,  -1,
        4,   7,  -6,   1,   0,  -5,  -1,  -8,
       13,   8,   8,  10,  13,   0,   2,  -7,
        0,   0,   0,   0,   0,   0,   0,   0 },
    // Knight
    { -58, -38, -13, -28, -31, -27, -63, -99,
      -25,  -8, -25,  -2,  -9, -25, -24, -52,
      -24, -20,  10,   9,  -1,  -9, -19, -41,
      -17,   3,  22,  22,  22,  11,   8, -18,
      -18,  -6,  16,  25,  16,  17,   4, -18,
      -23,  -3,  -1,  15,  10,  -3, -20, -22,
      -42, -20, -10,  -5,  -2, -20, -23, -44,
      -29, -51, -23, -15, -22, -18, -50, -64 },
    // Bishop
    { -14, -21, -11,  -8, -7,  -9, -17, -24,
       -8,  -4,   7, -12, -3, -13,  -4, -14,
        2,  -8,   0,  -1, -2,   6,   0,   4,
       -3,   9,  12,   9, 14,  10,   3,   2,
       -6,   3,  13,  19,  7,  10,  -3,  -9,
      -12,  -3,   8,  10, 13,   3,  -7, -15,
      -14, -18,  -7,  -1,  4,  -9, -15, -27,
      -23,  -9, -23,  -5, -9, -16,  -5, -17 },
    // Rook
    {  13, 10, 18, 15, 12,  12,   8,   5,
       11, 13, 13, 11, -3,   3,   8,   3,
        7,  7,  7,  5,  4,  -3,  -5,  -3,
        4,  3, 13,  1,  2,   1,  -1,   2,
        3,  5,  8,  4, -5,  -6,  -8, -11,
       -4,  0, -5, -1, -7, -12,  -8, -16,
       -6, -6,  0,  2, -9,  -9, -11,  -3,
       -9,  2,  3, -1, -5, -13,   4, -20 },
    // Queen
    {  -9,  22,  22,  27,  27,  19,  10,  20,
      -17,  20,  32,  41,  58,  25,  30,   0,
      -20,   6,   9,  49,  47,  35,  19,   9,
        3,  22,  24,  45,  57,  40,  57,  36,
      -18,  28,  19,  47,  31,  34,  39,  23,
      -16, -27,  15,   6,   9,  17,  10,   5,
      -22, -23, -30, -16, -16, -23, -36, -32,
      -33, -28, -22, -43,  -5, -32, -20, -41 },
    // King (Modified for endgame)
    { -74, -35, -18, -18, -11,  15,   4, -17,
      -12,  17,  14,  17,  17,  38,  23,  11,
       10,  17,  23,  15,  20,  45,  44,  13,
       -8,  22,  24,  27,  26,  33,  26,   3,
      -18,  -4,  21,  24,  27,  23,   9, -11,
      -19,  -3,  11,  21,  23,  16,   7,  -9,
      -27, -11,   4,  13,  14,   4,  -5, -17,
      -53, -34, -21, -11, -28, -14, -24, -43 }
}};

// Game phase weights
constexpr int PHASE_WEIGHTS[6] = {0, 1, 1, 2, 4, 0};  // Pawn, Knight, Bishop, Rook, Queen, King

// Evaluation function - enhanced with king safety
int evaluate(const Board& board) {
    int mg_score = 0;
    int eg_score = 0;
    int game_phase = 0;
    
    // Arrays to track pawn positions
    array<int, 8> white_pawn_files = {0};
    array<int, 8> black_pawn_files = {0};
    
    // Count pieces and calculate material
    for (int sq = 0; sq < 64; ++sq) {
        auto square = static_cast<Square>(sq);
        Piece piece = board.at(square);
        if (piece == Piece::NONE) continue;

        PieceType pt = piece.type();
        int pt_index = static_cast<int>(pt);
        int lookup_sq = (piece.color() == Color::WHITE) ? sq : 63 - sq;

        // Add material and PST
        if (piece.color() == Color::WHITE) {
            mg_score += MATERIAL_VALUE[pt_index] + mg_psqt[pt_index][lookup_sq];
            eg_score += MATERIAL_VALUE[pt_index] + eg_psqt[pt_index][lookup_sq];
            game_phase += PHASE_WEIGHTS[pt_index];
            
            if (pt == PieceType::PAWN) {
                white_pawn_files[square.file()]++;
            }
        } else {
            mg_score -= MATERIAL_VALUE[pt_index] + mg_psqt[pt_index][lookup_sq];
            eg_score -= MATERIAL_VALUE[pt_index] + eg_psqt[pt_index][lookup_sq];
            game_phase += PHASE_WEIGHTS[pt_index];
            
            if (pt == PieceType::PAWN) {
                black_pawn_files[square.file()]++;
            }
        }
    }
    
    // Normalize game phase (0-24)
    game_phase = min(game_phase, 24);
    
    // Pawn structure evaluation
    int pawn_structure = 0;
    for (int file = 0; file < 8; file++) {
        // Doubled pawns penalty
        if (white_pawn_files[file] > 1) pawn_structure -= 20 * (white_pawn_files[file] - 1);
        if (black_pawn_files[file] > 1) pawn_structure += 20 * (black_pawn_files[file] - 1);
        
        // Isolated pawns penalty
        bool white_isolated = (file == 0 || white_pawn_files[file-1] == 0) && 
                             (file == 7 || white_pawn_files[file+1] == 0);
        bool black_isolated = (file == 0 || black_pawn_files[file-1] == 0) && 
                             (file == 7 || black_pawn_files[file+1] == 0);
        
        if (white_isolated && white_pawn_files[file] > 0) pawn_structure -= 25;
        if (black_isolated && black_pawn_files[file] > 0) pawn_structure += 25;
    }
    
    // Bishop pair bonus
    if (board.pieces(PieceType::BISHOP, Color::WHITE).count() >= 2) {
        mg_score += 30;
        eg_score += 50;
    }
    if (board.pieces(PieceType::BISHOP, Color::BLACK).count() >= 2) {
        mg_score -= 30;
        eg_score -= 50;
    }
    
    // King safety - enhanced to encourage castling
    Square white_king = board.kingSq(Color::WHITE);
    Square black_king = board.kingSq(Color::BLACK);
    int king_safety = 0;
    
    // Penalty for king in center, bonus for castled king
    if (game_phase > 8) {  // Only in opening/midgame
        // White king
        if (white_king == Square::SQ_E1) {
            king_safety -= 100;  // Big penalty for king on starting square
        } 
        else if (white_king == Square::SQ_G1 || white_king == Square::SQ_B1) {
            king_safety += 60;  // Bonus for castled king
        }
        else if (int(white_king.file()) >= 2 && int(white_king.file()) <= 5) {
            king_safety -= 80;  // Penalty for king in center files
        }
        
        // Black king
        if (black_king == Square::SQ_E8) {
            king_safety += 100;
        } 
        else if (black_king == Square::SQ_G8 || black_king == Square::SQ_B8) {
            king_safety -= 60;
        }
        else if (int(black_king.file())>= 2 && int(black_king.file()) <= 5) {
            king_safety += 80;
        }
    }
    
    // Pawn shield bonus
    int shield_bonus_white = 0;
    int shield_bonus_black = 0;
    
    // White king
    if (int(white_king.rank()) <= 1) {
        int king_file = white_king.file();
        for (int file = max(0, king_file-1); file <= min(7, king_file+1); file++) {
            for (int rank = 1; rank <= 2; rank++) {
                Square shield_sq = Square(file + (rank << 3));
                if (board.at(shield_sq) == Piece(PieceType::PAWN, Color::WHITE)) {
                    shield_bonus_white += 15;
                }
            }
        }
    }
    
    // Black kingg8f6
    if (int(black_king.rank()) >= 6) {
        int king_file = black_king.file();
        for (int file = max(0, king_file-1); file <= min(7, king_file+1); file++) {
            for (int rank = 5; rank <= 6; rank++) {
                Square shield_sq = Square(file + (rank << 3));
                if (board.at(shield_sq) == Piece(PieceType::PAWN, Color::BLACK)) {
                    shield_bonus_black += 15;
                }
            }
        }
    }
    
    king_safety += shield_bonus_white - shield_bonus_black;
    
    // Passed pawn bonus
    auto is_passed_pawn = [](Color color, Square pawn_sq, const array<int, 8>& enemy_pawns) {
        int file = pawn_sq.file();
        for (int f = max(0, file-1); f <= min(7, file+1); f++) {
            if (enemy_pawns[f] > 0) {
                return false;
            }
        }
        return true;
    };
    
    for (int sq = 0; sq < 64; ++sq) {
        Square square = Square(sq);
        Piece piece = board.at(square);
        if (piece.type() != PieceType::PAWN) continue;
        
        if (piece.color() == Color::WHITE) {
            int rank = square.rank();
            if (is_passed_pawn(Color::WHITE, square, black_pawn_files)) {
                int bonus = rank * rank * 10;  // Bonus increases with rank
                eg_score += bonus;
                mg_score += bonus / 2;
            }
        } else {
            int rank = 7 - square.rank();
            if (is_passed_pawn(Color::BLACK, square, white_pawn_files)) {
                int bonus = rank * rank * 10;
                eg_score -= bonus;
                mg_score -= bonus / 2;
            }
        }
    }
    
    // Combine midgame and endgame scores
    int score = (mg_score * game_phase + eg_score * (24 - game_phase)) / 24;
    score += pawn_structure + king_safety;
    
    return (board.sideToMove() == Color::WHITE) ? score : -score;
}

// Opening book implementation with castling encouragement
class OpeningBook {
private:
    unordered_map<uint64_t, vector<Move>> book_moves;
    mt19937 rng;
    
public:
    OpeningBook() : rng(time(nullptr)) {
        Board board;
        
        // Starting position
        board.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        book_moves[board.hash()] = {
            uci::uciToMove(board, "e2e4"),
            uci::uciToMove(board, "d2d4"),
            uci::uciToMove(board, "c2c4"),
            uci::uciToMove(board, "g1f3")
        };
        
        // Add Sicilian Defense
        board.setFen("rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2");
        book_moves[board.hash()] = {
            uci::uciToMove(board, "g1f3"),
            uci::uciToMove(board, "b1c3"),
            uci::uciToMove(board, "f1c4"),
            uci::uciToMove(board, "d2d4")
        };
        
        // Add positions that lead to castling
        board.setFen("rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 3");
        book_moves[board.hash()] = {
            uci::uciToMove(board, "f1c4"),
            uci::uciToMove(board, "f1b5"),
            uci::uciToMove(board, "g1f3"),
            uci::uciToMove(board, "e1g1")  // Castle!
        };
        
        board.setFen("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3");
        book_moves[board.hash()] = {
            uci::uciToMove(board, "f1c4"),
            uci::uciToMove(board, "d2d4"),
            uci::uciToMove(board, "b1c3"),
            uci::uciToMove(board, "e1g1")  // Castle!
        };
    }

    Move get_move(const Board& board) {
        uint64_t hash = board.hash();
        auto it = book_moves.find(hash);
        if (it != book_moves.end() && !it->second.empty()) {
            const vector<Move>& moves = it->second;
            uniform_int_distribution<size_t> dist(0, moves.size() - 1);
            return moves[dist(rng)];
        }
        return Move::NO_MOVE;
    }
};

// Enhanced move ordering with higher castling priority
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
        
        // 2. Capture ordering - MVV/LVA
        if (board.isCapture(move)) {
            Piece captured = board.at(move.to());
            Piece attacker = board.at(move.from());
            
            if (captured != Piece::NONE) {
                int victim_val = MATERIAL_VALUE[static_cast<int>(captured.type())];
                int aggressor_val = MATERIAL_VALUE[static_cast<int>(attacker.type())];
                int score = 900000 + (victim_val * 10) - aggressor_val;
                
                // Winning captures get bonus
                if (victim_val > aggressor_val) score += 50000;
                return score;
            }
        }
        
        // 3. Castling moves - HIGHEST priority after TT moves
        if (move.typeOf() == Move::CASTLING) {
            return 950000;  // Higher than captures
        }
        
        // 4. Killer moves
        if (ply < 64) {
            if (move == killer_moves[ply][0]) return 800000;
            if (move == killer_moves[ply][1]) return 750000;
        }
        
        // 5. History heuristic
        int color_idx = (board.sideToMove() == Color::WHITE) ? 0 : 1;
        int history_score = history[color_idx][move.to().index()];
        
        // 6. Promotion bonus
        if (move.typeOf() == Move::PROMOTION) {
            return 700000 + history_score;
        }
        
        // 7. Check bonus
        board.makeMove(move);
        if (board.inCheck()) {
            board.unmakeMove(move);
            return 500000 + history_score;
        }
        board.unmakeMove(move);
        
        // 8. History score as default
        return history_score;
    }
};

// Enhanced quiescence search
int quiescence(Board& board, int alpha, int beta, int& nodes_searched) {
    nodes_searched++;
    
    // Stand pat evaluation
    int stand_pat = evaluate(board);
    
    if (stand_pat >= beta) return beta;
    if (alpha < stand_pat) alpha = stand_pat;
    
    // Generate captures
    Movelist moves;
    movegen::legalmoves<movegen::MoveGenType::CAPTURE>(moves, board);
    
    // Generate non-capture checks
    Movelist all_moves;
    movegen::legalmoves(all_moves, board);
    for (int i = 0; i < all_moves.size(); i++) {
        Move move = all_moves[i];
        if (!board.isCapture(move)) {
            board.makeMove(move);
            bool gives_check = board.inCheck();
            board.unmakeMove(move);
            if (gives_check) {
                // Avoid duplicates
                bool found = false;
                for (int j = 0; j < moves.size(); j++) {
                    if (moves[j] == move) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    moves.add(move);
                }
            }
        }
    }
    
    // MVV/LVA ordering
    vector<pair<int, Move>> scored_moves;
    for (int i = 0; i < moves.size(); i++) {
        int score = 0;
        Piece captured = board.at(moves[i].to());
        Piece attacker = board.at(moves[i].from());
        
        if (captured != Piece::NONE) {
            score = MATERIAL_VALUE[static_cast<int>(captured.type())] * 10 - 
                    MATERIAL_VALUE[static_cast<int>(attacker.type())];
        } else {
            // Checking move bonus
            score = 200;
        }
        scored_moves.push_back({score, moves[i]});
    }
    
    // Sort moves by score descending
    sort(scored_moves.begin(), scored_moves.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });
    
    // Search moves
    for (auto& [score, move] : scored_moves) {
        board.makeMove(move);
        int eval = -quiescence(board, -beta, -alpha, nodes_searched);
        board.unmakeMove(move);
        
        if (eval >= beta) return beta;
        if (eval > alpha) alpha = eval;
    }
    
    return alpha;
}

// Alpha-beta with optimizations
int alpha_beta(Board& board, int depth, int alpha, int beta, bool maximizing_player, 
               int ply, TranspositionTable& tt, int& nodes_searched, 
               MoveOrderer& move_orderer) {
    nodes_searched++;
    
    // TT Lookup
    uint64_t hash_key = board.hash();
    TTEntry* entry = tt.probe(hash_key);
    Move tt_move = (entry) ? entry->best_move : Move::NO_MOVE;
    
    if (entry && entry->depth >= depth) {
        if (entry->bound == EXACT) {
            return entry->score;
        } else if (entry->bound == LOWER) {
            alpha = max(alpha, entry->score);
        } else if (entry->bound == UPPER) {
            beta = min(beta, entry->score);
        }
        if (alpha >= beta) {
            return entry->score;
        }
    }
    
    // Leaf node: use quiescence search
    if (depth <= 0) {
        return quiescence(board, alpha, beta, nodes_searched);
    }
    
    // Generate moves
    Movelist moves;
    movegen::legalmoves(moves, board);
    
    // Terminal state check
    if (moves.size() == 0) {
        if (board.inCheck()) {
            return (maximizing_player ? -MATE_SCORE + ply : MATE_SCORE - ply);
        }
        return 0;  // Stalemate
    }
    
    // Move ordering
    vector<pair<int, Move>> scored_moves;
    for (int i = 0; i < moves.size(); i++) {
        int score = move_orderer.score_move(board, moves[i], tt_move, ply);
        scored_moves.push_back({score, moves[i]});
    }
    
    // Sort moves by score descending
    sort(scored_moves.begin(), scored_moves.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });
    
    int best_score = maximizing_player ? -MATE_SCORE - 1 : MATE_SCORE + 1;
    Move best_move = scored_moves[0].second;
    int original_alpha = alpha;
    
    for (auto& [score, move] : scored_moves) {
        board.makeMove(move);
        int eval = alpha_beta(board, depth - 1, alpha, beta, !maximizing_player, 
                             ply + 1, tt, nodes_searched, move_orderer);
        board.unmakeMove(move);
        
        if (maximizing_player) {
            if (eval > best_score) {
                best_score = eval;
                best_move = move;
                
                if (eval > alpha) {
                    alpha = eval;
                    if (alpha >= beta) {
                        // Record killer and history
                        move_orderer.record_killer(ply, move);
                        move_orderer.record_history(board.sideToMove(), move, depth);
                        break;
                    }
                }
            }
        } else {
            if (eval < best_score) {
                best_score = eval;
                best_move = move;
                
                if (eval < beta) {
                    beta = eval;
                    if (beta <= alpha) {
                        // Record killer and history
                        move_orderer.record_killer(ply, move);
                        move_orderer.record_history(board.sideToMove(), move, depth);
                        break;
                    }
                }
            }
        }
    }
    
    // TT Store
    Bound bound;
    if (best_score <= original_alpha) {
        bound = UPPER;
    } else if (best_score >= beta) {
        bound = LOWER;
    } else {
        bound = EXACT;
    }
    tt.store(hash_key, depth, best_score, bound, best_move);
    
    return best_score;
}

// Iterative deepening without time limit
Move iterative_deepening(Board& board, TranspositionTable& tt) {
    Move best_move = Move::NO_MOVE;
    int best_score = -MATE_SCORE - 1;
    MoveOrderer move_orderer;

    // Search deeper and deeper until we reach MAX_DEPTH
    for (int depth = 1; depth <= MAX_DEPTH; depth++) {
        int nodes_this_depth = 0;
        int score = alpha_beta(board, depth, -MATE_SCORE - 1, MATE_SCORE + 1, 
                              true, 0, tt, nodes_this_depth, move_orderer);

        TTEntry* entry = tt.probe(board.hash());
        if (entry) {
            best_score = score;
            best_move = entry->best_move;
        }

        cerr << "Depth " << depth << ": " << score 
             << " (" << nodes_this_depth << " nodes)\n";
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
    string line;
    while (getline(cin, line)) {
        stringstream ss(line);
        string cmd;
        ss >> cmd;

        if (cmd == "quit") break;
        
        // UCI initialization
        if (cmd == "uci") {
            cout << "id name MyChessEngine\n";
            cout << "id author YourName\n";
            cout << "option name Hash type spin default 256 min 1 max 16384\n";
            cout << "option name Depth type spin default " << MAX_DEPTH 
                 << " min 1 max 50\n";
            cout << "uciok" << endl;
        }
        
        // Engine readiness
        else if (cmd == "isready") {
            cout << "readyok" << endl;
        }
        
        // New game setup
        else if (cmd == "ucinewgame") {
            board.setFen(chess::constants::STARTPOS);
            tt = TranspositionTable(tt_size);
        }
        
        // Position setup
        else if (cmd == "position") {
            string arg;
            ss >> arg;
            if (arg == "startpos") {
                board.setFen(chess::constants::STARTPOS);
                // Consume "moves" if present
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
        
        // Search command with robust validation
        else if (cmd == "go") {
            Move book_move = book.get_move(board);
            if (book_move != Move::NO_MOVE) {
                cout << "bestmove " << uci::moveToUci(book_move) << endl;
                continue;  // skip search and use book move directly
            }
            // Parse parameters
            string param;
            int depth = max_search_depth;
            while (ss >> param) {
                if (param == "depth") ss >> depth;
            }
            
            // Start search
            Move best_move = iterative_deepening(board, tt);
            
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
        
        // Configuration options
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
                max_search_depth = stoi(value);
            }
        }
    }
    return 0;
}
