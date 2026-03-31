#include "board.h"
#include <iostream>

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
//   board[0] holds bits 0-63, board[1] holds bits 64-80
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
    if (start + 9 <= 64) {
        return static_cast<uint16_t>((b[0] >> start) & 0x1FF);
    } else if (start >= 64) {
        return static_cast<uint16_t>((b[1] >> (start - 64)) & 0x1FF);
    } else {
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
    for (int i = 0; i < 8; i++) {
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
    board[0] = 0;
    board[1] = 0;
    board_mask[0] = 0;
    board_mask[1] = 0;
    active_macro_board = ANY_BOARD;
    macro_board = 0;
    macro_board_mask = 0;
    current_player = Player::X;
    game_state = GameState::ONGOING;
    // Clear move history
    while (!move_history.empty())
        move_history.pop();
}

// ============================================================================
// Private helpers
// ============================================================================
bool Board::is_sub_board_decided(uint8_t sub) const
{
    // Won by someone
    if ((macro_board >> sub) & 1)
        return true;
    // All 9 cells filled (drawn sub-board)
    return (extract_sub_board(board, sub) & 0x1FF) == 0x1FF;
}

GameState Board::update_state()
{
    // Extract each player's won sub-boards (lower 9 bits of macro_board)
    uint16_t x_macro = macro_board & ~macro_board_mask & 0x1FF;
    uint16_t o_macro = macro_board & macro_board_mask & 0x1FF;

    if (check_win(x_macro)) {
        game_state = GameState::X_WINS;
        return game_state;
    }
    if (check_win(o_macro)) {
        game_state = GameState::O_WINS;
        return game_state;
    }

    // Check for draw: all 9 sub-boards decided (won or full)
    bool all_decided = true;
    for (uint8_t s = 0; s < 9; s++) {
        if (!is_sub_board_decided(s)) {
            all_decided = false;
            break;
        }
    }
    if (all_decided) {
        game_state = GameState::DRAW;
        return game_state;
    }

    game_state = GameState::ONGOING;
    return game_state;
}

// ============================================================================
// Move validation
// ============================================================================
bool Board::is_valid_move(uint8_t move) const
{
    if (game_state != GameState::ONGOING)
        return false;
    if (move > 80)
        return false;

    uint8_t sub = move / 9;

    // Cell must be empty
    if (get_bit(board, move))
        return false;

    // Sub-board must not be decided (won or full)
    if (is_sub_board_decided(sub))
        return false;

    // Must play in the active sub-board if constrained
    if (active_macro_board != ANY_BOARD && sub != active_macro_board)
        return false;

    return true;
}

std::vector<uint8_t> Board::get_valid_moves() const
{
    std::vector<uint8_t> moves;
    if (game_state != GameState::ONGOING)
        return moves;

    if (active_macro_board != ANY_BOARD) {
        // Forced to play in a specific sub-board
        uint8_t base = active_macro_board * 9;
        uint16_t occupied = extract_sub_board(board, active_macro_board);
        for (uint8_t c = 0; c < 9; c++) {
            if (!((occupied >> c) & 1))
                moves.push_back(base + c);
        }
    } else {
        // Free choice: any empty cell in any undecided sub-board
        for (uint8_t s = 0; s < 9; s++) {
            if (is_sub_board_decided(s))
                continue;
            uint8_t base = s * 9;
            uint16_t occupied = extract_sub_board(board, s);
            for (uint8_t c = 0; c < 9; c++) {
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
    record.prev_active_macro_board = active_macro_board;
    record.prev_game_state = game_state;
    record.macro_board_updated = false;

    // Place the piece
    set_bit(board, move);
    if (current_player == Player::O)
        set_bit(board_mask, move);

    // Check if this move wins the sub-board for the current player
    uint16_t occupied = extract_sub_board(board, sub);
    uint16_t mask = extract_sub_board(board_mask, sub);
    uint16_t player_cells = (current_player == Player::X)
        ? (occupied & ~mask & 0x1FF)
        : (occupied & mask);

    if (check_win(player_cells)) {
        macro_board |= (1 << sub);
        if (current_player == Player::O)
            macro_board_mask |= (1 << sub);
        record.macro_board_updated = true;
    }

    move_history.push(record);

    // Determine next active sub-board:
    // The cell index within the sub-board dictates which sub-board the opponent must play in.
    // If that target sub-board is decided, the opponent gets free choice.
    if (is_sub_board_decided(cell))
        active_macro_board = ANY_BOARD;
    else
        active_macro_board = cell;

    // Update game state (win / draw / ongoing)
    update_state();

    // Switch player
    current_player = (current_player == Player::X) ? Player::O : Player::X;

    return true;
}

void Board::undo_move()
{
    if (move_history.empty())
        return;

    MoveRecord record = move_history.top();
    move_history.pop();

    // Switch player back (undo the switch that happened in make_move)
    current_player = (current_player == Player::X) ? Player::O : Player::X;

    // Clear the piece from both bitboards
    clear_bit(board, record.move);
    clear_bit(board_mask, record.move);

    // Undo macro_board update if this move caused a sub-board win
    if (record.macro_board_updated) {
        uint8_t sub = record.move / 9;
        macro_board &= ~(1 << sub);
        macro_board_mask &= ~(1 << sub);
    }

    // Restore saved state
    active_macro_board = record.prev_active_macro_board;
    game_state = record.prev_game_state;
}

// ============================================================================
// Accessors
// ============================================================================
bool Board::is_game_over() const
{
    return game_state != GameState::ONGOING;
}

Player Board::get_current_player() const
{
    return current_player;
}

GameState Board::get_game_state() const
{
    return game_state;
}

// ============================================================================
// Display
// ============================================================================
void Board::display_board() const
{
    // Visual layout maps display coordinates to move indices.
    // Display row r (0-8), col c (0-8):
    //   sub_board = (r/3)*3 + (c/3)
    //   cell      = (r%3)*3 + (c%3)
    //   move      = sub_board * 9 + cell

    auto cell_char = [&](uint8_t move) -> char {
        if (!get_bit(board, move))
            return '.';
        return get_bit(board_mask, move) ? 'O' : 'X';
    };

    for (int r = 0; r < 9; r++) {
        if (r == 3 || r == 6)
            std::cout << "------+-------+------" << std::endl;
        for (int c = 0; c < 9; c++) {
            if (c == 3 || c == 6)
                std::cout << "| ";
            uint8_t sub = (r / 3) * 3 + (c / 3);
            uint8_t cell = (r % 3) * 3 + (c % 3);
            uint8_t move = sub * 9 + cell;
            std::cout << cell_char(move) << " ";
        }
        std::cout << std::endl;
    }

    std::cout << "Current player: " << (current_player == Player::X ? "X" : "O") << std::endl;
    std::cout << "Active sub-board: ";
    if (active_macro_board == ANY_BOARD)
        std::cout << "Any";
    else
        std::cout << static_cast<int>(active_macro_board);
    std::cout << std::endl;

    std::cout << "Game state: ";
    switch (game_state) {
        case GameState::ONGOING: std::cout << "Ongoing"; break;
        case GameState::X_WINS:  std::cout << "X wins";  break;
        case GameState::O_WINS:  std::cout << "O wins";  break;
        case GameState::DRAW:    std::cout << "Draw";     break;
    }
    std::cout << std::endl;
}

// ============================================================================
// Accessors for graphical rendering
// ============================================================================
Player Board::get_cell(uint8_t move) const
{
    if (!get_bit(board, move))
        return Player::NONE;
    return get_bit(board_mask, move) ? Player::O : Player::X;
}

uint8_t Board::get_active_macro_board() const
{
    return active_macro_board;
}

Player Board::get_sub_board_winner(uint8_t sub) const
{
    if (!((macro_board >> sub) & 1))
        return Player::NONE;
    return ((macro_board_mask >> sub) & 1) ? Player::O : Player::X;
}
