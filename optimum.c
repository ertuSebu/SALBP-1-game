#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>


typedef struct {
    int *nodes;      // Sommets de la clique
    int size;        // Taille de la clique
    int heuristic_forward;   // Heuristique forward associée
    int heuristic_backward;  // Heuristique backward associée
} Clique;

typedef struct {
    Clique *cliques;
    int count;
    int capacity;
} AllCliquesList;


typedef struct {
    int u;
    int v;
} Arc;

typedef struct {
    int n_tasks;
    int n_nodes;
    int cycle_time;
    int *task_times;   // taille n_nodes (source et puits inclus)
    Arc *arcs;         // arcs avec source/puits
    int n_arcs;
} Graph;

typedef struct {
    int u;
    int v;
} Edge;

typedef struct {
    int n;        // nombre de sommets (y compris source et puits)
    int m;        // nombre d'arêtes
    Edge *edges;  // tableau d'arêtes
} UndirectedGraph;



typedef struct {
    int *parent_from_source;
    int *dist_from_source;
    int *parent_from_sink;
    int *dist_from_sink;
    int meeting_point; // sommet où les deux recherches se sont rencontrées
    int n;
} BidirectionalBFSResult;



int is_line_empty_or_whitespace(const char *line) {
    while (*line) {
        if (!isspace((unsigned char)*line)) return 0;
        line++;
    }
    return 1;
}

void trim(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
}

// Fonctions pour lire sections individuelles, adaptées pour un FILE* déjà ouvert :

int read_number_of_tasks_fp(FILE *fp) {
    char line[256];
    rewind(fp);
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "<number of tasks>")) {
            // Lire ligne suivante non vide
            while (fgets(line, sizeof(line), fp)) {
                if (!is_line_empty_or_whitespace(line)) {
                    int n;
                    if (sscanf(line, "%d", &n) == 1) return n;
                    else return -1;
                }
            }
        }
    }
    return -1;
}

int read_cycle_time_fp(FILE *fp) {
    char line[256];
    rewind(fp);
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "<cycle time>")) {
            while (fgets(line, sizeof(line), fp)) {
                if (!is_line_empty_or_whitespace(line)) {
                    int cycle;
                    if (sscanf(line, "%d", &cycle) == 1) return cycle;
                    else return -1;
                }
            }
        }
    }
    return -1;
}

int read_task_times_fp(FILE *fp, int *task_times, int n_tasks) {
    char line[256];
    rewind(fp);
    int reading = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (!reading) {
            if (strstr(line, "<task times>")) {
                reading = 1;
                continue;
            }
        } else {
            if (line[0] == '<') break; // fin section
            if (!is_line_empty_or_whitespace(line)) {
                int tid, ttime;
                if (sscanf(line, "%d %d", &tid, &ttime) == 2) {
                    if (tid > 0 && tid <= n_tasks) {
                        task_times[tid] = ttime;
                    } else {
                        fprintf(stderr, "ID tâche hors bornes: %d\n", tid);
                        return -1;
                    }
                }
            }
        }
    }
    return 0;
}

int read_precedence_relations_fp(FILE *fp, Arc *arcs, int max_arcs) {
    char line[256];
    rewind(fp);
    int reading = 0;
    int count = 0;

    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strcmp(line, "<precedence relations>") == 0) {
            reading = 1;
            continue;
        }
        if (reading && (strcmp(line, "<end>") == 0 || (line[0] == '<' && line[1] != '\0'))) {
            break;
        }
        if (reading) {
            int u,v;
            if (sscanf(line, "%d,%d", &u, &v) == 2) {
                if (count >= max_arcs) {
                    fprintf(stderr, "Trop d'arcs (> %d)\n", max_arcs);
                    return -1;
                }
                arcs[count].u = u;
                arcs[count].v = v;
                count++;
            }
        }
    }
    return count;
}

