#include "buffer.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* dup_str(const char* s) {
  size_t len  = strlen(s);
  char*  copy = malloc(len + 1);
  if (!copy) return NULL;
  memcpy(copy, s, len + 1);
  return copy;
}

static char* dup_nstr(const char* s, size_t n) {
  char* copy = malloc(n + 1);
  if (!copy) return NULL;
  memcpy(copy, s, n);
  copy[n] = '\0';
  return copy;
}

int ensure_line_capacity(TextBuffer* buf, int needed) {
  int    new_capacity;
  char** new_lines;

  if (needed <= buf->capacity) return 0;

  new_capacity = (buf->capacity > 0) ? buf->capacity : 8;
  while (new_capacity < needed) new_capacity *= 2;

  new_lines = realloc(buf->lines, (size_t)new_capacity * sizeof(char*));
  if (!new_lines) return -1;

  buf->lines    = new_lines;
  buf->capacity = new_capacity;
  return 0;
}

static void clear_lines(TextBuffer* buf) {
  int i;
  for (i = 0; i < buf->line_count; i++) free(buf->lines[i]);
  buf->line_count = 0;
}

static int append_owned_line(TextBuffer* buf, char* line) {
  if (ensure_line_capacity(buf, buf->line_count + 1) != 0) {
    free(line);
    return -1;
  }
  buf->lines[buf->line_count++] = line;
  return 0;
}

static int append_empty_line(TextBuffer* buf) {
  char* line = dup_str("");
  if (!line) return -1;
  return append_owned_line(buf, line);
}

void buffer_init(TextBuffer* buf) {
  buf->lines      = NULL;
  buf->line_count = 0;
  buf->capacity   = 0;
  if (append_empty_line(buf) != 0) {
    buf->line_count = 0;
  }
}

void buffer_free(TextBuffer* buf) {
  clear_lines(buf);
  free(buf->lines);
  buf->lines    = NULL;
  buf->capacity = 0;
}

int buffer_load_file(TextBuffer* buf, const char* path, char* err, size_t err_size) {
  FILE*  fp;
  char*  line = NULL;
  size_t len  = 0;
  size_t cap  = 0;
  int    c;

  fp = fopen(path, "r");
  if (!fp) {
    if (errno == ENOENT) return 0;
    snprintf(err, err_size, "%s", strerror(errno));
    return -1;
  }

  clear_lines(buf);

  cap  = 64;
  line = malloc(cap);
  if (!line) {
    fclose(fp);
    snprintf(err, err_size, "out of memory");
    return -1;
  }

  while ((c = fgetc(fp)) != EOF) {
    if (c == '\r') continue;
    if (c == '\n') {
      line[len] = '\0';
      if (append_owned_line(buf, line) != 0) {
        fclose(fp);
        snprintf(err, err_size, "out of memory");
        return -1;
      }
      cap  = 64;
      len  = 0;
      line = malloc(cap);
      if (!line) {
        fclose(fp);
        snprintf(err, err_size, "out of memory");
        return -1;
      }
      continue;
    }

    if (len + 1 >= cap) {
      char* tmp;
      cap *= 2;
      tmp = realloc(line, cap);
      if (!tmp) {
        free(line);
        fclose(fp);
        snprintf(err, err_size, "out of memory");
        return -1;
      }
      line = tmp;
    }
    line[len++] = (char)c;
  }

  if (len > 0 || buf->line_count == 0) {
    line[len] = '\0';
    if (append_owned_line(buf, line) != 0) {
      fclose(fp);
      snprintf(err, err_size, "out of memory");
      return -1;
    }
  } else {
    free(line);
  }

  fclose(fp);
  return 0;
}

int buffer_save_file(const TextBuffer* buf, const char* path, char* err, size_t err_size) {
  FILE* fp;
  int   i;

  fp = fopen(path, "w");
  if (!fp) {
    snprintf(err, err_size, "%s", strerror(errno));
    return -1;
  }

  for (i = 0; i < buf->line_count; i++) {
    if (fputs(buf->lines[i], fp) == EOF) {
      snprintf(err, err_size, "%s", strerror(errno));
      fclose(fp);
      return -1;
    }
    if (i + 1 < buf->line_count && fputc('\n', fp) == EOF) {
      snprintf(err, err_size, "%s", strerror(errno));
      fclose(fp);
      return -1;
    }
  }

  if (fclose(fp) != 0) {
    snprintf(err, err_size, "%s", strerror(errno));
    return -1;
  }
  return 0;
}

int buffer_line_len(const TextBuffer* buf, int row) {
  if (row < 0 || row >= buf->line_count) return 0;
  return (int)strlen(buf->lines[row]);
}

void buffer_clamp_cursor(const TextBuffer* buf, int* row, int* col) {
  int len;

  if (buf->line_count <= 0) {
    *row = 0;
    *col = 0;
    return;
  }

  if (*row < 0) *row = 0;
  if (*row >= buf->line_count) *row = buf->line_count - 1;

  len = buffer_line_len(buf, *row);
  if (*col < 0) *col = 0;
  if (*col > len) *col = len;
}

