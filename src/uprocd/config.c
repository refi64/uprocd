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

      if (strcmp(key, "ProcessName") == 0) {
        cfg->process_name = sdsdup(value);
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
        break;
      }

      PARSE_ERROR("Invalid key '%S'", key);
      goto parse_end;
    }

    parse_end:
    sdsfree(line);
    line = NULL;
    if (key) {
      sdsfree(key);
    }
    if (value) {
      sdsfree(value);
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

void config_free(config *cfg) {
  if (cfg->path) {
    sdsfree(cfg->path);
  }

  switch (cfg->kind) {
  case CONFIG_NATIVE_MODULE:
    if (cfg->native.native_lib) {
      sdsfree(cfg->native.native_lib);
    }
    break;
  case CONFIG_DERIVED_MODULE:
    break;
  }

  free(cfg);
}