// Ajoute les arcs de source et de puits
int add_source_and_sink(const Arc *in_arcs, int in_count, int n_tasks, Arc *out_arcs, int max_out) {
    int *has_pred = calloc(n_tasks+2, sizeof(int));
    int *has_succ = calloc(n_tasks+2, sizeof(int));
    if (!has_pred || !has_succ) {
        fprintf(stderr, "Erreur allocation mémoire\n");
        free(has_pred); free(has_succ);
        return -1;
    }
    int out_count = 0;

    for (int i=0; i<in_count; i++) {
        int u = in_arcs[i].u;
        int v = in_arcs[i].v;
        out_arcs[out_count++] = in_arcs[i];
        has_succ[u] = 1;
        has_pred[v] = 1;
    }

    int sink = n_tasks+1;

    for (int i=1; i<=n_tasks; i++) {
        if (!has_pred[i]) {
            if (out_count >= max_out) {
                free(has_pred); free(has_succ);
                return -1;
            }
            out_arcs[out_count++] = (Arc){0, i};
        }
    }

    for (int i=1; i<=n_tasks; i++) {
        if (!has_succ[i]) {
            if (out_count >= max_out) {
                free(has_pred); free(has_succ);
                return -1;
            }
            out_arcs[out_count++] = (Arc){i, sink};
        }
    }

    free(has_pred);
    free(has_succ);
    return out_count;
}

// Fonction principale pour charger tout le graphe depuis un fichier .alb
Graph *load_graph_from_file(const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        perror("Erreur ouverture fichier");
        return NULL;
    }

    int n_tasks = read_number_of_tasks_fp(fp);
    if (n_tasks <= 0) {
        fprintf(stderr, "Erreur lecture nombre de tâches\n");
        fclose(fp);
        return NULL;
    }

    int cycle_time = read_cycle_time_fp(fp);
    if (cycle_time < 0) {
        fprintf(stderr, "Erreur lecture cycle time\n");
        fclose(fp);
        return NULL;
    }

    int n_nodes = n_tasks + 2; // source 0 et puits n+1

    // Allocation dynamique
    int *task_times = calloc(n_nodes, sizeof(int));
    if (!task_times) {
        fprintf(stderr, "Erreur allocation task_times\n");
        fclose(fp);
        return NULL;
    }

    if (read_task_times_fp(fp, task_times, n_tasks) != 0) {
        fprintf(stderr, "Erreur lecture task times\n");
        free(task_times);
        fclose(fp);
        return NULL;
    }

    // Arcs sans source/puits : on ne sait pas encore combien, allouons assez grand
    int max_arcs = 100000; // ou 10*n_tasks^2 par exemple
    Arc *arcs_tmp = malloc(max_arcs * sizeof(Arc));
    if (!arcs_tmp) {
        fprintf(stderr, "Erreur allocation arcs_tmp\n");
        free(task_times);
        fclose(fp);
        return NULL;
    }

    int n_arcs_raw = read_precedence_relations_fp(fp, arcs_tmp, max_arcs);
    if (n_arcs_raw < 0) {
        fprintf(stderr, "Erreur lecture arcs\n");
        free(task_times);
        free(arcs_tmp);
        fclose(fp);
        return NULL;
    }

    // Ajout source/puits
    Arc *arcs_final = malloc((n_arcs_raw + 2*n_tasks + 10) * sizeof(Arc)); // marge
    if (!arcs_final) {
        fprintf(stderr, "Erreur allocation arcs_final\n");
        free(task_times);
        free(arcs_tmp);
        fclose(fp);
        return NULL;
    }

    int n_arcs_final = add_source_and_sink(arcs_tmp, n_arcs_raw, n_tasks, arcs_final, n_arcs_raw + 2*n_tasks + 10);
    if (n_arcs_final < 0) {
        fprintf(stderr, "Erreur ajout source/puits arcs\n");
        free(task_times);
        free(arcs_tmp);
        free(arcs_final);
        fclose(fp);
        return NULL;
    }

    free(arcs_tmp);
    fclose(fp);

    Graph *G = malloc(sizeof(Graph));
    if (!G) {
        fprintf(stderr, "Erreur allocation Graph\n");
        free(task_times);
        free(arcs_final);
        return NULL;
    }

    G->n_tasks = n_tasks;
    G->n_nodes = n_nodes;
    G->cycle_time = cycle_time;
    G->task_times = task_times;
    G->arcs = arcs_final;
    G->n_arcs = n_arcs_final;

    return G;
}




// Calcule la matrice des ancêtres : A[i][j] = 1 s'il existe un chemin de i vers j (i ancêtre de j)
int **compute_ancestor_matrix(const Graph *G) {
    int n = G->n_nodes;

    // Allocation
    int **A = malloc(n * sizeof(int *));
    for (int i = 0; i < n; i++) {
        A[i] = calloc(n, sizeof(int));
    }

    // Initialisation : arcs directs
    for (int i = 0; i < G->n_arcs; i++) {
        int u = G->arcs[i].u;
        int v = G->arcs[i].v;
        A[u][v] = 1;

    }
    // A[i][i] = 1 pour tout i
    for (int i = 0; i < n; i++) {
        A[i][i] = 1;
    }
    // Floyd-Warshall
    for (int k = 0; k < n; k++) {
        for (int i = 0; i < n; i++) {
            if (A[i][k]) {
                for (int j = 0; j < n; j++) {
                    if (A[k][j]) {
                        A[i][j] = 1;
                    }
                }
            }
        }
    }

    return A;
}

