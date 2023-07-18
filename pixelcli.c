#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <setjmp.h>
#include <png.h>
#include <pngconf.h>
#include <string.h>

/*** defines ***/

#define BLOCK '█'
#define COLOR_INX_OFFSET 7
#define RED_OFFSET 7
#define GREEN_OFFSET 11
#define BLUE_OFFSET 15
#define BYTES_PER_CHAR 20
#define ERROR -1
#define SUCCESS 0
#define SIZEOF_POINTER 8
#define IMAGE_DEPTH 4
#define ASCII_NUMBERS_START 48
#define CONFIG_PATH_AMOUNT 4
#define COMMANDC 30

/*** data ***/

// struct to save the terminal configuration
struct term_config {
  int rows;
  int cols;
  struct termios origin;
};

struct term_config term;

int x_cursor = 1;
int y_cursor = 1;

char *image;
unsigned int image_bytec;
unsigned int image_width;
unsigned int image_height;
int x_offset = 0;
int y_offset = 0;

int selected_row = -1;
int selected_col = -1;

int r_sel = 0;
int g_sel = 0;
int b_sel = 0;

int color_palette[10][3] = {
  {0x00, 0x00, 0x00}, // BLACK
  {0x69, 0x69, 0x69}, // GRAY
  {0xFF, 0xFF, 0xFF}, // WHITE 
  {0xF0, 0x2F, 0x5F}, // RED
  {0xFF, 0x7F, 0x00}, // ORANGE
  {0xF9, 0xC2, 0x2E}, // YELLOW
  {0x04, 0xE7, 0x37}, // GREEN
  {0x00, 0xA1, 0xE4}, // BLUE
  {0x94, 0x00, 0xD3}, // VIOLET
  {0xFC, 0x46, 0xAA}, // PINK
};

int transparency_color[3] = {0x00, 0x0A, 0x12};

char *commands[COMMANDC][2] = {
  {"quit", "q"},
  {"move_left", "h"},
  {"move_down", "j"},
  {"move_up", "k"},
  {"move_right", "l"},
  {"offset_left", "H"},
  {"offset_down", "J"},
  {"offset_up", "K"},
  {"offset_right", "L"},
  {"move_top", "g"},
  {"move_bottom", "G"},
  {"fill", "f"},
  {"delete", "d"},
  {"select", "v"},
  {"jump_forward", "w"},
  {"jump_backward", "b"},
  {"color_0", "0"},
  {"color_1", "1"},
  {"color_2", "2"},
  {"color_3", "3"},
  {"color_4", "4"},
  {"color_5", "5"},
  {"color_6", "6"},
  {"color_7", "7"},
  {"color_8", "8"},
  {"color_9", "9"},
  {"save", "s"},
  {"reload", "r"},
  {"pipette", "i"},
  {"pipette_save", "I"}
};

char *error_msg = NULL;

/*** terminal ***/

void disable_raw_mode() {
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

  // convert to 0-based index
  *row = *row - 1;
  *col = *col - 1;

  return 0;
}

