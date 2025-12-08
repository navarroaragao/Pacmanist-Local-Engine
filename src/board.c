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

FILE * debugfile;

// Helper private function to find and kill pacman at specific position
static int find_and_kill_pacman(board_t* board, int new_x, int new_y) {
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

// Helper private function for getting board position index
static inline int get_board_index(board_t* board, int x, int y) {
    return y * board->width + x;
}

// Helper private function for checking valid position
static inline int is_valid_position(board_t* board, int x, int y) {
    return (x >= 0 && x < board->width) && (y >= 0 && y < board->height); // Inside of the board boundaries
}

// Helper private function to unlock positions in order
static inline void unlock_positions(board_t* board, int first_idx, int second_idx) {
    if (first_idx != second_idx) {
        pthread_mutex_unlock(&board->board[second_idx].pos_mutex);
    }
    pthread_mutex_unlock(&board->board[first_idx].pos_mutex);
}

// Helper private function to get random direction
static inline char get_random_direction(void) {
    static const char directions[] = {'W', 'S', 'A', 'D'};
    return directions[rand() % 4];
}

// Helper private function to calculate new position based on direction
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
        return DEAD_PACMAN; // Invalid or dead pacman
    }

    pacman_t* pac = &board->pacmans[pacman_index];
    int new_x = pac->pos_x;
    int new_y = pac->pos_y;

    // check passo
    if (pac->waiting > 0) {
        pac->waiting -= 1;
        return VALID_MOVE;        
    }
    pac->waiting = pac->passo;

    char direction = command->command;

    if (direction == 'R') {
        direction = get_random_direction();
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W': // Up
        case 'S': // Down
        case 'A': // Left
        case 'D': // Right
            calculate_new_position(&new_x, &new_y, direction);
            break;
        case 'T': // Wait
            if (command->turns_left == 1) {
                pac->current_move += 1; // move on
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        case 'Q': // Quit (from file)
            pac->current_move += 1;
            return DEAD_PACMAN; // Terminate game
        case 'G': // Quicksave (from file)
            pac->current_move += 1;
            return QUICKSAVE; // Trigger quicksave
        default:
            return INVALID_MOVE; // Invalid direction
    }

    // Logic for the WASD movement
    ++pac->current_move;

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, pac->pos_x, pac->pos_y);
    
    // Lock positions in order to prevent deadlocks
    int first_idx = (old_index < new_index) ? old_index : new_index;
    int second_idx = (old_index < new_index) ? new_index : old_index;
    
    pthread_mutex_lock(&board->board[first_idx].pos_mutex);
    if (first_idx != second_idx) {
        pthread_mutex_lock(&board->board[second_idx].pos_mutex);
    }
    
    char target_content = board->board[new_index].content;

    if (board->board[new_index].has_portal) {
        board->board[old_index].content = ' ';
        board->board[new_index].content = 'P';
        unlock_positions(board, first_idx, second_idx);
        return REACHED_PORTAL;
    }

    // Check for walls
    if (target_content == 'W') {
        unlock_positions(board, first_idx, second_idx);
        return INVALID_MOVE;
    }

    // Check for ghosts
    if (target_content == 'M') {
        kill_pacman(board, pacman_index);
        unlock_positions(board, first_idx, second_idx);
        return DEAD_PACMAN;
    }

    // Collect points
    if (board->board[new_index].has_dot) {
        pac->points++;
        board->board[new_index].has_dot = 0;
    }

    board->board[old_index].content = ' ';
    pac->pos_x = new_x;
    pac->pos_y = new_y;
    board->board[new_index].content = 'P';

    unlock_positions(board, first_idx, second_idx);
    return VALID_MOVE;
}

