#include "runner.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>

static char* dup_str(const char* s) {
  size_t len  = strlen(s);
  char*  copy = malloc(len + 1);
  if (!copy) return NULL;
  memcpy(copy, s, len + 1);
  return copy;
}

static char* fmt_str(const char* fmt, ...) {
  va_list ap;
  va_list copy;
  int     needed;
  char*   out;

  va_start(ap, fmt);
  va_copy(copy, ap);
  needed = vsnprintf(NULL, 0, fmt, copy);
  va_end(copy);
  if (needed < 0) {
    va_end(ap);
    return NULL;
  }

  out = malloc((size_t)needed + 1);
  if (!out) {
    va_end(ap);
    return NULL;
  }
  vsnprintf(out, (size_t)needed + 1, fmt, ap);
  va_end(ap);
  return out;
}

static char* shell_quote(const char* s) {
  size_t i;
  size_t len = 2;
  char*  out;
  size_t at = 0;

  for (i = 0; s[i]; i++) {
    len += (s[i] == '\'') ? 4 : 1;
  }

  out = malloc(len + 1);
  if (!out) return NULL;
  out[at++] = '\'';
  for (i = 0; s[i]; i++) {
    if (s[i] == '\'') {
      out[at++] = '\'';
      out[at++] = '\\';
      out[at++] = '\'';
      out[at++] = '\'';
    } else {
      out[at++] = s[i];
    }
  }
  out[at++] = '\'';
  out[at]   = '\0';
  return out;
}

static int append_chunk(char** dst, size_t* len, size_t* cap, const char* chunk, size_t chunk_len) {
  char*  new_buf;
  size_t new_cap;

  if (*dst == NULL) {
    *cap = 256;
    while (*cap < chunk_len + 1) *cap *= 2;
    *dst = malloc(*cap);
    if (!*dst) return -1;
    (*dst)[0] = '\0';
    *len      = 0;
  } else if (*len + chunk_len + 1 > *cap) {
    new_cap = *cap;
    while (new_cap < *len + chunk_len + 1) new_cap *= 2;
    new_buf = realloc(*dst, new_cap);
    if (!new_buf) return -1;
    *dst = new_buf;
    *cap = new_cap;
  }

  memcpy(*dst + *len, chunk, chunk_len);
  *len += chunk_len;
  (*dst)[*len] = '\0';
  return 0;
}

static int run_command_capture(const char* command, char** output, int* exit_code) {
  FILE*  pipe;
  char   buf[1024];
  size_t out_len = 0;
  size_t out_cap = 0;
  int    status;

  *output    = NULL;
  *exit_code = 1;

  pipe = popen(command, "r");
  if (!pipe) return -1;

  while (fgets(buf, sizeof(buf), pipe) != NULL) {
    if (append_chunk(output, &out_len, &out_cap, buf, strlen(buf)) != 0) {
      pclose(pipe);
      free(*output);
      *output = NULL;
      return -1;
    }
  }

  status = pclose(pipe);
  if (status >= 0 && WIFEXITED(status)) {
    *exit_code = WEXITSTATUS(status);
  }

  if (!*output) *output = dup_str("");
  return *output ? 0 : -1;
}

static char* binary_name_from_path(const char* path) {
  const char* base    = strrchr(path, '/');
  const char* name    = base ? base + 1 : path;
  const char* dot     = strrchr(name, '.');
  size_t      raw_len = dot ? (size_t)(dot - name) : strlen(name);
  size_t      i;
  char*       out;

  if (raw_len == 0) raw_len = strlen(name);
  if (raw_len == 0) return dup_str("thingy_run");

  out = malloc(raw_len + 1);
  if (!out) return NULL;
  for (i = 0; i < raw_len; i++) {
    char c = name[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
      out[i] = c;
    } else {
      out[i] = '_';
    }
  }
  out[raw_len] = '\0';
  return out;
}

static const char* interpreter_for_extension(const char* ext) {
  if (!ext) return NULL;
  if (strcmp(ext, ".py") == 0) return "python3";
  if (strcmp(ext, ".sh") == 0) return "sh";
  if (strcmp(ext, ".js") == 0) return "node";
  if (strcmp(ext, ".rb") == 0) return "ruby";
  if (strcmp(ext, ".php") == 0) return "php";
  if (strcmp(ext, ".pl") == 0) return "perl";
  return NULL;
}

