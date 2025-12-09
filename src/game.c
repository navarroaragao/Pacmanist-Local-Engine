#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define SAVE_STATE 3
#define FORCE_QUIT 4

// Thread argument structure
typedef struct {
    board_t* board;
    int character_index;
} thread_arg_t;

// Helper function to set game result and stop game
static inline void set_game_result(board_t* board, int result) {
    board->game_result = result;
    board->level_complete = true;
    board->game_running = false;
}

// Helper function for adaptive sleep based on tempo
static inline void adaptive_sleep(board_t* board) {
    sleep_ms(board->tempo > 0 ? board->tempo : 50);
}

// Pacman thread function
void* pacman_thread(void* arg) {
    thread_arg_t* targ = (thread_arg_t*)arg;
    board_t* board = targ->board;
    int pac_index = targ->character_index;
    free(targ);
    
    pacman_t* pacman = &board->pacmans[pac_index];
    
    while (board->game_running) {
        if (!pacman->alive || board->level_complete) {
            break;
        }
        
        // File-controlled movement only
        command_t* play = &pacman->moves[pacman->current_move % pacman->n_moves];
        
        pthread_mutex_lock(&board->display_mutex);
        int result = move_pacman(board, pac_index, play);
        pthread_mutex_unlock(&board->display_mutex);
        
        if (result == REACHED_PORTAL) {
            set_game_result(board, NEXT_LEVEL);
        } else if (result == DEAD_PACMAN) {
            set_game_result(board, QUIT_GAME);
        } else if (result == QUICKSAVE) {
            set_game_result(board, SAVE_STATE);
        }
        
        adaptive_sleep(board);
    }
    
    return NULL;
}

// Ghost thread function
void* ghost_thread(void* arg) {
    thread_arg_t* targ = (thread_arg_t*)arg;
    board_t* board = targ->board;
    int ghost_index = targ->character_index;
    free(targ);
    
    ghost_t* ghost = &board->ghosts[ghost_index];
    
    while (board->game_running) {
        if (board->level_complete) {
            break;
        }
        
        command_t* cmd = &ghost->moves[ghost->current_move % ghost->n_moves];
        
        pthread_mutex_lock(&board->display_mutex);
        move_ghost(board, ghost_index, cmd);
        pthread_mutex_unlock(&board->display_mutex);
        
        // Check if pacman died
        if (!board->pacmans[0].alive) {
            set_game_result(board, QUIT_GAME);
        }
        
        adaptive_sleep(board);
    }
    
    return NULL;
}

// Display thread function
void* display_thread(void* arg) {
    board_t* board = (board_t*)arg;
    
    while (board->game_running) {
        pthread_mutex_lock(&board->display_mutex);
        
        draw_board(board, DRAW_MENU);
        refresh_screen();
        
        pthread_mutex_unlock(&board->display_mutex);
        
        // Use a consistent display refresh rate to reduce flicker
        sleep_ms(100); // Fixed 100ms refresh rate
    }
    
    return NULL;
}

// Input thread function (for user-controlled pacman and Q/G commands)
void* input_thread(void* arg) {
    board_t* board = (board_t*)arg;
    pacman_t* pacman = &board->pacmans[0];
    
    while (board->game_running) {
        char input = get_input();
        
        // Allow Q to quit in any mode
        if (input == 'Q') {
            set_game_result(board, FORCE_QUIT);
            break;
        }
        
        // Allow G (quicksave) in any mode
        if (input == 'G') {
            set_game_result(board, SAVE_STATE);
            break;
        }
        
        // Handle user-controlled pacman movement
        if (pacman->n_moves == 0 && input != '\0') {
            command_t user_cmd;
            user_cmd.command = input;
            user_cmd.turns = 1;
            user_cmd.turns_left = 1;
            
            pthread_mutex_lock(&board->display_mutex);
            int result = move_pacman(board, 0, &user_cmd);
            pthread_mutex_unlock(&board->display_mutex);
            
            if (result == REACHED_PORTAL) {
                set_game_result(board, NEXT_LEVEL);
            } else if (result == DEAD_PACMAN) {
                set_game_result(board, QUIT_GAME);
            } else if (result == QUICKSAVE) {
                set_game_result(board, SAVE_STATE);
            }
        }
        
        sleep_ms(50); // Poll input every 50ms
    }
    
    return NULL;
}

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    pthread_mutex_lock(&game_board->display_mutex);
    draw_board(game_board, mode);
    refresh_screen();
    pthread_mutex_unlock(&game_board->display_mutex);
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

