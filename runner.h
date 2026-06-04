#ifndef RUNNER_H
#define RUNNER_H

typedef enum {
  RUN_OK = 0,
  RUN_COMPILE_ERROR = 1,
  RUN_EXEC_ERROR = 2,
  RUN_INTERNAL_ERROR = 3
} RunResult;

RunResult runner_smart_run(const char *file_path, const char *lang_override, char **output);

#endif