static const char* interpreter_from_shebang(const char* file_path) {
  FILE* fp;
  char  line[1024];
  char* nl;
  const char* shebang;
  const char* prog;

  fp = fopen(file_path, "r");
  if (!fp) return NULL;

  if (!fgets(line, sizeof(line), fp)) {
    fclose(fp);
    return NULL;
  }
  fclose(fp);

  /* strip trailing newline */
  nl = strchr(line, '\n');
  if (nl) *nl = '\0';
  nl = strchr(line, '\r');
  if (nl) *nl = '\0';

  if (strncmp(line, "#!", 2) != 0) return NULL;
  shebang = line + 2;

  /* skip leading spaces */
  while (*shebang == ' ' || *shebang == '\t') shebang++;

  /* #!/usr/bin/env <prog> — extract prog */
  prog = strstr(shebang, "/env ");
  if (prog) {
    prog += 5;
    while (*prog == ' ' || *prog == '\t') prog++;
    /* skip "python" -> "python3" if it's just "python" */
    if (strcmp(prog, "python") == 0 || strcmp(prog, "python2") == 0) return "python3";
    if (strcmp(prog, "python3") == 0) return "python3";
    if (strcmp(prog, "node") == 0 || strcmp(prog, "nodejs") == 0) return "node";
    if (strcmp(prog, "ruby") == 0) return "ruby";
    if (strcmp(prog, "php") == 0) return "php";
    if (strcmp(prog, "perl") == 0) return "perl";
    if (strcmp(prog, "sh") == 0 || strcmp(prog, "bash") == 0 || strcmp(prog, "zsh") == 0) return "sh";
    return NULL;
  }

  /* #!/bin/<prog> — extract basename */
  prog = strrchr(shebang, '/');
  if (prog) {
    prog++;
  } else {
    prog = shebang;
  }

  if (strcmp(prog, "python") == 0 || strcmp(prog, "python2") == 0) return "python3";
  if (strcmp(prog, "python3") == 0) return "python3";
  if (strcmp(prog, "node") == 0 || strcmp(prog, "nodejs") == 0) return "node";
  if (strcmp(prog, "ruby") == 0) return "ruby";
  if (strcmp(prog, "php") == 0) return "php";
  if (strcmp(prog, "perl") == 0) return "perl";
  if (strcmp(prog, "sh") == 0 || strcmp(prog, "bash") == 0 || strcmp(prog, "zsh") == 0) return "sh";

  return NULL;
}

static char* combine_output(const char* a, const char* b) {
  if ((!a || a[0] == '\0') && (!b || b[0] == '\0')) return dup_str("(no output)\n");
  if (!a || a[0] == '\0') return dup_str(b);
  if (!b || b[0] == '\0') return dup_str(a);
  return fmt_str("%s%s", a, b);
}

static const char* skip_ws(const char* s) {
  while (*s && isspace((unsigned char)*s)) s++;
  return s;
}

static int file_looks_like_c(const char* file_path) {
  FILE* fp;
  char  line[1024];
  int   score   = 0;
  int   scanned = 0;

  fp = fopen(file_path, "r");
  if (!fp) return 0;

  while (fgets(line, sizeof(line), fp) != NULL) {
    const char* p = skip_ws(line);
    scanned++;
    if (strncmp(p, "#include <", 10) == 0 || strncmp(p, "#include \"", 10) == 0) score += 2;
    if (strstr(p, "int main(") || strstr(p, "void main(")) score += 3;
    if (strstr(p, "printf(")) score += 1;
    if (strchr(p, ';')) score += 1;
    if (score >= 4) break;
    if (scanned >= 120) break;
  }

  fclose(fp);
  return score >= 4;
}

