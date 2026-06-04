#include <ctype.h>
#include <limits.h>
#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buffer.h"
#include "runner.h"

#define CTRL_KEY(k) ((k) & 0x1f)

typedef struct {
  TextBuffer buffer;
  FoldList folds;
  int cx;
  int cy;
  int row_offset;
  int col_offset;
  int screen_rows;
  int screen_cols;
  int output_visible;
  char *output_text;
  char status[256];
  char filename[PATH_MAX];
  int should_quit;
} Editor;

static int is_fold_start_row(const FoldList *folds, int row);

static char *dup_str(const char *s) {
  size_t len = strlen(s);
  char *copy = malloc(len + 1);
  if (!copy) return NULL;
  memcpy(copy, s, len + 1);
  return copy;
}

static void set_status(Editor *ed, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(ed->status, sizeof(ed->status), fmt, ap);
  va_end(ap);
}

static void set_output(Editor *ed, const char *text) {
  char *copy = dup_str(text ? text : "");
  if (!copy) return;
  free(ed->output_text);
  ed->output_text = copy;
}

static void clamp_cursor(Editor *ed) {
  buffer_clamp_cursor(&ed->buffer, &ed->cy, &ed->cx);
  while (ed->cy < ed->buffer.line_count - 1 &&
         folds_is_hidden_row(&ed->folds, ed->cy) &&
         !is_fold_start_row(&ed->folds, ed->cy))
    ed->cy++;
  while (ed->cy > 0 &&
         folds_is_hidden_row(&ed->folds, ed->cy) &&
         !is_fold_start_row(&ed->folds, ed->cy))
    ed->cy--;
}

static int output_height(const Editor *ed) {
  if (!ed->output_visible) return 0;
  if (ed->screen_rows < 8) return 3;
  return ed->screen_rows / 3;
}

static int text_rows(const Editor *ed) {
  int rows = ed->screen_rows - 1 - output_height(ed);
  return rows > 0 ? rows : 1;
}

static int visible_rows_before(const FoldList *folds, int row) {
  int i, count = 0;
  for (i = 0; i < row; i++) {
    if (!folds_is_hidden_row(folds, i)) count++;
  }
  return count;
}

static void scroll_editor(Editor *ed) {
  int rows = text_rows(ed);
  int cursor_screen_y = visible_rows_before(&ed->folds, ed->cy);
  int offset_screen_y = visible_rows_before(&ed->folds, ed->row_offset);
  int f;

  if (!is_fold_start_row(&ed->folds, ed->cy)) {
    for (f = 0; f < ed->folds.count; f++) {
      int fr = ed->folds.items[f].row;
      if (fr > ed->row_offset && fr < ed->cy) cursor_screen_y++;
    }
  }
  for (f = 0; f < ed->folds.count; f++) {
    int fr = ed->folds.items[f].row;
    if (fr < ed->row_offset) offset_screen_y++;
  }

  if (cursor_screen_y < offset_screen_y) {
    ed->row_offset = ed->cy;
  }
  if (cursor_screen_y >= offset_screen_y + rows) {
    int target = ed->cy;
    int needed = rows - 1;
    while (target > 0 && needed > 0) {
      target--;
      if (!folds_is_hidden_row(&ed->folds, target) || is_fold_start_row(&ed->folds, target)) needed--;
    }
    ed->row_offset = target;
  }

  if (ed->cx < ed->col_offset) ed->col_offset = ed->cx;
  if (ed->cx >= ed->col_offset + ed->screen_cols) {
    ed->col_offset = ed->cx - ed->screen_cols + 1;
  }
}

static const char *tail_start(const char *text, int max_lines) {
  const char *start = text;
  const char *p;
  int lines = 0;

  if (!text || max_lines <= 0) return "";

  for (p = text; *p; p++) {
    if (*p == '\n') lines++;
  }
  if (lines <= max_lines) return text;

  for (p = text + strlen(text); p > text; p--) {
    if (p[-1] == '\n') {
      lines--;
      if (lines <= max_lines) {
        start = p;
        break;
      }
    }
  }

  return start;
}

