#include "board.h"
#include "parse.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#define SMALL_BUFFER 128
#define MEDIUM_BUFFER 512
#define LARGE_BUFFER 1024
#define PRINT_BUFFER 8192

FILE * debugfile;

static inline int find_and_kill_pacman(board_t* board, int new_x, int new_y) {
    for (int p = 0; p < board->n_pacmans; p++) {
        pacman_t* pac = &board->pacmans[p];
        if (pac->pos_x == new_x && pac->pos_y == new_y && pac->alive) {
            pac->alive = 0;
            kill_pacman(board, p);
            return DEAD_PACMAN;
        }
    }
    return VALID_MOVE;
}

static inline int get_board_index(board_t* board, int x, int y) {
    return y * board->width + x;
}

static inline int is_valid_position(board_t* board, int x, int y) {
    return x >= 0 && x < board->width && y >= 0 && y < board->height;
}

static inline void lock_positions(board_t* board, int first_idx, int second_idx) {
    pthread_mutex_lock(&board->board[first_idx].pos_mutex);
    if (first_idx != second_idx) {
        pthread_mutex_lock(&board->board[second_idx].pos_mutex);
    }
}

static inline void unlock_positions(board_t* board, int first_idx, int second_idx) {
    if (first_idx != second_idx) {
        pthread_mutex_unlock(&board->board[second_idx].pos_mutex);
    }
    pthread_mutex_unlock(&board->board[first_idx].pos_mutex);
}

static inline char get_random_direction(void) {
    static const char directions[] = {'W', 'S', 'A', 'D'};
    return directions[rand() % 4];
}

static inline void calculate_new_position(int* new_x, int* new_y, char direction) {
    switch (direction) {
        case 'W': (*new_y)--; break;
        case 'S': (*new_y)++; break;
        case 'A': (*new_x)--; break;
        case 'D': (*new_x)++; break;
    }
}

