#include "board_display.h"
#include <iostream>

namespace uttt
{

    void display_board(const Board &board)
    {
        // Visual layout maps display coordinates to move indices.
        // Display row r (0-8), col c (0-8):
        //   sub_board = (r/3)*3 + (c/3)
        //   cell      = (r%3)*3 + (c%3)
        //   move      = sub_board * 9 + cell

        auto cell_char = [&](uint8_t move) -> char
        {
            Player p = board.get_cell(move);
            if (p == Player::NONE)
                return '.';
            return (p == Player::O) ? 'O' : 'X';
        };

        for (int r = 0; r < 9; r++)
        {
            if (r == 3 || r == 6)
                std::cout << "------+-------+------" << std::endl;
            for (int c = 0; c < 9; c++)
            {
                if (c == 3 || c == 6)
                    std::cout << "| ";
                uint8_t sub = (r / 3) * 3 + (c / 3);
                uint8_t cell = (r % 3) * 3 + (c % 3);
                uint8_t move = sub * 9 + cell;
                std::cout << cell_char(move) << " ";
            }
            std::cout << std::endl;
        }

        std::cout << "Current player: " << (board.get_current_player() == Player::X ? "X" : "O") << std::endl;
        std::cout << "Active sub-board: ";
        uint8_t active = board.get_active_sub_board();
        if (active == Board::ANY_BOARD)
            std::cout << "Any";
        else
            std::cout << static_cast<int>(active);
        std::cout << std::endl;

        std::cout << "Game state: ";
        switch (board.get_game_state())
        {
        case GameState::ONGOING:
            std::cout << "Ongoing";
            break;
        case GameState::X_WINS:
            std::cout << "X wins";
            break;
        case GameState::O_WINS:
            std::cout << "O wins";
            break;
        case GameState::DRAW:
            std::cout << "Draw";
            break;
        }
        std::cout << std::endl;
    }

} // namespace uttt
