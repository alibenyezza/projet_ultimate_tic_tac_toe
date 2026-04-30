# Exposé : Algorithmes de l'IA Ultimate Tic-Tac-Toe

---

## Plan de l'exposé

1. Introduction et contexte
2. Représentation du plateau (Bitboards)
3. Monte Carlo Tree Search (MCTS)
4. UCB1 — Compromis exploration/exploitation
5. RAVE (Rapid Action Value Estimation)
6. MCTS-Solver — Propagation des valeurs prouvées
7. Hachage Zobrist et Table de Transposition
8. Rollouts pondérés (Heavy Playouts)
9. Minimax avec élagage Alpha-Bêta (fin de partie)
10. Conclusion et performances

---

## 1. Introduction et contexte

**Ultimate Tic-Tac-Toe** est une variante du morpion jouée sur un méta-plateau de 3×3, où chaque case contient elle-même un plateau 3×3. Le jeu est beaucoup plus complexe que le morpion classique (~$10^{15}$ positions possibles vs ~$10^5$).

**Problème :** L'espace de recherche est trop vaste pour un minimax classique. On utilise donc une approche hybride combinant **MCTS** (pour l'exploration intelligente) et **Minimax** (pour la résolution exacte en fin de partie).

---

## 2. Représentation du plateau — Bitboards

### Principe

Au lieu d'utiliser un tableau 2D, le plateau est encodé sous forme de **bitboards** (entiers 128 bits = 2 × `uint64_t`).

- **81 cases** du plateau complet → 81 bits pour savoir si une case est occupée
- **81 bits supplémentaires** pour savoir à qui appartient chaque case occupée

### Avantages

| Aspect                     | Tableau classique | Bitboard              |
| -------------------------- | ----------------- | --------------------- |
| Vérification de victoire   | O(n) parcours     | O(1) via lookup table |
| Extraction de coups légaux | Boucle            | Opérations bit-à-bit  |
| Copie du plateau           | Copie 81 éléments | Copie 4 entiers       |

### Table de victoire précalculée (WIN_LUT)

```
512 entrées (2^9 combinaisons pour un sous-plateau 3×3)
Index = masque de 9 bits des cases occupées par un joueur
Valeur = 1 si cette configuration est gagnante, 0 sinon
```

**Détection de victoire en O(1) :**

```cpp
// Vérifie si le joueur gagne le sous-plateau s
if (WIN_LUT[extract_sub_board(player_bits, s)])
    → victoire !
```

### Extraction de coups via CTZ (Count Trailing Zeros)

```cpp
while (empty_mask) {
    int move = ctz(empty_mask);     // Trouve le premier bit à 1
    empty_mask &= empty_mask - 1;   // Efface ce bit
    // Traiter le coup 'move'
}
```

→ Extraction sans branchement, extrêmement rapide.

---

## 3. Monte Carlo Tree Search (MCTS)

### Vue d'ensemble

MCTS est un algorithme de recherche basé sur des **simulations aléatoires** pour évaluer les coups. Il construit progressivement un **arbre de jeu** en mémoire.

### Les 4 phases de MCTS

```
       ┌──────────┐
       │ SÉLECTION│ ← Parcourir l'arbre existant (UCB1+RAVE)
       └────┬─────┘
            │
       ┌────▼─────┐
       │ EXPANSION│ ← Ajouter un nouveau nœud (coup légal)
       └────┬─────┘
            │
       ┌────▼──────┐
       │ SIMULATION│ ← Jouer aléatoirement jusqu'à la fin
       └────┬──────┘
            │
       ┌────▼───────────────┐
       │ RÉTROPROPAGATION   │ ← Mettre à jour les statistiques
       └────────────────────┘
```

### Phase 1 : Sélection

On descend dans l'arbre depuis la racine en choisissant à chaque nœud l'enfant avec le **meilleur score UCB1+RAVE** jusqu'à atteindre une feuille.

### Phase 2 : Expansion

On génère tous les enfants (coups légaux) du nœud feuille sélectionné. Un enfant est choisi pour la simulation.

### Phase 3 : Simulation (Rollout)

On joue la partie jusqu'à la fin avec des coups semi-aléatoires (voir section "Rollouts pondérés").

### Phase 4 : Rétropropagation

Le résultat de la simulation (victoire/défaite/nul) est propagé depuis la feuille jusqu'à la racine, en **alternant la perspective** à chaque niveau.

```cpp
while (cur != racine) {
    node.visits++;
    node.value += result;
    result = 1.0 - result;  // Inversion de perspective
    cur = cur.parent;
}
```

### Memory Pool statique

- **2 000 000 nœuds** pré-alloués (pas de `new`/`delete`)
- Chaque nœud = 32 octets (compact)
- Zéro allocation dynamique pendant la recherche → pas de fragmentation ni de latence GC

---

## 4. UCB1 — Upper Confidence Bound

### Le dilemme exploration/exploitation

- **Exploitation** : Jouer le coup qui a le meilleur taux de victoire
- **Exploration** : Essayer des coups peu visités (peut-être meilleurs)

### Formule UCB1