void buffer_insert_char(TextBuffer* buf, int row, int col, char c) {
  char* line;
  int   len;
  char* new_line;

  if (row < 0 || row >= buf->line_count) return;

  line = buf->lines[row];
  len  = (int)strlen(line);
  if (col < 0) col = 0;
  if (col > len) col = len;

  new_line = realloc(line, (size_t)len + 2);
  if (!new_line) return;

  memmove(new_line + col + 1, new_line + col, (size_t)(len - col + 1));
  new_line[col]   = c;
  buf->lines[row] = new_line;
}

int buffer_insert_newline(TextBuffer* buf, int* row, int* col) {
  char* line;
  int   len;
  int   split_col;
  char* left;
  char* right;

  if (*row < 0 || *row >= buf->line_count) return -1;
  if (ensure_line_capacity(buf, buf->line_count + 1) != 0) return -1;

  line      = buf->lines[*row];
  len       = (int)strlen(line);
  split_col = *col;
  if (split_col < 0) split_col = 0;
  if (split_col > len) split_col = len;

  left  = dup_nstr(line, (size_t)split_col);
  right = dup_str(line + split_col);
  if (!left || !right) {
    free(left);
    free(right);
    return -1;
  }

  free(buf->lines[*row]);
  buf->lines[*row] = left;
  memmove(&buf->lines[*row + 2], &buf->lines[*row + 1],
          (size_t)(buf->line_count - *row - 1) * sizeof(char*));
  buf->lines[*row + 1] = right;
  buf->line_count++;

  *row = *row + 1;
  *col = 0;
  return *row;
}

int buffer_backspace(TextBuffer* buf, int* row, int* col) {
  char* line;
  int   len;
  int   removed_row;

  if (*row < 0 || *row >= buf->line_count) return -1;

  line = buf->lines[*row];
  len  = (int)strlen(line);
  if (*col > len) *col = len;

  if (*col > 0) {
    memmove(line + *col - 1, line + *col, (size_t)(len - *col + 1));
    *col = *col - 1;
    return -1;
  }

  if (*row == 0) return -1;

  {
    int   prev_row = *row - 1;
    int   prev_len = (int)strlen(buf->lines[prev_row]);
    char* merged   = realloc(buf->lines[prev_row], (size_t)prev_len + (size_t)len + 1);
    if (!merged) return -1;
    memcpy(merged + prev_len, line, (size_t)len + 1);
    buf->lines[prev_row] = merged;
    free(buf->lines[*row]);
    removed_row = *row;
    memmove(&buf->lines[*row], &buf->lines[*row + 1],
            (size_t)(buf->line_count - *row - 1) * sizeof(char*));
    buf->line_count--;
    *row = prev_row;
    *col = prev_len;
    return removed_row;
  }
}

void folds_init(FoldList* folds) {
  folds->items    = NULL;
  folds->count    = 0;
  folds->capacity = 0;
}

static void free_fold_entry(FoldEntry* entry) {
  free(entry->label);
  free(entry->hidden_rows);
}

void folds_free(FoldList* folds) {
  int i;
  for (i = 0; i < folds->count; i++) free_fold_entry(&folds->items[i]);
  free(folds->items);
  folds->items    = NULL;
  folds->count    = 0;
  folds->capacity = 0;
}

static int ensure_fold_capacity(FoldList* folds, int needed) {
  int        cap;
  FoldEntry* items;

  if (needed <= folds->capacity) return 0;
  cap = folds->capacity > 0 ? folds->capacity : 4;
  while (cap < needed) cap *= 2;
  items = realloc(folds->items, (size_t)cap * sizeof(FoldEntry));
  if (!items) return -1;
  folds->items    = items;
  folds->capacity = cap;
  return 0;
}

static int find_fold_index(const FoldList* folds, int row) {
  int i;
  for (i = 0; i < folds->count; i++) {
    if (folds->items[i].row == row) return i;
  }
  return -1;
}

int folds_is_folded_row(const FoldList* folds, int row) { return find_fold_index(folds, row) >= 0; }

int folds_is_hidden_row(const FoldList* folds, int row) {
  int i, j;
  for (i = 0; i < folds->count; i++) {
    for (j = 0; j < folds->items[i].hidden_count; j++) {
      if (folds->items[i].hidden_rows[j] == row) return 1;
    }
  }
  return 0;
}

void folds_on_line_insert(FoldList* folds, int at_row) {
  int i, j;
  for (i = 0; i < folds->count; i++) {
    if (folds->items[i].row >= at_row) folds->items[i].row++;
    for (j = 0; j < folds->items[i].hidden_count; j++) {
      if (folds->items[i].hidden_rows[j] >= at_row) folds->items[i].hidden_rows[j]++;
    }
  }
}

void folds_on_line_delete(FoldList* folds, int at_row) {
  int i, j;
  for (i = 0; i < folds->count;) {
    if (folds->items[i].row == at_row) {
      free_fold_entry(&folds->items[i]);
      memmove(&folds->items[i], &folds->items[i + 1],
              (size_t)(folds->count - i - 1) * sizeof(FoldEntry));
      folds->count--;
      continue;
    }
    if (folds->items[i].row > at_row) folds->items[i].row--;
    for (j = 0; j < folds->items[i].hidden_count; j++) {
      if (folds->items[i].hidden_rows[j] > at_row) folds->items[i].hidden_rows[j]--;
    }
    i++;
  }
}

