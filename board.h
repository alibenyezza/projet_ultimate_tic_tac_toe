#pragma once
#include <vector>
#include <stack>
#include <cstdint>

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
/// @details Manages 9 sub-boards, move constraints, and win detection.
class Board
{
public:
    static constexpr uint8_t ANY_BOARD = 0xFF; // Sentinel value: any sub-board is playable

private:
    /// @brief Stores the state needed to undo a move.
    struct MoveRecord
    {
        uint8_t move;                    // The move that was made (0-80)
        uint8_t prev_active_macro_board; // Active sub-board before the move
        GameState prev_game_state;       // Game state before the move
        bool macro_board_updated;        // Whether the move caused a sub-board win
    };

    uint64_t board[2];      // If 0 -> empty, if 1 -> occupied; index 0 for first 64 bits, index 1 for next 64 bits (total 128 bits for 81 cells)
    uint64_t board_mask[2]; // If 0 -> player X, if 1 -> player O; index 0 for first 64 bits, index 1 for next 64 bits (total 128 bits for 81 cells)

    uint8_t active_macro_board; // ANY_BOARD if any sub-board is playable, otherwise the index of the active sub-board (0-8)

    uint16_t macro_board;      // If 0 -> empty, if 1 -> won (only set on sub-board wins, not draws)
    uint16_t macro_board_mask; // If 0 -> player X, if 1 -> player O

    std::stack<MoveRecord> move_history; // Stack to keep track of moves for undo functionality

    Player current_player;

    GameState game_state;
    GameState update_state();                     // Updates the game state based on the current board configuration and returns the new state
    bool is_sub_board_decided(uint8_t sub) const; // Checks if a sub-board is won or completely filled

public:
    Board();
    void reset(); // Resets the board to the initial state

    bool make_move(uint8_t move); // Returns true if the move was successful, false if invalid
    void undo_move();             // Undoes the last move

    std::vector<uint8_t> get_valid_moves() const; // Returns a list of valid moves based on the current game state
    bool is_valid_move(uint8_t move) const;       // Checks if a move is valid

    bool is_game_over() const; // Checks if the game has ended

    Player get_current_player() const; // Returns the current player
    GameState get_game_state() const;  // Returns the current game state

    void display_board() const; // Utility function to print the board state for debugging

    // Accessors for graphical rendering
    Player get_cell(uint8_t move) const;            // Returns who occupies a cell (NONE/X/O)
    uint8_t get_active_macro_board() const;          // Returns the active sub-board (ANY_BOARD if free)
    Player get_sub_board_winner(uint8_t sub) const;  // Returns who won a sub-board (NONE if not won)
};