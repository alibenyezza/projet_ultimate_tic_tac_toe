#include "mcts_solver.h"
#include <chrono>
#include <cstring>
#include <algorithm>

#ifdef _MSC_VER
#include <intrin.h>
static inline int ctz32(uint32_t x)
{
    unsigned long idx;
    _BitScanForward(&idx, x);
    return static_cast<int>(idx);
}
#else
static inline int ctz32(uint32_t x) { return __builtin_ctz(x); }
#endif

namespace uttt
{

    // ============================================================================
    // Lightweight xorshift64 PRNG (no heap, no state beyond one uint64_t)
    // ============================================================================
    static uint64_t s_rng_state = 0x12345678ABCDEFULL;

    static inline uint64_t xorshift64()
    {
        uint64_t x = s_rng_state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        s_rng_state = x;
        return x;
    }

    static inline uint32_t rand_under(uint32_t n)
    {
        return static_cast<uint32_t>(xorshift64() % n);
    }

    // ============================================================================
    // MCTSSolver — Static member definitions
    // ============================================================================
    Node MCTSSolver::pool_[MAX_NODES];
    TTEntry MCTSSolver::tt_[TT_SIZE];
    AMAFEntry MCTSSolver::amaf_[AMAF_SIZE];

    // ============================================================================
    // MCTSSolver — Constructor
    // ============================================================================
    MCTSSolver::MCTSSolver()
        : pool_next_(0), last_iterations_(0)
    {
        std::memset(tt_, 0, sizeof(tt_));
        std::memset(amaf_, 0, sizeof(amaf_));
    }

    // ============================================================================
    // Memory pool
    // ============================================================================
    void MCTSSolver::reset_pool()
    {
        pool_next_ = 0;
    }

    uint32_t MCTSSolver::alloc_node()
    {
        if (pool_next_ >= MAX_NODES)
            return NULL_NODE;
        uint32_t idx = pool_next_++;
        Node &n = pool_[idx];
        n.parent = NULL_NODE;
        n.first_child = NULL_NODE;
        n.next_sibling = NULL_NODE;
        n.visits = 0;
        n.value = 0.0f;
        n.move = 0xFF;
        n.proven = UNPROVEN;
        n.num_children = 0;
        return idx;
    }

    // ============================================================================
    // Transposition Table
    // ============================================================================
    void MCTSSolver::tt_store(uint64_t hash, float value, uint32_t visits, int8_t proven)
    {
        uint32_t idx = static_cast<uint32_t>(hash & TT_MASK);
        TTEntry &e = tt_[idx];
        // Always-replace policy (replace if new entry has more visits)
        if (visits >= e.visits || e.hash != hash)
        {
            e.hash = hash;
            e.value = value;
            e.visits = visits;
            e.proven = proven;
        }
    }

    const TTEntry *MCTSSolver::tt_probe(uint64_t hash) const
    {
        uint32_t idx = static_cast<uint32_t>(hash & TT_MASK);
        const TTEntry &e = tt_[idx];
        if (e.hash == hash && e.visits > 0)
            return &e;
        return nullptr;
    }

    // ============================================================================
    // Global AMAF / RAVE table
    // ============================================================================
    uint32_t MCTSSolver::amaf_index(uint64_t pos_hash, uint8_t move) const
    {
        // Mix position hash with move to get table index
        uint64_t key = pos_hash ^ (static_cast<uint64_t>(move) * 0x9E3779B97F4A7C15ULL);
        return static_cast<uint32_t>(key & AMAF_MASK);
    }

    void MCTSSolver::amaf_update(uint64_t pos_hash, uint8_t move, float reward)
    {
        uint32_t idx = amaf_index(pos_hash, move);
        AMAFEntry &e = amaf_[idx];
        if (e.hash == pos_hash && e.move == move)
        {
            e.visits++;
            e.value += reward;
        }
        else if (e.visits == 0)
        {
            // Empty slot — claim it
            e.hash = pos_hash;
            e.move = move;
            e.visits = 1;
            e.value = reward;
        }
        else
        {
            // Collision — replace if stale (few visits)
            if (e.visits < 4)
            {
                e.hash = pos_hash;
                e.move = move;
                e.visits = 1;
                e.value = reward;
            }
            // else: keep existing entry (more established)
        }
    }

