#include <SDL2/SDL_stdinc.h>
#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define BLOCK '█'
#define COLOR_INX_OFFSET 7
#define BYTES_PER_CHAR 10

/*** data ***/

// struct to save the terminal configuration
struct term_config {
    int rows;
    int cols;
    struct termios origin;
};

struct term_config term;
char *screen; // each character has 10 bytes for escape sequence and character
              // at the end there are 3 bytes to reset everything
int screen_bitc;
char selected_color = '0';
int x_cursor = 1;
int y_cursor = 1;

int selected_row = -1;
int selected_col = -1;

char *error_msg = NULL;

/*** terminal ***/

void disable_raw_mode() {
    free(screen);

    // clear screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    // set original terminal configuration
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &term.origin) == -1) {
        exit(2);
    }

    // print error if there is one
    if (error_msg != NULL) {
        printf("%s", error_msg);
    }
}

void die(const char* s) {
    // create error message
    error_msg = malloc(sizeof(char) * 32);
    sprintf(error_msg, "ERROR: %s", s);

    // exit program
    exit(1);
}

void enable_raw_mode() {
    // save current terminal configuration
    if (tcgetattr(STDIN_FILENO, &term.origin) == -1) {
        die("tcgetattr");
    }
    atexit(disable_raw_mode);

    // enter raw mode
    struct termios raw = term.origin;
    raw.c_iflag &= ~(ICRNL | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_lflag &= ~(ECHO | ICANON);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
}

int get_cursor_pos(int *row, int *col) {
    // ask terminal for cursor position
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -1;
    }

    char buf[16];
    for (int i = 0; i < sizeof(buf); i++) {
        // read each char of the escape sequence
        if (read(STDIN_FILENO, &buf[i], 1) != 1) {
            buf[i] = '\0';
            break;
        }
        // stop if R is reached (end of returned escape sequence)
        if (buf[i] == 'R') {
            buf[i] = '\0';
            break;
        }
    }
    
    // check if an escape sequence was read
    if (buf[0] != '\x1b' || buf[1] != '[') {
        return -1;
    }

    // extract values from buf
    if (sscanf(&buf[2], "%d;%d", row, col) == -1) {
        return -1;
    }

    return 0;
}

int init_screen() {
    for (int i = 0; i < screen_bitc; i+=10) {
        screen[i + 0] = '\x1b';
        screen[i + 1] = '[';
        screen[i + 2] = '4';
        screen[i + 3] = '8';
        screen[i + 4] = ';';
        screen[i + 5] = '5';
        screen[i + 6] = ';';
        screen[i + 7] = '0';
        screen[i + 8] = 'm';
        screen[i + 9] = ' ';
    }
    return 0;
}

int init_terminal_state() {
    struct winsize ws;
    int result = 0;

    // get terminal size with ioctl
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // fallback to escape sequences for querying terminal size
        // move cursor down and then to the end
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
            return -1;
        }
        // get cursor position with escape sequences
        result = get_cursor_pos(&term.rows, &term.cols);
    }
    else {
        // set term values with ioctl return values
        term.cols = ws.ws_col;
        term.rows = ws.ws_row;
    }

    // init screen
    screen_bitc = (term.cols * term.rows) * BYTES_PER_CHAR;
    screen = malloc(screen_bitc);
    init_screen();

    return result;
}

void print_screen() {
    int row;
    int col;
    // get current cursor position
    if (get_cursor_pos(&row, &col) == -1) {
        die("get_cursor_pos");
        return;
    }

    // move cursor to beginning
    write(STDOUT_FILENO, "\x1b[H", 3);
    // print screen to terminal
    write(STDOUT_FILENO, screen, screen_bitc);
    
    // reset cursor position
    char *buf = malloc(10);
    int len = sprintf(buf, "\x1b[%d;%dH", row, col);
    write(STDOUT_FILENO, buf, len);
}

int get_inx(int row, int col) {
    return (col - 1 + term.cols * (row - 1)) * BYTES_PER_CHAR + 7;
}