void sleep_ms(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

int move_pacman(board_t* board, int pacman_index, command_t* command) {
    if (pacman_index < 0 || !board->pacmans[pacman_index].alive) {
        return DEAD_PACMAN; 
    }

    pacman_t* pac = &board->pacmans[pacman_index];
    int new_x = pac->pos_x;
    int new_y = pac->pos_y;

    if (pac->waiting > 0) {
        pac->waiting -= 1;
        return VALID_MOVE;        
    }
    pac->waiting = pac->passo;

    char direction = command->command;

    if (direction == 'R') {
        direction = get_random_direction();
    }

    switch (direction) {
        case 'W': 
        case 'S': 
        case 'A': 
        case 'D':
            calculate_new_position(&new_x, &new_y, direction);
            break;
        case 'T': 
            if (command->turns_left == 1) {
                pac->current_move += 1; 
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        case 'Q': 
            pac->current_move += 1;
            return DEAD_PACMAN; 
        case 'G': 
            pac->current_move += 1;
            return QUICKSAVE; 
        default:
            return INVALID_MOVE; 
    }

    ++pac->current_move;

    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    pthread_mutex_lock(&board->display_mutex);
    
    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, pac->pos_x, pac->pos_y);
    
    int first_idx = (old_index < new_index) ? old_index : new_index;
    int second_idx = (old_index < new_index) ? new_index : old_index;
    
    lock_positions(board, first_idx, second_idx);
    
    char target_content = board->board[new_index].content;

    if (board->board[new_index].has_portal) {
        board->board[old_index].content = ' ';
        board->board[new_index].content = 'P';
        unlock_positions(board, first_idx, second_idx);
        pthread_mutex_unlock(&board->display_mutex);
        return REACHED_PORTAL;
    }

    if (target_content == 'W') {
        unlock_positions(board, first_idx, second_idx);
        pthread_mutex_unlock(&board->display_mutex);
        return INVALID_MOVE;
    }

    if (target_content == 'M') {
        kill_pacman(board, pacman_index);
        unlock_positions(board, first_idx, second_idx);
        pthread_mutex_unlock(&board->display_mutex);
        return DEAD_PACMAN;
    }

    if (board->board[new_index].has_dot) {
        pac->points++;
        board->board[new_index].has_dot = 0;
    }

    board->board[old_index].content = ' ';
    pac->pos_x = new_x;
    pac->pos_y = new_y;
    board->board[new_index].content = 'P';

    unlock_positions(board, first_idx, second_idx);
    pthread_mutex_unlock(&board->display_mutex);
    return VALID_MOVE;
}

static int move_ghost_charged_direction(board_t* board, ghost_t* ghost, char direction, int* new_x, int* new_y) {
    *new_x = ghost->pos_x;
    *new_y = ghost->pos_y;
    int x = *new_x;
    int y = *new_y;
    
    switch (direction) {
        case 'W': 
            if (y == 0) return INVALID_MOVE;
            *new_y = 0; 
            for (int i = y - 1; i >= 0; i--) {
                char target_content = board->board[get_board_index(board, x, i)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_y = i + 1; 
                    return VALID_MOVE;
                }
                else if (target_content == 'P') {
                    *new_y = i;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'S': 
            if (y == board->height - 1) return INVALID_MOVE;
            *new_y = board->height - 1; 
            for (int i = y + 1; i < board->height; i++) {
                char target_content = board->board[get_board_index(board, x, i)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_y = i - 1; 
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_y = i;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'A': 
            if (x == 0) return INVALID_MOVE;
            *new_x = 0; 
            for (int j = x - 1; j >= 0; j--) {
                char target_content = board->board[get_board_index(board, j, y)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_x = j + 1; 
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_x = j;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'D':
            if (x == board->width - 1) return INVALID_MOVE;
            *new_x = board->width - 1; 
            for (int j = x + 1; j < board->width; j++) {
                char target_content = board->board[get_board_index(board, j, y)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_x = j - 1; 
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_x = j;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;
        default:
            return INVALID_MOVE;
    }
    return VALID_MOVE;
}   

int move_ghost_charged(board_t* board, int ghost_index, char direction) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int new_x, new_y;

    pthread_mutex_lock(&board->display_mutex);
    
    ghost->charged = 0; 
    int result = move_ghost_charged_direction(board, ghost, direction, &new_x, &new_y);
    if (result == INVALID_MOVE) {
        pthread_mutex_unlock(&board->display_mutex);
        return INVALID_MOVE;
    }

    int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
    int new_index = get_board_index(board, new_x, new_y);

    board->board[old_index].content = ' ';
    ghost->pos_x = new_x;
    ghost->pos_y = new_y;
    board->board[new_index].content = 'M';
    
    pthread_mutex_unlock(&board->display_mutex);
    return result;
}

int move_ghost(board_t* board, int ghost_index, command_t* command) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int new_x = ghost->pos_x;
    int new_y = ghost->pos_y;

    if (ghost->waiting > 0) {
        ghost->waiting -= 1;
        return VALID_MOVE;
    }
    ghost->waiting = ghost->passo;

    char direction = command->command;
    
    if (direction == 'R') {
        direction = get_random_direction();
    }

    switch (direction) {
        case 'W': 
        case 'S': 
        case 'A': 
        case 'D': 
            calculate_new_position(&new_x, &new_y, direction);
            break;
        case 'C': 
            pthread_mutex_lock(&board->display_mutex);
            ghost->current_move += 1;
            ghost->charged = 1;
            pthread_mutex_unlock(&board->display_mutex);
            return VALID_MOVE;
        case 'T': 
            if (command->turns_left == 1) {
                ghost->current_move += 1; 
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        default:
            return INVALID_MOVE; 
    }

    ++ghost->current_move;
    if (ghost->charged)
        return move_ghost_charged(board, ghost_index, direction);

    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    pthread_mutex_lock(&board->display_mutex);
    
    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
    
    int first_idx = (old_index < new_index) ? old_index : new_index;
    int second_idx = (old_index < new_index) ? new_index : old_index;
    
    lock_positions(board, first_idx, second_idx);
    
    char target_content = board->board[new_index].content;

    if (target_content == 'W' || target_content == 'M') {
        unlock_positions(board, first_idx, second_idx);
        pthread_mutex_unlock(&board->display_mutex);
        return INVALID_MOVE;
    }

    int result = VALID_MOVE;
    if (target_content == 'P') {
        result = find_and_kill_pacman(board, new_x, new_y);
    }

    board->board[old_index].content = ' '; 

    ghost->pos_x = new_x;
    ghost->pos_y = new_y;

    board->board[new_index].content = 'M';
    
    unlock_positions(board, first_idx, second_idx);
    pthread_mutex_unlock(&board->display_mutex);
    return result;
}

void kill_pacman(board_t* board, int pacman_index) {
    pacman_t* pac = &board->pacmans[pacman_index];
    int index = get_board_index(board, pac->pos_x, pac->pos_y);

    board->board[index].content = ' ';
    pac->alive = 0;
}

int load_level(board_t *board, int points, const char* level_dir) {
    pthread_mutex_init(&board->display_mutex, NULL);
    board->game_running = true;
    board->level_complete = false;
    board->game_result = 0; 
    
    board->n_pacmans = 1;
    board->pacmans = calloc(board->n_pacmans, sizeof(pacman_t));
    
    if (parse_level_file(board, level_dir) != 0) {
        return -1;
    }

    board->ghosts = calloc(board->n_ghosts, sizeof(ghost_t));

    for (int i = 0; i < board->n_ghosts; i++) {
        int passo;
        if (parse_pacman_ghost_file(board->ghosts_files[i], board->ghosts[i].moves, 
                               &board->ghosts[i].n_moves, &passo) == 0) {
            board->ghosts[i].passo = passo;
            board->ghosts[i].waiting = passo;
            board->ghosts[i].current_move = 0;
            board->ghosts[i].charged = 0;

            int fd = open(board->ghosts_files[i], O_RDONLY);
            if (fd != -1) {
                char buf[LARGE_BUFFER];
                ssize_t n = read(fd, buf, sizeof(buf) - 1);
                if (n > 0) {
                    buf[n] = '\0';
                    char* line = buf;
                    int pos_x = 1, pos_y = 1;
                    while (line && *line) {
                        while (*line == ' ' || *line == '\t') line++;
                        if (*line != '#' && strncmp(line, "POS", 3) == 0) {
                            if (sscanf(line, "POS %d %d", &pos_y, &pos_x) == 2) {
                                board->ghosts[i].pos_x = pos_x;
                                board->ghosts[i].pos_y = pos_y;
                                int idx = pos_y * board->width + pos_x;
                                board->board[idx].content = 'M';
                                break;
                            }
                        }
                        line = strchr(line, '\n');
                        if (line) line++;
                    }
                }
                close(fd);
            }
        }
    }

    if (board->pacman_file[0] != '\0') {
        int passo;
        if (parse_pacman_ghost_file(board->pacman_file, board->pacmans[0].moves, 
                               &board->pacmans[0].n_moves, &passo) == 0) {
            board->pacmans[0].passo = passo;
            board->pacmans[0].waiting = passo;
        }
    } else {
        board->pacmans[0].n_moves = 0; 
        board->pacmans[0].passo = 0;
        board->pacmans[0].waiting = 0;
    }

    if (board->pacman_file[0] == '\0') {
        int pacman_placed = 0;
        for (int y = 0; y < board->height && !pacman_placed; y++) {
            for (int x = 0; x < board->width && !pacman_placed; x++) {
                int idx = y * board->width + x;
                if (board->board[idx].content == ' ' && !board->board[idx].has_portal) {

                    int ghost_here = 0;
                    for (int g = 0; g < board->n_ghosts; g++) {
                        if (board->ghosts[g].pos_x == x && board->ghosts[g].pos_y == y) {
                            ghost_here = 1;
                            break;
                        }
                    }
                    if (!ghost_here) {
                        board->board[idx].content = 'P';
                        board->pacmans[0].pos_x = x;
                        board->pacmans[0].pos_y = y;
                        board->pacmans[0].alive = 1;
                        board->pacmans[0].points = points;
                        board->pacmans[0].current_move = 0;
                        pacman_placed = 1;
                    }
                }
            }
        }
    } else {

        int fd = open(board->pacman_file, O_RDONLY);
        if (fd != -1) {
            char buf[LARGE_BUFFER];
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                char* line = buf;
                int pos_x = 1, pos_y = 1;
                while (line && *line) {
                    while (*line == ' ' || *line == '\t') line++;
                    if (*line != '#' && strncmp(line, "POS", 3) == 0) {
                        if (sscanf(line, "POS %d %d", &pos_y, &pos_x) == 2) {
                            board->pacmans[0].pos_x = pos_x;
                            board->pacmans[0].pos_y = pos_y;
                            int idx = pos_y * board->width + pos_x;
                            board->board[idx].content = 'P';
                            board->pacmans[0].alive = 1;
                            board->pacmans[0].points = points;
                            board->pacmans[0].current_move = 0;
                            break;
                        }
                    }
                    line = strchr(line, '\n');
                    if (line) line++;
                }
            }
            close(fd);
        }
    }

    return 0;
}

void unload_level(board_t * board) {
    for (int i = 0; i < board->width * board->height; i++) {
        pthread_mutex_destroy(&board->board[i].pos_mutex);
    }
    pthread_mutex_destroy(&board->display_mutex);
    free(board->board);
    free(board->pacmans);
    free(board->ghosts);
}

void open_debug_file(char *filename) {
    debugfile = fopen(filename, "w");
}

void close_debug_file() {
    fclose(debugfile);
}

void debug(const char * format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(debugfile, format, args);
    va_end(args);

    fflush(debugfile);
}

void print_board(board_t *board) {
    if (!board || !board->board) {
        return;
    }

    char buffer[PRINT_BUFFER];
    size_t offset = 0;

    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "=== [%d] LEVEL INFO ===\n"
                       "Dimensions: %d x %d\n"
                       "Tempo: %d\n"
                       "Pacman file: %s\n",
                       getpid(), board->height, board->width, board->tempo, board->pacman_file);

    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "Monster files (%d):\n", board->n_ghosts);

    for (int i = 0; i < board->n_ghosts; i++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                           "  - %s\n", board->ghosts_files[i]);
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n=== BOARD ===\n");

    for (int y = 0; y < board->height; y++) {
        for (int x = 0; x < board->width; x++) {
            int idx = y * board->width + x;
            if (offset < sizeof(buffer) - 2) {
                buffer[offset++] = board->board[idx].content;
            }
        }
        if (offset < sizeof(buffer) - 2) {
            buffer[offset++] = '\n';
        }
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "==================\n");

    buffer[offset] = '\0';

    debug("%s", buffer);
}