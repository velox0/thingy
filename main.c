#include <ctype.h>
#include <errno.h>
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
  int lang_popup_visible;
  int lang_selection;
  char *output_text;
  char current_lang[32];
  char status[256];
  char filename[PATH_MAX];
  int should_quit;
} Editor;

static int is_fold_start_row(const FoldList *folds, int row);

static const char *path_basename(const char *path) {
  const char *base = strrchr(path, '/');
  return base ? base + 1 : path;
}

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

static const char *lang_names[] = {
  "auto", "c", "python", "node", "ruby", "php", "perl", "sh"
};
#define LANG_COUNT (int)(sizeof(lang_names) / sizeof(lang_names[0]))

static void draw_lang_popup(Editor *ed) {
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
    int row = popup_y + 1 + i;
    int selected = (i == ed->lang_selection);
    int is_current = (strcmp(lang_names[i], ed->current_lang) == 0) ||
                     (i == 0 && ed->current_lang[0] == '\0');

    if (selected) {
      if (has_colors()) attron(COLOR_PAIR(1) | A_BOLD);
      else attron(A_REVERSE);
    } else if (is_current) {
      if (has_colors()) attron(COLOR_PAIR(3));
    }

    mvaddnstr(row, popup_x + 1, lang_names[i], popup_w - 2);
    if (is_current && !selected) {
      mvaddch(row, popup_x + popup_w - 2, '*');
    }

    if (selected) {
      if (has_colors()) attroff(COLOR_PAIR(1) | A_BOLD);
      else attroff(A_REVERSE);
    } else if (is_current) {
      if (has_colors()) attroff(COLOR_PAIR(3));
    }
  }
}

