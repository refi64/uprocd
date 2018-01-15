/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "common.h"
#include "uprocd.h"

static int readline(FILE *fp, sds *pline) {
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

config *config_parse(const char *path) {
  FILE *fp = fopen(path, "r");
  if (fp == NULL) {
    FAIL("Error opening config file: %s", strerror(errno));
    return NULL;
  }

  config *cfg = new(config);
  sds line = NULL, cursect = NULL, key = NULL, value = NULL;

  int lineno = 0, rc = 0;

  #define PARSE_ERROR(fmt, ...) do { \
    FAIL("Error parsing %s:%i: " fmt, path, lineno, ##__VA_ARGS__); \
    rc = 1; \
  } while (0)

  while ((rc = readline(fp, &line)) == 0 && line != NULL) {
    lineno++;
    sdstrim(line, "\t ");

    size_t len = sdslen(line);
    if (len == 0 || line[0] == '#') {
      goto parse_end;
    }

    if (line[0] == '[') {
      if (line[len - 1] != ']') {
        PARSE_ERROR("Expected right bracket to end line");
        goto parse_end;
      }

      sdsrange(line, 1, -2);
      cursect = sdsdup(line);

      int isnative = strcmp(cursect, "NativeModule") == 0,
          isderived = strcmp(cursect, "DerivedModule") == 0;
      if (isnative || isderived) {
        if (cfg->kind) {
          PARSE_ERROR("Duplicate module declaration");
          goto parse_end;
        }

        cfg->kind = isnative ? CONFIG_NATIVE_MODULE : CONFIG_DERIVED_MODULE;
        goto parse_end;
      } else if (strcmp(cursect, "Arguments") == 0 ||
                 strcmp(cursect, "Defaults") == 0) {
        if (cfg->kind != CONFIG_NATIVE_MODULE) {
          PARSE_ERROR("%S can only be used with a NativeModule.", cursect);
          goto parse_end;
        }
      } else {
        PARSE_ERROR("Invalid section '%S'", cursect);
        goto parse_end;
      }
    } else {
      char *eq = memchr(line, '=', len);
      if (eq == NULL) {
        PARSE_ERROR("Invalid line");
        goto parse_end;
      }

      key = sdsdup(line), value = sdsdup(line);
      sdsrange(key, 0, eq - line - 1);
      sdsrange(value, eq - line + 1, -1);

      if (!cfg->kind) {
        PARSE_ERROR("Key '%S' outside section", key);
        goto parse_end;
      }

      if (strcmp(cursect, "NativeModule") == 0 ||
          strcmp(cursect, "DerivedModule") == 0) {
        if (strcmp(key, "ProcessName") == 0) {
          cfg->process_name = sdsdup(value);
          goto parse_end;
        } else if (strcmp(key, "Description") == 0) {
          cfg->description = sdsdup(value);
          goto parse_end;
        }

        switch (cfg->kind) {
        case CONFIG_NATIVE_MODULE:
          if (strcmp(key, "NativeLib") == 0) {
            cfg->native.native_lib = sdsdup(value);
            goto parse_end;
          }
          break;
        case CONFIG_DERIVED_MODULE:
          if (strcmp(key, "Base") == 0) {
            cfg->derived.base = sdsdup(value);
            goto parse_end;
          } else {
            table_add(&cfg->derived.value_strings, key, sdsdup(value));
            goto parse_end;
          }
        }
      } else if (strcmp(cursect, "Arguments") == 0) {
        user_type *type = new(user_type), *current = type;
        size_t vlen = sdslen(value);
        int islist = 0;

        while (strncmp(value, "list ", 5) == 0) {
          if (islist) {
            PARSE_ERROR("Nested lists are not allowed");
            user_type_free(type);
            goto parse_end;
          }
          islist = 1;
          sdsrange(value, 5, -1);

          type->kind = TYPE_LIST;
          type->child = new(user_type);
          current = type->child;
        }

        if (strcmp(value, "string") == 0) {
          current->kind = TYPE_STRING;
        } else if (strcmp(value, "number") == 0) {
          current->kind = TYPE_NUMBER;
        } else {
          PARSE_ERROR("Invalid type: '%S'", value);
          user_type_free(type);
          goto parse_end;
        }

        table_add(&cfg->native.arguments, key, type);
        goto parse_end;
      } else if (strcmp(cursect, "Defaults") == 0) {
        user_type *type = table_get(&cfg->native.arguments, key);
        if (type == NULL) {
          PARSE_ERROR("Unknown key: '%S'", key);
          goto parse_end;
        }

        user_value *usr = user_value_parse(key, value, type);
        if (usr == NULL) {
          rc = 1;
          goto parse_end;
        }

        table_add(&cfg->native.values, key, usr);
        goto parse_end;
      }

      PARSE_ERROR("Invalid key '%S'", key);
      goto parse_end;
    }

    parse_end:
    sdsfree(line);
    line = NULL;
    if (key) {
      sdsfree(key);
      key = NULL;
    }
    if (value) {
      sdsfree(value);
      value = NULL;
    }
    if (rc != 0) {
      break;
    }
  }

  fclose(fp);
  if (line) {
    sdsfree(line);
  }
  if (cursect) {
    sdsfree(cursect);
  }

  if (cfg->kind == CONFIG_DERIVED_MODULE && !cfg->derived.base) {
    PARSE_ERROR("DerivedModule needs a Base.");
  }

  if (rc != 0) {
    if (rc < 0) {
      FAIL("Error reading %s: %s", path, strerror(-rc));
    }
    config_free(cfg);
    return NULL;
  }

  cfg->path = sdsnew(path);
  return cfg;
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
    while ((arg = table_next(&cfg->native.arguments, arg, (void**)&type))) {
      user_type_free(type);
    }
    table_free(&cfg->native.arguments);

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
