#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

/*** data ***/

// struct to save the terminal configuration
struct term_config {
    int rows;
    int cols;
    struct termios origin;
};

struct term_config term;

/*** terminal ***/

void die(const char* s) {
    // clear screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    // print error message and exit
    perror(s);
    exit(1);
}

void disable_raw_mode() {
    // set original terminal configuration
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &term.origin) == -1) {
        die("tcsetattr");
    }
}

void enable_raw_mode() {
    // save current terminal configuration
    if (tcgetattr(STDIN_FILENO, &term.origin) == -1) {
        die("tcgetattr");
    }
    atexit(disable_raw_mode);

    // enter raw mode
    struct termios raw = term.origin;
    raw.c_oflag &= ~(OPOST);
    raw.c_lflag &= ~(ECHO | ICANON);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
}

void init_terminal_state() {
    // TODO: get rows and cols with ioctl
    // fallback will be with escape sequences
}

void clear_screen() {
    // clear screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    // move cursor to beginning
    write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** main ***/

int main(int argc, char *argv[])
{
    enable_raw_mode();
    clear_screen();

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 &&  c != 'q') {
        if (iscntrl(c)) {
            printf("%d\r\n", c);
        }
        else {
            printf("%d: %c\r\n", c, c);
        }
    }

    return 0;
}
