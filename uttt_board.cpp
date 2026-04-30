#include "uttt_board.h"
#include <random>

namespace uttt
{

    // ============================================================================
    // Win patterns for a 3x3 board (9-bit masks)
    // ============================================================================
    static constexpr uint16_t WIN_PATTERNS[8] = {
        0x007, 0x038, 0x1C0, 0x049, 0x092, 0x124, 0x111, 0x054};

    // ============================================================================
    // Precomputed 512-entry O(1) win lookup table
    // WIN_LUT[mask] = 1 if the 9-bit mask contains any winning 3-in-a-row
    // Initialized once at startup via ensure_win_lut().
    // ============================================================================
    static uint8_t s_win_lut[512];
    static bool s_win_lut_ready = false;

    static void ensure_win_lut()
    {
        if (s_win_lut_ready)
            return;
        for (int i = 0; i < 512; i++)
        {
            s_win_lut[i] = 0;
            for (int p = 0; p < 8; p++)
            {
                if ((static_cast<uint16_t>(i) & WIN_PATTERNS[p]) == WIN_PATTERNS[p])
                {
                    s_win_lut[i] = 1;
                    break;
                }
            }
        }
        s_win_lut_ready = true;
    }

    // Public symbol referenced from the header
    const uint8_t *WIN_LUT = s_win_lut;

    // ============================================================================
    // Zobrist hashing tables
    // ============================================================================
    uint64_t ZOBRIST_CELL[81][2] = {};
    uint64_t ZOBRIST_SIDE = 0;

    void init_zobrist()
    {
        std::mt19937_64 rng(0xDEADBEEF42ULL); // Fixed seed for reproducibility
        for (int i = 0; i < 81; i++)
        {
            ZOBRIST_CELL[i][0] = rng();
            ZOBRIST_CELL[i][1] = rng();
        }
        ZOBRIST_SIDE = rng();
    }

    // ============================================================================
    // O(1) win check using the lookup table
    // ============================================================================
    static inline bool check_win(uint16_t cells)
    {
        return s_win_lut[cells] != 0;
    }

    // ============================================================================
    // Constructor / Reset
    // ============================================================================
    Board::Board()
    {
        ensure_win_lut();
        reset();
    }

    void Board::reset()
    {
        cell_bits_[0] = 0;
        cell_bits_[1] = 0;
        owner_bits_[0] = 0;
        owner_bits_[1] = 0;
        active_sub_board_ = ANY_BOARD;
        sub_board_wins_ = 0;
        sub_board_win_owner_ = 0;
        decided_boards_ = 0;
        history_size_ = 0;
        move_count_ = 0;
        current_player_ = Player::X;
        game_state_ = GameState::ONGOING;
        zobrist_hash_ = 0;
    }

    // ============================================================================
    // Private helpers
    // ============================================================================
    GameState Board::update_state()
    {
        uint16_t x_macro = sub_board_wins_ & ~sub_board_win_owner_ & 0x1FF;
        uint16_t o_macro = sub_board_wins_ & sub_board_win_owner_ & 0x1FF;

        if (check_win(x_macro))
        {
            game_state_ = GameState::X_WINS;
            return game_state_;
        }
        if (check_win(o_macro))
        {
            game_state_ = GameState::O_WINS;
            return game_state_;
        }

        // Draw: all 9 sub-boards decided
        if ((decided_boards_ & 0x1FF) == 0x1FF)
        {
            game_state_ = GameState::DRAW;
            return game_state_;
        }

        game_state_ = GameState::ONGOING;
        return game_state_;
    }

    // ============================================================================
    // Move validation
    // ============================================================================
    bool Board::is_valid_move(uint8_t move) const
    {
        if (game_state_ != GameState::ONGOING)
            return false;
        if (move > 80)
            return false;

        uint8_t sub = move / 9;

        if (get_bit(cell_bits_, move))
            return false;

        if ((decided_boards_ >> sub) & 1)
            return false;

        if (active_sub_board_ != ANY_BOARD && sub != active_sub_board_)
            return false;

        return true;
    }

