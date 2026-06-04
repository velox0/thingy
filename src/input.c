#include <ctype.h>
#include <limits.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "editor.h"

int save_file(Editor *ed) {
  char err[256];
  if (buffer_save_file(&ed->buffer, ed->filename, err, sizeof(err)) != 0) {
    set_status(ed, "Save failed: %s", err);
    return -1;
  }
  set_status(ed, "Saved %s", ed->filename);
  return 0;
}

void run(Editor *ed) {
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

void process_keypress(Editor *ed) {
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
      if (!ed->output_visible) {
        ed->output_scroll = 0;
        ed->scroll_focus = 0;
      }
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

    case KEY_MOUSE: {
      MEVENT ev;
      if (getmouse(&ev) == OK) {
        int panel_top = ed->output_visible ? ed->screen_rows - output_height(ed) : ed->screen_rows;
        if (ed->output_visible && ev.y >= panel_top) {
          ed->scroll_focus = 1;
        } else if (ev.y < panel_top) {
          ed->scroll_focus = 0;
        }
        if (ev.bstate & BUTTON4_PRESSED) {
          if (ed->scroll_focus && ed->output_visible) {
            if (ed->output_scroll > 0) ed->output_scroll--;
            else ed->scroll_focus = 0;
          } else {
            if (ed->cy > 0) {
              ed->cy--;
              while (ed->cy > 0 &&
                     folds_is_hidden_row(&ed->folds, ed->cy) &&
                     !is_fold_start_row(&ed->folds, ed->cy))
                ed->cy--;
            }
          }
        }
        if (ev.bstate & BUTTON5_PRESSED) {
          if (ed->scroll_focus && ed->output_visible) {
            int total = count_output_screen_rows(ed->output_text ? ed->output_text : "", ed->screen_cols);
            int visible = output_height(ed) - 1;
            if (ed->output_scroll < total - visible)
              ed->output_scroll++;
            else
              ed->scroll_focus = 0;
          } else {
            if (ed->cy + 1 < ed->buffer.line_count) {
              ed->cy++;
              while (ed->cy + 1 < ed->buffer.line_count &&
                     folds_is_hidden_row(&ed->folds, ed->cy) &&
                     !is_fold_start_row(&ed->folds, ed->cy))
                ed->cy++;
            }
          }
        }
      }
      break;
    }

    case KEY_UP:
      if (ed->scroll_focus && ed->output_visible) {
        if (ed->output_scroll > 0) ed->output_scroll--;
        else ed->scroll_focus = 0;
      } else {
        if (ed->cy > 0) {
          ed->cy--;
          while (ed->cy > 0 &&
                 folds_is_hidden_row(&ed->folds, ed->cy) &&
                 !is_fold_start_row(&ed->folds, ed->cy))
            ed->cy--;
        }
      }
      break;

    case KEY_DOWN:
      if (ed->scroll_focus && ed->output_visible) {
        int total = count_output_screen_rows(ed->output_text ? ed->output_text : "", ed->screen_cols);
        int visible = output_height(ed) - 1;
        if (ed->output_scroll < total - visible)
          ed->output_scroll++;
        else
          ed->scroll_focus = 0;
      } else {
        if (ed->cy + 1 < ed->buffer.line_count) {
          ed->cy++;
          while (ed->cy + 1 < ed->buffer.line_count &&
                 folds_is_hidden_row(&ed->folds, ed->cy) &&
                 !is_fold_start_row(&ed->folds, ed->cy))
            ed->cy++;
        }
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

    case '\t':
      if (cursor_on_folded_row(ed)) {
        set_status(ed, "Unfold the collapsed line before editing.");
      } else {
        int t;
        for (t = 0; t < 4; t++) {
          buffer_insert_char(&ed->buffer, ed->cy, ed->cx, ' ');
          ed->cx++;
        }
      }
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
