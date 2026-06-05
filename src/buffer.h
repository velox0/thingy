#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>

/*
 * TextBuffer - line-oriented text storage.
 *
 * Stores file content as an array of heap-allocated strings, one per line.
 * Lines are 0-indexed. The buffer always has at least one line (even if
 * empty) so line_count >= 1 after init.
 *
 * This is the authoritative copy of the file content. Fold operations
 * never modify the buffer -- they only affect what the editor displays.
 */
typedef struct {
  char** lines;
  int    line_count;
  int    capacity;  // allocated size of lines[] (grows via realloc)
} TextBuffer;

/*
 * Fold marker lines:  <<< [label]   (start)
 *                      >>>          (end)
 *
 * A fold region is defined by a start marker line followed by an end marker
 * line. When folded, the start marker stays visible (displayed as
 * "<<< label >>>") while the content and end marker are hidden.
 *
 * The markers are plain text in the buffer. They are only meaningful to the
 * fold system, which scans for them to find foldable regions.
 */

/*
 * FoldEntry - one active fold.
 *
 * When a fold is toggled on, the editor records which rows should be
 * hidden. Folds are display-only: the TextBuffer is never modified.
 * Hidden rows are stored as absolute 0-indexed row numbers so that
 * multiple folds can coexist without index conflicts.
 *
 * The start marker row is NOT hidden -- it remains visible and is
 * displayed as "<<< label >>>" in the editor. Only the content lines
 * and the end marker are hidden.
 *
 * row:          The row of the start marker (<<<). Visible; displayed
 *               with " >>>" appended when folded.
 * label:        The text after <<<, or "fold" if none.
 * hidden_rows:  Heap array of absolute row indices to hide. Contains
 *               content rows and the end marker row (start + 1 through
 *               end). Does NOT include the start marker row.
 * hidden_count: Number of entries in hidden_rows.
 */
typedef struct {
  int   row;
  char* label;
  int*  hidden_rows;
  int   hidden_count;
} FoldEntry;

/*
 * FoldList - all active folds.
 */
typedef struct {
  FoldEntry* items;
  int        count;
  int        capacity;  // allocated size of items[] (grows via realloc)
} FoldList;

void buffer_init(TextBuffer* buf);
void buffer_free(TextBuffer* buf);
int  ensure_line_capacity(TextBuffer* buf, int needed);

int buffer_load_file(TextBuffer* buf, const char* path, char* err, size_t err_size);
int buffer_save_file(const TextBuffer* buf, const char* path, char* err, size_t err_size);

int  buffer_line_len(const TextBuffer* buf, int row);
void buffer_clamp_cursor(const TextBuffer* buf, int* row, int* col);
void buffer_insert_char(TextBuffer* buf, int row, int col, char c);
int  buffer_insert_newline(TextBuffer* buf, int* row, int* col);
int  buffer_backspace(TextBuffer* buf, int* row, int* col);

void folds_init(FoldList* folds);
void folds_free(FoldList* folds);

/* Returns 1 if row is the start marker of a fold (i.e. cursor is on a
   folded block's marker line). */
int folds_is_folded_row(const FoldList* folds, int row);

/* Returns 1 if row should be hidden from display. Covers both the marker
   lines and all content lines between them. */
int folds_is_hidden_row(const FoldList* folds, int row);

/* Adjust fold row indices after the buffer gains or loses a line. */
void folds_on_line_insert(FoldList* folds, int at_row);
void folds_on_line_delete(FoldList* folds, int at_row);

/* Toggle fold at cursor_row. If the cursor is on an existing fold's start
   marker, the fold is removed (unfold). Otherwise, scans upward for a
   <<< marker and downward for the matching >>> to create a new fold.
   Writes a status message into msg. Returns 1 on toggle, 0 if no fold
   found, -1 on error. */
int buffer_toggle_fold(TextBuffer* buf, FoldList* folds, int* cursor_row, char* msg,
                       size_t msg_size);

/* Save the buffer to disk, stripping all fold marker lines (<<< and >>>).
   Use this for running code so the output compiles cleanly. */
int buffer_save_file_filtered(const TextBuffer* buf, const char* path, char* err, size_t err_size);

#endif