$$UCB1(i) = \frac{w_i}{n_i} + C \cdot \sqrt{\frac{\ln N}{n_i}}$$

| Symbole | Signification                                     |
| ------- | ------------------------------------------------- |
| $w_i$   | Victoires accumulées du nœud enfant $i$           |
| $n_i$   | Nombre de visites du nœud enfant $i$              |
| $N$     | Nombre de visites du nœud parent                  |
| $C$     | Constante d'exploration ($\sqrt{2} \approx 1.41$) |

- **Premier terme** : taux de victoire moyen (exploitation)
- **Second terme** : bonus d'exploration (décroît avec les visites)

### Intuition

Un nœud peu visité a un gros bonus d'exploration → on l'essaie. Plus on le visite, plus le bonus diminue → seuls les bons coups continuent d'être explorés.

---

## 5. RAVE — Rapid Action Value Estimation

### Problème

MCTS est lent à converger : un bon coup doit être joué **à partir du bon nœud** pour que sa statistique s'améliore.

### Idée de RAVE (All-Moves-As-First)

Si un coup $m$ donne de bons résultats **quelque part** dans l'arbre, il a probablement une bonne valeur **partout**. RAVE collecte des statistiques "globales" pour chaque coup.

### Combinaison UCB1 + RAVE

$$Score(i) = (1 - \beta) \cdot UCB1(i) + \beta \cdot RAVE(i)$$

$$\beta = \sqrt{\frac{K}{3n_i + K}}$$

| Paramètre    | Valeur   | Rôle                      |
| ------------ | -------- | ------------------------- |
| $K$ (RAVE_K) | 300      | Contrôle le poids de RAVE |
| $\beta$      | Variable | Poids de RAVE vs UCB1     |

- **Début de recherche** ($n_i$ petit) → $\beta$ grand → RAVE domine (guide rapide)
- **Recherche avancée** ($n_i$ grand) → $\beta$ petit → UCB1 domine (précision)

### Table AMAF globale

- 256K entrées indexées par `hash(position) XOR hash(coup)`
- Permet de partager les statistiques RAVE entre positions similaires

---

## 6. MCTS-Solver — Propagation des valeurs prouvées

### Idée

Quand un sous-arbre est **complètement exploré**, on peut déterminer son résultat exact (victoire/défaite/nul) et le propager vers la racine.

### États prouvés

```
UNPROVEN   → Résultat inconnu (par défaut)
PROVEN_WIN → Le joueur qui a joué ce coup gagne à coup sûr
PROVEN_LOSS → Le joueur qui a joué ce coup perd à coup sûr
PROVEN_DRAW → La position mène forcément au nul
```

### Règles de propagation

```
Pour un nœud parent avec N enfants :

SI un enfant est PROVEN_WIN
   → Le parent peut choisir ce coup → Parent = PROVEN_WIN

SI TOUS les enfants sont PROVEN_LOSS
   → Aucun coup gagnant → Parent = PROVEN_LOSS

SI TOUS les enfants sont prouvés ET au moins un est DRAW (aucun WIN)
   → Parent = PROVEN_DRAW
```

### Impact sur la sélection

- Un enfant `PROVEN_WIN` est immédiatement choisi (score = +∞)
- Un enfant `PROVEN_LOSS` est évité (score = −∞)

---

## 7. Hachage Zobrist et Table de Transposition

### Hachage Zobrist

Technique pour calculer un hash unique pour chaque position de manière **incrémentale** (en O(1) par coup).

```
Initialisation : générer des nombres aléatoires 64 bits
    ZOBRIST_CELL[81][2]  → un nombre par (case, joueur)
    ZOBRIST_SIDE         → XOR quand le trait change

Hash de la position = XOR de tous les ZOBRIST_CELL des pièces posées
```

**Propriété clé :** Pour faire/défaire un coup :

```cpp
hash ^= ZOBRIST_CELL[case][joueur];  // Ajout/retrait d'une pièce
hash ^= ZOBRIST_SIDE;                // Changement de trait
```

→ O(1) au lieu de recalculer tout le plateau.

### Table de Transposition (TT)

- **1 048 576 entrées** (~24 Mo en mémoire)
- Stocke pour chaque position : taux de victoire, nombre de visites, résultat prouvé
- Permet de **réutiliser** les informations si la même position est atteinte par un chemin différent (transposition)
- Politique de remplacement : "always replace" si la nouvelle entrée a plus de visites

---

## 8. Rollouts pondérés (Heavy Playouts)

### Problème des rollouts uniformes

Des simulations purement aléatoires donnent des résultats bruités — elles ne distinguent pas un bon coup d'un mauvais.

### Stratégie de pondération (par priorité)

```
1. GAGNER un sous-plateau local    → Priorité maximale
2. BLOQUER une victoire adverse    → Priorité haute
3. Coup ALÉATOIRE                  → Par défaut
```

### Algorithme

```
Pour chaque sous-plateau jouable :
    ① Chercher un coup qui complète une ligne → JOUER (victoire locale)
    ② Chercher un coup qui bloque l'adversaire → JOUER (défense)

Si aucun coup tactique trouvé :
    ③ Choisir un coup uniformément au hasard parmi les coups légaux
```

