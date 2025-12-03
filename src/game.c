#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <dirent.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define SAVE_STATE 3

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

int play_board(board_t * game_board) {
    pacman_t* pacman = &game_board->pacmans[0];
    command_t* play;
    command_t user_cmd;
    
    // Always check for user input (G and Q commands)
    user_cmd.command = get_input();
    
    if (user_cmd.command == 'Q') {
        return QUIT_GAME;
    }
    
    if (user_cmd.command == 'G') {
        return SAVE_STATE;
    }
    
    if (pacman->n_moves == 0) { // if is user input controlled
        if(user_cmd.command == '\0')
            return CONTINUE_PLAY;

        user_cmd.turns = 1;
        play = &user_cmd;
    }
    else { // else if the moves are pre-defined in the file
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the pacman
        play = &pacman->moves[pacman->current_move%pacman->n_moves];
    }

    debug("KEY %c\n", play->command);

    if (play->command == 'Q') {
        return QUIT_GAME;
    }

    if (play->command == 'G') {
        return SAVE_STATE;
    }

    int result = move_pacman(game_board, 0, play);
    if (result == REACHED_PORTAL) {
        // Next level
        return NEXT_LEVEL;
    }

    if(result == DEAD_PACMAN) {
        return QUIT_GAME;
    }
    
    for (int i = 0; i < game_board->n_ghosts; i++) {
        ghost_t* ghost = &game_board->ghosts[i];
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the ghost
        move_ghost(game_board, i, &ghost->moves[ghost->current_move%ghost->n_moves]);
    }

    if (!game_board->pacmans[0].alive) {
        return QUIT_GAME;
    }      

    return CONTINUE_PLAY;  
}

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("Usage: %s <level_directory>\n", argv[0]);
        return 1;
    }

    char* level_dir = argv[1];

    // Read all .lvl files from directory
    DIR* dir = opendir(level_dir);
    if (!dir) {
        printf("Failed to open level directory: %s\n", level_dir);
        return 1;
    }

    // Count and store .lvl files
    char level_files[MAX_LEVELS][MAX_FILENAME];
    int level_count = 0;
    struct dirent* entry;
    
    while ((entry = readdir(dir)) != NULL && level_count < MAX_LEVELS) {
        char* ext = strrchr(entry->d_name, '.');
        if (ext && strcmp(ext, ".lvl") == 0) {
            strncpy(level_files[level_count], entry->d_name, MAX_FILENAME - 1);
            level_files[level_count][MAX_FILENAME - 1] = '\0';
            level_count++;
        }
    }
    closedir(dir);

    if (level_count == 0) {
        printf("No .lvl files found in %s\n", level_dir);
        return 1;
    }

    // Random seed for any random movements
    srand((unsigned int)time(NULL));

    open_debug_file("debug.log");

    terminal_init();
    
    int accumulated_points = 0;
    bool end_game = false;
    board_t game_board;
    int current_level_index = 0;

    while (!end_game && current_level_index < level_count) {
        // Set level filename from the list
        snprintf(game_board.level_name, sizeof(game_board.level_name), "%s", level_files[current_level_index]);
        
        if (load_level(&game_board, accumulated_points, level_dir) != 0) {
            debug("Failed to load level %s\n", level_files[current_level_index]);
            break;
        }
        draw_board(&game_board, DRAW_MENU);
        refresh_screen();

        bool is_child_process = false;

        while(true) {
            int result = play_board(&game_board); 

            if(result == NEXT_LEVEL) {
                screen_refresh(&game_board, DRAW_WIN);
                sleep_ms(game_board.tempo);
                current_level_index++; // Avançar para o próximo nível
                break;
            }

            if(result == SAVE_STATE && !is_child_process) {
                pid_t pid = fork();
                
                if (pid == 0) {
                    is_child_process = true; //continua o jogo

                } else if (pid > 0) {
                    int status;
                    waitpid(pid, &status, 0); //mete em wait enquanto o filho está nos works
                    
                    terminal_cleanup(); //limpa tabuleiro atual
                    terminal_init(); //inicia um novo
                    
                    draw_board(&game_board, DRAW_MENU); //refazer o tabuleiro
                    refresh_screen(); //mostrar cenas no ecrã
                }
            }

            if(result == QUIT_GAME) {
                screen_refresh(&game_board, DRAW_GAME_OVER);
                sleep_ms(game_board.tempo);
                
                if (is_child_process) {
                    unload_level(&game_board);
                    terminal_cleanup();
                    close_debug_file();
                    exit(0);
                }
                
                end_game = true;
                break;
            }
    
            screen_refresh(&game_board, DRAW_MENU); 

            accumulated_points = game_board.pacmans[0].points;      
        }
        print_board(&game_board);
        unload_level(&game_board);
    }    

    terminal_cleanup();

    close_debug_file();

    return 0;
}
