#include "board.h"
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
        char directions[] = {'W', 'S', 'A', 'D'};
        direction = directions[rand() % 4];
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W': // Up
            new_y--;
            break;
        case 'S': // Down
            new_y++;
            break;
        case 'A': // Left
            new_x--;
            break;
        case 'D': // Right
            new_x++;
            break;
        case 'T': // Wait
            if (command->turns_left == 1) {
                pac->current_move += 1; // move on
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        default:
            return INVALID_MOVE; // Invalid direction
    }

    // Logic for the WASD movement
    pac->current_move+=1;

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, pac->pos_x, pac->pos_y);
    char target_content = board->board[new_index].content;

    if (board->board[new_index].has_portal) {
        board->board[old_index].content = ' ';
        board->board[new_index].content = 'P';
        return REACHED_PORTAL;
    }

    // Check for walls
    if (target_content == 'W') {
        return INVALID_MOVE;
    }

    // Check for ghosts
    if (target_content == 'M') {
        kill_pacman(board, pacman_index);
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

    return VALID_MOVE;
}

// Helper private function for charged ghost movement in one direction
static int move_ghost_charged_direction(board_t* board, ghost_t* ghost, char direction, int* new_x, int* new_y) {
    int x = ghost->pos_x;
    int y = ghost->pos_y;
    *new_x = x;
    *new_y = y;
    
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
    int x = ghost->pos_x;
    int y = ghost->pos_y;
    int new_x = x;
    int new_y = y;

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
        char directions[] = {'W', 'S', 'A', 'D'};
        direction = directions[rand() % 4];
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W': // Up
            new_y--;
            break;
        case 'S': // Down
            new_y++;
            break;
        case 'A': // Left
            new_x--;
            break;
        case 'D': // Right
            new_x++;
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
    ghost->current_move++;
    if (ghost->charged)
        return move_ghost_charged(board, ghost_index, direction);

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    // Check board position
    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
    char target_content = board->board[new_index].content;

    // Check for walls and ghosts
    if (target_content == 'W' || target_content == 'M') {
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

// Parse behavior file for Pacman or Monster
int parse_behavior_file(const char* filename, command_t* moves, int* n_moves, int* passo) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        debug("Failed to open behavior file: %s\n", filename);
        return -1;
    }

    char buffer[4096];
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(fd);
        return -1;
    }
    buffer[bytes_read] = '\0';
    close(fd);

    char* line = buffer;
    char* next_line;
    int move_count = 0;
    int has_passo = 0, has_pos = 0;

    while ((next_line = strchr(line, '\n')) != NULL) {
        *next_line = '\0';
        
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\0') {
            line = next_line + 1;
            continue;
        }

        // Parse based on keyword, not line number
        if (strncmp(line, "PASSO", 5) == 0 && !has_passo) {
            if (sscanf(line, "PASSO %d", passo) != 1) {
                *passo = 0;
            }
            debug("Read PASSO: %d from file: %s\n", *passo, filename);
            has_passo = 1;
        } else if (strncmp(line, "POS", 3) == 0 && !has_pos) {
            // Position is read elsewhere, just mark as seen
            debug("Found POS line in file: %s\n", filename);
            has_pos = 1;
        } else if (has_passo && has_pos) {
            // Movement commands - only parse after PASSO and POS
            char cmd;
            int turns;
            if (sscanf(line, "%c %d", &cmd, &turns) == 2) {
                if (move_count < MAX_MOVES) {
                    moves[move_count].command = cmd;
                    moves[move_count].turns = turns;
                    moves[move_count].turns_left = turns;
                    move_count++;
                }
            } else if (sscanf(line, "%c", &cmd) == 1) {
                if (move_count < MAX_MOVES) {
                    moves[move_count].command = cmd;
                    moves[move_count].turns = 1;
                    moves[move_count].turns_left = 1;
                    move_count++;
                }
            }
        }
        
        line = next_line + 1;
    }

    *n_moves = move_count;
    return 0;
}

