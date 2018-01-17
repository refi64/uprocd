/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "common.h"

#include <sys/prctl.h>

void * alloc(size_t sz) {
  void *res = calloc(sz, 1);
  if (res == NULL) {
    fputs("Out of memory.", stderr);
    abort();
  }
  return res;
}

void * ralloc(void *p, size_t sz) {
  void *res = realloc(p, sz);
  if (res == NULL) {
    fputs("Out of memory.", stderr);
    free(p);
    abort();
  }
  return res;
}

void get_bus_params(const char *module, sds *pservice, sds *pobject) {
  *pservice = sdscat(sdsnew("com.refi64.uprocd.modules."), module);
  *pobject = sdscat(sdsnew("/com/refi64/uprocd/modules/"), module);
}

static char **g_argv;

void __setproctitle_init(char **argv) {
  g_argv = argv;
}

void setproctitle(const char *fmt, ...) {
  sds name;
  if (fmt[0] == '-') {
    fmt++;
    name = sdsempty();
  } else {
    name = sdscat(sdsnew(g_argv[0]), " ");
  }

  va_list args;
  va_start(args, fmt);
  name = sdscatvprintf(name, fmt, args);
  va_end(args);

  prctl(PR_SET_NAME, name, NULL, NULL, NULL);
  sdsfree(name);
}