static void draw_status(Editor *ed) {
  char line[512];

  snprintf(line, sizeof(line),
           "%s  Ln %d, Col %d  |  ^S Save  ^R Run  ^F Fold  ^O Output  ^Q Quit",
           ed->filename, ed->cy + 1, ed->cx + 1);
  if (has_colors()) {
    attron(COLOR_PAIR(2) | A_BOLD);
  } else {
    attron(A_REVERSE);
  }
  mvhline(0, 0, ' ', ed->screen_cols);
  mvaddnstr(0, 0, line, ed->screen_cols);
  if (has_colors()) {
    attroff(COLOR_PAIR(2) | A_BOLD);
  } else {
    attroff(A_REVERSE);
  }
}

static void draw_editor_area(Editor *ed) {
  int rows = text_rows(ed);
  int screen_y = 1;
  int file_row = ed->row_offset;
  int drawn = 0;

  if (has_colors()) {
    attron(COLOR_PAIR(1));
  }
  while (drawn < rows && screen_y < ed->screen_rows) {
    if (file_row >= ed->buffer.line_count) {
      move(screen_y, 0);
      clrtoeol();
      if (has_colors()) attron(COLOR_PAIR(4));
      mvaddch(screen_y, 0, '~');
      if (has_colors()) attroff(COLOR_PAIR(4));
      screen_y++;
      drawn++;
      continue;
    }

    if (is_fold_start_row(&ed->folds, file_row)) {
      const char *line = ed->buffer.lines[file_row];
      char folded[512];
      snprintf(folded, sizeof(folded), "%s >>>", line);
      move(screen_y, 0);
      clrtoeol();
      if (has_colors()) attron(COLOR_PAIR(4));
      mvaddnstr(screen_y, 0, folded + ed->col_offset, ed->screen_cols);
      if (has_colors()) attroff(COLOR_PAIR(4));
      file_row++;
      while (file_row < ed->buffer.line_count &&
             folds_is_hidden_row(&ed->folds, file_row)) {
        file_row++;
      }
    } else {
      const char *line = ed->buffer.lines[file_row];
      int len = (int)strlen(line);
      move(screen_y, 0);
      clrtoeol();
      if (len > ed->col_offset) {
        mvaddnstr(screen_y, 0, line + ed->col_offset, ed->screen_cols);
      }
      file_row++;
    }
    screen_y++;
    drawn++;
  }
  if (has_colors()) {
    attroff(COLOR_PAIR(1));
  }
}

static void draw_output(Editor *ed) {
  int panel_h = output_height(ed);
  int panel_top = ed->screen_rows - panel_h;
  int content_rows = panel_h - 1;
  int i;
  const char *cursor;

  if (panel_h <= 0) return;

  if (has_colors()) {
    attron(COLOR_PAIR(2) | A_BOLD);
  } else {
    attron(A_REVERSE);
  }
  mvhline(panel_top, 0, ' ', ed->screen_cols);
  mvaddnstr(panel_top, 0, "Output", ed->screen_cols);
  if (has_colors()) {
    attroff(COLOR_PAIR(2) | A_BOLD);
  } else {
    attroff(A_REVERSE);
  }

  if (has_colors()) {
    attron(COLOR_PAIR(3));
  }
  cursor = tail_start(ed->output_text ? ed->output_text : "", content_rows);
  for (i = 0; i < content_rows; i++) {
    int row = panel_top + 1 + i;
    const char *nl = strchr(cursor, '\n');
    int len = nl ? (int)(nl - cursor) : (int)strlen(cursor);

    move(row, 0);
    clrtoeol();
    if (len > 0) mvaddnstr(row, 0, cursor, ed->screen_cols);

    if (!nl) break;
    cursor = nl + 1;
  }
  if (has_colors()) {
    attroff(COLOR_PAIR(3));
  }
}

