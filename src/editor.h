#ifndef EDITOR_H
#define EDITOR_H

#include <limits.h>
#include <ncurses.h>

#ifndef BUTTON5_PRESSED
#define BUTTON5_PRESSED NCURSES_MOUSE_MASK(5, NCURSES_BUTTON_PRESSED)
#endif
#ifndef BUTTON4_PRESSED
#define BUTTON4_PRESSED NCURSES_MOUSE_MASK(4, NCURSES_BUTTON_PRESSED)
#endif

#include "buffer.h"
#include "runner.h"

#define CTRL_KEY(k) ((k) & 0x1f)

typedef struct {
  TextBuffer buffer;
  FoldList   folds;
  int        cx;
  int        cy;
  int        row_offset;
  int        col_offset;
  int        screen_rows;
  int        screen_cols;
  int        output_visible;
  int        output_scroll;
  int        scroll_focus;
  int        lang_popup_visible;
  int        lang_selection;
  char*      output_text;
  char       current_lang[32];
  char       status[256];
  char       filename[PATH_MAX];
  int        should_quit;
} Editor;

extern const char* lang_names[];
#define LANG_COUNT 8

const char* path_basename(const char* path);
char*       dup_str(const char* s);

void set_status(Editor* ed, const char* fmt, ...);
void set_output(Editor* ed, const char* text);
void clamp_cursor(Editor* ed);
int  is_fold_start_row(const FoldList* folds, int row);
int  cursor_on_folded_row(Editor* ed);

int  output_height(const Editor* ed);
int  text_rows(const Editor* ed);
int  visible_rows_before(const FoldList* folds, int row);
void scroll_editor(Editor* ed);
int  count_output_screen_rows(const char* text, int screen_cols);

void draw_lang_popup(Editor* ed);
void draw_status(Editor* ed);
void draw_editor_area(Editor* ed);
void draw_output(Editor* ed);
void refresh_screen(Editor* ed);
void init_ncurses(void);
void fetch_progress(void* ctx);
void process_keypress(Editor* ed);

int  save_file(Editor* ed);
void run(Editor* ed);

#endif
