/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <assert.h>

#include "common.h"

void * alloc(size_t sz) {
  void *res = calloc(sz, 1);
  assert(res != NULL);
  return res;
}

/* static const char* error_messages[] = { */
/*   [ERR_SUCCESS] = "No error", */
/*   [ERR_MISC] = "Unknown error", */
/*   [ERR_DL] = "Unkown dl error", */
/* }; */

/* const char* error_reason(const error *perr) { */
/*   if (perr->what == ERR_PARSE) { */
/*     return sdscatfmt(sdsempty(), "Parse error: line %i: %S", perr->parse.line, */
/*                      perr->reason); */
/*   } else if (perr->what == ERR_ERRNO) { */
/*     return strerror(perr->errno_); */
/*   } else { */
/*     return perr->reason ? perr->reason : error_messages[perr->what]; */
/*   } */
/* } */

/* void error_clear(error *perr) { */
/*   if  (perr->reason) { */
/*     sdsfree(perr->reason); */
/*     perr->reason = NULL; */
/*   } */
/* } */
