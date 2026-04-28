#pragma once
#include <vector>
#include <stack>
#include <cstdint>

namespace uttt
{

    enum Player : uint8_t
    {
        NONE = 0,
        X = 1,
        O = 2
    };

    enum GameState : uint8_t
    {
        ONGOING = 0,
        X_WINS = 1,
        O_WINS = 2,
        DRAW = 3
    };

    /// @brief Represents an Ultimate Tic-Tac-Toe board.
    /// @details Manages 9 sub-boards, move constraints, and win detection
    ///          using a compact 128-bit bitboard representation.
    class Board
    {
    public:
        static constexpr uint8_t ANY_BOARD = 0xFF; // Sentinel value: any sub-board is playable

    private:
        /// @brief Stores the state needed to undo a move.
        struct MoveRecord
        {
            uint8_t move;                  // The move that was made (0-80)
            uint8_t prev_active_sub_board; // Active sub-board before the move
            GameState prev_game_state;     // Game state before the move
            bool sub_board_won;            // Whether the move caused a sub-board win
        };

        uint64_t cell_bits_[2];  // Occupancy bitboard: bit set = cell occupied; [0] bits 0-63, [1] bits 64-80
        uint64_t owner_bits_[2]; // Owner bitboard: bit set = player O; [0] bits 0-63, [1] bits 64-80

        uint8_t active_sub_board_; // ANY_BOARD if free choice, otherwise index (0-8) of the required sub-board

        uint16_t sub_board_wins_;      // Bit set = sub-board won by someone (not set for draws)
        uint16_t sub_board_win_owner_; // Bit set = sub-board won by O (only meaningful when corresponding bit in sub_board_wins_ is set)

        std::stack<MoveRecord> move_history_;

        Player current_player_;
        GameState game_state_;

        GameState update_state();                     // Updates and returns the game state based on current board
        bool is_sub_board_decided(uint8_t sub) const; // Checks if a sub-board is won or completely filled

    public:
        Board();
        void reset(); // Resets the board to the initial state

        bool make_move(uint8_t move); // Returns true if the move was successful, false if invalid
        void undo_move();             // Undoes the last move

        std::vector<uint8_t> get_valid_moves() const; // Returns valid moves for the current position
        bool is_valid_move(uint8_t move) const;       // Checks if a specific move is valid

        bool is_game_over() const; // Checks if the game has ended

        Player get_current_player() const; // Returns the current player (X or O)
        GameState get_game_state() const;  // Returns the current game state

        /// @brief Returns which player occupies a cell, or NONE if empty.
        /// @param position Cell index (0-80).
        inline Player get_cell(uint8_t position) const;

        /// @brief Returns the active sub-board index, or ANY_BOARD if free choice.
        inline uint8_t get_active_sub_board() const;
    };

    // ============================================================================
    // Inline accessors (defined in header for zero call overhead)
    // ============================================================================
    inline Player Board::get_cell(uint8_t position) const
    {
        uint64_t mask = 1ULL << (position % 64);
        bool occupied = (cell_bits_[position / 64] & mask) != 0;
        if (!occupied)
            return Player::NONE;
        bool is_o = (owner_bits_[position / 64] & mask) != 0;
        return is_o ? Player::O : Player::X;
    }

    inline uint8_t Board::get_active_sub_board() const
    {
        return active_sub_board_;
    }

} // namespace uttt