void fill_screen(int color) {
    for (int i = COLOR_INX_OFFSET; i < screen_bitc; i+= 10) {
        screen[i] = color;
    }
}

void fill_pixel(int row, int col, int color) {
    screen[get_inx(row, col)] = color;
    screen[get_inx(row, col) + BYTES_PER_CHAR] = color;
    print_screen();
}

void fill_selection(int from_r, int from_c, int to_r, int to_c, int color) {
    int start_col = MIN(from_c - 1, to_c - 1) * BYTES_PER_CHAR;
    int end_col = (MAX(from_c - 1, to_c - 1) + 1) * BYTES_PER_CHAR;
    int start_row = MIN(from_r - 1, to_r - 1) * term.cols * BYTES_PER_CHAR;
    int end_row = MAX(from_r - 1, to_r - 1) * term.cols * BYTES_PER_CHAR;

    for (int row = start_row; row <= end_row; row+=term.cols*BYTES_PER_CHAR) {
        for (int col = start_col; col <= end_col; col+=BYTES_PER_CHAR) {
            screen[row + col + 7] = color;
        }
    }
    print_screen();
}

void clear_screen() {
    // clear screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    // move cursor to beginning
    write(STDOUT_FILENO, "\x1b[H", 3);
}


char poll_input() {
    int nread;
    char c;
    if ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("read");
        }
    }
    return c;
}

int handle_input(char c) {
    int row;
    int col;
    // get current cursor position
    if (get_cursor_pos(&row, &col) == -1) {
        die("get_cursor_pos");
        return -1;
    }

    switch (c) {
        case 'q':
            return 1;
        case 'h': // left
            write(STDIN_FILENO, "\x1b[2D", 4);
            break;
        case 'j': // down
            write(STDIN_FILENO, "\x1b[B", 3);
            break;
        case 'k': // up
            write(STDIN_FILENO, "\x1b[A", 3);
            break;
        case 'l': // right
            if (col > term.cols - 3) { break; } // don't allow move 
                                                // to single last col
            write(STDIN_FILENO, "\x1b[2C", 4);
            break;
        case 'g': // top
            write(STDOUT_FILENO, "\x1b[H", 3);
            break;
        case 'f':
            if (selected_row != -1 && selected_col != -1) {
                fill_selection(selected_row, selected_col, 
                        row, col, selected_color);
                selected_row = -1;
                selected_col = -1;
                break;
            }
            fill_pixel(row, col, selected_color);
            break;
        case 'd':
            if (selected_row != -1 && selected_col != -1) {
                fill_selection(selected_row, selected_col, 
                        row, col, '0');
                selected_row = -1;
                selected_col = -1;
                break;
            }
            fill_pixel(row, col, '0');
            break;
        case 'v':
            if (selected_row != -1 && selected_col != -1) {
                selected_row = -1;
                selected_col = -1;
                break;
            }
            selected_row = row;
            selected_col = col;
            break;
        case '0':
            selected_color = '0';
            break;
        case '1':
            selected_color = '1';
            break;
        case '2':
            selected_color = '2';
            break;
        case '3':
            selected_color = '3';
            break;
        case '4':
            selected_color = '4';
            break;
        case '5':
            selected_color = '5';
            break;
        case '6':
            selected_color = '6';
            break;
        case '7':
            selected_color = '7';
            break;
        case '8':
            selected_color = '8';
            break;
        case '9':
            selected_color = '9';
            break;
        default:
            break;
    }
    return 0;
}

/*** main ***/

int main(int argc, char *argv[])
{
    if (argc > 2) {
        printf("Usage: pixelcli [filepath]");
        return 1;
    }
    if (argc == 2) {
        // TODO: load image
        return 0;
    }
    else {
        // TODO: ask for image dimensions to create
        // create image
    }

    enable_raw_mode();
    init_terminal_state();
    clear_screen();
    print_screen();

    int exit = 0;
    while (exit == 0) {
        char c = poll_input();
        if (c == -1) {
            die("poll_input");
        }
        exit = handle_input(c);
    }

    return 0;
}