// Helper private function for charged ghost movement in one direction
static int move_ghost_charged_direction(board_t* board, ghost_t* ghost, char direction, int* new_x, int* new_y) {
    *new_x = ghost->pos_x;
    *new_y = ghost->pos_y;
    int x = *new_x;
    int y = *new_y;
    
    switch (direction) {
        case 'W': // Up
            if (y == 0) return INVALID_MOVE;
            *new_y = 0; // In case there is no colision
            for (int i = y - 1; i >= 0; i--) {
                char target_content = board->board[get_board_index(board, x, i)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_y = i + 1; // stop before colision
                    return VALID_MOVE;
                }
                else if (target_content == 'P') {
                    *new_y = i;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'S': // Down
            if (y == board->height - 1) return INVALID_MOVE;
            *new_y = board->height - 1; // In case there is no colision
            for (int i = y + 1; i < board->height; i++) {
                char target_content = board->board[get_board_index(board, x, i)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_y = i - 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_y = i;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'A': // Left
            if (x == 0) return INVALID_MOVE;
            *new_x = 0; // In case there is no colision
            for (int j = x - 1; j >= 0; j--) {
                char target_content = board->board[get_board_index(board, j, y)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_x = j + 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_x = j;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'D': // Right
            if (x == board->width - 1) return INVALID_MOVE;
            *new_x = board->width - 1; // In case there is no colision
            for (int j = x + 1; j < board->width; j++) {
                char target_content = board->board[get_board_index(board, j, y)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_x = j - 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_x = j;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;
        default:
            debug("DEFAULT CHARGED MOVE - direction = %c\n", direction);
            return INVALID_MOVE;
    }
    return VALID_MOVE;
}   

int move_ghost_charged(board_t* board, int ghost_index, char direction) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int new_x, new_y;

    ghost->charged = 0; //uncharge
    int result = move_ghost_charged_direction(board, ghost, direction, &new_x, &new_y);
    if (result == INVALID_MOVE) {
        debug("DEFAULT CHARGED MOVE - direction = %c\n", direction);
        return INVALID_MOVE;
    }

    // Get board indices
    int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
    int new_index = get_board_index(board, new_x, new_y);

    // Update board - clear old position (restore what was there)
    board->board[old_index].content = ' '; // Or restore the dot if ghost was on one
    // Update ghost position
    ghost->pos_x = new_x;
    ghost->pos_y = new_y;
    // Update board - set new position
    board->board[new_index].content = 'M';
    return result;
}

int move_ghost(board_t* board, int ghost_index, command_t* command) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int new_x = ghost->pos_x;
    int new_y = ghost->pos_y;

    // check passo
    if (ghost->waiting > 0) {
        ghost->waiting -= 1;
        return VALID_MOVE;
    }
    ghost->waiting = ghost->passo;

    char direction = command->command;
    
    if (direction == 'R') {
        direction = get_random_direction();
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W': // Up
        case 'S': // Down
        case 'A': // Left
        case 'D': // Right
            calculate_new_position(&new_x, &new_y, direction);
            break;
        case 'C': // Charge
            ghost->current_move += 1;
            ghost->charged = 1;
            return VALID_MOVE;
        case 'T': // Wait
            if (command->turns_left == 1) {
                ghost->current_move += 1; // move on
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        default:
            return INVALID_MOVE; // Invalid direction
    }

    // Logic for the WASD movement
    ++ghost->current_move;
    if (ghost->charged)
        return move_ghost_charged(board, ghost_index, direction);

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    // Check board position
    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
    
    // Lock positions in order to prevent deadlocks
    int first_idx = (old_index < new_index) ? old_index : new_index;
    int second_idx = (old_index < new_index) ? new_index : old_index;
    
    pthread_mutex_lock(&board->board[first_idx].pos_mutex);
    if (first_idx != second_idx) {
        pthread_mutex_lock(&board->board[second_idx].pos_mutex);
    }
    
    char target_content = board->board[new_index].content;

    // Check for walls and ghosts
    if (target_content == 'W' || target_content == 'M') {
        unlock_positions(board, first_idx, second_idx);
        return INVALID_MOVE;
    }

    int result = VALID_MOVE;
    // Check for pacman
    if (target_content == 'P') {
        result = find_and_kill_pacman(board, new_x, new_y);
    }

    // Update board - clear old position (restore what was there)
    board->board[old_index].content = ' '; // Or restore the dot if ghost was on one

    // Update ghost position
    ghost->pos_x = new_x;
    ghost->pos_y = new_y;

    // Update board - set new position
    board->board[new_index].content = 'M';
    
    unlock_positions(board, first_idx, second_idx);
    return result;
}

void kill_pacman(board_t* board, int pacman_index) {
    debug("Killing %d pacman\n\n", pacman_index);
    pacman_t* pac = &board->pacmans[pacman_index];
    int index = pac->pos_y * board->width + pac->pos_x;

    // Remove pacman from the board
    board->board[index].content = ' ';

    // Mark pacman as dead
    pac->alive = 0;
}

// Static Loading
int load_pacman(board_t* board, int points) {
    board->board[1 * board->width + 1].content = 'P'; // Pacman
    board->pacmans[0].pos_x = 1;
    board->pacmans[0].pos_y = 1;
    board->pacmans[0].alive = 1;
    board->pacmans[0].points = points;
    return 0;
}

// Static Loading
int load_ghost(board_t* board) {
    // Ghost 0
    board->board[3 * board->width + 1].content = 'M'; // Monster
    board->ghosts[0].pos_x = 1;
    board->ghosts[0].pos_y = 3;
    board->ghosts[0].passo = 0;
    board->ghosts[0].waiting = 0;
    board->ghosts[0].current_move = 0;
    board->ghosts[0].n_moves = 16;
    for (int i = 0; i < 8; i++) {
        board->ghosts[0].moves[i].command = 'D';
        board->ghosts[0].moves[i].turns = 1; 
    }
    for (int i = 8; i < 16; i++) {
        board->ghosts[0].moves[i].command = 'A';
        board->ghosts[0].moves[i].turns = 1; 
    }

    // Ghost 1
    board->board[2 * board->width + 4].content = 'M'; // Monster
    board->ghosts[1].pos_x = 4;
    board->ghosts[1].pos_y = 2;
    board->ghosts[1].passo = 1;
    board->ghosts[1].waiting = 1;
    board->ghosts[1].current_move = 0;
    board->ghosts[1].n_moves = 1;
    board->ghosts[1].moves[0].command = 'R'; // Random
    board->ghosts[1].moves[0].turns = 1; 
    
    return 0;
}

// Parse functions moved to parse.c

int load_level(board_t *board, int points, const char* level_dir) {
    // Initialize sync structures
    pthread_mutex_init(&board->display_mutex, NULL);
    board->game_running = true;
    board->level_complete = false;
    board->game_result = 0; // CONTINUE_PLAY
    
    // Allocate initial structures
    board->n_pacmans = 1;
    board->pacmans = calloc(board->n_pacmans, sizeof(pacman_t));
    
    // Try to parse level file
    if (parse_level_file(board, level_dir) != 0) {
        debug("Failed to parse level file, loading static level\n");
        
        // Fallback to static 6x8 level
        board->height = 6;
        board->width = 8;
        board->tempo = 100;
        board->n_ghosts = 2;
        board->board = calloc(board->width * board->height, sizeof(board_pos_t));
        board->ghosts = calloc(board->n_ghosts, sizeof(ghost_t));
        board->pacman_file[0] = '\0';
        
        // Build static board
        for (int i = 0; i < board->height; i++) {
            for (int j = 0; j < board->width; j++) {
                int idx = i * board->width + j;
                pthread_mutex_init(&board->board[idx].pos_mutex, NULL);
                if (i == 0 || i == board->height - 1 || j == 0 || j == board->width - 1) {
                    board->board[idx].content = 'W';
                    board->board[idx].has_dot = 0;
                } else if (i == board->height - 2 && j == board->width - 2) {
                    board->board[idx].content = ' ';
                    board->board[idx].has_portal = 1;
                    board->board[idx].has_dot = 0;
                } else {
                    board->board[idx].content = ' ';
                    board->board[idx].has_dot = 1;
                }
            }
        }
        
        // Place Pacman at (1,1)
        board->board[1 * board->width + 1].content = 'P';
        board->pacmans[0].pos_x = 1;
        board->pacmans[0].pos_y = 1;
        board->pacmans[0].alive = 1;
        board->pacmans[0].points = points;
        board->pacmans[0].current_move = 0;
        board->pacmans[0].n_moves = 0;
        board->pacmans[0].passo = 0;
        board->pacmans[0].waiting = 0;
        
        // Load static ghosts
        load_ghost(board);
        
        return 0;
    }

    // Allocate ghosts based on parsed n_ghosts
    board->ghosts = calloc(board->n_ghosts, sizeof(ghost_t));

    // Load Pacman behavior
    if (board->pacman_file[0] != '\0') {
        int passo;
        if (parse_pacman_ghost_file(board->pacman_file, board->pacmans[0].moves, 
                               &board->pacmans[0].n_moves, &passo) == 0) {
            board->pacmans[0].passo = passo;
            board->pacmans[0].waiting = passo;
        }
    } else {
        board->pacmans[0].n_moves = 0; // User controlled
        board->pacmans[0].passo = 0;
        board->pacmans[0].waiting = 0;
    }

    // Place Pacman based on whether PAC file exists
    if (board->pacman_file[0] == '\0') {
        // No PAC file - place at first empty space
        int pacman_placed = 0;
        for (int y = 0; y < board->height && !pacman_placed; y++) {
            for (int x = 0; x < board->width && !pacman_placed; x++) {
                int idx = y * board->width + x;
                if (board->board[idx].content == ' ' && !board->board[idx].has_portal) {
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
    } else {
        // PAC file exists - read position from POS line
        int fd = open(board->pacman_file, O_RDONLY);
        if (fd != -1) {
            char buf[1024];
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                // Find POS line that is not a comment
                char* line = buf;
                int pos_x = 1, pos_y = 1;
                int found = 0;
                while (line && *line) {
                    // Skip leading whitespace
                    while (*line == ' ' || *line == '\t') line++;
                    // Check if line starts with POS (not a comment)
                    if (*line != '#' && strncmp(line, "POS", 3) == 0) {
                        if (sscanf(line, "POS %d %d", &pos_y, &pos_x) == 2) {
                            board->pacmans[0].pos_x = pos_x;
                            board->pacmans[0].pos_y = pos_y;
                            int idx = pos_y * board->width + pos_x;
                            board->board[idx].content = 'P';
                            board->pacmans[0].alive = 1;
                            board->pacmans[0].points = points;
                            board->pacmans[0].current_move = 0;
                            found = 1;
                            break;
                        }
                    }
                    // Move to next line
                    line = strchr(line, '\n');
                    if (line) line++;
                }
                if (!found) {
                    debug("Pacman: Failed to find POS command\n");
                }
            }
            close(fd);
        }
    }

    // Load Ghosts behaviors and place them
    for (int i = 0; i < board->n_ghosts; i++) {
        int passo;
        if (parse_pacman_ghost_file(board->ghosts_files[i], board->ghosts[i].moves, 
                               &board->ghosts[i].n_moves, &passo) == 0) {
            board->ghosts[i].passo = passo;
            board->ghosts[i].waiting = passo;
            board->ghosts[i].current_move = 0;
            board->ghosts[i].charged = 0;

            // Find position from behavior file POS line
            int fd = open(board->ghosts_files[i], O_RDONLY);
            if (fd != -1) {
                char buf[1024];
                ssize_t n = read(fd, buf, sizeof(buf) - 1);
                if (n > 0) {
                    buf[n] = '\0';
                    // Find POS line that is not a comment
                    char* line = buf;
                    int pos_x = 1, pos_y = 1;
                    int found = 0;
                    while (line && *line) {
                        // Skip leading whitespace
                        while (*line == ' ' || *line == '\t') line++;
                        // Check if line starts with POS (not a comment)
                        if (*line != '#' && strncmp(line, "POS", 3) == 0) {
                            if (sscanf(line, "POS %d %d", &pos_y, &pos_x) == 2) {
                                board->ghosts[i].pos_x = pos_x;
                                board->ghosts[i].pos_y = pos_y;
                                int idx = pos_y * board->width + pos_x;
                                board->board[idx].content = 'M';
                                found = 1;
                                break;
                            }
                        }
                        // Move to next line
                        line = strchr(line, '\n');
                        if (line) line++;
                    }
                    if (!found) {
                        debug("Ghost %d: Failed to find POS command\n", i);
                    }
                }
                close(fd);
            }
        }
    }

    return 0;
}

void unload_level(board_t * board) {
    // Destroy mutexes for all positions
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
        debug("[%d] Board is empty or not initialized.\n", getpid());
        return;
    }

    // Large buffer to accumulate the whole output
    char buffer[8192];
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