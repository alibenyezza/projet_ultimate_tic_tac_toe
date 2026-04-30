#pragma once
#include <vector>
#include <cstdint>

namespace uttt
{

    // ============================================================================
    // Precomputed 512-entry win lookup table (9-bit index → bool)
    // ============================================================================
    extern const uint8_t *WIN_LUT;

    // ============================================================================
    // Zobrist hashing tables (initialized once at program start)
    // ============================================================================
    extern uint64_t ZOBRIST_CELL[81][2]; // [position][0=X, 1=O]
    extern uint64_t ZOBRIST_SIDE;        // XOR'd when side-to-move changes
    void init_zobrist();                 // Call once at startup

    // ============================================================================
    // Enums
    // ============================================================================
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

    // ============================================================================
    // Forward declarations
    // ============================================================================
    class MCTSSolver;

    // ============================================================================
    // Bitboard helpers (used by Board and MCTS)
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
            uint8_t bits_in_lo = 64 - start;
            uint16_t lo = static_cast<uint16_t>(b[0] >> start);
            uint16_t hi = static_cast<uint16_t>(b[1] << bits_in_lo);
            return (lo | hi) & 0x1FF;
        }
    }

    /// @brief Represents an Ultimate Tic-Tac-Toe board.
    /// @details Manages 9 sub-boards, move constraints, and win detection
    ///          using a compact 128-bit bitboard representation.
    class Board
    {
        friend class MCTSSolver;

    public:
        static constexpr uint8_t ANY_BOARD = 0xFF;
        static constexpr uint8_t MAX_MOVES = 81;

    private:
        struct MoveRecord
        {
            uint8_t move;
            uint8_t prev_active_sub_board;
            GameState prev_game_state;
            uint16_t prev_decided;
            bool sub_board_won;
        };

        uint64_t cell_bits_[2];
        uint64_t owner_bits_[2];

        uint8_t active_sub_board_;

        uint16_t sub_board_wins_;
        uint16_t sub_board_win_owner_;
        uint16_t decided_boards_; // Bit set = sub-board is decided (won or full)

        MoveRecord history_[MAX_MOVES];
        uint8_t history_size_;
        uint8_t move_count_; // Total moves played so far

        Player current_player_;
        GameState game_state_;
        uint64_t zobrist_hash_;

        GameState update_state();

    public:
        Board();
        void reset();

        bool make_move(uint8_t move);
        void undo_move();

        std::vector<uint8_t> get_valid_moves() const;
        bool is_valid_move(uint8_t move) const;

        bool is_game_over() const;

        Player get_current_player() const;
        GameState get_game_state() const;

        inline Player get_cell(uint8_t position) const;
        inline uint8_t get_active_sub_board() const;
        inline uint64_t hash() const;
        inline uint8_t get_move_count() const;

        /// @brief Returns a 9-bit mask of empty cells for a given sub-board.
        inline uint16_t get_empty_mask(uint8_t sub) const;

        /// @brief Returns the 9-bit decided-boards mask (lower 9 bits).
        inline uint16_t get_decided_boards() const;

        /// @brief Returns sub_board_wins_ (lower 9 bits, bit set = won by someone).
        inline uint16_t get_sub_board_wins() const;

        /// @brief Returns sub_board_win_owner_ (bit set = won by O).
        inline uint16_t get_sub_board_win_owner() const;
    };

    // ============================================================================
    // Inline accessors
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

    inline uint64_t Board::hash() const
    {
        return zobrist_hash_;
    }

    inline uint8_t Board::get_move_count() const
    {
        return move_count_;
    }

    inline uint16_t Board::get_empty_mask(uint8_t sub) const
    {
        return (~extract_sub_board(cell_bits_, sub)) & 0x1FF;
    }

    inline uint16_t Board::get_decided_boards() const
    {
        return decided_boards_;
    }

    inline uint16_t Board::get_sub_board_wins() const
    {
        return sub_board_wins_;
    }

    inline uint16_t Board::get_sub_board_win_owner() const
    {
        return sub_board_win_owner_;
    }

} // namespace uttt