/// initializes the image array
///
/// if rows are given those pixels will be loaded
/// (color_type is needed to load correctly!
///  if rows are given this function will panic and exit)
/// 
/// if no rows are given a blank image will be created
/// (color_type will be ignored)
int init_image(int w, int h, png_bytepp rows, int color_type) {
  // init global image vars
  // (2 * w because pixel has two chars in terminal)
  // + 1 to accommodate the \0 character
  image_bytec = (2 * w) * h * BYTES_PER_CHAR; 
  image = malloc(image_bytec + 1);
  image[2 * w * h * BYTES_PER_CHAR] = '\0';
  image_width = w * 2;
  image_height = h;

  char r0 = (int)(transparency_color[0] / 100) + ASCII_NUMBERS_START;
  char r1 = (int)((transparency_color[0] % 100) / 10) + ASCII_NUMBERS_START;
  char r2 = (int)(transparency_color[0] % 10) + ASCII_NUMBERS_START;
  char g0 = (int)(transparency_color[1] / 100) + ASCII_NUMBERS_START;
  char g1 = (int)((transparency_color[1] % 100) / 10) + ASCII_NUMBERS_START;
  char g2 = (int)(transparency_color[1] % 10) + ASCII_NUMBERS_START;
  char b0 = (int)(transparency_color[2] / 100) + ASCII_NUMBERS_START;
  char b1 = (int)((transparency_color[2] % 100) / 10) + ASCII_NUMBERS_START;
  char b2 = (int)(transparency_color[2] % 10) + ASCII_NUMBERS_START;

  if (rows && !color_type) {
    die("color_type is needed");
  }
  
  int create_alpha = 0;
  if (color_type == PNG_COLOR_TYPE_RGBA
    || color_type == PNG_COLOR_TYPE_GA
    || color_type == PNG_COLOR_TYPE_RGB_ALPHA
    || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
  {
    create_alpha = 1;
  }

  for (int i = 0; i < image_bytec; i+=BYTES_PER_CHAR) {
    if (rows) {
      // NOTE: I know it does it double unnecessarily
      //       but as it just increases the loading time and
      //       not the responsiveness whilst editing
      //       I'm just gonna ignore that for now and might 
      //       fix it in the future

      // calculate image coordinates in png struct
      int row = (int)((i / BYTES_PER_CHAR) / (2 * w));
      int col = (int)(((i / BYTES_PER_CHAR) / 2) % w) * IMAGE_DEPTH;

      // extract rgb values
      int red = rows[row][col];
      int green = rows[row][col + 1];
      int blue = rows[row][col + 2];

      // replace pixel color if it should 
      // be totally transparent
      if (create_alpha == 1 && rows[row][col + 3] == 0) {
        red = transparency_color[0];
        green = transparency_color[1];
        blue = transparency_color[2];
      }

      // put the correct chars into the variables
      r0 = (int)(red / 100) + ASCII_NUMBERS_START;
      r1 = (int)((red % 100) / 10) + ASCII_NUMBERS_START;
      r2 = (int)(red % 10) + ASCII_NUMBERS_START;
      g0 = (int)(green / 100) + ASCII_NUMBERS_START;
      g1 = (int)((green % 100) / 10) + ASCII_NUMBERS_START;
      g2 = (int)(green % 10) + ASCII_NUMBERS_START;
      b0 = (int)(blue / 100) + ASCII_NUMBERS_START;
      b1 = (int)((blue % 100) / 10) + ASCII_NUMBERS_START;
      b2 = (int)(blue % 10) + ASCII_NUMBERS_START;
    }

    image[i + 0] = '\x1b';
    image[i + 1] = '[';
    image[i + 2] = '4';
    image[i + 3] = '8';
    image[i + 4] = ';';
    image[i + 5] = '2';
    image[i + 6] = ';';
    image[i + 7] = r0;
    image[i + 8] = r1;
    image[i + 9] = r2;
    image[i + 10] = ';';
    image[i + 11] = g0;
    image[i + 12] = g1;
    image[i + 13] = g2;
    image[i + 14] = ';';
    image[i + 15] = b0;
    image[i + 16] = b1;
    image[i + 17] = b2;
    image[i + 18] = 'm';
    image[i + 19] = ' ';
  }
  if (rows) {
    for (int i = 0; i < h; i++) {
      png_bytep row = rows[i];
      free(row);
    }
    free(rows);
  }
  return 0;
}

int set_terminal_size() {
  struct winsize ws;
  int result = 0;

  // get terminal size with ioctl
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || 
      ws.ws_col == 0) {
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
    term.cols = ws.ws_col / 2;
    term.rows = ws.ws_row;
  }

  return result;
}

int init_terminal_state() {
  enable_raw_mode();
  return set_terminal_size();
}

static inline int ASCII_TO_NUM(int inx) { 
  return 
    (  image[inx  ] - ASCII_NUMBERS_START) * 100 
    + (image[inx + 1] - ASCII_NUMBERS_START) * 10 
    + (image[inx + 2] - ASCII_NUMBERS_START); 
}

/// prints specified line to screen
void println(int row, int col) {
  // move cursor to beginning of line
  write(STDOUT_FILENO, "\x1b[G", 3);
  // clear line
  write(STDOUT_FILENO, "\x1b[2K", 4);
  // print screen to terminal
  write(STDOUT_FILENO, 
      &image[(row * image_width + col) * BYTES_PER_CHAR], 
      MIN(term.cols, image_width - col) * BYTES_PER_CHAR);
  // reset formatting
  write(STDOUT_FILENO, "\x1b[0m", 4);
}