static int save_file(Editor *ed) {
  char err[256];
  if (buffer_save_file(&ed->buffer, ed->filename, err, sizeof(err)) != 0) {
    set_status(ed, "Save failed: %s", err);
    return -1;
  }
  set_status(ed, "Saved %s", ed->filename);
  return 0;
}

static void run(Editor *ed) {
  RunResult result;
  char *output = NULL;
  char err[256];
  char tmppath[PATH_MAX];

  if (save_file(ed) != 0) return;

  snprintf(tmppath, sizeof(tmppath), "/tmp/thingy_run_%d.c", getpid());
  if (buffer_save_file_filtered(&ed->buffer, tmppath, err, sizeof(err)) != 0) {
    set_status(ed, "Run failed: %s", err);
    return;
  }

  result = runner_smart_run(tmppath, &output);
  set_output(ed, output ? output : "");
  free(output);
  remove(tmppath);
  ed->output_visible = 1;

  switch (result) {
    case RUN_OK:
      set_status(ed, "Run finished.");
      break;
    case RUN_COMPILE_ERROR:
      set_status(ed, "Compilation failed.");
      break;
    case RUN_EXEC_ERROR:
      set_status(ed, "Execution failed.");
      break;
    default:
      set_status(ed, "Run failed.");
      break;
  }
}

static int cursor_on_folded_row(Editor *ed) {
  return folds_is_folded_row(&ed->folds, ed->cy);
}

static int is_fold_start_row(const FoldList *folds, int row) {
  int i;
  for (i = 0; i < folds->count; i++) {
    if (folds->items[i].row == row) return 1;
  }
  return 0;
}

static void process_keypress(Editor *ed) {
  int ch = getch();

  switch (ch) {
    case CTRL_KEY('q'):
      ed->should_quit = 1;
      return;

    case CTRL_KEY('s'):
      save_file(ed);
      break;

    case CTRL_KEY('r'):
      run(ed);
      break;

    case CTRL_KEY('o'):
      ed->output_visible = !ed->output_visible;
      break;

    case CTRL_KEY('f'): {
      char msg[256];
      buffer_toggle_fold(&ed->buffer, &ed->folds, &ed->cy, msg, sizeof(msg));
      set_status(ed, "%s", msg);
      clamp_cursor(ed);
      break;
    }

    case KEY_UP:
      if (ed->cy > 0) {
        ed->cy--;
        while (ed->cy > 0 &&
               folds_is_hidden_row(&ed->folds, ed->cy) &&
               !is_fold_start_row(&ed->folds, ed->cy))
          ed->cy--;
      }
      break;

    case KEY_DOWN:
      if (ed->cy + 1 < ed->buffer.line_count) {
        ed->cy++;
        while (ed->cy + 1 < ed->buffer.line_count &&
               folds_is_hidden_row(&ed->folds, ed->cy) &&
               !is_fold_start_row(&ed->folds, ed->cy))
          ed->cy++;
      }
      break;

    case KEY_LEFT:
      if (ed->cx > 0) {
        ed->cx--;
      } else if (ed->cy > 0) {
        ed->cy--;
        while (ed->cy > 0 &&
               folds_is_hidden_row(&ed->folds, ed->cy) &&
               !is_fold_start_row(&ed->folds, ed->cy))
          ed->cy--;
        ed->cx = buffer_line_len(&ed->buffer, ed->cy);
      }
      break;

    case KEY_RIGHT:
      if (ed->cx < buffer_line_len(&ed->buffer, ed->cy)) {
        ed->cx++;
      } else if (ed->cy + 1 < ed->buffer.line_count) {
        ed->cy++;
        while (ed->cy + 1 < ed->buffer.line_count &&
               folds_is_hidden_row(&ed->folds, ed->cy) &&
               !is_fold_start_row(&ed->folds, ed->cy))
          ed->cy++;
        ed->cx = 0;
      }
      break;

    case KEY_BACKSPACE:
    case 127:
      if (cursor_on_folded_row(ed)) {
        set_status(ed, "Unfold the collapsed line before editing.");
        break;
      } else {
        int deleted_row = buffer_backspace(&ed->buffer, &ed->cy, &ed->cx);
        if (deleted_row >= 0) folds_on_line_delete(&ed->folds, deleted_row);
      }
      break;

    case '\n':
    case '\r':
    case KEY_ENTER:
      if (cursor_on_folded_row(ed)) {
        set_status(ed, "Unfold the collapsed line before editing.");
      } else {
        int inserted_row = buffer_insert_newline(&ed->buffer, &ed->cy, &ed->cx);
        if (inserted_row >= 0) folds_on_line_insert(&ed->folds, inserted_row);
      }
      break;

    case KEY_RESIZE:
      break;

    default:
      if (isprint(ch)) {
        if (cursor_on_folded_row(ed)) {
          set_status(ed, "Unfold the collapsed line before editing.");
        } else {
          buffer_insert_char(&ed->buffer, ed->cy, ed->cx, (char)ch);
          ed->cx++;
        }
      }
      break;
  }

  clamp_cursor(ed);
}