RunResult runner_smart_run(const char* file_path, const char* lang_override, char** output) {
  const char* ext;
  char        cwd[PATH_MAX];
  char*       q_file     = NULL;
  char*       q_cwd      = NULL;
  char*       cmd        = NULL;
  char*       run_out    = NULL;
  int         treat_as_c = 0;
  int         run_code   = 1;

  *output = NULL;
  if (!file_path || file_path[0] == '\0') {
    *output = dup_str("No file selected.\n");
    return RUN_INTERNAL_ERROR;
  }

  ext = strrchr(file_path, '.');
  if (lang_override && lang_override[0]) {
    treat_as_c = (strcmp(lang_override, "c") == 0);
  } else {
    treat_as_c = (ext && strcmp(ext, ".c") == 0) || file_looks_like_c(file_path);
  }
  if (!getcwd(cwd, sizeof(cwd))) {
    *output = fmt_str("Could not get working directory: %s\n", strerror(errno));
    return RUN_INTERNAL_ERROR;
  }

  q_file = shell_quote(file_path);
  q_cwd  = shell_quote(cwd);
  if (!q_file || !q_cwd) {
    free(q_file);
    free(q_cwd);
    *output = dup_str("Out of memory.\n");
    return RUN_INTERNAL_ERROR;
  }

  if (treat_as_c) {
    char*     binary_name  = NULL;
    char*     binary_path  = NULL;
    char*     q_binary     = NULL;
    char*     compile_cmd  = NULL;
    char*     compile_out  = NULL;
    int       compile_code = 1;
    RunResult result;

    if (mkdir("/tmp/thingy_bin", 0755) != 0 && errno != EEXIST) {
      *output = fmt_str("Could not create /tmp/thingy_bin: %s\n", strerror(errno));
      free(q_file);
      free(q_cwd);
      return RUN_INTERNAL_ERROR;
    }

    binary_name = binary_name_from_path(file_path);
    if (!binary_name) {
      *output = dup_str("Out of memory.\n");
      free(q_file);
      free(q_cwd);
      return RUN_INTERNAL_ERROR;
    }

    binary_path = fmt_str("/tmp/thingy_bin/%s", binary_name);
    q_binary    = shell_quote(binary_path);
    if (ext && strcmp(ext, ".c") == 0) {
      compile_cmd = fmt_str("gcc %s -o %s 2>&1", q_file, q_binary);
    } else {
      compile_cmd = fmt_str("gcc -x c %s -o %s 2>&1", q_file, q_binary);
    }
    if (!binary_path || !q_binary || !compile_cmd) {
      free(binary_name);
      free(binary_path);
      free(q_binary);
      free(compile_cmd);
      free(q_file);
      free(q_cwd);
      *output = dup_str("Out of memory.\n");
      return RUN_INTERNAL_ERROR;
    }

    if (run_command_capture(compile_cmd, &compile_out, &compile_code) != 0) {
      free(binary_name);
      free(binary_path);
      free(q_binary);
      free(compile_cmd);
      free(q_file);
      free(q_cwd);
      *output = dup_str("Failed to invoke compiler.\n");
      return RUN_INTERNAL_ERROR;
    }

    if (compile_code != 0) {
      *output = combine_output(compile_out, "");
      free(compile_out);
      free(binary_name);
      free(binary_path);
      free(q_binary);
      free(compile_cmd);
      free(q_file);
      free(q_cwd);
      return RUN_COMPILE_ERROR;
    }

    cmd = fmt_str("cd %s && %s 2>&1", q_cwd, q_binary);
    if (!cmd || run_command_capture(cmd, &run_out, &run_code) != 0) {
      free(binary_name);
      free(binary_path);
      free(q_binary);
      free(compile_cmd);
      free(compile_out);
      free(cmd);
      free(q_file);
      free(q_cwd);
      *output = dup_str("Failed to execute compiled binary.\n");
      return RUN_INTERNAL_ERROR;
    }

    *output = combine_output(compile_out, run_out);
    result  = (run_code == 0) ? RUN_OK : RUN_EXEC_ERROR;

    free(binary_name);
    free(binary_path);
    free(q_binary);
    free(compile_cmd);
    free(compile_out);
    free(cmd);
    free(run_out);
    free(q_file);
    free(q_cwd);
    return result;
  }

  {
    const char* interpreter = NULL;
    if (lang_override && lang_override[0]) {
      if (strcmp(lang_override, "python") == 0 || strcmp(lang_override, "py") == 0)
        interpreter = "python3";
      else if (strcmp(lang_override, "node") == 0 || strcmp(lang_override, "js") == 0)
        interpreter = "node";
      else if (strcmp(lang_override, "ruby") == 0 || strcmp(lang_override, "rb") == 0)
        interpreter = "ruby";
      else if (strcmp(lang_override, "php") == 0)
        interpreter = "php";
      else if (strcmp(lang_override, "perl") == 0 || strcmp(lang_override, "pl") == 0)
        interpreter = "perl";
      else if (strcmp(lang_override, "sh") == 0 || strcmp(lang_override, "bash") == 0)
        interpreter = "sh";
    } else {
      interpreter = interpreter_for_extension(ext);
      if (!interpreter) interpreter = interpreter_from_shebang(file_path);
    }
    if (interpreter) {
      cmd = fmt_str("cd %s && %s %s 2>&1", q_cwd, interpreter, q_file);
    } else {
      *output = fmt_str(
          "Unknown language for '%s'. Use --lang to specify, or add a shebang (#!) or a "
          "recognized extension (.py, .js, .rb, .php, .pl, .sh).\n",
          file_path);
      free(q_file);
      free(q_cwd);
      return RUN_INTERNAL_ERROR;
    }
    if (!cmd || run_command_capture(cmd, &run_out, &run_code) != 0) {
      free(cmd);
      free(q_file);
      free(q_cwd);
      free(run_out);
      *output = dup_str("Failed to execute file.\n");
      return RUN_INTERNAL_ERROR;
    }
  }

  *output = combine_output("", run_out);
  free(cmd);
  free(run_out);
  free(q_file);
  free(q_cwd);
  return (run_code == 0) ? RUN_OK : RUN_EXEC_ERROR;
}