/// prints specified lines to screen
void printlns(int from_row, int to_row, int col) {
  int f = MIN(from_row, to_row);
  int t = MAX(from_row, to_row);
  int draw_start = f - y_offset;

  // adjust starting row if it's offscreen
  if (draw_start < 0) {
    f = f + (draw_start * -1);
    draw_start = 0;
  }

  // adjust end to be last row if t is offscreen
  t = MIN(t, term.rows + y_offset);

  // place cursor to beginning of selection
  char *buf = malloc(10);
  int len = sprintf(buf, "\x1b[%d;1H", draw_start + 1);
  write(STDOUT_FILENO, buf, len);
  free(buf);

  for (int i = f; i <= t; i++) {
    println(i, col);
    // cursor in next line
    write(STDOUT_FILENO, "\x1b[E", 3);
  }
}

/// prints the whole screen based on the offsets
void print_screen() {
  // move cursor to beginning of screen
  write(STDOUT_FILENO, "\x1b[H", 3);

  for (int i = y_offset; 
      i < MIN(term.rows + y_offset, image_height); i++) 
  {
    println(i, x_offset);
    // cursor in next line
    write(STDOUT_FILENO, "\x1b[E", 3);
  }
}

/// returns the starting index of given coordinates in the image
int get_inx(int row, int col) {
  return (col + image_width * row) * BYTES_PER_CHAR;
}

static inline void set_pixel(int r, int g, int b, int base_offset)
{
  image[base_offset + RED_OFFSET] = (int)(r / 100) + ASCII_NUMBERS_START;
  image[base_offset + RED_OFFSET + 1] = (int)((r % 100) / 10) + ASCII_NUMBERS_START;
  image[base_offset + RED_OFFSET + 2] = (int)(r % 10) + ASCII_NUMBERS_START;
  image[base_offset + GREEN_OFFSET] = (int)(g / 100) + ASCII_NUMBERS_START;
  image[base_offset + GREEN_OFFSET + 1] = (int)((g % 100) / 10) + ASCII_NUMBERS_START;
  image[base_offset + GREEN_OFFSET + 2] = (int)(g % 10) + ASCII_NUMBERS_START;
  image[base_offset + BLUE_OFFSET] = (int)(b / 100) + ASCII_NUMBERS_START;
  image[base_offset + BLUE_OFFSET + 1] = (int)((b % 100) / 10) + ASCII_NUMBERS_START;
  image[base_offset + BLUE_OFFSET + 2] = (int)(b % 10) + ASCII_NUMBERS_START;
}

void pipette(int row, int col) {
  int inx = get_inx(row, col);
  r_sel = ASCII_TO_NUM(inx + RED_OFFSET);
  g_sel = ASCII_TO_NUM(inx + GREEN_OFFSET);
  b_sel = ASCII_TO_NUM(inx + BLUE_OFFSET);
}

/// fills the whole image with given color
void fill_image(int r, int g, int b) {
  for (int i = 0; i < image_bytec; i+= BYTES_PER_CHAR) {
    set_pixel(r, g, b, i);
  }
}

/// fills a pixel with the given color 
/// and redraws the affected line
///
/// index shall be absolute to the image coordinates
void fill_pixel(int row, int col, int r, int g, int b) {
  // abort if not in image
  if (row >= image_height || col >= image_width - 1) {
    return;
  }
  int inx = get_inx(row, col);
  set_pixel(r, g, b, inx);
  set_pixel(r, g, b, inx + BYTES_PER_CHAR);

  // save cursor position
  int row_save;
  int col_save;
  // get current cursor position
  if (get_cursor_pos(&row_save, &col_save) == -1) {
    die("get_cursor_pos");
    return;
  }

  println(row, x_offset);

  // reset cursor position
  char *buf = malloc(10);
  int len = sprintf(buf, "\x1b[%d;%dH", row_save + 1, col_save + 1);
  write(STDOUT_FILENO, buf, len);
  free(buf);
}