    std::vector<uint8_t> Board::get_valid_moves() const
    {
        std::vector<uint8_t> moves;
        if (game_state_ != GameState::ONGOING)
            return moves;

        if (active_sub_board_ != ANY_BOARD)
        {
            uint8_t base = active_sub_board_ * 9;
            uint16_t empty = get_empty_mask(active_sub_board_);
            while (empty)
            {
                int c = __builtin_ctz(empty);
                moves.push_back(base + c);
                empty &= empty - 1;
            }
        }
        else
        {
            uint16_t open = (~decided_boards_) & 0x1FF;
            while (open)
            {
                int s = __builtin_ctz(open);
                open &= open - 1;
                uint8_t base = s * 9;
                uint16_t empty = get_empty_mask(s);
                while (empty)
                {
                    int c = __builtin_ctz(empty);
                    moves.push_back(base + c);
                    empty &= empty - 1;
                }
            }
        }
        return moves;
    }

    // ============================================================================
    // Make / Undo moves
    // ============================================================================
    bool Board::make_move(uint8_t move)
    {
        if (!is_valid_move(move))
            return false;

        uint8_t sub = move / 9;
        uint8_t cell = move % 9;

        // Save state for undo
        MoveRecord &record = history_[history_size_];
        record.move = move;
        record.prev_active_sub_board = active_sub_board_;
        record.prev_game_state = game_state_;
        record.prev_decided = decided_boards_;
        record.sub_board_won = false;

        // Place the piece + update Zobrist hash
        set_bit(cell_bits_, move);
        uint8_t player_idx = (current_player_ == Player::X) ? 0 : 1;
        if (current_player_ == Player::O)
            set_bit(owner_bits_, move);
        zobrist_hash_ ^= ZOBRIST_CELL[move][player_idx];

        // Check if this move wins the sub-board (O(1) lookup)
        uint16_t occupied = extract_sub_board(cell_bits_, sub);
        uint16_t omask = extract_sub_board(owner_bits_, sub);
        uint16_t player_cells = (current_player_ == Player::X)
                                    ? (occupied & ~omask & 0x1FF)
                                    : (occupied & omask);

        if (check_win(player_cells))
        {
            sub_board_wins_ |= (1 << sub);
            if (current_player_ == Player::O)
                sub_board_win_owner_ |= (1 << sub);
            record.sub_board_won = true;
            decided_boards_ |= (1 << sub);
        }
        else if ((occupied & 0x1FF) == 0x1FF)
        {
            // Sub-board is full but no winner = drawn sub-board
            decided_boards_ |= (1 << sub);
        }

        history_size_++;
        move_count_++;

        // Determine next active sub-board
        if ((decided_boards_ >> cell) & 1)
            active_sub_board_ = ANY_BOARD;
        else
            active_sub_board_ = cell;

        // Update game state
        update_state();

        // Switch player + toggle side hash
        zobrist_hash_ ^= ZOBRIST_SIDE;
        current_player_ = (current_player_ == Player::X) ? Player::O : Player::X;

        return true;
    }

    void Board::undo_move()
    {
        if (history_size_ == 0)
            return;

        history_size_--;
        move_count_--;
        const MoveRecord &record = history_[history_size_];

        // Switch player back + toggle side hash
        current_player_ = (current_player_ == Player::X) ? Player::O : Player::X;
        zobrist_hash_ ^= ZOBRIST_SIDE;

        // Undo Zobrist for the piece
        uint8_t player_idx = (current_player_ == Player::X) ? 0 : 1;
        zobrist_hash_ ^= ZOBRIST_CELL[record.move][player_idx];

        // Clear the piece
        clear_bit(cell_bits_, record.move);
        clear_bit(owner_bits_, record.move);

        // Undo sub-board win
        if (record.sub_board_won)
        {
            uint8_t sub = record.move / 9;
            sub_board_wins_ &= ~(1 << sub);
            sub_board_win_owner_ &= ~(1 << sub);
        }

        // Restore saved state
        active_sub_board_ = record.prev_active_sub_board;
        game_state_ = record.prev_game_state;
        decided_boards_ = record.prev_decided;
    }

    // ============================================================================
    // Accessors
    // ============================================================================
    bool Board::is_game_over() const
    {
        return game_state_ != GameState::ONGOING;
    }

    Player Board::get_current_player() const
    {
        return current_player_;
    }

    GameState Board::get_game_state() const
    {
        return game_state_;
    }

} // namespace uttt