static size_t write_callback(void* ptr, size_t size, size_t nmemb, void* userdata) {
  char** buf     = (char**)userdata;
  size_t new_len = strlen(*buf) + size * nmemb;
  char*  tmp     = realloc(*buf, new_len + 1);
  if (!tmp) return 0;
  *buf = tmp;
  memcpy(*buf + new_len - size * nmemb, ptr, size * nmemb);
  (*buf)[new_len] = '\0';
  return size * nmemb;
}

static const char* http_status_phrase(long code) {
  switch (code) {
    case 100:
      return "Continue";
    case 101:
      return "Switching Protocols";
    case 200:
      return "OK";
    case 201:
      return "Created";
    case 204:
      return "No Content";
    case 301:
      return "Moved Permanently";
    case 302:
      return "Found";
    case 303:
      return "See Other";
    case 304:
      return "Not Modified";
    case 307:
      return "Temporary Redirect";
    case 308:
      return "Permanent Redirect";
    case 400:
      return "Bad Request";
    case 401:
      return "Unauthorized";
    case 403:
      return "Forbidden";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 408:
      return "Request Timeout";
    case 429:
      return "Too Many Requests";
    case 500:
      return "Internal Server Error";
    case 502:
      return "Bad Gateway";
    case 503:
      return "Service Unavailable";
    case 504:
      return "Gateway Timeout";
    default:
      return NULL;
  }
}

static int should_retry_http(long code) {
  return code == 500 || code == 502 || code == 503 || code == 504;
}

static int should_retry_network(CURLcode code) {
  return code == CURLE_COULDNT_CONNECT || code == CURLE_COULDNT_RESOLVE_HOST ||
         code == CURLE_OPERATION_TIMEDOUT || code == CURLE_SEND_ERROR || code == CURLE_RECV_ERROR;
}

typedef struct {
  char location[2048];
  int  got_location;
  int  redirect_count;
  int  verbose;
} RedirectHeaderData;

static size_t redirect_header_cb(char* buffer, size_t size, size_t nitems, void* userdata) {
  RedirectHeaderData* data  = (RedirectHeaderData*)userdata;
  size_t              total = size * nitems;

  if (total > 10 && strncasecmp(buffer, "Location: ", 10) == 0) {
    size_t val_len = total - 10;
    if (val_len >= sizeof(data->location)) val_len = sizeof(data->location) - 1;
    memcpy(data->location, buffer + 10, val_len);
    data->location[val_len] = '\0';
    while (val_len > 0 &&
           (data->location[val_len - 1] == '\r' || data->location[val_len - 1] == '\n'))
      data->location[--val_len] = '\0';
    data->got_location = 1;
    data->redirect_count++;
    if (data->verbose) {
      fprintf(stderr, "thingy:   -> %s\n", data->location);
    }
  }
  return total;
}

