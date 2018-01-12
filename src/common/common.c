/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "common.h"

#include <bsd/stdlib.h>

void * alloc(size_t sz) {
  void *res = calloc(sz, 1);
  if (res == NULL) {
    fputs("Out of memory.", stderr);
    abort();
  }
  return res;
}

void * ralloc(void *p, size_t sz) {
  p = reallocf(p, sz);
  if (p == NULL) {
    fputs("Out of memory.", stderr);
    abort();
  }
  return p;
}

sds get_bus_params(const char *module, sds *pservice, sds *pobject) {
  *pservice = sdscat(sdsnew("com.refi64.uprocd.modules."), module);
  *pobject = sdscat(sdsnew("/com/refi64/uprocd/modules/"), module);
}