/// fills a range of pixels with the given color 
/// and redraws affected lines
///
/// index shall be absolute to the image coordinates
void fill_selection(int from_r, int from_c, 
    int to_r, int to_c,
    int r, int g, int b) {
  // abort if not in image
  if (from_r >= image_height || from_c >= image_width - 1 ||
    to_r >= image_height || to_c >= image_width - 1) {
    return;
  }

  int start_col = MIN(from_c, to_c) * BYTES_PER_CHAR;
  int end_col = (MAX(from_c, to_c) + 1) * BYTES_PER_CHAR;
  int start_row = MIN(from_r, to_r) 
    * image_width * BYTES_PER_CHAR;
  int end_row = MAX(from_r, to_r) 
    * image_width * BYTES_PER_CHAR;

  for (int row = start_row; row <= end_row; 
     row+=image_width*BYTES_PER_CHAR) 
  {
    for (int col = start_col; col <= end_col; 
       col+=BYTES_PER_CHAR) 
    {
      set_pixel(r, g, b, row + col);
    }
  }

  // save cursor position
  int row_save;
  int col_save;
  // get current cursor position
  if (get_cursor_pos(&row_save, &col_save) == -1) {
    die("get_cursor_pos");
    return;
  }

  printlns(from_r, to_r, x_offset);

  // reset cursor position
  char *buf = malloc(10);
  int len = sprintf(buf, "\x1b[%d;%dH", row_save + 1, col_save + 1);
  write(STDOUT_FILENO, buf, len);
  free(buf);
}

void clear_screen() {
  // clear screen
  write(STDOUT_FILENO, "\x1b[2J", 4);
  // move cursor to beginning
  write(STDOUT_FILENO, "\x1b[H", 3);
}

int save_pipette_color(char c) {
  if (c < ASCII_NUMBERS_START || c > ASCII_NUMBERS_START + 9) {
    return ERROR;
  }

  color_palette[c - ASCII_NUMBERS_START][0] = r_sel;
  color_palette[c - ASCII_NUMBERS_START][1] = g_sel;
  color_palette[c - ASCII_NUMBERS_START][2] = b_sel;

  return SUCCESS;
}

/// compares the two pixels at the given indexes
///
/// returns 1 if they are different, 0 if they are the same 
/// and -1 if there was an error
int cmp_pixel_color_by_index(int inx1, int inx2) {
  if (!image) {
    return -1;
  }

  if (   image[inx1 + RED_OFFSET]   == image[inx2 + RED_OFFSET]
    && image[inx1 + GREEN_OFFSET] == image[inx2 + GREEN_OFFSET]
    && image[inx1 + BLUE_OFFSET]  == image[inx2 + BLUE_OFFSET])
  {
    return 0;
  }

  return 1;
}

/// jumps to the next color in the line where the cursor is
/// if dir is 1 it will search to the right of the cursor
/// if dir is -1 it will search to the left of the cursor
/// after the cursor is moved it moves the offsets if it is
/// now offscreen and redraws the screen
void jmp_next_color(int cursor_row, int cursor_col, int dir) {
  int diff; // specifies the maximum amount of pixels to jump
  char command;
  int index = get_inx(cursor_row, cursor_col);
  int origin_pixel = index;
  int move_by = -1;
  int back_color_found = -1;

  // direction specific setup
  if (dir < 0) {
    // return if in first column
    if (cursor_col == 0) {
      return;
    }

    diff = cursor_col;
    command = 'D';

    // check if cursor is at beginning of color region
    // and if so change the color to the adjacent one
    // so that it later won't stop because it sees a 
    // different color right away
    if (cmp_pixel_color_by_index(
          index, index - BYTES_PER_CHAR
        ) == 1) 
    {
      origin_pixel = index - BYTES_PER_CHAR;
    }
  }
  else {
    // return if in last column
    if (cursor_col == MIN(term.cols, image_width) - 1) { 
      return; 
    }

    diff = MIN(term.cols, image_width) - cursor_col - 1;
    command = 'C';
  }

  // search for color change in the given direction
  for (int i = 0; i < diff; i++) {
    index += dir * BYTES_PER_CHAR;
    if (cmp_pixel_color_by_index(origin_pixel, index) == 1) {
      move_by = i + ((dir > 0) ? 1 : 0);
      break;
    }
  }

  // if no color change was found move to beginning/end
  if (move_by < 0) {
    move_by = diff;
  }

  // round down to next even number so that there cannot be a
  // move by half a pixel
  move_by -= move_by % 2;

  // move by calculated amount in specified direction (command var)
  char *buf = malloc(10);
  int len = sprintf(buf, "\x1b[%d%c", move_by, command);
  write(STDOUT_FILENO, buf, len);
  free(buf);
}

static inline int readline(char **line, size_t *len, FILE *f) {
  ssize_t read_bytes;
  if ((read_bytes = getline(line, len, f)) <= 0) {
    perror("empty file");
    exit(1);
  }
  return read_bytes;
}