UndirectedGraph *build_cocomparability_graph(const Graph *G, int **A) {
    int n = G->n_nodes;
    int max_edges = n * (n - 1) / 2;
    Edge *edges = malloc(max_edges * sizeof(Edge));
    if (!edges) {
        fprintf(stderr, "Erreur d'allocation mémoire pour les arêtes\n");
        return NULL;
    }

    int count = 0;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (A[i][j] == 0 && A[j][i] == 0) {
                edges[count].u = i;
                edges[count].v = j;
                count++;
            }
        }
    }

    UndirectedGraph *H = malloc(sizeof(UndirectedGraph));
    if (!H) {
        fprintf(stderr, "Erreur d'allocation mémoire pour le graphe H\n");
        free(edges);
        return NULL;
    }

    H->n = n;
    H->m = count;
    H->edges = edges;

    return H;
}


// Calcule l'ordre de dégénérescence (retourne un tableau d'indices de sommets)
int *degeneracy_order(const UndirectedGraph *G) {
    int n = G->n;
    int *deg = calloc(n, sizeof(int));
    int *order = malloc(n * sizeof(int));
    int *used = calloc(n, sizeof(int));
    int **adj = malloc(n * sizeof(int *));
    int *adj_count = calloc(n, sizeof(int));
    int *adj_capacity = calloc(n, sizeof(int));
    for (int i = 0; i < n; i++) deg[i] = 0;
    for (int i = 0; i < G->m; i++) {
        deg[G->edges[i].u]++;
        deg[G->edges[i].v]++;
    }
    for (int i = 0; i < n; i++) {
        adj_capacity[i] = deg[i] > 0 ? deg[i] : 1;
        adj[i] = malloc(adj_capacity[i] * sizeof(int));
    }
    for (int i = 0; i < G->m; i++) {
        int u = G->edges[i].u, v = G->edges[i].v;
        adj[u][adj_count[u]++] = v;
        adj[v][adj_count[v]++] = u;
    }
    for (int k = 0; k < n; k++) {
        int min_deg = n+1, v = -1;
        for (int i = 0; i < n; i++)
            if (!used[i] && deg[i] < min_deg) { min_deg = deg[i]; v = i; }
        order[k] = v;
        used[v] = 1;
        for (int i = 0; i < adj_count[v]; i++) {
            int u = adj[v][i];
            if (!used[u]) deg[u]--;
        }
    }
    for (int i = 0; i < n; i++) free(adj[i]);
    free(adj); free(adj_count); free(adj_capacity); free(deg); free(used);
    return order;
}


// Construit la matrice d'adjacence à partir du UndirectedGraph
int **build_adj_matrix(const UndirectedGraph *G) {
    int n = G->n;
    int **adj = malloc(n * sizeof(int *));
    for (int i = 0; i < n; i++) {
        adj[i] = calloc(n, sizeof(int));
    }
    for (int i = 0; i < G->m; i++) {
        int u = G->edges[i].u;
        int v = G->edges[i].v;
        adj[u][v] = 1;
        adj[v][u] = 1;
    }
    return adj;
}


void add_to_all_cliques(AllCliquesList *out, int *clique, int size, int heuristic_forward,int heuristic_backward) {
    if (out->count == out->capacity) {
        out->capacity = out->capacity ? 2 * out->capacity : 8;
        out->cliques = realloc(out->cliques, out->capacity * sizeof(Clique));
    }
    out->cliques[out->count].nodes = malloc(size * sizeof(int));
    memcpy(out->cliques[out->count].nodes, clique, size * sizeof(int));
    out->cliques[out->count].size = size;
    out->cliques[out->count].heuristic_forward = heuristic_forward;
    out->cliques[out->count].heuristic_backward= heuristic_backward;
    out->count++;
}


