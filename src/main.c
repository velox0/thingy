#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "editor.h"

#ifndef GIT_VERSION
#define GIT_VERSION "dev"
#endif

static void print_usage(void) {
  printf(
      "\n"
      " \xF0\x9F\x8C\xB8 thingy \xE2\x80\x94 Sakura TUI Editor\n"
      "\n"
      " Usage: thingy [options] [file]\n"
      "        thingy --run [options] <file|url>\n"
      "\n"
      " Options:\n"
      "   -h, --help              Show this help and retreat\n"
      "   --version               Show version and stand down\n"
      "   -v, --verbose           Verbose fetch output (--run mode only)\n"
      "   --run                   Execute a file without the TUI\n"
      "   --lang <lang>           Force language (c, python, node, ruby, php, perl, sh)\n"
      "\n"
      " Keys: ^S save  ^R run  ^F fold  ^O output  ^L lang  ^Q quit\n"
      "\n");
}

static int check_terminal(void) {
  if (!isatty(STDOUT_FILENO)) {
    fprintf(stderr,
            "\n \xF0\x9F\x8C\xB8 thingy needs a real terminal, soldier.\n"
            "    Stop piping me into the void. Open a terminal and try again.\n\n");
    return -1;
  }
  const char* term = getenv("TERM");
  if (!term || !term[0]) {
    fprintf(stderr,
            "\n \xF0\x9F\x8C\xB8 TERM is not set, soldier.\n"
            "    I don't know what battlefield you're on. Set TERM and try again.\n\n");
    return -1;
  }
  return 0;
}

static int init_editor(Editor* ed, const char* filename) {
  char err[256];
  int  is_url;
  int  is_stdin = !isatty(STDIN_FILENO);

  memset(ed, 0, sizeof(*ed));
  buffer_init(&ed->buffer);
  folds_init(&ed->folds);
  if (ed->buffer.line_count <= 0) return -1;

  if (is_stdin) {
    snprintf(ed->filename, sizeof(ed->filename), "stdin");
  } else {
    snprintf(ed->filename, sizeof(ed->filename), "%s",
             (filename && filename[0]) ? filename : "untitled.txt");
  }

  is_url = !is_stdin && filename &&
           (strncmp(filename, "http://", 7) == 0 || strncmp(filename, "https://", 8) == 0);

  if (is_stdin) {
    init_ncurses();
    if (buffer_load_stdin(&ed->buffer, err, sizeof(err)) != 0) {
      set_status(ed, "Read failed: %s", err);
    } else {
      set_status(ed, "Read from stdin (%d lines)", ed->buffer.line_count);
    }
  } else if (is_url) {
    init_ncurses();
    set_status(ed, "Fetching %s ...", filename);
    getmaxyx(stdscr, ed->screen_rows, ed->screen_cols);
    erase();
    draw_status(ed);
    draw_editor_area(ed);
    refresh();

    ed->buffer.line_count = 0;
    ed->fetching          = 1;
    if (runner_fetch_stream_start(&ed->stream_ctx, filename, &ed->buffer, fetch_progress, ed) !=
        0) {
      set_status(ed, "Fetch failed.");
      ed->fetching = 0;
    }
    if (ed->buffer.line_count <= 0) {
      ensure_line_capacity(&ed->buffer, 1);
      ed->buffer.lines[0]   = dup_str("");
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

static void shutdown_editor(Editor* ed) {
  if (ed->fetching) {
    runner_fetch_stream_free(&ed->stream_ctx);
    ed->fetching = 0;
  }
  endwin();
  reset_shell_mode();
  free(ed->output_text);
  buffer_free(&ed->buffer);
  folds_free(&ed->folds);
}

static int run_cli(const char* path, const char* lang, int verbose) {
  TextBuffer  buf;
  char        err[256];
  char        tmppath[PATH_MAX];
  const char* ext;
  RunResult   result;
  char*       output = NULL;
  int         is_url = (strncmp(path, "http://", 7) == 0 || strncmp(path, "https://", 8) == 0);

  const char* src_name = is_url ? strrchr(path, '/') : path_basename(path);
  if (!src_name)
    src_name = "file";
  else
    src_name++;
  {
    const char* dot      = strrchr(src_name, '.');
    int         name_len = dot ? (int)(dot - src_name) : (int)strlen(src_name);
    char        safe_name[256];
    if (name_len > (int)sizeof(safe_name) - 1) name_len = (int)sizeof(safe_name) - 1;
    memcpy(safe_name, src_name, (size_t)name_len);
    safe_name[name_len] = '\0';

    buffer_init(&buf);
    if (is_url) {
      char*        content = NULL;
      FILE*        fp;
      FetchOptions opts = {0};
      opts.verbose      = verbose;
      opts.max_retries  = 3;
      if (runner_fetch_url(path, &content, &opts) != 0 || !content) {
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
      snprintf(tmppath, sizeof(tmppath), "/tmp/%d_%s", getpid(), safe_name);
    } else {
      if (buffer_load_file(&buf, path, err, sizeof(err)) != 0) {
        fprintf(stderr, "Error loading %s: %s\n", path, err);
        buffer_free(&buf);
        return 1;
      }
      ext = strrchr(path, '.');
      snprintf(tmppath, sizeof(tmppath), "/tmp/%d_%s%s", getpid(), safe_name, ext ? ext : "");
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

int main(int argc, char** argv) {
  Editor ed;
  int    i;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_usage();
      return 0;
    }
  }

  if (argc > 1 && strcmp(argv[1], "--run") == 0) {
    int         verbose = 0;
    const char* path    = NULL;
    const char* lang    = NULL;

    for (i = 2; i < argc; i++) {
      if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
        verbose = 1;
      } else if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
        lang = argv[++i];
      } else if (!path) {
        path = argv[i];
      }
    }

    if (!path) {
      fprintf(stderr, "Usage: thingy --run [-v] [--lang <lang>] <file|url>\n");
      return 1;
    }

    return run_cli(path, lang, verbose);
  }

  /* standalone -v/--version (outside --run) */
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
      printf("thingy %s\n", GIT_VERSION);
      return 0;
    }
  }

  if (check_terminal() != 0) return 1;

  if (init_editor(&ed, argc > 1 ? argv[1] : "untitled.txt") != 0) {
    fprintf(stderr, "Failed to initialize editor.\n");
    return 1;
  }

  while (!ed.should_quit) {
    if (ed.fetching) {
      int result = runner_fetch_stream_poll(&ed.stream_ctx);
      if (result == 0) {
        ed.fetching = 0;
        set_status(&ed, "Fetched %s", ed.filename);
        clamp_cursor(&ed);
      } else if (result < 0) {
        ed.fetching = 0;
        set_status(&ed, "Fetch failed.");
        clamp_cursor(&ed);
      }
    }
    if (ed.status[0]) {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      double now = tv.tv_sec + tv.tv_usec / 1000000.0;
      if (now - ed.status_time >= 3.0) {
        ed.status[0] = '\0';
      }
    }
    refresh_screen(&ed);
    process_keypress(&ed);
  }

  shutdown_editor(&ed);
  return 0;
}
