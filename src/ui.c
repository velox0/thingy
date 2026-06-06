#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "editor.h"

const char* lang_names[] = {"auto", "c", "python", "node", "ruby", "php", "perl", "sh"};

const char* path_basename(const char* path) {
  const char* base = strrchr(path, '/');
  return base ? base + 1 : path;
}

char* dup_str(const char* s) {
  size_t len  = strlen(s);
  char*  copy = malloc(len + 1);
  if (!copy) return NULL;
  memcpy(copy, s, len + 1);
  return copy;
}

void set_status(Editor* ed, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(ed->status, sizeof(ed->status), fmt, ap);
  va_end(ap);
  struct timeval tv;
  gettimeofday(&tv, NULL);
  ed->status_time = tv.tv_sec + tv.tv_usec / 1000000.0;
}

void set_output(Editor* ed, const char* text) {
  char* copy = dup_str(text ? text : "");
  if (!copy) return;
  free(ed->output_text);
  ed->output_text = copy;
}

int is_fold_start_row(const FoldList* folds, int row) {
  int i;
  for (i = 0; i < folds->count; i++) {
    if (folds->items[i].row == row) return 1;
  }
  return 0;
}

int cursor_on_folded_row(Editor* ed) { return folds_is_folded_row(&ed->folds, ed->cy); }

void clamp_cursor(Editor* ed) {
  buffer_clamp_cursor(&ed->buffer, &ed->cy, &ed->cx);
  while (ed->cy < ed->buffer.line_count - 1 && folds_is_hidden_row(&ed->folds, ed->cy) &&
         !is_fold_start_row(&ed->folds, ed->cy))
    ed->cy++;
  while (ed->cy > 0 && folds_is_hidden_row(&ed->folds, ed->cy) &&
         !is_fold_start_row(&ed->folds, ed->cy))
    ed->cy--;
}

int output_height(const Editor* ed) {
  if (!ed->output_visible) return 0;
  if (ed->screen_rows < 8) return 3;
  return ed->screen_rows / 3;
}

int text_rows(const Editor* ed) {
  int rows = ed->screen_rows - 1 - output_height(ed);
  return rows > 0 ? rows : 1;
}

int visible_rows_before(const FoldList* folds, int row) {
  int i, count = 0;
  for (i = 0; i < row; i++) {
    if (!folds_is_hidden_row(folds, i)) count++;
  }
  return count;
}

void scroll_editor(Editor* ed) {
  int rows            = text_rows(ed);
  int cursor_screen_y = visible_rows_before(&ed->folds, ed->cy);
  int offset_screen_y = visible_rows_before(&ed->folds, ed->row_offset);
  int f;
  int scroll_margin = 3;

  if (rows < 10)
    scroll_margin = 1;
  else if (rows < 15)
    scroll_margin = 2;

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

  if (cursor_screen_y < offset_screen_y + scroll_margin) {
    int target = ed->cy;
    int needed = scroll_margin;
    while (target > 0 && needed > 0) {
      target--;
      if (!folds_is_hidden_row(&ed->folds, target) || is_fold_start_row(&ed->folds, target))
        needed--;
    }
    ed->row_offset = target;
  }
  if (cursor_screen_y >= offset_screen_y + rows - scroll_margin) {
    int target = ed->cy;
    int needed = rows - 1 - scroll_margin;
    while (target > 0 && needed > 0) {
      target--;
      if (!folds_is_hidden_row(&ed->folds, target) || is_fold_start_row(&ed->folds, target))
        needed--;
    }
    ed->row_offset = target;
  }

  if (ed->cx < ed->col_offset) ed->col_offset = ed->cx;
  if (ed->cx >= ed->col_offset + ed->screen_cols) {
    ed->col_offset = ed->cx - ed->screen_cols + 1;
  }
}

int count_output_screen_rows(const char* text, int screen_cols) {
  int         total      = 0;
  const char* line_start = text;
  if (!text || !text[0] || screen_cols <= 0) return 0;
  while (*line_start) {
    const char* nl       = strchr(line_start, '\n');
    int         line_len = nl ? (int)(nl - line_start) : (int)strlen(line_start);
    int         rows     = (line_len > 0) ? ((line_len - 1) / screen_cols + 1) : 1;
    total += rows;
    if (!nl) break;
    line_start = nl + 1;
  }
  return total;
}

void draw_lang_popup(Editor* ed) {
  int popup_w = 18;
  int popup_h = LANG_COUNT + 2;
  int popup_x = (ed->screen_cols - popup_w) / 2;
  int popup_y = (ed->screen_rows - popup_h) / 2;
  int i;

  if (popup_x < 0) popup_x = 0;
  if (popup_y < 1) popup_y = 1;

  if (has_colors()) attron(COLOR_PAIR(2) | A_BOLD);
  for (i = 0; i < popup_h; i++) {
    mvhline(popup_y + i, popup_x, ' ', popup_w);
  }
  mvaddnstr(popup_y, popup_x, " Language (Enter)", popup_w);
  if (has_colors()) attroff(COLOR_PAIR(2) | A_BOLD);

  for (i = 0; i < LANG_COUNT; i++) {
    int row      = popup_y + 1 + i;
    int selected = (i == ed->lang_selection);
    int is_current =
        (strcmp(lang_names[i], ed->current_lang) == 0) || (i == 0 && ed->current_lang[0] == '\0');

    if (selected) {
      if (has_colors())
        attron(COLOR_PAIR(1) | A_BOLD);
      else
        attron(A_REVERSE);
    } else if (is_current) {
      if (has_colors()) attron(COLOR_PAIR(3));
    }

    mvaddnstr(row, popup_x + 1, lang_names[i], popup_w - 2);
    if (is_current && !selected) {
      mvaddch(row, popup_x + popup_w - 2, '*');
    }

    if (selected) {
      if (has_colors())
        attroff(COLOR_PAIR(1) | A_BOLD);
      else
        attroff(A_REVERSE);
    } else if (is_current) {
      if (has_colors()) attroff(COLOR_PAIR(3));
    }
  }
}