// Génère tous les sous-ensembles de voisins de x formant une clique avec x
void generate_cliques_with_x(int x, int *neighbors, int k, int **adj, AllCliquesList *out,int heuristic_forward, int heuristic_backward) {
    int total = 1 << k;
    for (int mask = 0; mask < total; mask++) {
        int clique[k + 1];
        int size = 1;
        clique[0] = x;
        int valid = 1;
        // Ajoute les voisins sélectionnés
        for (int i = 0; i < k; i++) {
            if (mask & (1 << i)) {
                // Vérifie que ce voisin est adjacent à tous les autres déjà dans la clique
                for (int j = 0; j < size; j++) {
                    if (!adj[clique[j]][neighbors[i]]) {
                        valid = 0;
                        break;
                    }
                }
                if (!valid) break;
                clique[size++] = neighbors[i];
            }
        }
        if (valid) {
            add_to_all_cliques(out, clique, size,heuristic_forward,heuristic_backward);
        }
    }
}

// Génère toutes les cliques du graphe H (méthode dégénérescence Alix, sans Bron-Kerbosch)
AllCliquesList *generate_all_cliques(const UndirectedGraph *H, int **A, Graph *G) {
    int n = H->n;
    int **adj = build_adj_matrix(H);
    int *order = degeneracy_order(H);
    int *removed = calloc(n, sizeof(int));
    AllCliquesList *all = calloc(1, sizeof(AllCliquesList));

    for (int step = 0; step < n; step++) {
        int x = -1;
        // Trouver le sommet de degré minimum non encore retiré
        for (int i = 0; i < n; i++) {
            if (!removed[order[i]]) {
                x = order[i];
                
                break;
            }
        }
        if (x == -1) break;
        // Liste des voisins non retirés
        //Affichage somme des durées de tâches qui précèdent x
        int heuristic_forward = 0;
        int heuristic_backward = 0;
        for (int i = 0; i < n; i++) {
            if (A[i][x] == 1) {
                heuristic_forward += G->task_times[i];
            }
            if (A[x][i] == 1 && i!=x) {
                heuristic_backward += G->task_times[i];
            }
        }
        int neighbors[n], k = 0;
        for (int v = 0; v < n; v++) {
            if (!removed[v] && adj[x][v]) {
                neighbors[k++] = v;
            }
        }
        // Génère toutes les cliques contenant x et un sous-ensemble de ses voisins
        generate_cliques_with_x(x, neighbors, k, adj, all,heuristic_forward, heuristic_backward);
        removed[x] = 1;
    }
    for (int i = 0; i < n; i++) free(adj[i]);
    free(adj); free(order); free(removed);
    return all;
}





// Vérifie si F est inclus dans F' selon la matrice des ancêtres A
// F, F' : tableaux d'indices de sommets, de tailles f_size et fp_size
// A : matrice des ancêtres (A[i][j] == 1 si i est ancêtre de j)
bool inclusion(const int *F, int f_size, const int *Fp, int fp_size, int **A) {
    int compteur = 0;
    for (int i = 0; i < f_size; i++) {
        int task = F[i];
        for (int j = 0; j < fp_size; j++) {
            int taskp = Fp[j];
            if (task == taskp || A[task][taskp] == 1) {
                compteur++;
                break;
            }
        }
    }
    return compteur == f_size;
}

// Vérifie si le sommet l doit être dans C selon F, F' et la matrice des ancêtres A
bool check_ell(int l, const int *F, int f_size, const int *Fp, int fp_size, int **A) {
    for (int i = 0; i < f_size; i++) {
        int task = F[i];
        if (A[l][task] == 1) {
            return false;
        }
    }
    for (int j = 0; j < fp_size; j++) {
        int taskp = Fp[j];
        if (A[l][taskp] == 1) {
            return true;
        }
    }
    return false;
}

// Construit l'ensemble C des tâches intermédiaires
// V : tableau des sommets (par exemple 0..n-1), v_size : taille de V
// F, F' : frontières, f_size, fp_size : tailles respectives
// A : matrice des ancêtres
// C : tableau de sortie (doit être alloué de taille au moins v_size), retourne la taille de C
int construction_C(const int *F, int f_size, const int *Fp, int fp_size, const int *V, int v_size, int **A, int *C) {
    int c_size = 0;
    for (int i = 0; i < v_size; i++) {
        int l = V[i];
        if (check_ell(l, F, f_size, Fp, fp_size, A)) {
            C[c_size++] = l;
        }
    }
    return c_size;
}


// Trouve l'indice de la frontière contenant uniquement le sommet 'node'
int find_singleton_frontiere(const AllCliquesList *frontieres, int node) {
    for (int i = 0; i < frontieres->count; i++) {
        if (frontieres->cliques[i].size == 1 && frontieres->cliques[i].nodes[0] == node) {
            return i;
        }
    }
    return -1;
}