static void verbose_print(int verbose, const char* fmt, ...) {
  va_list ap;
  if (!verbose) return;
  va_start(ap, fmt);
  fprintf(stderr, "thingy: ");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}

static void notify_status(const FetchOptions* opts, const char* fmt, ...) {
  va_list ap;
  if (!opts || !opts->on_status) return;
  va_start(ap, fmt);
  {
    char msg[512];
    vsnprintf(msg, sizeof(msg), fmt, ap);
    opts->on_status(opts->status_ctx, msg);
  }
  va_end(ap);
}

int runner_fetch_url(const char* url, char** content, const FetchOptions* opts) {
  CURL*           curl;
  CURLcode        res;
  long            http_code   = 0;
  int             attempt     = 0;
  int             max_retries = (opts && opts->max_retries > 0) ? opts->max_retries : 3;
  int             verbose     = (opts && opts->verbose);
  double          backoff     = 1.0;
  struct timespec ts;

  *content = dup_str("");
  if (!*content) return -1;

  for (attempt = 0; attempt <= max_retries; attempt++) {
    RedirectHeaderData redir = {{0}, 0, 0, verbose};

    if (attempt > 0) {
      verbose_print(verbose, "Retrying in %.0fs... (attempt %d/%d)", backoff, attempt + 1,
                    max_retries + 1);
      notify_status(opts, "Retrying (%d/%d)...", attempt + 1, max_retries + 1);
      ts.tv_sec  = (time_t)backoff;
      ts.tv_nsec = (long)((backoff - ts.tv_sec) * 1e9);
      nanosleep(&ts, NULL);
      free(*content);
      *content = dup_str("");
      if (!*content) return -1;
    } else {
      verbose_print(verbose, "GET %s", url);
      notify_status(opts, "Fetching %s ...", url);
    }

    curl = curl_easy_init();
    if (!curl) {
      free(*content);
      *content = dup_str("Failed to initialize curl.\n");
      return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "thingy/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, content);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, redirect_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &redir);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
      verbose_print(verbose, "Attempt %d failed: %s", attempt + 1, curl_easy_strerror(res));
      curl_easy_cleanup(curl);

      if (attempt < max_retries && should_retry_network(res)) {
        backoff *= 2.0;
        continue;
      }

      free(*content);
      *content = fmt_str("Network error fetching %s: %s\n", url, curl_easy_strerror(res));
      notify_status(opts, "Fetch failed: %s", curl_easy_strerror(res));
      return -1;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code == 200) {
      const char* phrase = http_status_phrase(http_code);
      verbose_print(verbose, "%ld %s", http_code, phrase ? phrase : "OK");
      notify_status(opts, "Fetched %s", url);
      return 0;
    }

    if (http_code != 200) {
      const char* phrase = http_status_phrase(http_code);
      verbose_print(verbose, "%ld %s", http_code, phrase ? phrase : "");
    }

    if (attempt < max_retries && should_retry_http(http_code)) {
      backoff *= 2.0;
      continue;
    }

    {
      const char* phrase = http_status_phrase(http_code);
      char*       hint   = dup_str("");
      if (http_code == 401 || http_code == 403) {
        free(hint);
        hint = dup_str(" (authentication may be required)");
      } else if (http_code == 404) {
        free(hint);
        hint = dup_str(" (resource not found)");
      } else if (http_code == 429) {
        free(hint);
        hint = dup_str(" (rate limited)");
      }
      free(*content);
      *content = fmt_str("HTTP %ld %s fetching %s%s\n", http_code, phrase ? phrase : "", url, hint);
      free(hint);
    }
    notify_status(opts, "HTTP %ld %s", http_code,
                  http_status_phrase(http_code) ? http_status_phrase(http_code) : "");
    return -1;
  }

  free(*content);
  *content = fmt_str("Failed after %d attempts\n", max_retries + 1);
  return -1;
}