### Impact

Les rollouts pondérés améliorent significativement la qualité des estimations car les simulations sont plus "réalistes" — elles reflètent un jeu de niveau intermédiaire plutôt que complètement aléatoire.

---

## 9. Minimax avec élagage Alpha-Bêta (fin de partie)

### Déclenchement

Activé **après le coup 40** (`ENDGAME_THRESHOLD = 40`), quand l'arbre est suffisamment petit pour une recherche exacte.

### Principe du Minimax

```
              MAX (nous)
             /    |    \
          MIN   MIN   MIN (adversaire)
         / \   / \   / \
       MAX MAX ...       (nous)
```

- **MAX** : choisit le coup avec la valeur la plus élevée
- **MIN** : choisit le coup avec la valeur la plus basse
- Les feuilles renvoient +1 (victoire), -1 (défaite) ou 0 (nul)

### Élagage Alpha-Bêta

Optimisation qui **élimine des branches** sans affecter le résultat :

```
α = meilleure valeur garantie pour MAX (borne inférieure)
β = meilleure valeur garantie pour MIN (borne supérieure)

Si α ≥ β → COUPER (cette branche ne sera jamais choisie)
```

**Exemple :**

```
MAX cherche le maximum. Il a trouvé α = 0.5.
Il explore un nœud MIN qui trouve un enfant valant 0.3.
→ MIN choisira ≤ 0.3 (il minimise)
→ MAX ne choisira jamais cette branche (0.3 < 0.5 = α)
→ COUPURE ! On arrête d'explorer les frères.
```

### Optimisations implémentées

| Technique                | Description                                                     |
| ------------------------ | --------------------------------------------------------------- |
| Move ordering            | Centre d'abord, puis coins, puis bords                          |
| Winning moves first      | Coups gagnants testés en priorité                               |
| Profondeur max           | Limite à 30 coups de profondeur                                 |
| Limite de nœuds          | Maximum 500 000 nœuds explorés                                  |
| Heuristique d'évaluation | Si limites atteintes : score = (sous-plateaux gagnés X − O) / 9 |

### Intégration avec MCTS

Le résultat du minimax est injecté dans le nœud MCTS :

- Si le minimax prouve une victoire/défaite → le nœud est marqué `PROVEN_WIN`/`PROVEN_LOSS`
- Sinon → le score heuristique est utilisé comme résultat de simulation

---

## 10. Synthèse et performances

### Architecture globale

```
┌─────────────────────────────────────────────────────┐
│                    MCTS SEARCH                       │
│                                                     │
│  Sélection (UCB1 + RAVE)                           │
│       │                                             │
│  Expansion (bitboard + CTZ)                        │
│       │                                             │
│  Simulation ─── Coup < 40 : Heavy Rollout          │
│       │     └── Coup ≥ 40 : Minimax α-β            │
│       │                                             │
│  Rétropropagation + RAVE + TT + MCTS-Solver        │
└─────────────────────────────────────────────────────┘
```

### Résumé des techniques

| Algorithme          | Rôle                                | Complexité                 |
| ------------------- | ----------------------------------- | -------------------------- |
| MCTS                | Exploration intelligente de l'arbre | O(itérations × profondeur) |
| UCB1                | Équilibre exploration/exploitation  | O(1) par nœud              |
| RAVE                | Convergence rapide au début         | O(1) par nœud              |
| MCTS-Solver         | Jeu exact quand possible            | O(enfants) par propagation |
| Zobrist             | Hash incrémental O(1)               | O(1) par coup              |
| Transposition Table | Réutilisation inter-chemins         | O(1) lookup                |
| Bitboards + WIN_LUT | Détection victoire O(1)             | O(1)                       |
| Heavy Playouts      | Simulations plus réalistes          | O(coups restants)          |
| Minimax α-β         | Résolution exacte (fin de partie)   | O(b^d) worst case, élagué  |

### Chiffres clés du programme

| Paramètre                     | Valeur              |
| ----------------------------- | ------------------- |
| Nœuds MCTS max                | 2 000 000           |
| Taille d'un nœud              | 32 octets           |
| Table de transposition        | 1M entrées (~24 Mo) |
| Table RAVE (AMAF)             | 256K entrées        |
| Constante d'exploration (C)   | 1.41                |
| Paramètre RAVE (K)            | 300                 |
| Seuil minimax                 | Coup 40             |
| Temps de réflexion par défaut | 1000 ms             |

---

## Conclusion

Ce projet combine **plusieurs algorithmes complémentaires** pour créer une IA performante :

1. **MCTS** fournit le cadre général de recherche
2. **UCB1 + RAVE** assurent une exploration efficace
3. **MCTS-Solver** garantit un jeu exact quand la preuve est possible
4. **Minimax α-β** résout la fin de partie de manière exacte
5. **Bitboards + Zobrist** optimisent les opérations de bas niveau

L'ensemble permet à l'IA de jouer à un niveau très élevé dans le budget de temps imparti (typiquement 1 seconde par coup).
