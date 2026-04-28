// Fichier généré par de l'IA
#include "uttt_board.h"
#include "board_display.h"
#include <iostream>
#include <string>
#include <limits>

static void print_help()
{
    std::cout << "\n=== Ultimate Tic-Tac-Toe ===\n"
              << "The board has 9 sub-boards (0-8), each with 9 cells (0-8):\n\n"
              << "  Sub-boards:        Cells within each sub-board:\n"
              << "   0 | 1 | 2          0 | 1 | 2\n"
              << "   ---------          ---------\n"
              << "   3 | 4 | 5          3 | 4 | 5\n"
              << "   ---------          ---------\n"
              << "   6 | 7 | 8          6 | 7 | 8\n\n"
              << "Enter moves as: <sub-board> <cell>  (e.g. '4 0' = center board, top-left cell)\n"
              << "Commands: 'undo' to undo last move, 'quit' to exit, 'help' for this message\n"
              << std::endl;
}

int main()
{
    uttt::Board board;
    print_help();

    while (!board.is_game_over())
    {
        uttt::display_board(board);
        std::cout << std::endl;

        uttt::Player p = board.get_current_player();
        std::cout << "Player " << (p == uttt::Player::X ? "X" : "O") << "'s turn. Enter move (sub cell): ";

        std::string input;
        if (!std::getline(std::cin, input))
            break;

        // Handle commands
        if (input == "quit" || input == "exit")
            break;
        if (input == "help")
        {
            print_help();
            continue;
        }
        if (input == "undo")
        {
            board.undo_move();
            std::cout << "Move undone.\n";
            continue;
        }

        // Parse two integers: sub-board and cell
        int sub = -1, cell = -1;
        try
        {
            size_t pos = 0;
            sub = std::stoi(input, &pos);
            cell = std::stoi(input.substr(pos));
        }
        catch (...)
        {
            std::cout << "Invalid input. Enter two numbers (sub cell), e.g. '4 0'.\n";
            continue;
        }

        if (sub < 0 || sub > 8 || cell < 0 || cell > 8)
        {
            std::cout << "Sub-board and cell must be 0-8.\n";
            continue;
        }

        uint8_t move = static_cast<uint8_t>(sub * 9 + cell);
        if (!board.make_move(move))
        {
            std::cout << "Invalid move! ";
            if (board.is_game_over())
                std::cout << "The game is already over.\n";
            else
                std::cout << "Check the active sub-board and cell availability.\n";
        }
    }

    // Final board
    std::cout << "\n";
    uttt::display_board(board);

    switch (board.get_game_state())
    {
    case uttt::GameState::X_WINS:
        std::cout << "\n*** Player X wins! ***\n";
        break;
    case uttt::GameState::O_WINS:
        std::cout << "\n*** Player O wins! ***\n";
        break;
    case uttt::GameState::DRAW:
        std::cout << "\n*** It's a draw! ***\n";
        break;
    default:
        std::cout << "\nGame ended early.\n";
        break;
    }

    return 0;
}