int load_failsave(char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    return ERROR;
  }

  char *line = NULL;
  unsigned long line_len;
  long read_bytes;

  // get width
  readline(&line, &line_len, f);
  int w = atoi(line);

  // get height
  readline(&line, &line_len, f);
  int h = atoi(line);

  png_bytepp img = malloc(h);
  int line_bytec = w * IMAGE_DEPTH;
  img[0] = malloc(line_bytec + 1);
  
  char c;
  char num[3] = { 0, 0, 0 };
  int inx = 0;
  int winx = 0;
  int hinx = 0;

  while ((c = getc(f)) != EOF) {
    num[inx] = c;
    inx++;
    if (inx < 3) {
      continue;
    }

    img[hinx][winx] = atoi(num);

    winx = (winx + 1) % (line_bytec);
    if (winx == 0) {
      hinx++;
      if (hinx != h) {
        img[hinx - 1][line_bytec] = '\0';
        img[hinx] = malloc(line_bytec + 1);
      }
    }

    inx = 0;
  }

  init_image(w, h, img, PNG_COLOR_TYPE_RGBA);

  return SUCCESS;
}

int load_image(char *path) {

  // check if path is a failsave filepath and if so load it
  char filetype[] = ".pcli_failsave";
  int pathlen = strlen(path);
  int typelen = strlen(filetype);
  if (strcmp(path + pathlen - typelen, filetype) == 0) {
    return load_failsave(path);
  }

  FILE *f = fopen(path, "rb");
  if (!f) {
    return ERROR;
  }

  int read_bytes_amount = 8;
  unsigned char *header = malloc(sizeof(char) * read_bytes_amount);

  if (fread(header, 1, read_bytes_amount, f) != read_bytes_amount) 
  {
    fclose(f);
    return ERROR;
  }

  int is_png = !png_sig_cmp(header, 0, read_bytes_amount);
  if (!is_png) {
    fclose(f);
    return ERROR;
  }

  rewind(f);
  
  png_structp png_ptr = png_create_read_struct(
      PNG_LIBPNG_VER_STRING, 
      NULL,
      NULL,
      NULL
    );

  if (!png_ptr) {
    fclose(f);
    return ERROR;
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);
  
  if (!info_ptr) {
    png_destroy_read_struct(&png_ptr, NULL, NULL);
    fclose(f);
    return ERROR;
  }

  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(f);
    return ERROR;
  }

  png_init_io(png_ptr, f);

  png_read_png(png_ptr, info_ptr, 
      PNG_TRANSFORM_SCALE_16 | 
      PNG_TRANSFORM_GRAY_TO_RGB, 
      NULL
    );

  unsigned int w;
  unsigned int h;
  int color_type;

  png_get_IHDR(png_ptr, info_ptr, &w, &h, 
      NULL, &color_type, NULL, NULL, NULL);

  png_bytepp row_pointers = png_get_rows(png_ptr, info_ptr);
  init_image(w, h, row_pointers, color_type);

  fclose(f);
  return SUCCESS;
}

png_bytepp get_preprocessed_image() {
  png_bytepp rows = malloc(SIZEOF_POINTER * image_height);

  for (int r = 0; r < image_height; r++) {
    rows[r] = malloc(
      sizeof(png_byte) * (int)(image_width / 2) * IMAGE_DEPTH
    );

    for (int c = 0; c < (int)(image_width / 2); c++) {
      int inx = get_inx(r, 2 * c);
      rows[r][c * IMAGE_DEPTH] = ASCII_TO_NUM(inx + RED_OFFSET);
      rows[r][c * IMAGE_DEPTH + 1] = ASCII_TO_NUM(inx + GREEN_OFFSET);
      rows[r][c * IMAGE_DEPTH + 2] = ASCII_TO_NUM(inx + BLUE_OFFSET);
      rows[r][c * IMAGE_DEPTH + 3] = (
        rows[r][c * IMAGE_DEPTH] == transparency_color[0] &&
        rows[r][c * IMAGE_DEPTH + 1] == transparency_color[1] &&
        rows[r][c * IMAGE_DEPTH + 2] == transparency_color[2]
      ) ? 0 : 255;
    }
  }

  return rows;
}

void user_error_fn() {
  die("png");
}

void user_warn_fn() { }

