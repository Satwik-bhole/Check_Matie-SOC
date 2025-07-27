✅ What I Did and Implemented
1. Board Representation and Move Generation:-
I am using the Disservin Chess Library, which provides:
   1) Efficient board representation
   2) Accurate move generation
   3) Piece tracking
   4) Utilities for board evaluation

2. Zobrist Hashing and Transposition Table
I learned how to use Zobrist hashing to generate a unique 64-bit (or 128-bit) key for each board position. This enables:
   1) Fast state comparison
   2) Efficient storage in a transposition table
The transposition table allows the engine to avoid recalculating positions it has already analyzed, significantly improving speed and performance.

3. Minimax, Alpha-Beta Pruning, and Heuristics
I studied and implemented:
   1) Minimax algorithm for decision-making through game-tree search
   2) Alpha-beta pruning to eliminate unpromising branches, thus reducing computation
   3) Heuristic evaluation functions to assess non-terminal positions
   4) Depth-first search traversal for efficient search
These techniques collectively form the decision-making core of the engine.

4. NNUE Integration
Implemented NNUE (Efficiently Updatable Neural Network Evaluation), inspired by modern engines like Stockfish. NNUE will:
   1) Replace or augment heuristic evaluation
   2) Be trained on position-score pairs to evaluate positions more accurately.

5. Iterative Deepening
   It helped in time managment and searching each depth increasingly with time in consideration.

6. Quiescence search
   To avoid the "horizon effect" that is when a chess engine uses alpha-beta pruning and a fixed depth       (say 5 plies), it might evaluate a position at the leaf node without realizing that a major change        (like a capture or check) is about to happen on the next move. This results in inaccurate evaluations     because the engine "can't see" just beyond the search horizon.

