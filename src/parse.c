#include "parse.h"
#include "board.h"
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#define READ_BUFFER_SIZE 256
#define LINE_BUFFER_SIZE 512
#define SMALL_BUFFER_SIZE 128
#define LARGE_BUFFER_SIZE 1024

static inline int parse_and_add_command(const char* line, command_t* moves, int move_count) {
    if (move_count >= MAX_MOVES) return move_count;
    
    char cmd;
    int turns;
    
    if (sscanf(line, "%c %d", &cmd, &turns) == 2 && cmd == 'T') {
        moves[move_count].command = cmd;
        moves[move_count].turns = turns;
        moves[move_count].turns_left = turns;
        return move_count + 1;
    }

    else if (sscanf(line, "%c", &cmd) == 1 && cmd != 'T') {
        moves[move_count].command = cmd;
        moves[move_count].turns = 1;
        moves[move_count].turns_left = 1;
        return move_count + 1;
    }
    
    return move_count;
}

static inline void process_board_char(board_pos_t* pos, char ch) {
    if (ch == 'X') {
        pos->content = 'W';
        pos->has_dot = 0;
    } else if (ch == 'o') {
        pos->content = ' ';
        pos->has_dot = 1;
    } else if (ch == '@') {
        pos->content = ' ';
        pos->has_portal = 1;
        pos->has_dot = 0;
    } else {
        pos->content = ' ';
        pos->has_dot = 0;
    }
}

static inline void process_board_line(board_t* board, const char* line, int board_line, int width) {
    int row_offset = board_line * width;
    for (int x = 0; x < width && line[x] != '\0'; x++) {
        int idx = row_offset + x;
        process_board_char(&board->board[idx], line[x]);
    }
}

static inline int process_command_line(const char* line, command_t* moves, int move_count, int has_passo, int has_pos) {
    if (line[0] != '#' && line[0] != '\0' && has_passo && has_pos) {
        return parse_and_add_command(line, moves, move_count);
    }
    return move_count;
}

int parse_pacman_ghost_file(const char* filename, command_t* moves, int* n_moves, int* passo) {
    int fd = open(filename, O_RDONLY); 
    if (fd == -1) {
        return -1;
    }

    char buffer[READ_BUFFER_SIZE];
    char line_buffer[LINE_BUFFER_SIZE];
    int line_pos = 0;
    ssize_t bytes_read;
    int move_count = 0;
    int has_passo = 0, has_pos = 0;

    while ((bytes_read = read(fd, buffer, READ_BUFFER_SIZE)) > 0) {
        for (ssize_t i = 0; i < bytes_read; i++) {
            char c = buffer[i];
            
            if (c == '\n' || line_pos >= (int)sizeof(line_buffer) - 1) {
                line_buffer[line_pos] = '\0';
                if (line_buffer[0] != '#' && line_buffer[0] != '\0') {
                    if (strncmp(line_buffer, "PASSO", 5) == 0 && !has_passo) {
                        if (sscanf(line_buffer, "PASSO %d", passo) != 1) {
                            *passo = 0;
                        }
                        has_passo = 1;
                    } else if (strncmp(line_buffer, "POS", 3) == 0 && !has_pos) {
                        has_pos = 1;
                    } else {
                        move_count = process_command_line(line_buffer, moves, move_count, has_passo, has_pos);
                    }
                }
                
                line_pos = 0;
            } else {
                line_buffer[line_pos++] = c;
            }
        }
    }
    
    if (line_pos > 0) {
        line_buffer[line_pos] = '\0';
        move_count = process_command_line(line_buffer, moves, move_count, has_passo, has_pos);
    }
    
    close(fd);
    *n_moves = move_count;
    return 0;
}

int parse_level_file(board_t* board, const char* level_dir) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", level_dir, board->level_name);

    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        return -1;
    }

    board->pacman_file[0] = '\0';

    char buffer[READ_BUFFER_SIZE];
    char line_buffer[LINE_BUFFER_SIZE];
    int line_pos = 0;
    ssize_t bytes_read;
    int board_line = 0;
    int has_dim = 0, has_tempo = 0, has_mon = 0;
    int width_cache = 0;

    while ((bytes_read = read(fd, buffer, READ_BUFFER_SIZE)) > 0) {
        for (ssize_t i = 0; i < bytes_read; i++) {
            char c = buffer[i];
            
            if (c == '\n' || line_pos >= (int)sizeof(line_buffer) - 1) {
                line_buffer[line_pos] = '\0';
                
                if (line_buffer[0] != '#' && line_buffer[0] != '\0') {
                    if (strncmp(line_buffer, "DIM ", 4) == 0 && !has_dim) {
                        if (sscanf(line_buffer, "DIM %d %d", &board->height, &board->width) != 2) {
                            close(fd);
                            return -1;
                        }
                        width_cache = board->width;
                        board->board = calloc(board->width * board->height, sizeof(board_pos_t));
                        for (int i = 0; i < board->width * board->height; i++) {
                            pthread_mutex_init(&board->board[i].pos_mutex, NULL);
                        }
                        has_dim = 1;
                    } else if (strncmp(line_buffer, "TEMPO ", 6) == 0 && !has_tempo) {
                        sscanf(line_buffer, "TEMPO %d", &board->tempo);
                        has_tempo = 1;
                    } else if (strncmp(line_buffer, "PAC", 3) == 0) {
                        char pac_file[SMALL_BUFFER_SIZE];
                        if (sscanf(line_buffer, "PAC %127s", pac_file) == 1) {
                            snprintf(board->pacman_file, sizeof(board->pacman_file), "%s/%s", level_dir, pac_file);
                        } else {
                            board->pacman_file[0] = '\0';
                        }
                    } else if (strncmp(line_buffer, "MON", 3) == 0 && !has_mon) {
                        char* mon_start = strchr(line_buffer, ' ');
                        if (mon_start) {
                            ++mon_start;
                            board->n_ghosts = 0;
                            char mon_file[SMALL_BUFFER_SIZE];
                            char line_copy[LINE_BUFFER_SIZE];
                            size_t copy_len = LINE_BUFFER_SIZE - 1;
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
                    } else if (has_dim && has_tempo && has_mon) {
                        if (board_line < board->height) {
                            process_board_line(board, line_buffer, board_line, width_cache);
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
    
    if (line_pos > 0) {
        line_buffer[line_pos] = '\0';
        if (line_buffer[0] != '#' && line_buffer[0] != '\0' && has_dim && has_tempo && has_mon) {
            if (board_line < board->height) {
                process_board_line(board, line_buffer, board_line, width_cache);
            }
        }
    }

    close(fd);
    return 0;
}
    