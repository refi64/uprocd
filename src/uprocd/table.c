/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "uprocd.h"

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
