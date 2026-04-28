# Ultimate Tic-Tac-Toe AI

Un moteur de jeu Ultimate Tic-Tac-Toe avec un bot MCTS-Solver haute performance, écrit en C++17.

## Compilation

Nécessite un compilateur compatible C++17 (GCC, Clang ou MSVC).

```bash
g++ -std=c++17 -O2 -o uttt main.cpp uttt_board.cpp board_display.cpp mcts_solver.cpp
```

Sur Windows avec MinGW :

```bash
g++ -std=c++17 -O2 -o uttt.exe main.cpp uttt_board.cpp board_display.cpp mcts_solver.cpp
```

## Exécution

```bash
./uttt
```

Au lancement, vous choisissez un mode de jeu :

```
1) Humain vs Humain
2) Humain (X) vs IA (O)    ← par défaut
3) IA (X) vs Humain (O)
4) IA vs IA
```

Si un joueur IA est sélectionné, le programme demande le temps de réflexion par coup en millisecondes (par défaut : 1000 ms).

## Comment jouer

Le plateau est composé de 9 sous-grilles (0–8), chacune contenant 9 cases (0–8) :

```
Sous-grilles :       Cases dans chaque sous-grille :
 0 | 1 | 2           0 | 1 | 2
 ---------           ---------
 3 | 4 | 5           3 | 4 | 5
 ---------           ---------
 6 | 7 | 8           6 | 7 | 8
```

Entrez un coup sous la forme de deux nombres séparés par un espace : `<sous-grille> <case>`

Exemple : `4 0` place votre symbole dans la sous-grille centrale, case en haut à gauche.

### Commandes

| Commande | Description                                                                      |
| -------- | -------------------------------------------------------------------------------- |
| `help`   | Afficher l'aide en jeu                                                           |
| `undo`   | Annuler le dernier coup (annule aussi le coup de l'IA si vous jouez contre elle) |
| `quit`   | Quitter le jeu                                                                   |

## Fonctionnalités de l'IA

- **MCTS (Monte Carlo Tree Search)** avec une memory pool statique de 2 M de nodes — zéro allocation dynamique pendant la recherche.
- **MCTS-Solver** — propagation des victoires/défaites/nuls prouvés dans l'arbre pour un jeu exact.
- **UCB1 + RAVE** (Rapid Action Value Estimation) pour la sélection des coups.
- **Détection de victoire en O(1)** via une table de correspondance précalculée de 512 entrées.
- **Intrinsèques matérielles** (`__builtin_ctz` / `_BitScanForward`) pour l'extraction de coups sans branchement à partir des bitboards.
- **Hachage Zobrist** avec une table de transposition de 1 M d'entries.
- **Rollouts pondérés** — simulations biased par des heuristiques qui privilégient les victoires et les blocages locaux.
- **Solveur minimax de fin de partie** avec élagage alpha-bêta, déclenché après le coup 40.

## Structure du projet

```
main.cpp             Point d'entrée et boucle de jeu
uttt_board.h/.cpp    Représentation du plateau (bitboards 128 bits) et règles
mcts_solver.h/.cpp   Moteur IA MCTS-Solver
board_display.h/.cpp Affichage du plateau dans le terminal
```