static const char* trim_left(const char* s) {
  while (*s == ' ' || *s == '\t') s++;
  return s;
}

static int is_start_marker_line(const char* line) {
  const char* p = trim_left(line);
  return strncmp(p, "<<<", 3) == 0 && strstr(p, ">>>") == NULL;
}

static int is_end_marker_line(const char* line) {
  const char* p = trim_left(line);
  return strncmp(p, ">>>", 3) == 0;
}

static char* extract_label(const char* line) {
  const char* p = trim_left(line);
  const char* end;

  if (strncmp(p, "<<<", 3) != 0) return dup_str("fold");
  p += 3;
  while (*p == ' ' || *p == '\t') p++;
  end = p + strlen(p);
  while (end > p && isspace((unsigned char)end[-1])) end--;
  if (end == p) return dup_str("fold");
  return dup_nstr(p, (size_t)(end - p));
}

static int intersects_existing_fold(const FoldList* folds, int start, int end) {
  int i, j;
  for (i = 0; i < folds->count; i++) {
    for (j = 0; j < folds->items[i].hidden_count; j++) {
      int hr = folds->items[i].hidden_rows[j];
      if (hr >= start && hr <= end) return 1;
    }
  }
  return 0;
}

int buffer_toggle_fold(TextBuffer* buf, FoldList* folds, int* cursor_row, char* msg,
                       size_t msg_size) {
  int fold_idx = find_fold_index(folds, *cursor_row);

  if (fold_idx >= 0) {
    int row = folds->items[fold_idx].row;
    free_fold_entry(&folds->items[fold_idx]);
    memmove(&folds->items[fold_idx], &folds->items[fold_idx + 1],
            (size_t)(folds->count - fold_idx - 1) * sizeof(FoldEntry));
    folds->count--;
    *cursor_row = row;
    snprintf(msg, msg_size, "Unfolded block.");
    return 1;
  }

  {
    int       row   = *cursor_row;
    int       start = -1;
    int       end   = -1;
    int       i;
    int       hidden_count;
    FoldEntry entry;
    char*     label;

    for (i = row; i >= 0; i--) {
      if (is_start_marker_line(buf->lines[i])) {
        start = i;
        break;
      }
    }
    if (start < 0) {
      snprintf(msg, msg_size, "Cursor is not inside a <<< >>> block.");
      return 0;
    }

    for (i = start + 1; i < buf->line_count; i++) {
      if (is_end_marker_line(buf->lines[i])) {
        end = i;
        break;
      }
    }
    if (end < 0 || row > end) {
      snprintf(msg, msg_size, "No matching >>> marker found.");
      return 0;
    }

    if (intersects_existing_fold(folds, start, end)) {
      snprintf(msg, msg_size, "Unfold nested collapsed blocks first.");
      return 0;
    }

    label = extract_label(buf->lines[start]);
    if (!label) {
      snprintf(msg, msg_size, "Out of memory while folding.");
      return -1;
    }

    hidden_count      = end - start + 1;
    entry.hidden_rows = malloc((size_t)hidden_count * sizeof(int));
    if (!entry.hidden_rows) {
      free(label);
      snprintf(msg, msg_size, "Out of memory while folding.");
      return -1;
    }

    if (ensure_fold_capacity(folds, folds->count + 1) != 0) {
      free(entry.hidden_rows);
      free(label);
      snprintf(msg, msg_size, "Out of memory while folding.");
      return -1;
    }

    for (i = 0; i < hidden_count; i++) entry.hidden_rows[i] = start + i;
    entry.row          = start;
    entry.label        = label;
    entry.hidden_count = hidden_count;

    folds->items[folds->count++] = entry;

    *cursor_row = start;
    snprintf(msg, msg_size, "Folded block: %s", label);
    return 1;
  }
}

int buffer_save_file_filtered(const TextBuffer* buf, const char* path, char* err, size_t err_size) {
  FILE* fp;
  int   i;
  int   first = 1;

  fp = fopen(path, "w");
  if (!fp) {
    snprintf(err, err_size, "%s", strerror(errno));
    return -1;
  }

  for (i = 0; i < buf->line_count; i++) {
    if (is_start_marker_line(buf->lines[i])) continue;
    if (is_end_marker_line(buf->lines[i])) continue;

    if (!first && fputc('\n', fp) == EOF) {
      snprintf(err, err_size, "%s", strerror(errno));
      fclose(fp);
      return -1;
    }
    if (fputs(buf->lines[i], fp) == EOF) {
      snprintf(err, err_size, "%s", strerror(errno));
      fclose(fp);
      return -1;
    }
    first = 0;
  }

  if (fclose(fp) != 0) {
    snprintf(err, err_size, "%s", strerror(errno));
    return -1;
  }
  return 0;
}