// Comparateur pour qsort_r : tri décroissant sur l’heuristique
int compare_by_heuristic_forward_desc(const void *a, const void *b, void *arg) {
    const AllCliquesList *all = (const AllCliquesList *)arg;
    int idx_a = *(const int *)a;
    int idx_b = *(const int *)b;
    return all->cliques[idx_b].heuristic_forward - all->cliques[idx_a].heuristic_forward;
}


// Comparateur pour qsort_r : tri décroissant sur l’heuristique
int compare_by_heuristic_backward_desc(const void *a, const void *b, void *arg) {
    const AllCliquesList *all = (const AllCliquesList *)arg;
    int idx_a = *(const int *)a;
    int idx_b = *(const int *)b;
    return all->cliques[idx_b].heuristic_backward - all->cliques[idx_a].heuristic_backward;
}

BidirectionalBFSResult* bidirectional_bfs(const AllCliquesList *all, int n_nodes, int **A,
                                          const int *task_times, int cycle_time,
                                          int idx_source, int idx_sink) {
    int n = all->count;
    int *parent_s = malloc(n * sizeof(int));
    int *dist_s = malloc(n * sizeof(int));
    int *parent_t = malloc(n * sizeof(int));
    int *dist_t = malloc(n * sizeof(int));

    for (int i = 0; i < n; i++) {
        parent_s[i] = -1; dist_s[i] = -1;
        parent_t[i] = -1; dist_t[i] = -1;
    }

    int *queue_s = malloc(n * sizeof(int)), *queue_t = malloc(n * sizeof(int));
    int *next_s = malloc(n * sizeof(int)), *next_t = malloc(n * sizeof(int));
    int size_s = 0, size_t = 0, next_size_s = 0, next_size_t = 0;

    dist_s[idx_source] = 0;
    queue_s[size_s++] = idx_source;

    dist_t[idx_sink] = 0;
    queue_t[size_t++] = idx_sink;

    int forward = 1;
    int level_s = 0;
    int level_t = 0;

    int *V = malloc(n_nodes * sizeof(int));
    int *C = malloc(n_nodes * sizeof(int));
    for (int k = 0; k < n_nodes; k++) V[k] = k;


    while (size_s > 0 || size_t > 0) {
        if (forward && size_s > 0) {
            //qsort_r(queue_s, size_s, sizeof(int), compare_by_heuristic_forward_desc, (void *)all);
            for (int i = 0; i < size_s; i++) {
                int u = queue_s[i];
                const int *F = all->cliques[u].nodes;
                int f_size = all->cliques[u].size;
                int current_level = dist_s[u];


                for (int v = 0; v < n; v++) {
                    if (u == v || dist_s[v] != -1) continue;
                    int h_v = all->cliques[v].heuristic_forward;
                    // h_v != cycle_time sert à ne pas filtrer le cas où h_v == cycle time (qui est une transition valide)
                    if (h_v != cycle_time && current_level < (h_v / cycle_time)) continue;

                    const int *Fp = all->cliques[v].nodes;
                    int fp_size = all->cliques[v].size;

                    
                    if (!inclusion(F, f_size, Fp, fp_size, A)) continue;

                    int c_size = construction_C(F, f_size, Fp, fp_size, V, n_nodes, A, C);
                    int sum = 0;
                    for (int k = 0; k < c_size; k++) sum += task_times[C[k]];
                    if (sum > cycle_time) continue;

                    dist_s[v] = current_level + 1;
                    parent_s[v] = u;
                    
                    if (dist_t[v] != -1) goto done;
                    next_s[next_size_s++] = v;
                }
            }
            int *tmp = queue_s; queue_s = next_s; next_s = tmp;
            size_s = next_size_s; next_size_s = 0;
            forward = 0; level_s++;
        } else if (!forward && size_t > 0) {
            //qsort_r(queue_t, size_t, sizeof(int), compare_by_heuristic_backward_desc, (void *)all);
            for (int i = 0; i < size_t; i++) {
                int u = queue_t[i];
                const int *F = all->cliques[u].nodes;
                int f_size = all->cliques[u].size;
                int current_level = dist_t[u];


                for (int v = 0; v < n; v++) {
                    if (u == v || dist_t[v] != -1) continue;
                    int h_v = all->cliques[v].heuristic_backward;

                    // h_v != cycle_time sert à ne pas filtrer le cas où h_v == cycle time (qui est une transition valide)
                    if (h_v != cycle_time && current_level < (h_v / cycle_time)) continue;


                    const int *Fp = all->cliques[v].nodes;
                    int fp_size = all->cliques[v].size;


                    if (!inclusion(Fp, fp_size, F, f_size, A)) continue;

                    int c_size = construction_C(Fp, fp_size, F, f_size, V, n_nodes, A, C);
                    int sum = 0;
                    for (int k = 0; k < c_size; k++) sum += task_times[C[k]];
                    if (sum > cycle_time) continue;

                    dist_t[v] = current_level + 1;
                    parent_t[v] = u;

                    if (dist_s[v] != -1) goto done;
                    next_t[next_size_t++] = v;
                }
            }
            int *tmp = queue_t; queue_t = next_t; next_t = tmp;
            size_t = next_size_t; next_size_t = 0;
            forward = 1; level_t++;
        } else {
            break;
        }
    }

    BidirectionalBFSResult *res =NULL;
done:
    res = malloc(sizeof(BidirectionalBFSResult));
    res->parent_from_source = parent_s;
    res->dist_from_source = dist_s;
    res->parent_from_sink = parent_t;
    res->dist_from_sink = dist_t;
    res->meeting_point = -1;

    for (int i = 0; i < n; i++) {
        if (dist_s[i] != -1 && dist_t[i] != -1) {
            res->meeting_point = i;
            break;
        }
    }

    free(queue_s); free(queue_t); free(next_s); free(next_t);
    free(V); free(C);
    return res;
}



