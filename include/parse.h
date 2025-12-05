#ifndef PARSE_H
#define PARSE_H

#include "board.h"

/*Parse behavior file (Pacman or Monster) - returns 0 on success*/
int parse_behavior_file(const char* filename, command_t* moves, int* n_moves, int* passo);

/*Parse level file and initialize board - returns 0 on success*/
int parse_level_file(board_t* board, const char* level_dir);

#endif
