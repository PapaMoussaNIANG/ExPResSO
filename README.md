# ExPResSO : bibliothèque de parallélisme par tâches en C

Projet individuel, Master 1 Calcul Haute Performance et Simulation, Université Paris-Saclay, 2025-2026.

L'enseignant a fourni l'interface de la bibliothèque, `expresso.h`, les tests et les versions séquentielles de référence des benchmarks. J'ai écrit l'implémentation de la bibliothèque, `expresso/expresso.c`, et les versions parallèles des quatre benchmarks.

## La bibliothèque

ExPResSO exécute des tâches en parallèle sur un pool de threads POSIX. Un programme soumet ses tâches avec `expresso_task`, ou `expresso_weighted_task` pour donner un poids à chaque tâche, puis attend leur fin avec `expresso_wait`.

- **Threads :** pour N travailleurs, la bibliothèque crée N − 1 threads avec `pthread_create`. Le thread principal calcule aussi.
- **Files de tâches :** une file globale et une file locale par thread.
- **Synchronisation :** un mutex et une variable de condition. Un thread sans tâche s'endort au lieu de boucler.
- **Statistiques :** `expresso_stats` affiche le nombre de tâches exécutées par chaque thread et le temps total.

Trois politiques d'ordonnancement sont disponibles :

| Politique | Fonctionnement |
|---|---|
| `dynamic` | chaque thread prend la tâche suivante quand il est libre |
| `static` | les tâches sont réparties entre les threads avant l'exécution |
| `balanced` | la répartition tient compte du poids des tâches |

Le nombre de threads et la politique se choisissent par variables d'environnement :

```sh
EXPRESSO_WORKER_COUNT=4 EXPRESSO_SCHEDULE=balanced ./mon_programme
```

Sans `EXPRESSO_WORKER_COUNT`, la bibliothèque prend le nombre de cœurs logiques.

## Les benchmarks

| Programme | Parallélisation |
|---|---|
| `vecmul` | produit élément par élément, vecteurs découpés en blocs contigus, une tâche par bloc |
| `dot` | produit scalaire, sommes partielles par bloc puis réduction finale |
| `list` | traitement d'une liste chaînée, une tâche par nœud, avec un coût qui varie d'un nœud à l'autre |
| `matmul` | produit de matrices, une tâche par bloc de lignes de la matrice résultat |

Meilleurs speedups mesurés dans le rapport, par rapport à la version séquentielle :

| Programme | Configuration | Speedup |
|---|---|---|
| `vecmul` | 8 threads, `dynamic` | 2,24 |
| `dot` | 8 threads, `static` | 2,01 |
| `list` | taille 10, `static` ou `balanced` | 3,06 |
| `matmul` | 512 × 512, blocs de 16 lignes, `static` | 2,39 |

## Compiler et lancer

Il faut gcc, make et pthread.

```sh
cd expresso
make            # construit libexpresso.a et libexpresso.so
make test       # lance les tests avec 1, 2, 4 et 8 threads et les trois politiques
make install PREFIX=$HOME/.local

cd ../expresso-benchmarks
make PREFIX=$HOME/.local
EXPRESSO_WORKER_COUNT=4 EXPRESSO_SCHEDULE=static ./matmul
```

Chaque benchmark compare son résultat à la version séquentielle et affiche les deux temps de calcul.

## Rapport

`rapport.pdf` décrit l'architecture de la bibliothèque, la parallélisation de chaque benchmark et l'ensemble des mesures.

## Organisation

```
expresso/              bibliothèque : expresso.h, expresso.c, Makefile
expresso/tests/        tests fournis
expresso-benchmarks/   vecmul, dot, list, matmul
rapport.pdf            rapport du projet
```