void draw_status(Editor* ed) {
  char line[512];

  if (ed->status[0]) {
    snprintf(line, sizeof(line), "%s", ed->status);
  } else if (ed->output_visible && ed->scroll_focus) {
    snprintf(line, sizeof(line), "%s  Output [focused] (Up/Dn scroll)  |  ^O Close  ^Q Quit",
             ed->filename);
  } else {
    snprintf(line, sizeof(line),
             "%s  Ln %d, Col %d  |  ^S Save  ^R Run  ^L Lang  ^F Fold  ^O Output  ^Q Quit",
             ed->filename, ed->cy + 1, ed->cx + 1);
  }
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

void draw_editor_area(Editor* ed) {
  int rows     = text_rows(ed);
  int screen_y = 1;
  int file_row = ed->row_offset;
  int drawn    = 0;

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
      const char* line = ed->buffer.lines[file_row];
      char        folded[512];
      snprintf(folded, sizeof(folded), "%s >>>", line);
      move(screen_y, 0);
      clrtoeol();
      if (has_colors()) attron(COLOR_PAIR(4));
      mvaddnstr(screen_y, 0, folded + ed->col_offset, ed->screen_cols);
      if (has_colors()) attroff(COLOR_PAIR(4));
      file_row++;
      while (file_row < ed->buffer.line_count && folds_is_hidden_row(&ed->folds, file_row)) {
        file_row++;
      }
    } else {
      const char* line = ed->buffer.lines[file_row];
      int         len  = (int)strlen(line);
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

void draw_output(Editor* ed) {
  int         panel_h      = output_height(ed);
  int         panel_top    = ed->screen_rows - panel_h;
  int         content_rows = panel_h - 1;
  const char* text;
  int         total_screen_rows;
  int         screen_row;
  int         scrolled = 0;

  if (panel_h <= 0) return;

  text              = ed->output_text ? ed->output_text : "";
  total_screen_rows = count_output_screen_rows(text, ed->screen_cols);

  if (has_colors()) {
    attron(COLOR_PAIR(2) | A_BOLD);
  } else {
    attron(A_REVERSE);
  }
  mvhline(panel_top, 0, ' ', ed->screen_cols);
  {
    char header[64];
    if (ed->output_scroll > 0 && total_screen_rows > 0) {
      int bottom = ed->output_scroll + content_rows;
      if (bottom > total_screen_rows) bottom = total_screen_rows;
      snprintf(header, sizeof(header), "Output %d-%d/%d", ed->output_scroll + 1, bottom,
               total_screen_rows);
    } else {
      snprintf(header, sizeof(header), "Output");
    }
    mvaddnstr(panel_top, 0, header, ed->screen_cols);
  }
  if (has_colors()) {
    attroff(COLOR_PAIR(2) | A_BOLD);
  } else {
    attroff(A_REVERSE);
  }

  if (has_colors()) {
    attron(COLOR_PAIR(3));
  }

  screen_row = panel_top + 1;
  {
    const char* line_start = text;
    while (*line_start && screen_row <= panel_top + content_rows) {
      const char* nl       = strchr(line_start, '\n');
      int         line_len = nl ? (int)(nl - line_start) : (int)strlen(line_start);
      int         col      = 0;

      if (line_len == 0) {
        if (scrolled >= ed->output_scroll) {
          move(screen_row, 0);
          clrtoeol();
          screen_row++;
        } else {
          scrolled++;
        }
        if (!nl) break;
        line_start = nl + 1;
        continue;
      }

      while (col < line_len && screen_row <= panel_top + content_rows) {
        int chunk = line_len - col;
        if (chunk > ed->screen_cols) chunk = ed->screen_cols;

        if (scrolled >= ed->output_scroll) {
          move(screen_row, 0);
          clrtoeol();
          mvaddnstr(screen_row, 0, line_start + col, chunk);
          screen_row++;
        } else {
          scrolled++;
        }
        col += chunk;
      }

      if (!nl) break;
      line_start = nl + 1;
    }
  }

  while (screen_row <= panel_top + content_rows) {
    move(screen_row, 0);
    clrtoeol();
    screen_row++;
  }

  if (has_colors()) {
    attroff(COLOR_PAIR(3));
  }
}

void refresh_screen(Editor* ed) {
  int screen_y;
  int screen_x;

  getmaxyx(stdscr, ed->screen_rows, ed->screen_cols);
  clamp_cursor(ed);
  scroll_editor(ed);

  erase();
  draw_status(ed);
  draw_editor_area(ed);
  draw_output(ed);
  if (ed->lang_popup_visible) draw_lang_popup(ed);

  screen_y =
      1 + visible_rows_before(&ed->folds, ed->cy) - visible_rows_before(&ed->folds, ed->row_offset);
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

void init_ncurses(void) {
  initscr();
  if (has_colors()) {
    start_color();
    init_pair(1, 218, 234);
    init_pair(2, 234, 218);
    init_pair(3, 114, 234);
    init_pair(4, 197, 234);
  }
  raw();
  noecho();
  keypad(stdscr, TRUE);
  timeout(100);
  set_escdelay(25);
  mousemask(BUTTON4_PRESSED | BUTTON5_PRESSED, NULL);
  mouseinterval(0);
}

void fetch_progress(void* ctx) {
  Editor* ed = (Editor*)ctx;
  getmaxyx(stdscr, ed->screen_rows, ed->screen_cols);
  erase();
  draw_status(ed);
  draw_editor_area(ed);
  refresh();
}
