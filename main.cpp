#include "board.h"
#include <allegro5/allegro.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_ttf.h>
#include <cmath>
#include <string>

// ============================================================================
// Constants
// ============================================================================
static const int CELL_SIZE   = 60;    // pixels par cellule
static const int SUB_GAP     = 6;     // espace entre sous-grilles
static const int MARGIN      = 40;    // marge autour de la grille
static const int INFO_HEIGHT = 80;    // hauteur zone info en bas

// Taille totale de la grille : 9 cellules + 2 gaps entre sous-grilles
static const int GRID_SIZE = 9 * CELL_SIZE + 2 * SUB_GAP;
static const int WIN_W = GRID_SIZE + 2 * MARGIN;
static const int WIN_H = GRID_SIZE + 2 * MARGIN + INFO_HEIGHT;

// Couleurs
static ALLEGRO_COLOR COL_BG, COL_GRID, COL_THICK, COL_X, COL_O;
static ALLEGRO_COLOR COL_HIGHLIGHT, COL_WIN_X, COL_WIN_O, COL_TEXT, COL_DRAW;

// ============================================================================
// Coordonnees : convertit (display_row, display_col) en position pixel
// display_row/col = 0..8 sur la grille 9x9
// ============================================================================
static float cellX(int col)
{
    int subCol = col / 3;
    return MARGIN + col * CELL_SIZE + subCol * SUB_GAP;
}

static float cellY(int row)
{
    int subRow = row / 3;
    return MARGIN + row * CELL_SIZE + subRow * SUB_GAP;
}

// ============================================================================
// Convertit (display_row, display_col) en move index pour Board
// ============================================================================
static uint8_t displayToMove(int row, int col)
{
    uint8_t sub  = (row / 3) * 3 + (col / 3);
    uint8_t cell = (row % 3) * 3 + (col % 3);
    return sub * 9 + cell;
}

// ============================================================================
// Dessine un X dans un rectangle
// ============================================================================
static void drawX(float x1, float y1, float x2, float y2, ALLEGRO_COLOR col, float thickness)
{
    float pad = (x2 - x1) * 0.15f;
    al_draw_line(x1 + pad, y1 + pad, x2 - pad, y2 - pad, col, thickness);
    al_draw_line(x2 - pad, y1 + pad, x1 + pad, y2 - pad, col, thickness);
}

// ============================================================================
// Dessine un O dans un rectangle
// ============================================================================
static void drawO(float x1, float y1, float x2, float y2, ALLEGRO_COLOR col, float thickness)
{
    float cx = (x1 + x2) / 2.0f;
    float cy = (y1 + y2) / 2.0f;
    float r  = (x2 - x1) * 0.35f;
    // Dessiner un cercle avec des segments
    const int segments = 36;
    for (int i = 0; i < segments; i++)
    {
        float a1 = i * 2.0f * ALLEGRO_PI / segments;
        float a2 = (i + 1) * 2.0f * ALLEGRO_PI / segments;
        al_draw_line(
            cx + r * cosf(a1), cy + r * sinf(a1),
            cx + r * cosf(a2), cy + r * sinf(a2),
            col, thickness);
    }
}

