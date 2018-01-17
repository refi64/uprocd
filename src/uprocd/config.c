/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "common.h"
#include "private.h"

user_type *user_type_clone(user_type *type) {
  user_type *res = new(user_type);
  res->kind = type->kind;
  if (type->kind == TYPE_LIST) {
    res->child = user_type_clone(type->child);
  }
  return res;
}

void user_type_free(user_type *type) {
  if (type->kind == TYPE_LIST && type->child) {
    user_type_free(type->child);
  }
  free(type);
}

user_value *user_value_parse(sds name, sds value, user_type *type) {
  user_value *res = new(user_value);
  char *ep;
  sds *parts;

  switch (type->kind) {
  case TYPE_NONE: abort();
  case TYPE_NUMBER:
    res->number = strtod(value, &ep);
    if (*ep) {
      FAIL("Error parsing value of %S: Invalid number.", name);
      free(res);
      return NULL;
    }
    break;
  case TYPE_STRING:
    res->string = sdsdup(value);
    break;
  case TYPE_LIST:
    parts = sdssplitlen(value, sdslen(value), " ", 1, &res->list.len);
    res->list.items = newa(user_value*, res->list.len);
    for (int i = 0; i < res->list.len; i++) {
      user_value *child = user_value_parse(name, parts[i], type->child);
      if (child == NULL) {
        user_value_free(res);
        return NULL;
      }
      res->list.items[i] = child;
    }
    sdsfreesplitres(parts, res->list.len);
    break;
  }

  res->type = user_type_clone(type);
  return res;
}

void user_value_free(user_value *usr) {
  if (usr->type) {
    if (usr->type->kind == TYPE_STRING) {
      sdsfree(usr->string);
    } else if (usr->type->kind == TYPE_LIST) {
      for (int i = 0; i < usr->list.len; i++) {
        if (usr->list.items[i]) {
          user_value_free(usr->list.items[i]);
        }
      }
    }
  }

  free(usr);
}

void config_move_out_values(config *cfg, table *values) {
  *values = cfg->native.values;
  table_init(&cfg->native.values);
}

void config_free(config *cfg) {
  if (cfg->path) {
    sdsfree(cfg->path);
  }
  if (cfg->process_name) {
    sdsfree(cfg->process_name);
  }
  if (cfg->description) {
    sdsfree(cfg->description);
  }

  char *arg = NULL;

  switch (cfg->kind) {
  case CONFIG_NATIVE_MODULE:
    if (cfg->native.native_lib) {
      sdsfree(cfg->native.native_lib);
    }

    user_type *type;
    while ((arg = table_next(&cfg->native.props, arg, (void**)&type))) {
      user_type_free(type);
    }
    table_free(&cfg->native.props);

    user_value *usr;
    arg = NULL;
    while ((arg = table_next(&cfg->native.values, arg, (void**)&usr))) {
      user_value_free(usr);
    }
    table_free(&cfg->native.values);
    break;
  case CONFIG_DERIVED_MODULE:
    if (cfg->derived.base) {
      sdsfree(cfg->derived.base);
    }

    sds value;
    while ((arg = table_next(&cfg->derived.value_strings, arg, (void**)&value))) {
      sdsfree(value);
    }
    table_free(&cfg->derived.value_strings);
    break;
  }

  free(cfg);
}
