#ifndef RUNNER_H
#define RUNNER_H

#include "buffer.h"

typedef enum {
  RUN_OK             = 0,
  RUN_COMPILE_ERROR  = 1,
  RUN_EXEC_ERROR     = 2,
  RUN_INTERNAL_ERROR = 3
} RunResult;

typedef struct {
  TextBuffer* buf;
  char*       partial;
  void (*on_progress)(void* ctx);
  void* progress_ctx;
  void* multi;
  void* curl;
  long  http_code;
} FetchStream;

RunResult runner_smart_run(const char* file_path, const char* lang_override, char** output);
int       runner_fetch_url(const char* url, char** content);
int       runner_fetch_url_stream(const char* url, TextBuffer* buf, void (*on_progress)(void* ctx),
                                  void* ctx);

int  runner_fetch_stream_start(FetchStream* fs, const char* url, TextBuffer* buf,
                               void (*on_progress)(void* ctx), void* progress_ctx);
int  runner_fetch_stream_poll(FetchStream* fs);
void runner_fetch_stream_free(FetchStream* fs);

#endif
