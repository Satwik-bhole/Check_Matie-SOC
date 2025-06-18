#include <iostream>
#include <fstream>
#include <unordered_map>
#include <chrono>
#include "chess-library/include/chess.hpp"
#include "json/single_include/nlohmann/json.hpp"

using namespace std;
using namespace chess;
using json = nlohmann::json;

int piece_value(Piece p) {
    switch (p.type()) {
        case static_cast<int>(PieceType::PAWN): return 1;
            case static_cast<int>(PieceType::KNIGHT): return 3;
            case static_cast<int>(PieceType::BISHOP): return 3;
            case static_cast<int>(PieceType::ROOK): return 5;
            case static_cast<int>(PieceType::QUEEN): return 9;
            case static_cast<int>(PieceType::KING): return 100;
            default: return 0;
    }
}

vector<Move> order_moves(Board& board_now, const Movelist& moves) {
    vector<pair<int, Move>> scored_moves;
    scored_moves.reserve(moves.size());

    for (int i = 0; i < moves.size(); i++) {
        Move move = moves[i];
        int score = 0;
        
        // Check bonus
        board_now.makeMove(move);
        if (board_now.inCheck()) {
            score += 1000;
        }
        board_now.unmakeMove(move);

        // Capture bonus
        if (board_now.isCapture(move)) {
            Piece captured_piece = board_now.at(move.to());
            if (captured_piece != Piece::NONE) {
                int victim_value = piece_value(captured_piece);
                int aggressor_value = piece_value(board_now.at(move.from()));
                score += 100 + (victim_value * 10) - aggressor_value;
            }
        }
        
        // Promotion bonus
        if (move.typeOf() == Move::PROMOTION) {
            score += 500;
        }
        
        // Danger penalty
        if (board_now.isAttacked(move.to(), !board_now.sideToMove())) {
            score -= 25;
        }
        
        scored_moves.push_back({score, move});
    }
    
    sort(scored_moves.begin(), scored_moves.end(), [](auto a, auto b) {
        return a.first > b.first;
    });
    
    vector<Move> ordered;
    for (const auto& sm : scored_moves) {
        ordered.push_back(sm.second);
    }
    return ordered;
}

pair<int, vector<string>> find_mate(Board& board, int depth, bool max_player, int alpha, int beta) {
    // Always check for terminal nodes first
    Movelist moves;
    movegen::legalmoves<movegen::MoveGenType::ALL>(moves, board);
    
    if (moves.size() == 0) {
        if (board.inCheck()) {
            // Return mate score based on perspective
            return {max_player ? 0 : 1, {}};
        }
        return {0, {}};  // Stalemate
    }
    
    // Depth limit reached
    if (depth == 0) {
        return {0, {}};
    }

    if (max_player) {
        int best_value = 0;
        vector<string> best_line;
        vector<Move> ordered_moves = order_moves(board, moves);
        
        for (auto& move : ordered_moves) {
            string move_san = uci::moveToSan(board, move);
            board.makeMove(move);
            auto [score, line] = find_mate(board, depth - 1, false, alpha, beta);
            board.unmakeMove(move);
            
            if (score > best_value) {
                best_value = score;
                best_line = {move_san};
                best_line.insert(best_line.end(), line.begin(), line.end());
                
                // Early return when mate found
                if (best_value == 1) break;
            }
            
            alpha = max(alpha, best_value);
            if (alpha >= beta) break;
        }
        return {best_value, best_line};
    } 
    else {  // Minimizing player (opponent)
        int best_value = 1;
        vector<string> best_line;
        vector<Move> ordered_moves = order_moves(board, moves);
        int longest_mate = 0;  // Track longest mate sequence
        
        for (auto& move : ordered_moves) {
            string move_san = uci::moveToSan(board, move);
            board.makeMove(move);
            auto [score, line] = find_mate(board, depth - 1, true, alpha, beta);
            board.unmakeMove(move);
            
            // Found escape
            if (score == 0) {
                return {0, {move_san}};
            }
            
            // Track the longest mate sequence
            if (line.size() > longest_mate) {
                longest_mate = line.size();
                best_value = score;
                best_line = {move_san};
                best_line.insert(best_line.end(), line.begin(), line.end());
            }
            
            beta = min(beta, best_value);
            if (alpha >= beta) break;
        }
        return {best_value, best_line};
    }
}
int main(){
    auto start = chrono::high_resolution_clock::now();
    ifstream file("mate_in_2.json");
    json data;
    file >> data;

    int total_cases = data.size();
    int correct_cases = 0;
    int count =1;
    for (auto& [fen, expected_solution] : data.items()) {
        Board board;
        board.setFen(fen);
        pair<int,vector<string>>result;
        int alpha=0;
        int beta=1;
        cout<<count<<": "<<"expected: ";
        count++;
        for(auto move:expected_solution){
            cout<<move<<" ";
        }
        cout<<endl;
        result=find_mate(board,3,true,alpha,beta);
        if(result.first==1){
            correct_cases++;
        }
        cout<<"calculated: ";
        for(int i=0;i<result.second.size();i++){
        cout<<result.second[i]<<" ";
    }
    cout<<endl;
    }

    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    cout << "Total test cases: " << total_cases << endl;
    cout << "correct test cases: " << correct_cases << endl;
    cout << endl << "Execution time " << duration.count() << " milliseconds" << endl;
}