    const AMAFEntry *MCTSSolver::amaf_probe(uint64_t pos_hash, uint8_t move) const
    {
        uint32_t idx = amaf_index(pos_hash, move);
        const AMAFEntry &e = amaf_[idx];
        if (e.hash == pos_hash && e.move == move && e.visits > 0)
            return &e;
        return nullptr;
    }

    // ============================================================================
    // UCB1 + RAVE scoring
    // ============================================================================
    float MCTSSolver::ucb_rave_score(uint32_t child_idx, uint32_t parent_visits,
                                     uint64_t parent_hash) const
    {
        const Node &child = pool_[child_idx];

        // Proven values take absolute priority
        if (child.proven == PROVEN_WIN)
            return -1e9f; // This child is a win for the child's side = loss for parent
        if (child.proven == PROVEN_LOSS)
            return 1e9f; // This child is a loss for child's side = win for parent

        if (child.visits == 0)
            return 1e9f; // Prioritize unvisited nodes

        // UCB1 exploitation + exploration
        float exploitation = child.value / static_cast<float>(child.visits);
        float exploration = UCB_C * std::sqrt(std::log(static_cast<float>(parent_visits)) / static_cast<float>(child.visits));
        float ucb = exploitation + exploration;

        // RAVE component
        const AMAFEntry *amaf = amaf_probe(parent_hash, child.move);
        if (amaf && amaf->visits > 0)
        {
            float rave_score = amaf->value / static_cast<float>(amaf->visits);
            float beta = std::sqrt(RAVE_K / (3.0f * static_cast<float>(child.visits) + RAVE_K));
            ucb = (1.0f - beta) * ucb + beta * rave_score;
        }

        return ucb;
    }

