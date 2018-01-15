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

typedef struct user_type {
  enum { TYPE_NONE, TYPE_LIST, TYPE_STRING, TYPE_NUMBER } kind;
  struct user_type *child;
} user_type;

user_type *user_type_clone(user_type *type);
void user_type_free(user_type *type);

typedef struct user_value {
  user_type *type;
  union {
    struct {
      struct user_value **values;
      int len;
    } list;
    sds string;
    double number;
  };
} user_value;

void user_value_free(user_value *value);

typedef struct config {
  enum { CONFIG_NATIVE_MODULE = 1, CONFIG_DERIVED_MODULE } kind;
  sds path, process_name, description;
  union {
    struct {
      sds native_lib;
      table arguments, values;
    } native;
    struct {
      sds base;
      table values;
    } derived;
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

