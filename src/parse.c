#include "parse.h"
#include "board.h"
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

// Parse behavior file for Pacman or Monster
int parse_pacman_ghost_file(const char* filename, command_t* moves, int* n_moves, int* passo) {
    int fd = open(filename, O_RDONLY); // Open the behavior file
    if (fd == -1) {
        debug("Failed to open behavior file: %s\n", filename);
        return -1;
    }

    char buffer[256];
    char line_buffer[512]; // For incomplete lines
    int line_pos = 0;
    ssize_t bytes_read;
    int move_count = 0;
    int has_passo = 0, has_pos = 0;

    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < bytes_read; i++) {
            char c = buffer[i];
            
            if (c == '\n' || line_pos >= (int)sizeof(line_buffer) - 1) {
                line_buffer[line_pos] = '\0';
                // Skip comments and empty lines
                if (line_buffer[0] != '#' && line_buffer[0] != '\0') {
                    // Parse based on keyword
                    if (strncmp(line_buffer, "PASSO", 5) == 0 && !has_passo) {
                        if (sscanf(line_buffer, "PASSO %d", passo) != 1) {
                            *passo = 0;
                        }
                        debug("Read PASSO: %d from file: %s\n", *passo, filename);
                        has_passo = 1;
                    } else if (strncmp(line_buffer, "POS", 3) == 0 && !has_pos) {
                        debug("Found POS line in file: %s\n", filename);
                        has_pos = 1;
                    } else if (has_passo && has_pos) {
                        // Movement commands
                        char cmd;
                        int turns;
                        if (sscanf(line_buffer, "%c %d", &cmd, &turns) == 2) {
                            if (move_count < MAX_MOVES) {
                                moves[move_count].command = cmd;
                                moves[move_count].turns = turns;
                                moves[move_count].turns_left = turns;
                                move_count++;
                            }
                        } else if (sscanf(line_buffer, "%c", &cmd) == 1) {
                            if (move_count < MAX_MOVES) {
                                moves[move_count].command = cmd;
                                moves[move_count].turns = 1;
                                moves[move_count].turns_left = 1;
                                move_count++;
                            }
                        }
                    }
                }
                
                line_pos = 0;
            } else {
                line_buffer[line_pos++] = c;
            }
        }
    }
    
    // Process last line if EOF reached with pending data
    if (line_pos > 0) {
        line_buffer[line_pos] = '\0';
        if (line_buffer[0] != '#' && line_buffer[0] != '\0' && has_passo && has_pos) {
            char cmd;
            int turns;
            if (sscanf(line_buffer, "%c %d", &cmd, &turns) == 2) {
                if (move_count < MAX_MOVES) {
                    moves[move_count].command = cmd;
                    moves[move_count].turns = turns;
                    moves[move_count].turns_left = turns;
                    move_count++;
                }
            } else if (sscanf(line_buffer, "%c", &cmd) == 1) {
                if (move_count < MAX_MOVES) {
                    moves[move_count].command = cmd;
                    moves[move_count].turns = 1;
                    moves[move_count].turns_left = 1;
                    move_count++;
                }
            }
        }
    }
    
    close(fd);
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

    char buffer[256];
    char line_buffer[512];
    int line_pos = 0;
    ssize_t bytes_read;
    int board_line = 0;
    int has_dim = 0, has_tempo = 0, has_pac = 0, has_mon = 0;
    int width_cache = 0;

    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < bytes_read; i++) {
            char c = buffer[i];
            
            if (c == '\n' || line_pos >= (int)sizeof(line_buffer) - 1) {
                line_buffer[line_pos] = '\0';
                
                // Skip comments and empty lines
                if (line_buffer[0] != '#' && line_buffer[0] != '\0') {
                    // Parse based on keyword
                    if (strncmp(line_buffer, "DIM ", 4) == 0 && !has_dim) {
                        if (sscanf(line_buffer, "DIM %d %d", &board->height, &board->width) != 2) {
                            close(fd);
                            return -1;
                        }
                        width_cache = board->width;
                        board->board = calloc(board->width * board->height, sizeof(board_pos_t));
                        // Initialize mutex for each position
                        for (int i = 0; i < board->width * board->height; i++) {
                            pthread_mutex_init(&board->board[i].pos_mutex, NULL);
                        }
                        has_dim = 1;
                    } else if (strncmp(line_buffer, "TEMPO ", 6) == 0 && !has_tempo) {
                        sscanf(line_buffer, "TEMPO %d", &board->tempo);
                        has_tempo = 1;
                    } else if (strncmp(line_buffer, "PAC", 3) == 0 && !has_pac) {
                        char pac_file[128];
                        if (sscanf(line_buffer, "PAC %127s", pac_file) == 1) {
                            snprintf(board->pacman_file, sizeof(board->pacman_file), "%s/%s", level_dir, pac_file);
                        } else {
                            board->pacman_file[0] = '\0';
                        }
                        has_pac = 1;
                    } else if (strncmp(line_buffer, "MON", 3) == 0 && !has_mon) {
                        char* mon_start = strchr(line_buffer, ' ');
                        if (mon_start) {
                            ++mon_start;
                            board->n_ghosts = 0;
                            char mon_file[128];
                            char line_copy[512];
                            size_t copy_len = sizeof(line_copy) - 1;
                            strncpy(line_copy, mon_start, copy_len);
                            line_copy[copy_len] = '\0';
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
                        // Board content
                        if (board_line < board->height) {
                            int row_offset = board_line * width_cache;
                            for (int x = 0; x < width_cache && line_buffer[x] != '\0'; x++) {
                                int idx = row_offset + x;
                                char ch = line_buffer[x];
                                
                                if (ch == 'X') {
                                    board->board[idx].content = 'W';
                                    board->board[idx].has_dot = 0;
                                } else if (ch == 'o') {
                                    board->board[idx].content = ' ';
                                    board->board[idx].has_dot = 1;
                                } else if (ch == '@') {
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
                }
                
                line_pos = 0;
            } else {
                line_buffer[line_pos++] = c;
            }
        }
    }

    // Process last line if EOF reached with pending data
    if (line_pos > 0) {
        line_buffer[line_pos] = '\0';
        if (line_buffer[0] != '#' && line_buffer[0] != '\0' && has_dim && has_tempo && has_pac && has_mon) {
            if (board_line < board->height) {
                int row_offset = board_line * width_cache;
                for (int x = 0; x < width_cache && line_buffer[x] != '\0'; x++) {
                    int idx = row_offset + x;
                    char ch = line_buffer[x];
                    
                    if (ch == 'X') {
                        board->board[idx].content = 'W';
                        board->board[idx].has_dot = 0;
                    } else if (ch == 'o') {
                        board->board[idx].content = ' ';
                        board->board[idx].has_dot = 1;
                    } else if (ch == '@') {
                        board->board[idx].content = ' ';
                        board->board[idx].has_portal = 1;
                        board->board[idx].has_dot = 0;
                    } else {
                        board->board[idx].content = ' ';
                        board->board[idx].has_dot = 0;
                    }
                }
            }
        }
    }

    close(fd);
    return 0;
}