    // ============================================================================
    // MCTS Search — main entry point
    // ============================================================================
    uint8_t MCTSSolver::search(const Board &board, int time_limit_ms)
    {
        using Clock = std::chrono::steady_clock;
        auto start_time = Clock::now();

        // Seed the PRNG
        s_rng_state = board.hash() ^ 0xCAFEBABE12345ULL;

        reset_pool();
        std::memset(amaf_, 0, sizeof(amaf_));

        // Create root node
        uint32_t root = alloc_node();
        Board sim_board;

        last_iterations_ = 0;

        while (true)
        {
            // Check time budget every 64 iterations
            if ((last_iterations_ & 63) == 0 && last_iterations_ > 0)
            {
                auto now = Clock::now();
                int elapsed = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count());
                if (elapsed >= time_limit_ms)
                    break;
            }

            // Check pool space (need room for expansion)
            if (pool_next_ + 81 >= MAX_NODES)
                break;

            // Copy the board for this iteration
            sim_board = board;

            // === Selection ===
            uint32_t leaf = select(sim_board);

            // === Expansion + Simulation ===
            float result;
            uint8_t playout_moves[81];
            int playout_len = 0;

            if (pool_[leaf].proven != UNPROVEN)
            {
                // Already proven — use proven value
                if (pool_[leaf].proven == PROVEN_WIN)
                    result = 1.0f;
                else if (pool_[leaf].proven == PROVEN_LOSS)
                    result = 0.0f;
                else
                    result = 0.5f; // draw
            }
            else if (sim_board.is_game_over())
            {
                // Terminal node
                GameState gs = sim_board.get_game_state();
                Player mover = sim_board.get_current_player();
                // The "current player" at a terminal is the one who would have moved next
                // We need the result from the perspective of the player who MADE the last move
                // (i.e., the parent's mover)
                if (gs == GameState::DRAW)
                {
                    result = 0.5f;
                    pool_[leaf].proven = PROVEN_DRAW;
                }
                else
                {
                    // gs is X_WINS or O_WINS
                    Player winner = (gs == GameState::X_WINS) ? Player::X : Player::O;
                    // Result from perspective of the node's side (the side that just moved
                    // is the OPPOSITE of sim_board.current_player)
                    Player just_moved = (mover == Player::X) ? Player::O : Player::X;
                    if (winner == just_moved)
                    {
                        result = 1.0f;
                        pool_[leaf].proven = PROVEN_WIN;
                    }
                    else
                    {
                        result = 0.0f;
                        pool_[leaf].proven = PROVEN_LOSS;
                    }
                }
            }
            else
            {
                // Expand this node
                expand(leaf, sim_board);

                // Pick a child to simulate from (first unvisited or random)
                uint32_t sim_node = pool_[leaf].first_child;
                if (sim_node != NULL_NODE)
                {
                    // Pick a random child for the first playout
                    uint32_t count = pool_[leaf].num_children;
                    uint32_t pick = rand_under(count);
                    uint32_t cur = pool_[leaf].first_child;
                    for (uint32_t i = 0; i < pick && cur != NULL_NODE; i++)
                        cur = pool_[cur].next_sibling;
                    sim_node = cur;

                    sim_board.make_move(pool_[sim_node].move);
                    leaf = sim_node;
                }

                // Check if we should use minimax endgame solver
                if (sim_board.get_move_count() >= ENDGAME_THRESHOLD && !sim_board.is_game_over())
                {
                    uint64_t nodes_searched = 0;
                    float mm_result = minimax(sim_board, 0, -2.0f, 2.0f, nodes_searched);
                    // minimax returns from perspective of current mover in sim_board
                    // Convert: 1.0 = current mover wins, -1.0 = loses, 0.0 = draw
                    result = (mm_result + 1.0f) * 0.5f; // Map [-1,1] to [0,1]
                    // Note: result is from the perspective of the side about to move in sim_board
                    // But for backprop we need it from the perspective of the node (the side that
                    // just moved to reach leaf). Since leaf's side just moved and sim_board's
                    // current player is the opponent, result=1.0 means the opponent wins,
                    // so we flip: result = 1.0 - result.
                    result = 1.0f - result;

                    // Set proven if exact
                    if (mm_result >= 0.99f)
                        pool_[leaf].proven = PROVEN_LOSS; // Opponent can force win
                    else if (mm_result <= -0.99f)
                        pool_[leaf].proven = PROVEN_WIN; // This side wins
                    else if (std::fabs(mm_result) < 0.01f)
                        pool_[leaf].proven = PROVEN_DRAW;
                }
                else
                {
                    // === Heavy rollout ===
                    // Remember which player "owns" this node (the player who just moved)
                    Player node_player = (sim_board.get_current_player() == Player::X) ? Player::O : Player::X;

                    Board rollout_board = sim_board;
                    playout_len = 0;

                    while (!rollout_board.is_game_over())
                    {
                        uint8_t m = pick_heavy_move(rollout_board);
                        if (playout_len < 81)
                            playout_moves[playout_len++] = m;
                        rollout_board.make_move(m);
                    }

                    GameState gs = rollout_board.get_game_state();
                    if (gs == GameState::DRAW)
                    {
                        result = 0.5f;
                    }
                    else
                    {
                        Player winner = (gs == GameState::X_WINS) ? Player::X : Player::O;
                        result = (winner == node_player) ? 1.0f : 0.0f;
                    }
                }
            }

            // === Backpropagation ===
            backpropagate(leaf, result, sim_board, playout_moves, playout_len);

            // Propagate proven values up the tree
            if (pool_[leaf].proven != UNPROVEN)
                propagate_proven(leaf);

            last_iterations_++;
        }

        // === Pick best move (most visited child of root) ===
        uint32_t best_child = NULL_NODE;
        uint32_t best_visits = 0;
        uint32_t cur = pool_[root].first_child;
        while (cur != NULL_NODE)
        {
            // Prefer proven wins, then most visits, avoid proven losses
            if (pool_[cur].proven == PROVEN_WIN)
            {
                // Proven win for the side that made this move = proven loss for root's opponent
                // Actually: node's proven result is from the perspective of the side that just moved
                // PROVEN_WIN at child means child's mover wins — but child's mover is the ROOT's side.
                // Wait, need to be careful. The root represents the state before a move.
                // Children represent states after a move. The child's "proven" is from perspective of
                // the side that made the move into that child, i.e., the root's current player.
                // So PROVEN_WIN at child = root's current player wins by playing that move.
                return pool_[cur].move;
            }
            if (pool_[cur].proven != PROVEN_LOSS && pool_[cur].visits > best_visits)
            {
                best_visits = pool_[cur].visits;
                best_child = cur;
            }
            cur = pool_[cur].next_sibling;
        }