int save_image() {
  png_voidp *user_error_ptr;
  
  png_structp png_ptr = png_create_write_struct(
      PNG_LIBPNG_VER_STRING, 
      (png_voidp)user_error_ptr, 
      &user_error_fn, 
      &user_warn_fn
    );

  if (!png_ptr) {
    return ERROR;
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    return ERROR;
  }

  FILE *f = fopen("./out.png", "wb");

  if (!f) {
    return ERROR;
  }

  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(f);
    return ERROR;
  }

  png_init_io(png_ptr, f);

  png_set_IHDR(png_ptr, info_ptr, 
      (int) (image_width / 2), image_height, 
      8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, 
      PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT
    );
  
  // save image to file
  png_bytepp rows = get_preprocessed_image();
  png_set_rows(png_ptr, info_ptr, rows);
  png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

  // free everything
  png_destroy_write_struct(&png_ptr, &info_ptr);
  fclose(f);
  return SUCCESS;
}

int save_image_fallback() {
  char *img = malloc(3 * IMAGE_DEPTH * 
                     (image_width / 2) * image_height
    );
  int insert_point = 0;
  for (int i = 0; i < image_bytec; i += 2 * BYTES_PER_CHAR){
    img[insert_point] = image[i + RED_OFFSET];
    insert_point++;
    img[insert_point] = image[i + RED_OFFSET + 1];
    insert_point++;
    img[insert_point] = image[i + RED_OFFSET + 2];
    insert_point++;
    img[insert_point] = image[i + GREEN_OFFSET];
    insert_point++;
    img[insert_point] = image[i + GREEN_OFFSET + 1];
    insert_point++;
    img[insert_point] = image[i + GREEN_OFFSET + 2];
    insert_point++;
    img[insert_point] = image[i + BLUE_OFFSET];
    insert_point++;
    img[insert_point] = image[i + BLUE_OFFSET + 1];
    insert_point++;
    img[insert_point] = image[i + BLUE_OFFSET +2];
    insert_point++;
    char transparency[] = { '2', '5', '5' };
    if (
      ASCII_TO_NUM(i + RED_OFFSET) == transparency_color[0] &&
      ASCII_TO_NUM(i + GREEN_OFFSET) == transparency_color[1] &&
      ASCII_TO_NUM(i + BLUE_OFFSET) == transparency_color[2]) 
    {
      transparency[0] = '0';
      transparency[1] = '0';
      transparency[2] = '0';
    }
    img[insert_point] = transparency[0];
    insert_point++;
    img[insert_point] = transparency[1];
    insert_point++;
    img[insert_point] = transparency[2];
    insert_point++;
  }
  
  FILE *f = fopen("saved_image.pcli_failsave", "w");

  char *wstr = malloc(16);
  char *hstr = malloc(16);
  int wstr_len = sprintf(wstr, "%d\n", (image_width / 2));
  int hstr_len = sprintf(hstr, "%d\n", image_height);

  fwrite(wstr, sizeof(char), wstr_len, f);
  fwrite(hstr, sizeof(char), hstr_len, f);
  fwrite(
    img, 
    sizeof(char), 
    3 * IMAGE_DEPTH * (image_width / 2) * image_height, 
    f
  );

  fclose(f);
  free(img);
  return SUCCESS;
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

void log_image() {
  FILE *f = fopen("log.txt", "w");
  fprintf(f, "%s", image);
  die("\nlog done");
}

int get_command_inx(char c) {
  for (int i = 0; i < COMMANDC; i++) {
    if (commands[i][1][0] == c) {
      return i;
    }
  }
  return -1;
}

int handle_input(char c) {
  int row;
  int col;
  // get current cursor position
  if (get_cursor_pos(&row, &col) == -1) {
    die("get_cursor_pos");
    return ERROR;
  }

  int inx = get_command_inx(c);

  switch (inx) {
    case 0: // quit
      return 1;
    case 1: // move_left
      write(STDIN_FILENO, "\x1b[2D", 4);
      break;
    case 2: // move_down
      write(STDIN_FILENO, "\x1b[B", 3);
      break;
    case 3: // move_up
      write(STDIN_FILENO, "\x1b[A", 3);
      break;
    case 4: // move_right
      if (col > term.cols - 3) { break; } // don't allow move 
                        // to single last col
      write(STDIN_FILENO, "\x1b[2C", 4);
      break;
    case 5: // offset_left
      if (x_offset > 1) {
        x_offset -= 2;

        // save cursor pos
        write(STDOUT_FILENO, "\x1b[s", 3);

        print_screen();

        // restore cursor pos
        write(STDOUT_FILENO, "\x1b[u", 3);
      }
      break;
    case 6: // offset_down
      if (image_height - y_offset > term.rows) {
        y_offset += 1;

        // save cursor pos
        write(STDOUT_FILENO, "\x1b[s", 3);

        print_screen();

        // restore cursor pos
        write(STDOUT_FILENO, "\x1b[u", 3);
      }
      break;
    case 7: // offset_up
      if (y_offset > 0) {
        y_offset -= 1;

        // save cursor pos
        write(STDOUT_FILENO, "\x1b[s", 3);

        print_screen();

        // restore cursor pos
        write(STDOUT_FILENO, "\x1b[u", 3);
      }
      break;
    case 8: // offset_right
      if (image_width - x_offset > term.cols + 1) {
        x_offset += 2;

        // save cursor pos
        write(STDOUT_FILENO, "\x1b[s", 3);

        print_screen();

        // restore cursor pos
        write(STDOUT_FILENO, "\x1b[u", 3);
      }
      break;
    case 9: // move_top
      write(STDOUT_FILENO, "\x1b[H", 3);
      break;
    case 10: // move_bottom
      write(STDOUT_FILENO, "\x1b[999B", 6);
      break;
    case 11: // fill
      if (selected_row != -1 && selected_col != -1) {
        fill_selection(selected_row, selected_col, 
          row + y_offset, col + x_offset, 
          r_sel, g_sel, b_sel
        );
        selected_row = -1;
        selected_col = -1;
        break;
      }
      fill_pixel(row + y_offset, col + x_offset, 
          r_sel, g_sel, b_sel
        );
      break;
    case 12: // delete
      if (selected_row != -1 && selected_col != -1) {
        fill_selection(selected_row, selected_col, 
          row + y_offset, col + x_offset, 
          transparency_color[0], 
          transparency_color[1], 
          transparency_color[2]);
        selected_row = -1;
        selected_col = -1;
        break;
      }
      fill_pixel(row + y_offset, col + x_offset, 
        transparency_color[0], 
        transparency_color[1], 
        transparency_color[2]);
      break;
    case 13: // select
      if (selected_row != -1 && selected_col != -1) {
        selected_row = -1;
        selected_col = -1;
        break;
      }
      selected_row = row + y_offset;
      selected_col = col + x_offset;
      break;
    case 14: // jump_forward
      jmp_next_color(row, col, 1);
      break;
    case 15: // jump_backward
      jmp_next_color(row, col, -1);
      break;
    case 16: // color 0
      r_sel = color_palette[0][0];
      g_sel = color_palette[0][1];
      b_sel = color_palette[0][2];
      break;
    case 17: // color 1
      r_sel = color_palette[1][0];
      g_sel = color_palette[1][1];
      b_sel = color_palette[1][2];
      break;
    case 18: // color 2
      r_sel = color_palette[2][0];
      g_sel = color_palette[2][1];
      b_sel = color_palette[2][2];
      break;
    case 19: // color 3
      r_sel = color_palette[3][0];
      g_sel = color_palette[3][1];
      b_sel = color_palette[3][2];
      break;
    case 20: // color 4
      r_sel = color_palette[4][0];
      g_sel = color_palette[4][1];
      b_sel = color_palette[4][2];
      break;
    case 21: // color 5
      r_sel = color_palette[5][0];
      g_sel = color_palette[5][1];
      b_sel = color_palette[5][2];
      break;
    case 22: // color 6
      r_sel = color_palette[6][0];
      g_sel = color_palette[6][1];
      b_sel = color_palette[6][2];
      break;
    case 23: // color 7
      r_sel = color_palette[7][0];
      g_sel = color_palette[7][1];
      b_sel = color_palette[7][2];
      break;
    case 24: // color 8
      r_sel = color_palette[8][0];
      g_sel = color_palette[8][1];
      b_sel = color_palette[8][2];
      break;
    case 25: // color 9
      r_sel = color_palette[9][0];
      g_sel = color_palette[9][1];
      b_sel = color_palette[9][2];
      break;
    case 26: // save
      if (1 || save_image() == ERROR) {
        save_image_fallback();
        perror("Error on save! Resorted to fallback method.");
        return ERROR;
      }
      break;
    case 27: // reload
      // BUG: doesn't do anything for some reason
      set_terminal_size(); // recalc terminal size
      break;
    case 28: // pipette
      pipette(row + y_offset, col + x_offset);
      break;
    case 29: // pipette_save
      pipette(row + y_offset, col + x_offset);
      save_pipette_color(poll_input());
      break;
    default:
      break;
  }
  return SUCCESS;
}

void rebind_command(char *command, char c) {
  for (int i = 0; i < COMMANDC; i++) {
    if (strcmp(command, commands[i][0]) != 0) {
      continue;
    }

    char *key = malloc(2);
    key[0] = c;
    key[1] = '\0';
    commands[i][1] = key;

    return;
  }
}

void process_config_line(char *line) {
  int inx;
  int r;
  int g;
  int b;
  
  int is_color_setting = sscanf(line, "color_%d = %x;%x;%x", 
      &inx, &r, &g, &b);
  int no_result = 0;

  if (is_color_setting != EOF && is_color_setting != no_result) {
    color_palette[inx][0] = r;
    color_palette[inx][1] = g;
    color_palette[inx][2] = b;
    return;
  }

  int is_transparency_color_setting = sscanf(
      line, "transparency_color = %x;%x;%x", &r, &g, &b
    );

  if (is_transparency_color_setting != EOF
    && is_transparency_color_setting != no_result)
  {
    transparency_color[0] = r;
    transparency_color[1] = g;
    transparency_color[2] = b;
    return;
  }

  char command[20];
  char c;
  int is_command_rebind = sscanf(line, "bind %s %c", command, &c);

  if (is_command_rebind != EOF && is_command_rebind != no_result) {
    rebind_command(command, c);
    return;
  }
}

/// try to load config from these locations:
/// - ~/.config/pixelcli/config
/// - ~/.config/pixelcli.config
/// - ~/.pixelcli/config
/// - ~/.pixelcli.config
int load_config() {
  char *home = getenv("HOME");
  char *config[CONFIG_PATH_AMOUNT] = { // WARN: longest path first
    "/.config/pixelcli/config",
    "/.config/pixelcli.config",
    "/.pixelcli/config",
    "/.pixelcli.config"
  };
  char *path = malloc(strlen(home) + strlen(config[0]) + 1);
  strcpy(path, home);
  strcat(path, config[0]);

  FILE *f = fopen(path, "r");
  int i = 1;
  while (f == NULL) {
    strcpy(path, home);
    strcat(path, config[i]);
    f = fopen(path, "r");
    i++;
    if (i >= CONFIG_PATH_AMOUNT) { break; }
  }
  if (f == NULL) {
    return ERROR;
  }
  char *line = NULL;
  size_t len = 0;
  ssize_t read;

  while ((read = getline(&line, &len, f)) != EOF) {
    if (read > 2 && line[0] != '#') {
      process_config_line(line);
    }
  }

  free(line);
  return SUCCESS;
}

/*** main ***/

int main(int argc, char *argv[])
{
  if (argc > 2) {
    printf("Usage: pixelcli [filepath]");
    return ERROR;
  }
  if (argc == 2) {
    if (load_image(argv[1]) == ERROR) {
      printf("ERROR: Couldn't load the image!");
      return ERROR;
    }
  }
  else {
    int width;
    int height;
    printf("How big should the image be?\n");
    printf("width = ");
    scanf("%d", &width);
    printf("height = ");
    scanf("%d", &height);
    printf("Creating image with dimensions %d:%d ...\n", width, height);
    if (height <= 0 || width <= 0) {
      fprintf(stderr, 
          "Cannot create image with dimensions %d x %d!", 
          width, height
      );
      return ERROR;
    }

    // init image
    init_image(width, height, NULL, -1);
  }

  int cfg_success = load_config();
  if (cfg_success == ERROR) {
    fprintf(stderr, "Couldn't load the config file!\
        \nContinuing with the defaults...\n");
    fprintf(stderr, "Errno: %d", errno);
  }

  init_terminal_state();
  clear_screen();
  print_screen();

  // move cursor to beginning of screen
  write(STDOUT_FILENO, "\x1b[H", 3);

  int exit = 0;
  while (exit == 0) {
    char c = poll_input();
    if (c == -1) {
      die("poll_input");
    }
    exit = handle_input(c);
  }

  return SUCCESS;
}
