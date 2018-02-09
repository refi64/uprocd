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

int readline(FILE *fp, sds *pline) {
  sds result = sdsempty();
  *pline = NULL;
  char buf[128];
  buf[0] = 0;
  for (;;) {
    if (fgets(buf, sizeof(buf), fp) == NULL) {
      if (ferror(fp)) {
        int errno_ = errno;
        sdsfree(result);
        return -errno_;
      } else if (feof(fp)) {
        break;
      }
    }

    result = sdscat(result, buf);
    if (result[sdslen(result) - 1] == '\n') {
      break;
    }
  }
  if (sdslen(result) == 0) {
    sdsfree(result);
    return 0;
  }
  sdstrim(result, "\n");
  *pline = result;
  return 0;
}

void table_init(table *tbl) {
  tbl->p = NULL;
  tbl->sz = 0;
}

void table_add(table *tbl, const char *key, void *value) {
  Word_t *pvalue;
  JSLI(pvalue, tbl->p, (const uint8_t*)key);
  *pvalue = (Word_t)value;
  tbl->sz++;
}

void * table_get(table *tbl, const char *key) {
  Word_t *pvalue;
  JSLG(pvalue, tbl->p, (const uint8_t*)key);
  if (pvalue) {
    return *(void**)pvalue;
  } else {
    return NULL;
  }
}

void * table_swap(table *tbl, const char *key, void *value) {
  void *orig = table_get(tbl, key);
  table_add(tbl, key, value);
  return orig;
}

int table_del(table *tbl, const char *key) {
  int rc;
  JSLD(rc, tbl->p, (const uint8_t*)key);
  if (rc) {
    tbl->sz--;
  }
  return rc;
}

char * table_next(table *tbl, char *prev, void **value) {
  Word_t *pvalue;
  uint8_t idx[4096];
  if (prev == NULL) {
    idx[0] = 0;
    JSLF(pvalue, tbl->p, idx);
  } else {
    strncpy((char*)idx, prev, sizeof(idx));
    JSLN(pvalue, tbl->p, idx);
  }
  free(prev);
  if (!pvalue) {
    return NULL;
  }
  size_t len = strlen((char*)idx);
  char *res = alloc(len + 1);
  memcpy(res, idx, len + 1);
  if (value) {
    *value = (void*)*pvalue;
  }
  return res;
}

void table_free(table *tbl) {
  JudySLFreeArray((PPvoid_t)&tbl->p, PJE0);
}
