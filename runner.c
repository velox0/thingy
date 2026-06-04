#include "runner.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <curl/curl.h>

static char *dup_str(const char *s) {
  size_t len = strlen(s);
  char *copy = malloc(len + 1);
  if (!copy) return NULL;
  memcpy(copy, s, len + 1);
  return copy;
}

static char *fmt_str(const char *fmt, ...) {
  va_list ap;
  va_list copy;
  int needed;
  char *out;

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

static char *shell_quote(const char *s) {
  size_t i;
  size_t len = 2;
  char *out;
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
  out[at] = '\0';
  return out;
}

static int append_chunk(char **dst, size_t *len, size_t *cap, const char *chunk, size_t chunk_len) {
  char *new_buf;
  size_t new_cap;

  if (*dst == NULL) {
    *cap = 256;
    while (*cap < chunk_len + 1) *cap *= 2;
    *dst = malloc(*cap);
    if (!*dst) return -1;
    (*dst)[0] = '\0';
    *len = 0;
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

static int run_command_capture(const char *command, char **output, int *exit_code) {
  FILE *pipe;
  char buf[1024];
  size_t out_len = 0;
  size_t out_cap = 0;
  int status;

  *output = NULL;
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

static char *binary_name_from_path(const char *path) {
  const char *base = strrchr(path, '/');
  const char *name = base ? base + 1 : path;
  const char *dot = strrchr(name, '.');
  size_t raw_len = dot ? (size_t)(dot - name) : strlen(name);
  size_t i;
  char *out;

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

static const char *interpreter_for_extension(const char *ext) {
  if (!ext) return NULL;
  if (strcmp(ext, ".py") == 0) return "python3";
  if (strcmp(ext, ".sh") == 0) return "sh";
  if (strcmp(ext, ".js") == 0) return "node";
  if (strcmp(ext, ".rb") == 0) return "ruby";
  if (strcmp(ext, ".php") == 0) return "php";
  if (strcmp(ext, ".pl") == 0) return "perl";
  return NULL;
}

static char *combine_output(const char *a, const char *b) {
  if ((!a || a[0] == '\0') && (!b || b[0] == '\0')) return dup_str("(no output)\n");
  if (!a || a[0] == '\0') return dup_str(b);
  if (!b || b[0] == '\0') return dup_str(a);
  return fmt_str("%s%s", a, b);
}

static const char *skip_ws(const char *s) {
  while (*s && isspace((unsigned char)*s)) s++;
  return s;
}

static int file_looks_like_c(const char *file_path) {
  FILE *fp;
  char line[1024];
  int score = 0;
  int scanned = 0;

  fp = fopen(file_path, "r");
  if (!fp) return 0;

  while (fgets(line, sizeof(line), fp) != NULL) {
    const char *p = skip_ws(line);
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

RunResult runner_smart_run(const char *file_path, const char *lang_override, char **output) {
  const char *ext;
  char cwd[PATH_MAX];
  char *q_file = NULL;
  char *q_cwd = NULL;
  char *cmd = NULL;
  char *run_out = NULL;
  int treat_as_c = 0;
  int run_code = 1;

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
  q_cwd = shell_quote(cwd);
  if (!q_file || !q_cwd) {
    free(q_file);
    free(q_cwd);
    *output = dup_str("Out of memory.\n");
    return RUN_INTERNAL_ERROR;
  }

  if (treat_as_c) {
    char *binary_name = NULL;
    char *binary_path = NULL;
    char *q_binary = NULL;
    char *compile_cmd = NULL;
    char *compile_out = NULL;
    int compile_code = 1;
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
    q_binary = shell_quote(binary_path);
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
    result = (run_code == 0) ? RUN_OK : RUN_EXEC_ERROR;

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
    const char *interpreter = NULL;
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
    }
    if (interpreter) {
      cmd = fmt_str("cd %s && %s %s 2>&1", q_cwd, interpreter, q_file);
    } else {
      cmd = fmt_str("cd %s && sh %s 2>&1", q_cwd, q_file);
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

static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
  char **buf = (char **)userdata;
  size_t new_len = strlen(*buf) + size * nmemb;
  char *tmp = realloc(*buf, new_len + 1);
  if (!tmp) return 0;
  *buf = tmp;
  memcpy(*buf + new_len - size * nmemb, ptr, size * nmemb);
  (*buf)[new_len] = '\0';
  return size * nmemb;
}

int runner_fetch_url(const char *url, char **content) {
  CURL *curl;
  CURLcode res;
  long http_code = 0;

  *content = dup_str("");
  if (!*content) return -1;

  curl = curl_easy_init();
  if (!curl) return -1;

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, content);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

  res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    free(*content);
    *content = fmt_str("Network error: %s\n", curl_easy_strerror(res));
    curl_easy_cleanup(curl);
    return -1;
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_easy_cleanup(curl);

  if (http_code != 200) {
    free(*content);
    *content = fmt_str("HTTP error: %ld\n", http_code);
    return -1;
  }
  return 0;
}