// Renvoie le chemin complet du BFS bidirectionnel
// en combinant parent_source (source → meet) et parent_sink (sink → meet)
// meet = sommet où les deux recherches se sont rencontrées
int *get_bidir_bfs_path(int meet, const int *parent_source, const int *parent_sink, int *path_len) {
    int maxlen = 2048;
    int *tmp_source = malloc(maxlen * sizeof(int));
    int *tmp_sink = malloc(maxlen * sizeof(int));
    int len_source = 0, len_sink = 0;

    // Construction de la partie source → meet
    int cur = meet;
    while (cur != -1) {
        tmp_source[len_source++] = cur;
        cur = parent_source[cur];
    }

    // Construction de la partie meet ← sink
    cur = parent_sink[meet];
    while (cur != -1) {
        tmp_sink[len_sink++] = cur;
        cur = parent_sink[cur];
    }

    // Fusion des deux segments
    int total_len = len_source + len_sink;
    int *path = malloc(total_len * sizeof(int));
    
    // Partie source → meet (inversée)
    for (int i = 0; i < len_source; i++) {
        path[i] = tmp_source[len_source - 1 - i];
    }

    // Partie meet ← sink (déjà dans le bon ordre)
    for (int i = 0; i < len_sink; i++) {
        path[len_source + i] = tmp_sink[i];
    }

    free(tmp_source);
    free(tmp_sink);
    *path_len = total_len;
    return path;
}





// Libération matrice ancêtres A (tableau de int* de taille n)
void free_ancestor_matrix(int **A, int n) {
    if (!A) return;
    for (int i = 0; i < n; i++) {
        free(A[i]);
    }
    free(A);
}

// Libération Graphe non orienté H
void free_undirected_graph(UndirectedGraph *H) {
    if (!H) return;
    if (H->edges) free(H->edges);
    free(H);
}

// Libération liste de cliques
void free_all_cliques_list(AllCliquesList *all) {
    if (!all) return;
    if (all->cliques) {
        for (int i = 0; i < all->count; i++) {
            if (all->cliques[i].nodes) free(all->cliques[i].nodes);
        }
        free(all->cliques);
    }
    free(all);
}

// Libération résultat bidirectional BFS
void free_bidirectional_bfs_result(BidirectionalBFSResult *res) {
    if (!res) return;
    if (res->parent_from_source) free(res->parent_from_source);
    if (res->dist_from_source) free(res->dist_from_source);
    if (res->parent_from_sink) free(res->parent_from_sink);
    if (res->dist_from_sink) free(res->dist_from_sink);
    free(res);
}

// Libération graphe G
void free_graph(Graph *G) {
    if (!G) return;
    if (G->task_times) free(G->task_times);
    if (G->arcs) free(G->arcs);
    free(G);
}