int play_board(board_t * game_board) {
    // Create threads for each character
    pthread_t display_tid, input_tid, pacman_tid;
    pthread_t ghost_tids[MAX_GHOSTS];
    
    // Start display thread
    pthread_create(&display_tid, NULL, display_thread, game_board);
    
    // Start input thread
    pthread_create(&input_tid, NULL, input_thread, game_board);
    
    // Start pacman thread (only if file-controlled)
    if (game_board->pacmans[0].n_moves > 0) {
        thread_arg_t* arg = malloc(sizeof(thread_arg_t));
        arg->board = game_board;
        arg->character_index = 0;
        pthread_create(&pacman_tid, NULL, pacman_thread, arg);
    }
    
    // Start ghost threads
    for (int i = 0; i < game_board->n_ghosts; i++) {
        thread_arg_t* arg = malloc(sizeof(thread_arg_t));
        arg->board = game_board;
        arg->character_index = i;
        pthread_create(&ghost_tids[i], NULL, ghost_thread, arg);
    }
    
    // Wait for all threads to finish
    pthread_join(input_tid, NULL);
    pthread_join(display_tid, NULL);
    
    if (game_board->pacmans[0].n_moves > 0) {
        pthread_join(pacman_tid, NULL);
    }
    
    for (int i = 0; i < game_board->n_ghosts; i++) {
        pthread_join(ghost_tids[i], NULL);
    }
    
    return game_board->game_result;
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
        printf("No .lvl files found in %s, loading static level\n", level_dir);
        // Add a dummy entry to load static level
        strcpy(level_files[0], "static.lvl");
        level_count = 1;
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
        
        pthread_mutex_lock(&game_board.display_mutex);
        draw_board(&game_board, DRAW_MENU);
        refresh_screen();
        pthread_mutex_unlock(&game_board.display_mutex);

        bool is_child_process = false;

        while(true) {
            int result = play_board(&game_board); 

            if(result == NEXT_LEVEL) {
                screen_refresh(&game_board, DRAW_WIN);
                sleep_ms(game_board.tempo);
                current_level_index++; // Avançar para o próximo nível
                break;
            }

            if(result == SAVE_STATE) {
                if (!is_child_process) {
                    // Only fork if we're in the parent process
                    pid_t pid = fork();
                    
                    if (pid == 0) {
                        is_child_process = true;
                        // Child continues playing - reset game state
                        game_board.game_running = true;
                        game_board.level_complete = false;
                        game_board.game_result = CONTINUE_PLAY;
                        continue; // Continue the game loop

                    } else if (pid > 0) {
                        int status;
                        waitpid(pid, &status, 0);
                        
                        // Check if child exited with FORCE_QUIT signal (exit code 1)
                        if (WIFEXITED(status) && WEXITSTATUS(status) == 1) {
                            // Child was force quit with Q, so parent should also quit
                            end_game = true;
                            break;
                        }
                        
                        // Don't cleanup/reinit terminal - just redraw
                        pthread_mutex_lock(&game_board.display_mutex);
                        draw_board(&game_board, DRAW_MENU);
                        refresh_screen();
                        pthread_mutex_unlock(&game_board.display_mutex);
                        
                        // Parent resets and continues
                        game_board.game_running = true;
                        game_board.level_complete = false;
                        game_board.game_result = CONTINUE_PLAY;
                        continue;
                    }
                } else {
                    // Already in child process - ignore G command and continue playing
                    game_board.game_running = true;
                    game_board.level_complete = false;
                    game_board.game_result = CONTINUE_PLAY;
                    continue;
                }
            }

            if(result == QUIT_GAME || result == FORCE_QUIT) {
                screen_refresh(&game_board, DRAW_GAME_OVER);
                sleep_ms(game_board.tempo);
                
                if (is_child_process) {
                    unload_level(&game_board);
                    // Don't call terminal_cleanup() - it corrupts parent's terminal
                    close_debug_file();
                    // Exit with 1 for FORCE_QUIT (Q command), 0 for death (restore save)
                    exit(result == FORCE_QUIT ? 1 : 0);
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