// Parse level file and load the board
int parse_level_file(board_t* board, const char* level_dir) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", level_dir, board->level_name);

    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        debug("Failed to open level file: %s\n", filepath);
        return -1;
    }

    char buffer[8192];
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(fd);
        return -1;
    }
    buffer[bytes_read] = '\0';
    close(fd);

    char* line = buffer;
    char* next_line;
    int board_line = 0;
    int has_dim = 0, has_tempo = 0, has_pac = 0, has_mon = 0;

    // Parse file line by line
    while ((next_line = strchr(line, '\n')) != NULL) {
        *next_line = '\0';
        
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\0') {
            line = next_line + 1;
            continue;
        }

        // Parse based on keyword, not line number
        if (strncmp(line, "DIM ", 4) == 0 && !has_dim) {
            if (sscanf(line, "DIM %d %d", &board->height, &board->width) != 2) {
                return -1;
            }
            board->board = calloc(board->width * board->height, sizeof(board_pos_t));
            has_dim = 1;
        } else if (strncmp(line, "TEMPO ", 6) == 0 && !has_tempo) {
            sscanf(line, "TEMPO %d", &board->tempo);
            has_tempo = 1;
        } else if (strncmp(line, "PAC", 3) == 0 && !has_pac) {
            char pac_file[128];
            if (sscanf(line, "PAC %127s", pac_file) == 1) {
                snprintf(board->pacman_file, sizeof(board->pacman_file), "%s/%s", level_dir, pac_file);
            } else {
                board->pacman_file[0] = '\0'; // No file = user controlled
            }
            has_pac = 1;
        } else if (strncmp(line, "MON", 3) == 0 && !has_mon) {
            char* mon_start = strchr(line, ' ');
            if (mon_start) {
                mon_start++;
                board->n_ghosts = 0;
                char mon_file[128];
                char line_copy[512];
                strncpy(line_copy, mon_start, sizeof(line_copy) - 1);
                line_copy[sizeof(line_copy) - 1] = '\0';
                char* token = strtok(line_copy, " ");
                while (token && board->n_ghosts < MAX_GHOSTS) {
                    strncpy(mon_file, token, sizeof(mon_file) - 1);
                    mon_file[sizeof(mon_file) - 1] = '\0';
                    snprintf(board->ghosts_files[board->n_ghosts], sizeof(board->ghosts_files[0]), "%s/%s", level_dir, mon_file);
                    board->n_ghosts++;
                    token = strtok(NULL, " ");
                }
            }
            has_mon = 1;
        } else if (has_dim && has_tempo && has_pac && has_mon) {
            // Board content - only parse after all headers are read
            if (board_line < board->height) {
                for (int x = 0; x < board->width && line[x] != '\0'; x++) {
                    int idx = board_line * board->width + x;
                    char c = line[x];
                    
                    if (c == 'X') {
                        board->board[idx].content = 'W';
                        board->board[idx].has_dot = 0;
                    } else if (c == 'o') {
                        board->board[idx].content = ' ';
                        board->board[idx].has_dot = 1;
                    } else if (c == '@') {
                        board->board[idx].content = ' ';
                        board->board[idx].has_portal = 1;
                        board->board[idx].has_dot = 0;
                    } else {
                        board->board[idx].content = ' ';
                        board->board[idx].has_dot = 0;
                    }
                }
                board_line++;
            }
        }
        
        line = next_line + 1;
    }

    return 0;
}

int load_level(board_t *board, int points, const char* level_dir) {
    // Allocate initial structures
    board->n_pacmans = 1;
    board->pacmans = calloc(board->n_pacmans, sizeof(pacman_t));
    
    // Try to parse level file
    if (parse_level_file(board, level_dir) != 0) {
        debug("Failed to parse level file, loading static level\n");
        
        // Fallback to static 6x6 level
        board->height = 6;
        board->width = 6;
        board->tempo = 100;
        board->n_ghosts = 2;
        board->board = calloc(board->width * board->height, sizeof(board_pos_t));
        board->ghosts = calloc(board->n_ghosts, sizeof(ghost_t));
        board->pacman_file[0] = '\0';
        
        // Build static board
        for (int i = 0; i < board->height; i++) {
            for (int j = 0; j < board->width; j++) {
                int idx = i * board->width + j;
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
        if (parse_behavior_file(board->pacman_file, board->pacmans[0].moves, 
                               &board->pacmans[0].n_moves, &passo) == 0) {
            board->pacmans[0].passo = passo;
            board->pacmans[0].waiting = passo;
        }
    } else {
        board->pacmans[0].n_moves = 0; // User controlled
        board->pacmans[0].passo = 0;
        board->pacmans[0].waiting = 0;
    }

    // Find Pacman position and place it
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

    // Load Ghosts behaviors and place them
    for (int i = 0; i < board->n_ghosts; i++) {
        int passo;
        if (parse_behavior_file(board->ghosts_files[i], board->ghosts[i].moves, 
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
                    char* pos_line = strstr(buf, "POS");
                    int pos_x = 1, pos_y = 1;
                    if (pos_line && sscanf(pos_line, "POS %d %d", &pos_y, &pos_x) == 2) {
                        board->ghosts[i].pos_x = pos_x;
                        board->ghosts[i].pos_y = pos_y;
                        int idx = pos_y * board->width + pos_x;
                        board->board[idx].content = 'M';
                    }
                }
                close(fd);
            }
        }
    }

    return 0;
}

void unload_level(board_t * board) {
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