// Implémentation adaptée de create_graph_in_memory
Graph *create_graph_in_memory(int n, int *durations, int m, int *edges, int cycle_time) {
    Graph *G = malloc(sizeof(Graph));
    if (!G) return NULL;

    G->n_tasks = n;
    G->n_nodes = n + 2;  // source + puits + tâches
    G->cycle_time = cycle_time;

    G->task_times = malloc(sizeof(int) * G->n_nodes);
    if (!G->task_times) {
        free(G);
        return NULL;
    }

    // Durée 0 pour source (0) et puits (n+1)
    G->task_times[0] = 0;
    for (int i = 0; i < n; i++) {
        G->task_times[i + 1] = durations[i];
    }
    G->task_times[G->n_nodes - 1] = 0;

    G->n_arcs = m;
    G->arcs = malloc(sizeof(Arc) * m);
    if (!G->arcs) {
        free(G->task_times);
        free(G);
        return NULL;
    }

    for (int i = 0; i < m; i++) {
        G->arcs[i].u = edges[2*i];
        G->arcs[i].v = edges[2*i + 1];
    }

    return G;
}


//To compile the function solve_instance with webAssembly, use the following command:
/*emcc optimum.c -O3   
-s MODULARIZE=1   -s EXPORT_NAME=createOptimumModule   
-s EXPORTED_FUNCTIONS="['_solve_instance','_malloc','_free']"   
-s EXPORTED_RUNTIME_METHODS="['ccall','cwrap','getValue','setValue','UTF8ToString','HEAP32']"   
-o optimum.js
*/
//It will create two files: optimum.js and optimum.wasm to put in the same directory as the HTML file that will use it.(public)
// Note: This code is designed to be compiled with Emscripten for use in a web environment.


// Variable globale pour stocker le chemin (ordre optimal)
int *global_path = NULL;
int global_path_len = 0;
#include <emscripten/emscripten.h>

EMSCRIPTEN_KEEPALIVE
int solve_instance(int n, int *durations, int m, int *edges, int cycle_time) {
    Graph *G = create_graph_in_memory(n, durations, m, edges, cycle_time);
    if (!G) return -1;

    int **A = compute_ancestor_matrix(G);
    if (!A) {
        free_graph(G);
        return -2;
    }

    UndirectedGraph *H = build_cocomparability_graph(G, A);
    if (!H) {
        free_ancestor_matrix(A, G->n_nodes);
        free_graph(G);
        return -3;
    }

    AllCliquesList *all = generate_all_cliques(H, A, G);
    if (!all) {
        free_undirected_graph(H);
        free_ancestor_matrix(A, G->n_nodes);
        free_graph(G);
        return -4;
    }

    int idx_source = find_singleton_frontiere(all, 0);
    int idx_sink = find_singleton_frontiere(all, G->n_nodes - 1);
    if (idx_source == -1 || idx_sink == -1) {
        free_all_cliques_list(all);
        free_undirected_graph(H);
        free_ancestor_matrix(A, G->n_nodes);
        free_graph(G);
        return -5;
    }

    BidirectionalBFSResult *res = bidirectional_bfs(all, G->n_nodes, A, G->task_times, G->cycle_time, idx_source, idx_sink);
    if (!res) {
        free_all_cliques_list(all);
        free_undirected_graph(H);
        free_ancestor_matrix(A, G->n_nodes);
        free_graph(G);
        return -6;
    }

    int path_len;
    int *path = get_bidir_bfs_path(res->meeting_point, res->parent_from_source, res->parent_from_sink, &path_len);
    if (!path) {
        free_bidirectional_bfs_result(res);
        free_all_cliques_list(all);
        free_undirected_graph(H);
        free_ancestor_matrix(A, G->n_nodes);
        free_graph(G);
        return -7;
    }

    // Affichage du résultat pour debug

    // Stockage global du chemin (libération possible ailleurs)
    if (global_path) free(global_path);
    global_path = malloc(path_len * sizeof(int));
    if (global_path) {
        for (int i = 0; i < path_len; i++) {
            global_path[i] = path[i];
        }
        global_path_len = path_len;
    } else {
        global_path_len = 0;
    }

    // Nettoyage temporaire
    free(path);
    free_bidirectional_bfs_result(res);
    free_all_cliques_list(all);
    free_undirected_graph(H);
    free_ancestor_matrix(A, G->n_nodes);
    free_graph(G);

    return path_len-1; // Retourne la longueur du chemin - 1 (pour correspondre à l'indexation)
}

// Fonction pour récupérer le chemin global (accessible depuis JS par export si nécessaire)
int* get_global_path(int *length) {
    if (length) *length = global_path_len;
    return global_path;
}






