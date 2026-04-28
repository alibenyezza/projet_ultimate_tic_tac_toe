#include "uttt_board.h"
#include <iostream>

namespace uttt
{

    // ============================================================================
    // Win patterns for a 3x3 board (9-bit masks)
    // Cell layout:  0 | 1 | 2
    //               3 | 4 | 5
    //               6 | 7 | 8
    // ============================================================================
    static constexpr uint16_t WIN_PATTERNS[8] = {
        0x007, // Row 0: 0b000'000'111
        0x038, // Row 1: 0b000'111'000
        0x1C0, // Row 2: 0b111'000'000
        0x049, // Col 0: 0b001'001'001
        0x092, // Col 1: 0b010'010'010
        0x124, // Col 2: 0b100'100'100
        0x111, // Diag:  0b100'010'001
        0x054  // Anti:  0b001'010'100
    };

    // ============================================================================
    // Bitboard helpers
    // Bit layout: move index = sub_board * 9 + cell (0-80)
    //   cell_bits_[0] holds bits 0-63, cell_bits_[1] holds bits 64-80
    // ============================================================================
    static inline bool get_bit(const uint64_t b[2], uint8_t pos)
    {
        return (b[pos / 64] >> (pos % 64)) & 1;
    }

    static inline void set_bit(uint64_t b[2], uint8_t pos)
    {
        b[pos / 64] |= (1ULL << (pos % 64));
    }

    static inline void clear_bit(uint64_t b[2], uint8_t pos)
    {
        b[pos / 64] &= ~(1ULL << (pos % 64));
    }

    /// @brief Extracts 9 bits for a sub-board from the 128-bit bitboard.
    /// @param b The bitboard array (2 x uint64_t).
    /// @param sub The sub-board index (0-8).
    /// @return A 9-bit mask of the sub-board's cells.
    static inline uint16_t extract_sub_board(const uint64_t b[2], uint8_t sub)
    {
        uint8_t start = sub * 9;
        if (start + 9 <= 64)
        {
            return static_cast<uint16_t>((b[0] >> start) & 0x1FF);
        }
        else if (start >= 64)
        {
            return static_cast<uint16_t>((b[1] >> (start - 64)) & 0x1FF);
        }
        else
        {
            // Spans both uint64_t's (sub-board 7: bits 63-71)
            uint8_t bits_in_lo = 64 - start;
            uint16_t lo = static_cast<uint16_t>(b[0] >> start);
            uint16_t hi = static_cast<uint16_t>(b[1] << bits_in_lo);
            return (lo | hi) & 0x1FF;
        }
    }

    /// @brief Checks if a 9-bit cell mask contains a winning 3-in-a-row pattern.
    static inline bool check_win(uint16_t cells)
    {
        for (int i = 0; i < 8; i++)
        {
            if ((cells & WIN_PATTERNS[i]) == WIN_PATTERNS[i])
                return true;
        }
        return false;
    }

    // ============================================================================
    // Constructor / Reset
    // ============================================================================
    Board::Board()
    {
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
        current_player_ = Player::X;
        game_state_ = GameState::ONGOING;
        while (!move_history_.empty())
            move_history_.pop();
    }

    // ============================================================================
    // Private helpers
    // ============================================================================
    bool Board::is_sub_board_decided(uint8_t sub) const
    {
        // Won by someone
        if ((sub_board_wins_ >> sub) & 1)
            return true;
        // All 9 cells filled (drawn sub-board)
        return (extract_sub_board(cell_bits_, sub) & 0x1FF) == 0x1FF;
    }

    GameState Board::update_state()
    {
        // Extract each player's won sub-boards (lower 9 bits)
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

        // Check for draw: all 9 sub-boards decided (won or full)
        bool all_decided = true;
        for (uint8_t s = 0; s < 9; s++)
        {
            if (!is_sub_board_decided(s))
            {
                all_decided = false;
                break;
            }
        }
        if (all_decided)
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

        // Cell must be empty
        if (get_bit(cell_bits_, move))
            return false;

        // Sub-board must not be decided (won or full)
        if (is_sub_board_decided(sub))
            return false;

        // Must play in the active sub-board if constrained
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
            // Forced to play in a specific sub-board
            uint8_t base = active_sub_board_ * 9;
            uint16_t occupied = extract_sub_board(cell_bits_, active_sub_board_);
            for (uint8_t c = 0; c < 9; c++)
            {
                if (!((occupied >> c) & 1))
                    moves.push_back(base + c);
            }
        }
        else
        {
            // Free choice: any empty cell in any undecided sub-board
            for (uint8_t s = 0; s < 9; s++)
            {
                if (is_sub_board_decided(s))
                    continue;
                uint8_t base = s * 9;
                uint16_t occupied = extract_sub_board(cell_bits_, s);
                for (uint8_t c = 0; c < 9; c++)
                {
                    if (!((occupied >> c) & 1))
                        moves.push_back(base + c);
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
        MoveRecord record;
        record.move = move;
        record.prev_active_sub_board = active_sub_board_;
        record.prev_game_state = game_state_;
        record.sub_board_won = false;

        // Place the piece
        set_bit(cell_bits_, move);
        if (current_player_ == Player::O)
            set_bit(owner_bits_, move);

        // Check if this move wins the sub-board for the current player
        uint16_t occupied = extract_sub_board(cell_bits_, sub);
        uint16_t mask = extract_sub_board(owner_bits_, sub);
        uint16_t player_cells = (current_player_ == Player::X)
                                    ? (occupied & ~mask & 0x1FF)
                                    : (occupied & mask);

        if (check_win(player_cells))
        {
            sub_board_wins_ |= (1 << sub);
            if (current_player_ == Player::O)
                sub_board_win_owner_ |= (1 << sub);
            record.sub_board_won = true;
        }

        move_history_.push(record);

        // Determine next active sub-board:
        // The cell index within the sub-board dictates which sub-board the opponent must play in.
        // If that target sub-board is decided, the opponent gets free choice.
        if (is_sub_board_decided(cell))
            active_sub_board_ = ANY_BOARD;
        else
            active_sub_board_ = cell;

        // Update game state (win / draw / ongoing)
        update_state();

        // Switch player
        current_player_ = (current_player_ == Player::X) ? Player::O : Player::X;

        return true;
    }

    void Board::undo_move()
    {
        if (move_history_.empty())
            return;

        MoveRecord record = move_history_.top();
        move_history_.pop();

        // Switch player back (undo the switch that happened in make_move)
        current_player_ = (current_player_ == Player::X) ? Player::O : Player::X;

        // Clear the piece from both bitboards
        clear_bit(cell_bits_, record.move);
        clear_bit(owner_bits_, record.move);

        // Undo sub-board win if this move caused one
        if (record.sub_board_won)
        {
            uint8_t sub = record.move / 9;
            sub_board_wins_ &= ~(1 << sub);
            sub_board_win_owner_ &= ~(1 << sub);
        }

        // Restore saved state
        active_sub_board_ = record.prev_active_sub_board;
        game_state_ = record.prev_game_state;
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
