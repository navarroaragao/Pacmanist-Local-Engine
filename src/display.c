#include "display.h"
#include "board.h"
#include <stdlib.h>
#include <ctype.h>

#define COLOR_PACMAN 1
#define COLOR_GHOST 2
#define COLOR_WALL 3
#define COLOR_DOT 4
#define COLOR_UI 5
#define COLOR_PORTAL 6

#define UI_START_ROW 3

static inline void draw_with_color(char ch, int color_pair, int attributes) {
    attron(color_pair | attributes);
    addch(ch);
    attroff(color_pair | attributes);
}

static inline int is_ghost_charged_at(board_t* board, int x, int y) {
    for (int g = 0; g < board->n_ghosts; g++) {
        ghost_t* ghost = &board->ghosts[g];
        if (ghost->pos_x == x && ghost->pos_y == y && ghost->charged) {
            return 1;
        }
    }
    return 0;
}


int terminal_init() {

    initscr();

    cbreak();

    noecho();

    keypad(stdscr, TRUE);

    nodelay(stdscr, TRUE);

    curs_set(0);

    if (has_colors()) {
        start_color();

        init_pair(1, COLOR_YELLOW, COLOR_BLACK);  
        init_pair(2, COLOR_RED, COLOR_BLACK);     
        init_pair(3, COLOR_BLUE, COLOR_BLACK);    
        init_pair(4, COLOR_WHITE, COLOR_BLACK);   
        init_pair(5, COLOR_GREEN, COLOR_BLACK);   
        init_pair(6, COLOR_MAGENTA, COLOR_BLACK); 
        init_pair(7, COLOR_CYAN, COLOR_BLACK);    
    }

    clear();

    return 0;
}


void draw_board(board_t* board, int mode) {
    clear();

    attron(COLOR_PAIR(COLOR_UI));
    mvprintw(0, 0, "=== PACMAN GAME ===");
    switch(mode) {
    case DRAW_GAME_OVER:
        mvprintw(1, 0, " GAME OVER ");
        break;

    case DRAW_WIN:
        mvprintw(1, 0, " VICTORY ");
        break;

    case DRAW_MENU:
        mvprintw(1, 0, "Level: %s | Use W/A/S/D to move | Q to quit | G to quicksave ", board->level_name);
        break;
    }

    int start_row = UI_START_ROW;

    for (int y = 0; y < board->height; y++) {
        for (int x = 0; x < board->width; x++) {
            int index = y * board->width + x;
            char ch = board->board[index].content;

            move(start_row + y, x);

            switch (ch) {
                case 'W': 
                    draw_with_color('#', COLOR_PAIR(COLOR_WALL), 0);
                    break;

                case 'P': 
                    draw_with_color('C', COLOR_PAIR(COLOR_PACMAN), A_BOLD);
                    break;

                case 'M': 
                    draw_with_color('M', COLOR_PAIR(COLOR_GHOST) | A_BOLD, 
                                   is_ghost_charged_at(board, x, y) ? A_DIM : 0);
                    break;

                case ' ': 
                    if (board->board[index].has_portal) {
                        draw_with_color('@', COLOR_PAIR(COLOR_PORTAL), 0);
                    }
                    else if (board->board[index].has_dot) {
                        draw_with_color('.', COLOR_PAIR(COLOR_DOT), 0);
                    }
                    else
                        addch(' ');
                    break;

                default:
                    addch(ch);
                    break;
            }
        }
    }

    attron(COLOR_PAIR(COLOR_UI));
    mvprintw(start_row + board->height + 1, 0, "Points: %d",
             board->pacmans[0].points);
    attroff(COLOR_PAIR(COLOR_UI));
}

void draw(char c, int colour_i, int pos_x, int pos_y) {
    move(pos_y, pos_x);
    attron(COLOR_PAIR(colour_i) | A_BOLD);
    addch(c);
    attroff(COLOR_PAIR(colour_i) | A_BOLD);
}

void refresh_screen() {
    refresh();
}

char get_input() {
    int ch = getch();

    if (ch == ERR) {
        return '\0'; 
    }

    ch = toupper((char)ch);

    switch ((char)ch) {
        case 'W':
        case 'S':
        case 'A':
        case 'D':
        case 'Q':
        case 'G':

            return (char)ch;
        
        default:
            return '\0';
    }
}

void terminal_cleanup() {
    
    endwin();
}