int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s fichier.alb\n", argv[0]);
        return 1;
    }

    const char *filepath = argv[1];
    const char *dot = strrchr(filepath, '.');
    const char *slash = strrchr(filepath, '/');
    const char *basename = slash ? slash + 1 : filepath;
    char base_noext[512];
    if (dot && dot > basename) {
        snprintf(base_noext, dot - basename + 1, "%s", basename);
    } else {
        snprintf(base_noext, sizeof(base_noext), "%s", basename);
    }
    char csvname[600];
    snprintf(csvname, sizeof(csvname), "%s.csv", base_noext);

    FILE *csv = fopen(csvname, "w");
    if (!csv) {
        perror("Erreur création CSV");
        return 1;
    }
    fprintf(csv, "fichier,temps (secondes)\n");

    Graph *G = load_graph_from_file(filepath);
    if (!G) {
        fclose(csv);
        return 1;
    }
    clock_t t1 = clock();
    int **A = compute_ancestor_matrix(G);
    UndirectedGraph *H = build_cocomparability_graph(G, A);
    if (!H) {
        for (int i = 0; i < G->n_nodes; i++) free(A[i]);
        free(A);
        free_graph(G);
        fclose(csv);
        return 1;
    }
    AllCliquesList *all = generate_all_cliques(H,A,G);
    // Trouve la frontière source (celle qui contient uniquement le sommet 0)
    int idx_source = find_singleton_frontiere(all, 0);
    if (idx_source == -1) {
        fprintf(stderr, "Aucune frontière source trouvée !\n");
        free_all_cliques_list(all);
        free(H->edges);
        free(H);
        for (int i = 0; i < G->n_nodes; i++) free(A[i]);
        free(A);
        free_graph(G);
        fclose(csv);
        return 1;
    }
    // Trouve la frontière puits (celle qui contient uniquement le sommet n+1)
    int idx_sink = find_singleton_frontiere(all, G->n_nodes - 1);
    if (idx_sink == -1) {
        fprintf(stderr, "Aucune frontière puits trouvée !\n");
        free_all_cliques_list(all);
        free(H->edges);
        free(H);
        for (int i = 0; i < G->n_nodes; i++) free(A[i]);
        free(A);
        free_graph(G);
        fclose(csv);
        return 1;
    }
    

    // Appelle du BFS bidirectionnel
    BidirectionalBFSResult *bidir_result = bidirectional_bfs(all, G->n_nodes, A, G->task_times, G->cycle_time, idx_source, idx_sink);
    if (!bidir_result) {
        fprintf(stderr, "Erreur lors de l'exécution du BFS bidirectionnel\n");
        goto CLEANUP;
    }

    // Récupération du chemin bidirectionnel
    int path_len = 0;
    int *bidir_path = get_bidir_bfs_path(bidir_result->meeting_point, bidir_result->parent_from_source, bidir_result->parent_from_sink, &path_len);
    if (!bidir_path) {
        fprintf(stderr, "Erreur lors de la reconstruction du chemin\n");
        free_bidirectional_bfs_result(bidir_result);
        goto CLEANUP;
    }
    clock_t t2 = clock();
    double elapsed = (double)(t2 - t1) / CLOCKS_PER_SEC;


    // Écriture du fichier solution
    char solname[600];
    snprintf(solname, sizeof(solname), "%s.sol", base_noext);
    FILE *sol = fopen(solname, "w");
    if (!sol) {
        perror("Erreur création fichier solution");
        free(bidir_path);
        free_bidirectional_bfs_result(bidir_result);
        goto CLEANUP;
    }

    for (int i = 1; i < path_len; i++) {
        int u = bidir_path[i - 1];
        int v = bidir_path[i];

        const int *F = all->cliques[u].nodes;
        int f_size = all->cliques[u].size;
        const int *Fp = all->cliques[v].nodes;
        int fp_size = all->cliques[v].size;

        int V[G->n_nodes];
        for (int k = 0; k < G->n_nodes; k++) V[k] = k;

        int C[G->n_nodes];
        int c_size = construction_C(F, f_size, Fp, fp_size, V, G->n_nodes, A, C);

        fprintf(sol, "station_%d:", i);
        for (int k = 0; k < c_size; k++) {
            fprintf(sol, " %d", C[k]);
        }
        fprintf(sol, "\n");
    }

    fclose(sol);

    
    fprintf(csv, "%s,%.2f\n", filepath, elapsed);
    fclose(csv);

    // Libération mémoire spécifique au BFS
    free(bidir_path);
    free_bidirectional_bfs_result(bidir_result);

    // Nettoyage général
    CLEANUP:
    free_all_cliques_list(all);
    free(H->edges);
    free(H);
    for (int i = 0; i < G->n_nodes; i++) free(A[i]);
    free(A);
    free_graph(G);

    return 0;

}

