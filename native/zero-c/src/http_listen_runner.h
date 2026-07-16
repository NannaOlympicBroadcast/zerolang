#ifndef ZERO_C_HTTP_LISTEN_RUNNER_H
#define ZERO_C_HTTP_LISTEN_RUNNER_H

#include "zero.h"

#include <stdint.h>

typedef struct {
  const char *zero_exe;
  const char *input;
  const char *target;
  const char *profile;
  const char *backend;
  const char *cc;
  const char *handler;
  uint16_t port;
  bool auto_increment_port;
} ZHttpListenRunConfig;

int z_http_listen_run(const ZHttpListenRunConfig *config, ZDiag *diag);

#endif
