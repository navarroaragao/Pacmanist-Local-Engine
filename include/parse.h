#ifndef PARSE_H
#define PARSE_H

#include "board.h"

int parse_pacman_ghost_file(const char* filename, command_t* moves, int* n_moves, int* passo);

int parse_level_file(board_t* board, const char* level_dir);

#endif