static void draw_status(Editor *ed) {
  char line[512];

  snprintf(line, sizeof(line),
           "%s  Ln %d, Col %d  |  ^S Save  ^R Run  ^L Lang  ^F Fold  ^O Output  ^Q Quit",
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
  const char *ext;
  int is_url = (strncmp(ed->filename, "http://", 7) == 0 || strncmp(ed->filename, "https://", 8) == 0);

  if (!is_url && save_file(ed) != 0) return;

  ext = strrchr(ed->filename, '.');
  {
    const char *base = path_basename(ed->filename);
    char base_no_ext[256];
    const char *dot = strrchr(base, '.');
    int base_len = dot ? (int)(dot - base) : (int)strlen(base);
    if (base_len > (int)sizeof(base_no_ext) - 1) base_len = (int)sizeof(base_no_ext) - 1;
    memcpy(base_no_ext, base, (size_t)base_len);
    base_no_ext[base_len] = '\0';
    snprintf(tmppath, sizeof(tmppath), "/tmp/%d_%s%s", getpid(), base_no_ext, ext ? ext : ".c");
  }
  if (buffer_save_file_filtered(&ed->buffer, tmppath, err, sizeof(err)) != 0) {
    set_status(ed, "Run failed: %s", err);
    return;
  }

  result = runner_smart_run(tmppath, ed->current_lang[0] ? ed->current_lang : NULL, &output);
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

  if (ed->lang_popup_visible) {
    switch (ch) {
      case CTRL_KEY('l'):
      case 27:
        ed->lang_popup_visible = 0;
        return;
      case KEY_UP:
        if (ed->lang_selection > 0) ed->lang_selection--;
        return;
      case KEY_DOWN:
        if (ed->lang_selection < LANG_COUNT - 1) ed->lang_selection++;
        return;
      case '\n':
      case '\r':
      case KEY_ENTER:
        if (ed->lang_selection == 0) {
          ed->current_lang[0] = '\0';
          set_status(ed, "Language: auto");
        } else {
          snprintf(ed->current_lang, sizeof(ed->current_lang), "%s", lang_names[ed->lang_selection]);
          set_status(ed, "Language: %s", ed->current_lang);
        }
        ed->lang_popup_visible = 0;
        return;
      default:
        return;
    }
  }

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

    case CTRL_KEY('l'):
      ed->lang_popup_visible = !ed->lang_popup_visible;
      ed->lang_selection = 0;
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
  if (ed->lang_popup_visible) draw_lang_popup(ed);

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

static void init_ncurses(void) {
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
  set_escdelay(25);
}

static void fetch_progress(void *ctx) {
  Editor *ed = (Editor *)ctx;
  getmaxyx(stdscr, ed->screen_rows, ed->screen_cols);
  erase();
  draw_status(ed);
  draw_editor_area(ed);
  refresh();
}

static int init_editor(Editor *ed, const char *filename) {
  char err[256];
  int is_url;

  memset(ed, 0, sizeof(*ed));
  buffer_init(&ed->buffer);
  folds_init(&ed->folds);
  if (ed->buffer.line_count <= 0) return -1;

  snprintf(ed->filename, sizeof(ed->filename), "%s",
           (filename && filename[0]) ? filename : "untitled.txt");

  is_url = filename && (strncmp(filename, "http://", 7) == 0 || strncmp(filename, "https://", 8) == 0);

  if (is_url) {
    init_ncurses();
    set_status(ed, "Fetching %s ...", filename);
    getmaxyx(stdscr, ed->screen_rows, ed->screen_cols);
    erase();
    draw_status(ed);
    draw_editor_area(ed);
    refresh();

    ed->buffer.line_count = 0;
    if (runner_fetch_url_stream(filename, &ed->buffer, fetch_progress, ed) != 0) {
      set_status(ed, "Fetch failed.");
    } else {
      set_status(ed, "Fetched %s", filename);
    }
    if (ed->buffer.line_count <= 0) {
      ensure_line_capacity(&ed->buffer, 1);
      ed->buffer.lines[0] = dup_str("");
      ed->buffer.line_count = 1;
    }
  } else {
    if (buffer_load_file(&ed->buffer, ed->filename, err, sizeof(err)) != 0) {
      set_status(ed, "Load failed: %s", err);
    } else {
      set_status(ed, "Opened %s", ed->filename);
    }
    init_ncurses();
  }
  return 0;
}

static void shutdown_editor(Editor *ed) {
  endwin();
  reset_shell_mode();
  free(ed->output_text);
  buffer_free(&ed->buffer);
  folds_free(&ed->folds);
}

int main(int argc, char **argv) {
  Editor ed;
  int i;

  if (argc > 1 && strcmp(argv[1], "--run") == 0) {
    const char *path = NULL;
    const char *lang = NULL;
    TextBuffer buf;
    char err[256];
    char tmppath[PATH_MAX];
    const char *ext;
    RunResult result;
    char *output = NULL;
    int is_url = 0;

    for (i = 2; i < argc; i++) {
      if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
        lang = argv[++i];
      } else if (!path) {
        path = argv[i];
      }
    }

    if (!path) {
      fprintf(stderr, "Usage: thingy --run [--lang <lang>] <file|url>\n");
      return 1;
    }

    is_url = (strncmp(path, "http://", 7) == 0 || strncmp(path, "https://", 8) == 0);

    {
      const char *src_name = is_url ? strrchr(path, '/') : path_basename(path);
      if (!src_name) src_name = "file";
      else src_name++;
      {
        const char *dot = strrchr(src_name, '.');
        int name_len = dot ? (int)(dot - src_name) : (int)strlen(src_name);
        char safe_name[256];
        if (name_len > (int)sizeof(safe_name) - 1) name_len = (int)sizeof(safe_name) - 1;
        memcpy(safe_name, src_name, (size_t)name_len);
        safe_name[name_len] = '\0';

        buffer_init(&buf);
        if (is_url) {
          char *content = NULL;
          FILE *fp;
          if (runner_fetch_url(path, &content) != 0 || !content) {
            fprintf(stderr, "%s", content ? content : "Error fetching URL\n");
            free(content);
            buffer_free(&buf);
            return 1;
          }
          snprintf(tmppath, sizeof(tmppath), "/tmp/%d_%s.tmp", getpid(), safe_name);
          fp = fopen(tmppath, "w");
          if (!fp) {
            fprintf(stderr, "Error writing temp file: %s\n", strerror(errno));
            free(content);
            buffer_free(&buf);
            return 1;
          }
          fputs(content, fp);
          fclose(fp);
          free(content);
          if (buffer_load_file(&buf, tmppath, err, sizeof(err)) != 0) {
            fprintf(stderr, "Error loading fetched content: %s\n", err);
            remove(tmppath);
            buffer_free(&buf);
            return 1;
          }
          remove(tmppath);
          snprintf(tmppath, sizeof(tmppath), "/tmp/%d_%s.c", getpid(), safe_name);
        } else {
          if (buffer_load_file(&buf, path, err, sizeof(err)) != 0) {
            fprintf(stderr, "Error loading %s: %s\n", path, err);
            buffer_free(&buf);
            return 1;
          }
          ext = strrchr(path, '.');
          snprintf(tmppath, sizeof(tmppath), "/tmp/%d_%s%s", getpid(), safe_name, ext ? ext : ".c");
        }
      }
    }

    if (buffer_save_file_filtered(&buf, tmppath, err, sizeof(err)) != 0) {
      fprintf(stderr, "Error preparing file: %s\n", err);
      buffer_free(&buf);
      return 1;
    }

    result = runner_smart_run(tmppath, lang, &output);
    remove(tmppath);
    buffer_free(&buf);

    if (output && output[0]) {
      printf("%s", output);
    }
    free(output);

    return result == RUN_OK ? 0 : 1;
  }

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