// ============================================================================
// Dessine toute la grille
// ============================================================================
static void drawBoard(const Board& board, ALLEGRO_FONT* font)
{
    al_clear_to_color(COL_BG);

    uint8_t activeSub = board.get_active_macro_board();

    // Highlight des sous-grilles jouables
    for (uint8_t s = 0; s < 9; s++)
    {
        if (board.is_game_over()) break;
        if (activeSub != Board::ANY_BOARD && s != activeSub) continue;

        // Verifier que la sous-grille n'est pas terminee
        bool hasValidMove = false;
        for (uint8_t c = 0; c < 9; c++)
            if (board.is_valid_move(s * 9 + c)) { hasValidMove = true; break; }
        if (!hasValidMove) continue;

        int sr = (s / 3) * 3;
        int sc = (s % 3) * 3;
        float x1 = cellX(sc) - 2;
        float y1 = cellY(sr) - 2;
        float x2 = cellX(sc + 2) + CELL_SIZE + 2;
        float y2 = cellY(sr + 2) + CELL_SIZE + 2;
        al_draw_filled_rectangle(x1, y1, x2, y2, COL_HIGHLIGHT);
    }

    // Lignes fines dans chaque sous-grille
    for (int subR = 0; subR < 3; subR++)
    {
        for (int subC = 0; subC < 3; subC++)
        {
            float sx = cellX(subC * 3);
            float sy = cellY(subR * 3);
            float ex = cellX(subC * 3 + 2) + CELL_SIZE;
            float ey = cellY(subR * 3 + 2) + CELL_SIZE;

            // Lignes horizontales internes
            for (int i = 1; i < 3; i++)
            {
                float y = cellY(subR * 3 + i);
                al_draw_line(sx, y, ex, y, COL_GRID, 1.0f);
            }
            // Lignes verticales internes
            for (int i = 1; i < 3; i++)
            {
                float x = cellX(subC * 3 + i);
                al_draw_line(x, sy, x, ey, COL_GRID, 1.0f);
            }
            // Contour epais de la sous-grille
            al_draw_rectangle(sx - 1, sy - 1, ex + 1, ey + 1, COL_THICK, 3.0f);
        }
    }

    // Dessiner les X et O dans chaque cellule
    for (int r = 0; r < 9; r++)
    {
        for (int c = 0; c < 9; c++)
        {
            uint8_t move = displayToMove(r, c);
            Player p = board.get_cell(move);
            if (p == Player::NONE) continue;

            float x1 = cellX(c);
            float y1 = cellY(r);
            float x2 = x1 + CELL_SIZE;
            float y2 = y1 + CELL_SIZE;

            if (p == Player::X)
                drawX(x1, y1, x2, y2, COL_X, 3.0f);
            else
                drawO(x1, y1, x2, y2, COL_O, 3.0f);
        }
    }

    // Dessiner les gros X/O sur les sous-grilles gagnees
    for (uint8_t s = 0; s < 9; s++)
    {
        Player winner = board.get_sub_board_winner(s);
        if (winner == Player::NONE) continue;

        int sr = (s / 3) * 3;
        int sc = (s % 3) * 3;
        float x1 = cellX(sc);
        float y1 = cellY(sr);
        float x2 = cellX(sc + 2) + CELL_SIZE;
        float y2 = cellY(sr + 2) + CELL_SIZE;

        // Fond semi-transparent
        ALLEGRO_COLOR bg = (winner == Player::X)
            ? al_map_rgba(255, 100, 0, 80)
            : al_map_rgba(0, 100, 255, 80);
        al_draw_filled_rectangle(x1, y1, x2, y2, bg);

        if (winner == Player::X)
            drawX(x1, y1, x2, y2, COL_WIN_X, 8.0f);
        else
            drawO(x1, y1, x2, y2, COL_WIN_O, 8.0f);
    }

    // Zone info en bas
    float infoY = MARGIN + GRID_SIZE + 15;

    // Joueur courant
    if (!board.is_game_over())
    {
        std::string msg = "Tour : Joueur ";
        msg += (board.get_current_player() == Player::X) ? "X" : "O";

        if (activeSub == Board::ANY_BOARD)
            msg += " (joue ou il veut)";
        else
            msg += " (sous-grille " + std::to_string(activeSub) + ")";

        ALLEGRO_COLOR col = (board.get_current_player() == Player::X) ? COL_X : COL_O;
        al_draw_text(font, col, WIN_W / 2.0f, infoY, ALLEGRO_ALIGN_CENTRE, msg.c_str());
    }
    else
    {
        std::string msg;
        switch (board.get_game_state())
        {
            case GameState::X_WINS: msg = "Joueur X gagne !"; break;
            case GameState::O_WINS: msg = "Joueur O gagne !"; break;
            case GameState::DRAW:   msg = "Match nul !";      break;
            default: break;
        }
        al_draw_text(font, COL_TEXT, WIN_W / 2.0f, infoY, ALLEGRO_ALIGN_CENTRE, msg.c_str());
        al_draw_text(font, COL_GRID, WIN_W / 2.0f, infoY + 25, ALLEGRO_ALIGN_CENTRE,
                     "Clic pour rejouer | Echap pour quitter");
    }

    al_flip_display();
}

