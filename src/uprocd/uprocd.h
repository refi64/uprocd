/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef UPROCD_H
#define UPROCD_H

#include "common.h"

#include <Judy.h>

#include <setjmp.h>

void _message(int failure, sds error);

#define _MESSAGE(failure, ...) _message(failure, sdscatfmt(sdsempty(), __VA_ARGS__))
#define INFO(...) _MESSAGE(0, __VA_ARGS__)
#define FAIL(...) _MESSAGE(1, __VA_ARGS__)

typedef struct {
  Pvoid_t p;
  size_t sz;
} table;

void table_init(table *tbl);
void table_add(table *tbl, const char *key, void *value);
void * table_get(table *tbl, const char *key);
char * table_next(table *tbl, char *prev, void **value);
void table_free(table *tbl);

typedef struct config {
  enum { CONFIG_NATIVE_MODULE = 1, CONFIG_DERIVED_MODULE } kind;
  sds path, process_name, description;
  union {
    struct {
      sds native_lib;
    } native;
  };
} config;

config *config_parse(const char *path);
void config_free(config *cfg);

struct {
  char *module;
  sds process_name, description;
  jmp_buf return_to_main, return_to_loop;
  void *exit_handler, *exit_handler_userdata;
  void *upcoming_context;
} global_run_data;

#endif