/* libcurl write callback: called when data arrives from the network.
 * Appends the incoming chunk to fs->partial (an incomplete-line buffer),
 * then splits off any complete lines (terminated by '\n') into the
 * TextBuffer. Whatever remains in fs->partial is an incomplete line that
 * will be completed by the next chunk. Calls on_progress after each
 * chunk so the editor can refresh the display. */
static size_t stream_write_callback(void* ptr, size_t size, size_t nmemb, void* userdata) {
  FetchStream* fs        = (FetchStream*)userdata;
  size_t       chunk_len = size * nmemb;
  char*        combined;
  size_t       combined_len;
  char*        nl;

  /* Append new data to the partial buffer. */
  combined_len = strlen(fs->partial) + chunk_len;
  combined     = malloc(combined_len + 1);
  if (!combined) return 0;
  memcpy(combined, fs->partial, strlen(fs->partial));
  memcpy(combined + strlen(fs->partial), ptr, chunk_len);
  combined[combined_len] = '\0';
  free(fs->partial);
  fs->partial = combined;

  /* Extract every complete line (ending with '\n') into the buffer. */
  while ((nl = strchr(fs->partial, '\n')) != NULL) {
    size_t line_len = (size_t)(nl - fs->partial);
    char*  line     = malloc(line_len + 1);
    if (line) {
      memcpy(line, fs->partial, line_len);
      line[line_len] = '\0';
      ensure_line_capacity(fs->buf, fs->buf->line_count + 1);
      fs->buf->lines[fs->buf->line_count++] = line;
    }
    /* Keep the remainder after '\n' as the new partial. */
    {
      char* rest = dup_str(nl + 1);
      free(fs->partial);
      fs->partial = rest;
    }
  }

  if (fs->on_progress) fs->on_progress(fs->progress_ctx);
  return chunk_len;
}

int runner_fetch_stream_start(FetchStream* fs, const char* url, TextBuffer* buf,
                              void (*on_progress)(void* ctx), void* progress_ctx) {
  CURLM* multi;
  CURL*  curl;

  fs->buf          = buf;
  fs->partial      = dup_str("");
  fs->on_progress  = on_progress;
  fs->progress_ctx = progress_ctx;
  fs->http_code    = 0;

  curl = curl_easy_init();
  if (!curl) {
    free(fs->partial);
    fs->partial = NULL;
    return -1;
  }

  multi = curl_multi_init();
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "thingy/1.0");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fs);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_multi_add_handle(multi, curl);

  fs->multi = multi;
  fs->curl  = curl;
  return 0;
}

int runner_fetch_stream_poll(FetchStream* fs) {
  int      running = 0;
  CURLMsg* msg;

  if (!fs->multi) return -1;

  curl_multi_perform((CURLM*)fs->multi, &running);

  if (!running) {
    msg = curl_multi_info_read((CURLM*)fs->multi, &running);
    if (msg) curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &fs->http_code);

    if (fs->partial && fs->partial[0]) {
      ensure_line_capacity(fs->buf, fs->buf->line_count + 1);
      fs->buf->lines[fs->buf->line_count++] = fs->partial;
      fs->partial                           = NULL;
    }

    curl_multi_remove_handle((CURLM*)fs->multi, (CURL*)fs->curl);
    curl_multi_cleanup((CURLM*)fs->multi);
    curl_easy_cleanup((CURL*)fs->curl);
    fs->multi = NULL;
    fs->curl  = NULL;

    return (fs->http_code == 200) ? 0 : -1;
  }

  curl_multi_wait((CURLM*)fs->multi, NULL, 0, 10, NULL);
  return 1;
}

void runner_fetch_stream_free(FetchStream* fs) {
  if (fs->partial) {
    free(fs->partial);
    fs->partial = NULL;
  }
  if (fs->multi) {
    curl_multi_remove_handle((CURLM*)fs->multi, (CURL*)fs->curl);
    curl_multi_cleanup((CURLM*)fs->multi);
    fs->multi = NULL;
  }
  if (fs->curl) {
    curl_easy_cleanup((CURL*)fs->curl);
    fs->curl = NULL;
  }
}
