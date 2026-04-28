// Fichier généré par de l'IA
#include "uttt_board.h"
#include "board_display.h"
#include "mcts_solver.h"
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
    uttt::init_zobrist();
    uttt::Board board;

    // === Mode selection ===
    std::cout << "Select game mode:\n"
              << "  1) Human vs Human\n"
              << "  2) Human (X) vs AI (O)\n"
              << "  3) AI (X) vs Human (O)\n"
              << "  4) AI vs AI\n"
              << "Choice [1-4]: ";

    int mode = 2;
    {
        std::string line;
        if (std::getline(std::cin, line))
        {
            try
            {
                mode = std::stoi(line);
            }
            catch (...)
            {
                mode = 2;
            }
        }
        if (mode < 1 || mode > 4)
            mode = 2;
    }

    bool x_is_ai = (mode == 3 || mode == 4);
    bool o_is_ai = (mode == 2 || mode == 4);

    int time_ms = 1000;
    if (x_is_ai || o_is_ai)
    {
        std::cout << "AI time per move in ms [1000]: ";
        std::string line;
        if (std::getline(std::cin, line) && !line.empty())
        {
            try
            {
                time_ms = std::stoi(line);
            }
            catch (...)
            {
                time_ms = 1000;
            }
        }
        if (time_ms < 50)
            time_ms = 50;
        if (time_ms > 60000)
            time_ms = 60000;
    }

    uttt::MCTSSolver solver;
    print_help();

    while (!board.is_game_over())
    {
        uttt::display_board(board);
        std::cout << std::endl;

        uttt::Player p = board.get_current_player();
        bool is_ai_turn = (p == uttt::Player::X) ? x_is_ai : o_is_ai;

        if (is_ai_turn)
        {
            std::cout << "AI (" << (p == uttt::Player::X ? "X" : "O") << ") is thinking...\n";
            uint8_t move = solver.search(board, time_ms);
            uint8_t sub = move / 9;
            uint8_t cell = move % 9;
            std::cout << "AI plays: sub-board " << static_cast<int>(sub)
                      << ", cell " << static_cast<int>(cell)
                      << " (" << solver.last_iterations() << " iterations)\n";
            board.make_move(move);
        }
        else
        {
            std::cout << "Player " << (p == uttt::Player::X ? "X" : "O") << "'s turn. Enter move (sub cell): ";

            std::string input;
            if (!std::getline(std::cin, input))
                break;

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
                if (is_ai_turn)
                    board.undo_move(); // Also undo AI's move
                std::cout << "Move undone.\n";
                continue;
            }

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
