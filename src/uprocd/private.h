/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef PRIVATE_H
#define PRIVATE_H

#include "common.h"

#include <setjmp.h>

void _message(int failure, sds error);

#define _MESSAGE(failure, ...) _message(failure, sdscatfmt(sdsempty(), __VA_ARGS__))
#define INFO(...) _MESSAGE(0, __VA_ARGS__)
#define FAIL(...) _MESSAGE(1, __VA_ARGS__)

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
      struct user_value **items;
      int len;
    } list;
    sds string;
    double number;
  };
} user_value;

user_value *user_value_parse(sds name, sds value, user_type *type);
void user_value_free(user_value *usr);

typedef struct config {
  enum { CONFIG_NATIVE_MODULE = 1, CONFIG_DERIVED_MODULE } kind;
  sds path, process_name, description;
  union {
    struct {
      sds native_lib;
      table props, values;
    } native;
    struct {
      sds base;
      table value_strings;
    } derived;
  };
} config;

config *config_parse(const char *path);
void config_move_out_values(config *cfg, table *values);
void config_free(config *cfg);

int prepare_context_and_fork(int argc, char **argv, table *env, char *cwd, int *fds,
                             pid_t pid);
typedef struct bus_data bus_data;
bus_data * bus_new();
int bus_pump(bus_data *data);
void bus_free(bus_data *data);

struct {
  char *module;
  sds module_dir;
  sds process_name, description;
  table config;
  jmp_buf return_to_main, return_to_loop;
  void *exit_handler, *exit_handler_userdata;
  void *upcoming_context;
} global_run_data;

#endif

