#pragma once
#include "uttt_board.h"
#include <cstdint>
#include <cmath>

namespace uttt
{

    // ============================================================================
    // Tunable constants
    // ============================================================================
    static constexpr uint32_t MAX_NODES = 2'000'000;
    static constexpr float UCB_C = 1.41f;            // Exploration constant
    static constexpr float RAVE_K = 300.0f;          // RAVE equivalence parameter
    static constexpr uint8_t ENDGAME_THRESHOLD = 40; // Move count to trigger minimax
    static constexpr uint32_t TT_SIZE = 1 << 20;     // 1M entries (~24 MB)
    static constexpr uint32_t TT_MASK = TT_SIZE - 1;
    static constexpr uint32_t AMAF_SIZE = 1 << 18; // 256K entries for global RAVE
    static constexpr uint32_t AMAF_MASK = AMAF_SIZE - 1;
    static constexpr uint32_t NULL_NODE = 0xFFFFFFFF;

    // ============================================================================
    // MCTS-Solver proven values
    // ============================================================================
    enum ProvenResult : uint8_t
    {
        UNPROVEN = 0,
        PROVEN_WIN = 1,
        PROVEN_LOSS = 2,
        PROVEN_DRAW = 3
    };

    // ============================================================================
    // MCTS Node — compact, stored in a static memory pool
    // ============================================================================
    struct Node
    {
        uint32_t parent;       // Index in pool (NULL_NODE if root)
        uint32_t first_child;  // Index of first child (NULL_NODE if leaf)
        uint32_t next_sibling; // Index of next sibling (NULL_NODE if last)
        uint32_t visits;       // Visit count
        float value;           // Accumulated reward (from this node's perspective)
        uint8_t move;          // Move that led to this node (0-80, 0xFF for root)
        ProvenResult proven;   // MCTS-Solver proven result
        uint16_t num_children; // Number of children expanded
    };
    static_assert(sizeof(Node) <= 32, "Node should be compact");

    // ============================================================================
    // Transposition Table entry
    // ============================================================================
    struct TTEntry
    {
        uint64_t hash;
        float value; // Win rate estimate
        uint32_t visits;
        int8_t proven; // -1 = loss, 0 = draw/unproven, 1 = win
    };

    // ============================================================================
    // Global AMAF (All-Moves-As-First) table for RAVE
    // Indexed by hash of (position_hash XOR move)
    // ============================================================================
    struct AMAFEntry
    {
        uint64_t hash; // Full position hash for verification
        uint32_t visits;
        float value;  // Accumulated reward
        uint8_t move; // Move index 0-80
    };

    // ============================================================================
    // MCTSSolver — the main AI engine
    // ============================================================================
    class MCTSSolver
    {
    public:
        MCTSSolver();

        /// @brief Run MCTS search and return the best move for the current position.
        /// @param board The current board state (not modified).
        /// @param time_limit_ms Time budget in milliseconds.
        /// @return Best move index (0-80).
        uint8_t search(const Board &board, int time_limit_ms);

        /// @brief Returns the number of iterations completed in the last search.
        uint32_t last_iterations() const { return last_iterations_; }

        /// @brief Toggle deterministic mode (same position always gives same move).
        void set_deterministic(bool d) { deterministic_ = d; }

    private:
        bool deterministic_ = false;

        // ---- Memory pool (static to avoid stack overflow) ----
        static Node pool_[MAX_NODES];
        uint32_t pool_next_;

        uint32_t alloc_node();
        void reset_pool();

        // ---- Transposition table (static) ----
        static TTEntry tt_[TT_SIZE];

        void tt_store(uint64_t hash, float value, uint32_t visits, int8_t proven);
        const TTEntry *tt_probe(uint64_t hash) const;

        // ---- Global RAVE / AMAF table (static) ----
        static AMAFEntry amaf_[AMAF_SIZE];

        void amaf_update(uint64_t pos_hash, uint8_t move, float reward);
        const AMAFEntry *amaf_probe(uint64_t pos_hash, uint8_t move) const;
        uint32_t amaf_index(uint64_t pos_hash, uint8_t move) const;

        // ---- MCTS phases ----
        uint32_t select(Board &board);
        void expand(uint32_t node_idx, Board &board);
        float simulate(Board &board);
        void backpropagate(uint32_t node_idx, float result, Board &board,
                           const uint8_t *playout_moves, int playout_len);

        // ---- Heavy rollout helpers ----
        uint8_t pick_heavy_move(Board &board);

        // ---- Minimax endgame solver ----
        float minimax(Board &board, int depth, float alpha, float beta, uint64_t &nodes_searched);

        // ---- Solver proven-value propagation ----
        void propagate_proven(uint32_t node_idx);

        // ---- UCB + RAVE scoring ----
        float ucb_rave_score(uint32_t child_idx, uint32_t parent_visits,
                             uint64_t parent_hash) const;

        // ---- Stats ----
        uint32_t last_iterations_;
    };

} // namespace uttt