// ============================================================================
// main
// ============================================================================
int main()
{
    al_init();
    al_init_primitives_addon();
    al_init_font_addon();
    al_init_ttf_addon();
    al_install_mouse();
    al_install_keyboard();

    ALLEGRO_DISPLAY* display = al_create_display(WIN_W, WIN_H);
    al_set_window_title(display, "Ultimate Tic-Tac-Toe");

    ALLEGRO_EVENT_QUEUE* queue = al_create_event_queue();
    al_register_event_source(queue, al_get_display_event_source(display));
    al_register_event_source(queue, al_get_mouse_event_source());
    al_register_event_source(queue, al_get_keyboard_event_source());

    // Charger une police (essaie plusieurs chemins courants)
    ALLEGRO_FONT* font = al_load_ttf_font("C:\\Windows\\Fonts\\arial.ttf", 18, 0);
    if (!font)
        font = al_create_builtin_font();

    // Couleurs
    COL_BG        = al_map_rgb(30, 30, 40);
    COL_GRID      = al_map_rgb(80, 80, 100);
    COL_THICK     = al_map_rgb(150, 150, 170);
    COL_X         = al_map_rgb(255, 120, 0);
    COL_O         = al_map_rgb(0, 150, 255);
    COL_HIGHLIGHT = al_map_rgb(50, 60, 50);
    COL_WIN_X     = al_map_rgb(255, 80, 0);
    COL_WIN_O     = al_map_rgb(0, 120, 255);
    COL_TEXT      = al_map_rgb(220, 220, 220);
    COL_DRAW      = al_map_rgb(180, 180, 180);

    Board board;
    drawBoard(board, font);

    bool running = true;
    while (running)
    {
        ALLEGRO_EVENT ev;
        al_wait_for_event(queue, &ev);

        if (ev.type == ALLEGRO_EVENT_DISPLAY_CLOSE)
        {
            running = false;
        }
        else if (ev.type == ALLEGRO_EVENT_KEY_DOWN && ev.keyboard.keycode == ALLEGRO_KEY_ESCAPE)
        {
            running = false;
        }
        else if (ev.type == ALLEGRO_EVENT_KEY_DOWN && ev.keyboard.keycode == ALLEGRO_KEY_Z)
        {
            // Ctrl+Z : undo
            board.undo_move();
            drawBoard(board, font);
        }
        else if (ev.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN && ev.mouse.button == 1)
        {
            int mx = ev.mouse.x;
            int my = ev.mouse.y;

            // Si le jeu est termine, clic = nouvelle partie
            if (board.is_game_over())
            {
                board.reset();
                drawBoard(board, font);
                continue;
            }

            // Trouver la cellule cliquee
            bool found = false;
            for (int r = 0; r < 9 && !found; r++)
            {
                for (int c = 0; c < 9 && !found; c++)
                {
                    float x1 = cellX(c);
                    float y1 = cellY(r);
                    float x2 = x1 + CELL_SIZE;
                    float y2 = y1 + CELL_SIZE;

                    if (mx >= x1 && mx < x2 && my >= y1 && my < y2)
                    {
                        uint8_t move = displayToMove(r, c);
                        if (board.make_move(move))
                        {
                            drawBoard(board, font);
                        }
                        found = true;
                    }
                }
            }
        }
    }

    al_destroy_font(font);
    al_destroy_display(display);
    al_destroy_event_queue(queue);

    return 0;
}