        // If all children are proven losses, pick the one with most visits
        if (best_child == NULL_NODE)
        {
            cur = pool_[root].first_child;
            while (cur != NULL_NODE)
            {
                if (pool_[cur].visits > best_visits)
                {
                    best_visits = pool_[cur].visits;
                    best_child = cur;
                }
                cur = pool_[cur].next_sibling;
            }
        }

        if (best_child == NULL_NODE)
        {
            // Fallback: no children expanded (shouldn't happen in normal play)
            auto moves = board.get_valid_moves();
            return moves.empty() ? 0 : moves[0];
        }

        return pool_[best_child].move;
    }

    // ============================================================================
    // Selection phase — traverse tree using UCB1+RAVE until a leaf is reached
    // ============================================================================
    uint32_t MCTSSolver::select(Board &board)
    {
        uint32_t node = 0; // root is always at index 0

        while (pool_[node].first_child != NULL_NODE && pool_[node].proven == UNPROVEN)
        {
            // Pick child with best UCB+RAVE score
            uint32_t best_child = NULL_NODE;
            float best_score = -1e10f;
            uint64_t pos_hash = board.hash();

            uint32_t child = pool_[node].first_child;
            while (child != NULL_NODE)
            {
                if (pool_[child].proven == PROVEN_WIN)
                {
                    // This child is proven win for child's mover = proven win for us (the side to move)
                    // But wait — from MCTS perspective, we want to PICK this move because it wins for us.
                    // In our UCB scoring, PROVEN_WIN at child means child's side wins, which IS the
                    // current player at `node`, so we should immediately pick it.
                    best_child = child;
                    break;
                }
                float score = ucb_rave_score(child, pool_[node].visits, pos_hash);
                if (score > best_score)
                {
                    best_score = score;
                    best_child = child;
                }
                child = pool_[child].next_sibling;
            }

            if (best_child == NULL_NODE)
                break;

            board.make_move(pool_[best_child].move);
            node = best_child;
        }

        return node;
    }

    // ============================================================================
    // Expansion — generate children from legal moves (bitmask + CTZ)
    // ============================================================================
    void MCTSSolver::expand(uint32_t node_idx, Board &board)
    {
        if (board.is_game_over())
            return;
        if (pool_[node_idx].first_child != NULL_NODE)
            return; // Already expanded

        uint8_t active = board.get_active_sub_board();
        uint32_t prev_child = NULL_NODE;

        if (active != Board::ANY_BOARD)
        {
            uint16_t empty = board.get_empty_mask(active);
            uint8_t base = active * 9;
            while (empty)
            {
                int c = ctz32(empty);
                empty &= empty - 1;

                uint32_t child = alloc_node();
                if (child == NULL_NODE)
                    return; // Pool exhausted

                pool_[child].parent = node_idx;
                pool_[child].move = base + c;

                if (prev_child == NULL_NODE)
                    pool_[node_idx].first_child = child;
                else
                    pool_[prev_child].next_sibling = child;
                prev_child = child;
                pool_[node_idx].num_children++;
            }
        }
        else
        {
            uint16_t open = (~board.get_decided_boards()) & 0x1FF;
            while (open)
            {
                int s = ctz32(open);
                open &= open - 1;

                uint16_t empty = board.get_empty_mask(s);
                uint8_t base = s * 9;
                while (empty)
                {
                    int c = ctz32(empty);
                    empty &= empty - 1;

                    uint32_t child = alloc_node();
                    if (child == NULL_NODE)
                        return;

                    pool_[child].parent = node_idx;
                    pool_[child].move = base + c;

                    if (prev_child == NULL_NODE)
                        pool_[node_idx].first_child = child;
                    else
                        pool_[prev_child].next_sibling = child;
                    prev_child = child;
                    pool_[node_idx].num_children++;
                }
            }
        }
    }

    // ============================================================================
    // Heavy rollout — pick a move with heuristic bias
    // Priority: 1) Win local board  2) Block opponent local win  3) Random
    // ============================================================================
    uint8_t MCTSSolver::pick_heavy_move(Board &board)
    {
        uint8_t active = board.get_active_sub_board();
        Player mover = board.get_current_player();

        // Collect playable sub-boards
        uint8_t subs[9];
        int num_subs = 0;

        if (active != Board::ANY_BOARD)
        {
            subs[0] = active;
            num_subs = 1;
        }
        else
        {
            uint16_t open = (~board.get_decided_boards()) & 0x1FF;
            while (open)
            {
                subs[num_subs++] = static_cast<uint8_t>(ctz32(open));
                open &= open - 1;
            }
        }

        // First pass: look for a move that wins a local board
        for (int i = 0; i < num_subs; i++)
        {
            uint8_t s = subs[i];
            uint8_t base = s * 9;

            uint16_t occupied = extract_sub_board(board.cell_bits_, s);
            uint16_t owner = extract_sub_board(board.owner_bits_, s);
            uint16_t my_cells = (mover == Player::X)
                                    ? (occupied & ~owner & 0x1FF)
                                    : (occupied & owner);
            uint16_t empty = (~occupied) & 0x1FF;

            uint16_t candidates = empty;
            while (candidates)
            {
                int c = ctz32(candidates);
                candidates &= candidates - 1;
                if (WIN_LUT[(my_cells | (1 << c)) & 0x1FF])
                    return base + c; // Immediate local win!
            }
        }

        // Second pass: look for a move that blocks opponent local win
        for (int i = 0; i < num_subs; i++)
        {
            uint8_t s = subs[i];
            uint8_t base = s * 9;

            uint16_t occupied = extract_sub_board(board.cell_bits_, s);
            uint16_t owner = extract_sub_board(board.owner_bits_, s);
            uint16_t opp_cells = (mover == Player::X)
                                     ? (occupied & owner)
                                     : (occupied & ~owner & 0x1FF);
            uint16_t empty = (~occupied) & 0x1FF;

            uint16_t candidates = empty;
            while (candidates)
            {
                int c = ctz32(candidates);
                candidates &= candidates - 1;
                if (WIN_LUT[(opp_cells | (1 << c)) & 0x1FF])
                    return base + c; // Block opponent win!
            }
        }

        // Third: random move from legal moves (extracted via CTZ)
        // Count total empty cells across playable sub-boards
        uint16_t all_empty[9];
        int total_moves = 0;
        for (int i = 0; i < num_subs; i++)
        {
            all_empty[i] = board.get_empty_mask(subs[i]);
            total_moves += __builtin_popcount(all_empty[i]);
        }

        if (total_moves == 0)
            return 0; // Should not happen during an ongoing game

        int pick = static_cast<int>(rand_under(static_cast<uint32_t>(total_moves)));
        for (int i = 0; i < num_subs; i++)
        {
            int count = __builtin_popcount(all_empty[i]);
            if (pick < count)
            {
                uint16_t mask = all_empty[i];
                for (int j = 0; j < pick; j++)
                    mask &= mask - 1; // Remove lowest bit `pick` times
                int c = ctz32(mask);
                return subs[i] * 9 + c;
            }
            pick -= count;
        }

        // Fallback (should not reach here)
        return subs[0] * 9 + ctz32(all_empty[0]);
    }

    // ============================================================================
    // Backpropagation — update visits/value up the tree + RAVE updates
    // ============================================================================
    void MCTSSolver::backpropagate(uint32_t node_idx, float result, Board &board,
                                   const uint8_t *playout_moves, int playout_len)
    {
        // Walk up the tree from leaf to root
        // `result` is from the perspective of the node (the side that just moved to reach it)
        // As we go up, we flip the result because parent's perspective is opposite
        float current_result = result;
        uint32_t cur = node_idx;

        while (cur != NULL_NODE)
        {
            pool_[cur].visits++;
            pool_[cur].value += current_result;

            // Update RAVE: for all moves in the playout, update AMAF entries at this node's position
            // We need the position hash at this node. Since we're walking up and undoing moves,
            // we can use board.hash() after undoing moves back to this position.
            // However, tracking exact hashes during backprop is complex. Instead, we use a
            // simpler approach: store RAVE stats keyed by (move) with the current hash.
            // This is approximate but effective.
            if (playout_len > 0)
            {
                uint64_t pos_hash = board.hash();
                // Update AMAF for playout moves from the perspective of this node
                // Moves played by the same side as this node get `current_result`,
                // moves by the opponent get `1 - current_result`
                for (int i = 0; i < playout_len; i++)
                {
                    float amaf_val = (i % 2 == 0) ? current_result : (1.0f - current_result);
                    amaf_update(pos_hash, playout_moves[i], amaf_val);
                }
            }

            // Store in TT
            if (pool_[cur].visits > 4)
            {
                int8_t proven_val = 0;
                if (pool_[cur].proven == PROVEN_WIN)
                    proven_val = 1;
                else if (pool_[cur].proven == PROVEN_LOSS)
                    proven_val = -1;
                tt_store(board.hash(),
                         pool_[cur].value / static_cast<float>(pool_[cur].visits),
                         pool_[cur].visits, proven_val);
            }

            // Move up
            uint32_t parent = pool_[cur].parent;
            if (parent != NULL_NODE)
                board.undo_move();

            current_result = 1.0f - current_result; // Flip perspective
            cur = parent;
        }
    }

    // ============================================================================
    // Propagate proven values up the tree (MCTS-Solver)
    // ============================================================================
    void MCTSSolver::propagate_proven(uint32_t node_idx)
    {
        uint32_t cur = pool_[node_idx].parent;

        while (cur != NULL_NODE)
        {
            // Check all children of `cur`
            bool all_children_proven = true;
            bool any_child_draw = false;

            uint32_t child = pool_[cur].first_child;
            while (child != NULL_NODE)
            {
                if (pool_[child].proven == UNPROVEN)
                {
                    all_children_proven = false;
                    break;
                }
                // Child's proven result is from perspective of the child's mover (= cur's side)
                // PROVEN_WIN at child means cur's side wins by choosing that child
                if (pool_[child].proven == PROVEN_WIN)
                {
                    // Cur's side can win — cur is proven win
                    pool_[cur].proven = PROVEN_WIN;
                    // Continue propagating up
                    cur = pool_[cur].parent;
                    goto next_ancestor;
                }
                if (pool_[child].proven == PROVEN_DRAW)
                    any_child_draw = true;

                child = pool_[child].next_sibling;
            }

            if (!all_children_proven)
                break; // Can't determine parent's proven status yet

            // All children are proven. If all are PROVEN_LOSS → cur is PROVEN_LOSS (opponent wins all)
            // Wait, this is confusing. Let me re-clarify:
            // Child's PROVEN_WIN means: the player who moved INTO that child (= cur's current player) wins.
            // Child's PROVEN_LOSS means: the player who moved INTO that child loses.
            // So from cur's perspective:
            //   - If ANY child is PROVEN_WIN → cur can choose it → cur is PROVEN_WIN (already handled above)
            //   - If ALL children are PROVEN_LOSS → cur has no winning move → cur is PROVEN_LOSS
            //   - If some children are PROVEN_DRAW and rest PROVEN_LOSS → cur is PROVEN_DRAW
            if (any_child_draw)
                pool_[cur].proven = PROVEN_DRAW;
            else
                pool_[cur].proven = PROVEN_LOSS;

            cur = pool_[cur].parent;
            continue;

        next_ancestor:;
        }
    }

    // ============================================================================
    // Minimax with Alpha-Beta pruning (endgame solver)
    // ============================================================================
    float MCTSSolver::minimax(Board &board, int depth, float alpha, float beta,
                              uint64_t &nodes_searched)
    {
        nodes_searched++;

        // Terminal check
        if (board.is_game_over())
        {
            GameState gs = board.get_game_state();
            if (gs == GameState::DRAW)
                return 0.0f;
            Player mover = board.get_current_player();
            Player winner = (gs == GameState::X_WINS) ? Player::X : Player::O;
            // The "current player" is the one who would move next (but the game is over).
            // The winner just moved (previous player).
            // Return from perspective of current mover: if winner != mover, mover lost.
            return (winner == mover) ? 1.0f : -1.0f;
        }

        // TT probe
        uint64_t hash = board.hash();
        const TTEntry *tt = tt_probe(hash);
        if (tt && tt->proven != 0)
        {
            if (tt->proven == 1)
                return 1.0f; // Win for current mover
            if (tt->proven == -1)
                return -1.0f; // Loss for current mover
        }

        // Hard depth limit to prevent excessive search
        if (depth > 30)
        {
            // Heuristic: count macro-board wins
            uint16_t x_macro = board.get_sub_board_wins() & ~board.get_sub_board_win_owner() & 0x1FF;
            uint16_t o_macro = board.get_sub_board_wins() & board.get_sub_board_win_owner() & 0x1FF;
            int x_count = __builtin_popcount(x_macro);
            int o_count = __builtin_popcount(o_macro);
            float eval = static_cast<float>(x_count - o_count) / 9.0f;
            if (board.get_current_player() == Player::O)
                eval = -eval;
            return eval;
        }

        // Node limit to prevent runaway searches
        if (nodes_searched > 500000)
        {
            uint16_t x_macro = board.get_sub_board_wins() & ~board.get_sub_board_win_owner() & 0x1FF;
            uint16_t o_macro = board.get_sub_board_wins() & board.get_sub_board_win_owner() & 0x1FF;
            int x_count = __builtin_popcount(x_macro);
            int o_count = __builtin_popcount(o_macro);
            float eval = static_cast<float>(x_count - o_count) / 9.0f;
            if (board.get_current_player() == Player::O)
                eval = -eval;
            return eval;
        }

        // Generate moves via bitmask
        float best = -2.0f;
        uint8_t active = board.get_active_sub_board();

        // Move ordering: try center cell (4) first, then corners (0,2,6,8), then edges
        static const uint8_t cell_order[9] = {4, 0, 2, 6, 8, 1, 3, 5, 7};

        if (active != Board::ANY_BOARD)
        {
            uint16_t empty = board.get_empty_mask(active);
            uint8_t base = active * 9;

            // First: try winning moves
            uint16_t occupied = extract_sub_board(board.cell_bits_, active);
            uint16_t owner = extract_sub_board(board.owner_bits_, active);
            Player mover = board.get_current_player();
            uint16_t my_cells = (mover == Player::X) ? (occupied & ~owner & 0x1FF) : (occupied & owner);

            // Check for immediate win moves first (move ordering)
            uint16_t temp = empty;
            while (temp)
            {
                int c = ctz32(temp);
                temp &= temp - 1;
                if (WIN_LUT[(my_cells | (1 << c)) & 0x1FF])
                {
                    board.make_move(base + c);
                    float val = -minimax(board, depth + 1, -beta, -alpha, nodes_searched);
                    board.undo_move();
                    if (val > best)
                        best = val;
                    if (best > alpha)
                        alpha = best;
                    if (alpha >= beta)
                        goto done;
                    empty &= ~(1 << c); // Don't revisit
                }
            }

            // Then ordered remaining moves
            for (int i = 0; i < 9; i++)
            {
                int c = cell_order[i];
                if (!((empty >> c) & 1))
                    continue;

                board.make_move(base + c);
                float val = -minimax(board, depth + 1, -beta, -alpha, nodes_searched);
                board.undo_move();

                if (val > best)
                    best = val;
                if (best > alpha)
                    alpha = best;
                if (alpha >= beta)
                    break;
            }
        }
        else
        {
            uint16_t open = (~board.get_decided_boards()) & 0x1FF;

            // Try each open sub-board
            while (open)
            {
                int s = ctz32(open);
                open &= open - 1;

                uint16_t empty = board.get_empty_mask(s);
                uint8_t base = s * 9;

                for (int i = 0; i < 9; i++)
                {
                    int c = cell_order[i];
                    if (!((empty >> c) & 1))
                        continue;

                    board.make_move(base + c);
                    float val = -minimax(board, depth + 1, -beta, -alpha, nodes_searched);
                    board.undo_move();

                    if (val > best)
                        best = val;
                    if (best > alpha)
                        alpha = best;
                    if (alpha >= beta)
                        goto done;
                }
            }
        }

    done:
        // Store result in TT
        if (best >= 0.99f)
            tt_store(hash, best, 1, 1);
        else if (best <= -0.99f)
            tt_store(hash, best, 1, -1);
        else
            tt_store(hash, best, 1, 0);

        return best;
    }

    // ============================================================================

} // namespace uttt
