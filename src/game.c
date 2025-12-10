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
    pthread_mutex_lock(&board->display_mutex);
    board->game_result = result;
    board->level_complete = true;
    board->game_running = false;
    pthread_mutex_unlock(&board->display_mutex);
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
    
    while (1) {
        pthread_mutex_lock(&board->display_mutex);
        int running = board->game_running;
        int alive = pacman->alive;
        int complete = board->level_complete;
        pthread_mutex_unlock(&board->display_mutex);
        
        if (!running || !alive || complete) {
            break;
        }
        
        // File-controlled movement only
        command_t* play = &pacman->moves[pacman->current_move % pacman->n_moves];
        
        int result = move_pacman(board, pac_index, play);
        
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
    
    while (1) {
        pthread_mutex_lock(&board->display_mutex);
        int running = board->game_running;
        int complete = board->level_complete;
        int alive = board->pacmans[0].alive;
        pthread_mutex_unlock(&board->display_mutex);
        
        if (!running || complete) {
            break;
        }
        
        command_t* cmd = &ghost->moves[ghost->current_move % ghost->n_moves];
        move_ghost(board, ghost_index, cmd);
        
        // Check if pacman died (already loaded above)
        if (!alive) {
            set_game_result(board, QUIT_GAME);
        }
        
        adaptive_sleep(board);
    }
    
    return NULL;
}

// Display thread function
void* display_thread(void* arg) {
    board_t* board = (board_t*)arg;
    
    while (1) {
        pthread_mutex_lock(&board->display_mutex);
        int running = board->game_running;
        if (!running) {
            pthread_mutex_unlock(&board->display_mutex);
            break;
        }
        
        draw_board(board, DRAW_MENU);
        refresh_screen();
        
        pthread_mutex_unlock(&board->display_mutex);
        
        // Use a consistent display refresh rate to reduce flicker
        sleep_ms(50); // 50ms refresh rate for more fluid animation
    }
    
    return NULL;
}

// Input thread function (for user-controlled pacman and Q/G commands)
void* input_thread(void* arg) {
    board_t* board = (board_t*)arg;
    pacman_t* pacman = &board->pacmans[0];
    
    while (1) {
        pthread_mutex_lock(&board->display_mutex);
        int running = board->game_running;
        pthread_mutex_unlock(&board->display_mutex);
        
        if (!running) {
            break;
        }
        char input = get_input();
        
        // Q e G só funcionam quando NÃO estamos a ler de ficheiro
        if (pacman->n_moves == 0) {
            // Allow Q to quit in user input mode
            if (input == 'Q') {
                set_game_result(board, FORCE_QUIT);
                break;
            }
            
            // Allow G (quicksave) in user input mode
            if (input == 'G') {
                set_game_result(board, SAVE_STATE);
                break;
            }
        }
        
        // Handle user-controlled pacman movement
        if (pacman->n_moves == 0 && input != '\0') {
            command_t user_cmd;
            user_cmd.command = input;
            user_cmd.turns = 1;
            user_cmd.turns_left = 1;
            
            int result = move_pacman(board, 0, &user_cmd);
            
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
    
    // Track if pacman thread was created
    bool pacman_thread_created = false;
    
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
        pacman_thread_created = true;
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
    
    if (pacman_thread_created) {
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
    bool has_saved_state = false;  // Track if a save state exists

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

        while(true) {
            int result = play_board(&game_board); 

            if(result == NEXT_LEVEL) {
                screen_refresh(&game_board, DRAW_WIN);
                current_level_index++;
                break;
            }

            if(result == SAVE_STATE) {
                // Only save if no save state exists yet
                if (!has_saved_state) {
                    pthread_mutex_lock(&game_board.display_mutex);
                    
                    pid_t pid = fork();
                    
                    if (pid == 0) {
                        // Child process continues playing
                        has_saved_state = true;  // Mark that we're in child (save exists in parent)
                        
                        pthread_mutex_destroy(&game_board.display_mutex);
                        pthread_mutex_init(&game_board.display_mutex, NULL);
                        
                        game_board.game_running = true;
                        game_board.level_complete = false;
                        game_board.game_result = CONTINUE_PLAY;
                        continue;

                    } else if (pid > 0) {
                        // Parent waits and holds the saved state
                        pthread_mutex_unlock(&game_board.display_mutex);
                        
                        int status;
                        waitpid(pid, &status, 0);
                        
                        // Child exited, check why
                        if (WIFEXITED(status)) {
                            int exit_code = WEXITSTATUS(status);
                            
                            if (exit_code == 1) {
                                // Child pressed Q, parent exits too
                                end_game = true;
                                break;
                            } else if (exit_code == 2) {
                                // Child completed all levels
                                end_game = true;
                                break;
                            }
                            // exit_code == 0 means death, restore from save
                        }
                        
                        // Restore the saved state (parent continues from save point)
                        pthread_mutex_lock(&game_board.display_mutex);
                        draw_board(&game_board, DRAW_MENU);
                        refresh_screen();
                        
                        game_board.game_running = true;
                        game_board.level_complete = false;
                        game_board.game_result = CONTINUE_PLAY;
                        pthread_mutex_unlock(&game_board.display_mutex);
                        continue;
                    }
                } else {
                    // Already has a saved state (we're in child), ignore G command
                    pthread_mutex_lock(&game_board.display_mutex);
                    game_board.game_running = true;
                    game_board.level_complete = false;
                    game_board.game_result = CONTINUE_PLAY;
                    pthread_mutex_unlock(&game_board.display_mutex);
                    continue;
                }
            }

            if(result == QUIT_GAME || result == FORCE_QUIT) {
                screen_refresh(&game_board, DRAW_GAME_OVER);
                
                // If we have a saved state, we're in the child process
                if (has_saved_state) {
                    unload_level(&game_board);
                    close_debug_file();
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

    // If we have a saved state (child process) and completed all levels, exit with code 2
    if (has_saved_state && !end_game) {
        terminal_cleanup();
        close_debug_file();
        exit(2);
    }

    terminal_cleanup();

    close_debug_file();

    return 0;
}