static void refresh_screen(Editor *ed) {
  int screen_y;
  int screen_x;

  getmaxyx(stdscr, ed->screen_rows, ed->screen_cols);
  clamp_cursor(ed);
  scroll_editor(ed);

  erase();
  draw_status(ed);
  draw_editor_area(ed);
  draw_output(ed);

  screen_y = 1 + visible_rows_before(&ed->folds, ed->cy) - visible_rows_before(&ed->folds, ed->row_offset);
  if (!is_fold_start_row(&ed->folds, ed->cy)) {
    int f;
    for (f = 0; f < ed->folds.count; f++) {
      int fr = ed->folds.items[f].row;
      if (fr > ed->row_offset && fr < ed->cy) screen_y++;
    }
  }
  screen_x = ed->cx - ed->col_offset;
  if (screen_y < 1 || screen_y >= ed->screen_rows || screen_x < 0 || screen_x >= ed->screen_cols) {
    screen_y = 1;
    screen_x = 0;
  }
  move(screen_y, screen_x);
  refresh();
}

static int init_editor(Editor *ed, const char *filename) {
  char err[256];

  memset(ed, 0, sizeof(*ed));
  buffer_init(&ed->buffer);
  folds_init(&ed->folds);
  if (ed->buffer.line_count <= 0) return -1;

  snprintf(ed->filename, sizeof(ed->filename), "%s",
           (filename && filename[0]) ? filename : "untitled.txt");
  if (buffer_load_file(&ed->buffer, ed->filename, err, sizeof(err)) != 0) {
    set_status(ed, "Load failed: %s", err);
  } else {
    set_status(ed, "Opened %s", ed->filename);
  }

  initscr();
  if (has_colors()) {
    start_color();
    // Sakura Palette:
    // BG: Deep Purple/Black (234)
    // FG: Soft White (255)
    // Accent 1 (Sakura Pink): 211 or 218
    // Accent 2 (Darker Pink): 197
    // Accent 3 (Leaf Green): 114

    init_pair(1, 218, 234); // Editor: Pink on Dark
    init_pair(2, 234, 218); // Status: Dark on Pink
    init_pair(3, 114, 234); // Output: Green on Dark
    init_pair(4, 197, 234); // Special: Red/Dark Pink on Dark
  }
  raw();
  noecho();
  keypad(stdscr, TRUE);
  set_escdelay(25);
  return 0;
}

static void shutdown_editor(Editor *ed) {
  endwin();
  free(ed->output_text);
  buffer_free(&ed->buffer);
  folds_free(&ed->folds);
}

int main(int argc, char **argv) {
  Editor ed;

  if (init_editor(&ed, argc > 1 ? argv[1] : "untitled.txt") != 0) {
    fprintf(stderr, "Failed to initialize editor.\n");
    return 1;
  }

  while (!ed.should_quit) {
    refresh_screen(&ed);
    process_keypress(&ed);
  }

  shutdown_editor(&ed);
  return 0;
}