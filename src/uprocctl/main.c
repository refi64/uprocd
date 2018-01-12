/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "common.h"

#include <systemd/sd-bus.h>

#include <unistd.h>

extern char **environ;
char *prog;

#define RUN_USAGE "run [-h] module [args...]"

void _fail(sds message) {
  fprintf(stderr, "%s\n", message);
  sdsfree(message);
}

#define FAIL(...) _fail(sdscatfmt(sdsempty(), __VA_ARGS__))

void usage() {
  printf("usage: %s -h\n", prog);
  printf("       %s " RUN_USAGE "\n", prog);
}

void run_usage() {
  printf("usage: %s " RUN_USAGE "\n", prog);
}

void help() {
  puts("uprocctl allows you to communicate with the uprocd modules.");
  puts("");
  puts("Commands:");
  puts("");
  puts("  run         Run a command through a uprocd module.");
}

void run_help() {
  puts("uprocctl run allows you to spawn commands via the uprocd modules.");
  puts("");
  puts("  -h          Show this screen.");
  puts("  module      The uprocd module to run.");
  puts("  [args...]   Command line arguments to pass to the module.");
}

int run(char *module, int argc, char **argv) {
  sd_bus *bus = NULL;
  sd_bus_message *msg = NULL, *reply = NULL;
  sd_bus_error err = SD_BUS_ERROR_NULL;
  int rc;

  char *cwd = getcwd(NULL, 0);
  if (cwd == NULL) {
    FAIL("Error retrieving current working directory: %s", strerror(errno));
    goto end;
  }

  rc = sd_bus_open_user(&bus);
  if (rc < 0) {
    FAIL("sd_bus_open_user failed: %s", strerror(-rc));
    goto end;
  }

  sds service, object;
  get_bus_params(module, &service, &object);

  rc = sd_bus_message_new_method_call(bus, &msg, service, object, service, "Run");
  if (rc < 0) {
    FAIL("sd_bus_message_new_method_call failed: %s", strerror(-rc));
    goto end;
  }

  rc = sd_bus_message_open_container(msg, 'a', "{ss}");
  if (rc < 0) {
    FAIL("sd_bus_message_open_container for env array failed: %s", strerror(-rc));
    goto end;
  }

  for (char **p = environ; *p; p++) {
    rc = sd_bus_message_open_container(msg, 'e', "ss");
    if (rc < 0) {
      FAIL("sd_bus_message_open_container for env entry failed: %s", strerror(-rc));
      goto end;
    }

    sds env = sdsnew(*p), key = sdsdup(env), value = sdsdup(env);
    sds eq = strchr(env, '=');
    sdsrange(key, 0, eq - env - 1);
    sdsrange(value, eq - env + 1, -1);

    rc = sd_bus_message_append(msg, "ss", key, value);
    sdsfree(key);
    sdsfree(value);
    if (rc < 0) {
      FAIL("sd_bus_message_append for env entries failed: %s", strerror(-rc));
      goto end;
    }

    rc = sd_bus_message_close_container(msg);
    if (rc < 0) {
      FAIL("sd_bus_message_close_container for env entry failed: %s", strerror(-rc));
      goto end;
    }
  }

  rc = sd_bus_message_close_container(msg);
  if (rc < 0) {
    FAIL("sd_bus_message_close_container for env array failed: %s", strerror(-rc));
    goto end;
  }

  rc = sd_bus_message_open_container(msg, 'a', "s");
  if (rc < 0) {
    FAIL("sd_bus_message_open_container for argv failed: %s", strerror(-rc));
    goto end;
  }

  for (int i = 0; i < argc; i++) {
    rc = sd_bus_message_append_basic(msg, 's', argv[i]);
    if (rc < 0) {
      FAIL("sd_bus_message_append_basic for argv failed: %s", strerror(-rc));
      goto end;
    }
  }

  rc = sd_bus_message_close_container(msg);
  if (rc < 0) {
    FAIL("sd_bus_message_close_container for argv failed: %s", strerror(-rc));
    goto end;
  }

  rc = sd_bus_message_append(msg, "sx", cwd, (int64_t)getpid());
  if (rc < 0) {
    FAIL("sd_bus_message_append for cwd+pid failed: %s", strerror(-rc));
    goto end;
  }

  rc = sd_bus_call(bus, msg, 0, &err, &reply);
  if (rc < 0) {
    FAIL("sd_bus_call failed: %s", err.message);
    FAIL("Are you sure the uprocd module has been started?");
    goto end;
  }

  end:
  if (cwd) {
    free(cwd);
  }
  sd_bus_error_free(&err);
  if (msg) {
    sd_bus_message_unref(msg);
  }
  if (bus) {
    sd_bus_unref(bus);
  }
  return rc < 0 ? 1 : 0;
}

int main(int argc, char **argv) {
  prog = argv[0];
  int opt;

  if (argc < 2) {
    FAIL("An argument is required.");
    usage();
    return 1;
  }

  if (strcmp(argv[1], "-h") == 0) {
    usage();
    putchar('\n');
    help();
    return 0;
  } else if (strcmp(argv[1], "run") == 0) {
    if (argc < 3) {
      FAIL("run requires an argument.");
      run_usage();
      return 1;
    } else if (strcmp(argv[2], "-h") == 0) {
      run_usage();
      putchar('\n');
      run_help();
      return 0;
    } else {
      return run(argv[2], argc - 3, argv + 3);
    }
  } else {
    FAIL("Invalid command.");
    usage();
    return 1;
  }
}
