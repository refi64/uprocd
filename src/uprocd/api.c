/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "uprocd.h"
#include "api/uprocd.h"

#include <bsd/stdlib.h>
#include <systemd/sd-bus.h>

#include <fcntl.h>
#include <unistd.h>

extern char **environ;

struct uprocd_context {
  int argc;
  sds *argv;
  sds *env;
  sds cwd;
  sds ttys[3];
};

UPROCD_EXPORT void uprocd_context_get_args(uprocd_context *ctx, int *pargc,
                                           char ***pargv) {
  *pargc = ctx->argc;
  *pargv = ctx->argv;
}

UPROCD_EXPORT const char ** uprocd_context_get_env(uprocd_context *ctx) {
  return (const char **)ctx->env;
}

UPROCD_EXPORT const char * uprocd_context_get_cwd(uprocd_context *ctx) {
  return ctx->cwd;
}

UPROCD_EXPORT void uprocd_context_free(uprocd_context *ctx) {
  for (sds *p = ctx->env; *p; p++) {
    sdsfree(*p);
  }
  free(ctx->env);
  for (int i = 0; i < ctx->argc; i++) {
    sdsfree(ctx->argv[i]);
  }
  free(ctx->argv);
  sdsfree(ctx->cwd);
  sdsfree(ctx->ttys[0]);
  sdsfree(ctx->ttys[1]);
  sdsfree(ctx->ttys[2]);
  free(ctx);
}

UPROCD_EXPORT void uprocd_context_enter(uprocd_context *ctx) {
  for (char **p = environ; *p; p++) {
    sds env = sdsnew(*p);
    sds eq = strchr(env, '=');
    sdsrange(env, 0, eq - env - 1);
    unsetenv(env);
    sdsfree(env);
  }

  for (sds *p = ctx->env; *p; p+=2) {
    setenv(p[0], p[1], 1);
  }

  chdir(ctx->cwd);

  int stdin_fd = -1, stdout_fd = -1, stderr_fd = -1;

  stdin_fd = open(ctx->ttys[0], O_RDONLY);
  if (stdin_fd == -1) {
    FAIL("Error opening stdin at %S: %s", ctx->ttys[0], strerror(errno));
    goto afterfd;
  }
  stdout_fd = open(ctx->ttys[1], O_WRONLY);
  if (stdout_fd == -1) {
    FAIL("Error opening stdout at %S: %s", ctx->ttys[1], strerror(errno));
    close(stdin_fd);
    goto afterfd;
  }
  stderr_fd = open(ctx->ttys[2], O_WRONLY);
  if (stderr_fd == -1) {
    FAIL("Error opening stderr at %S: %s", ctx->ttys[2], strerror(errno));
    close(stdin_fd);
    close(stdout_fd);
    goto afterfd;
  }

  afterfd:
  if (stdin_fd == -1 || stdout_fd == -1 || stderr_fd == -1) {
    return;
  }

  close(0);
  close(1);
  close(2);
  dup2(stdin_fd, 0);
  dup2(stdout_fd, 1);
  dup2(stderr_fd, 2);
}

sds * convert_env_to_api_format(table *penv) {
  sds *entries = newa(sds, penv->sz), *current = entries;
  char *name = NULL, *value;
  while ((name = table_next(penv, name, (void**)&value))) {
    *(current++) = sdsnew(name);
    *(current++) = sdsnew(value);
  }

  *current = NULL;
  return entries;
}

int service_method_run(sd_bus_message *msg, void *data, sd_bus_error *buserr) {
  int rc;
  table env;
  table_init(&env);

  rc = sd_bus_message_enter_container(msg, 'a', "{ss}");
  if (rc < 0) {
    goto read_end;
  }

  while ((rc = sd_bus_message_enter_container(msg, 'e', "ss")) > 0) {
    char *name, *value;
    rc = sd_bus_message_read(msg, "ss", &name, &value);
    if (rc < 0) {
      goto read_end;
    }
    table_add(&env, name, value);

    rc = sd_bus_message_exit_container(msg);
    if (rc < 0) {
      goto read_end;
    }
  }

  rc = sd_bus_message_exit_container(msg);
  if (rc < 0) {
    goto read_end;
  }

  rc = sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "s");
  if (rc < 0) {
    goto read_end;
  }

  int argc = 0;
  sds *argv = NULL;
  char *arg;
  while ((rc = sd_bus_message_read(msg, "s", &arg)) > 0) {
    argc++;
    argv = ralloc(argv, argc * sizeof(sds));
    argv[argc - 1] = sdsnew(arg);
  }

  rc = sd_bus_message_exit_container(msg);
  if (rc < 0) {
    goto read_end;
  }

  char *cwd;
  rc = sd_bus_message_read_basic(msg, 's', &cwd);
  if (rc < 0) {
    goto read_end;
  }

  char *ttys[3];
  rc = sd_bus_message_read(msg, "(sss)", &ttys[0], &ttys[1], &ttys[2]);
  if (rc < 0) {
    goto read_end;
  }

  read_end:
  if (rc < 0) {
    FAIL("Error parsing bus message: %s", strerror(-rc));
    return rc;
  }

  sds *entries = convert_env_to_api_format(&env);

  uprocd_context *ctx = new(uprocd_context);
  ctx->argc = argc;
  ctx->argv = argv;
  ctx->env = entries;
  ctx->cwd = sdsnew(cwd);
  ctx->ttys[0] = sdsnew(ttys[0]);
  ctx->ttys[1] = sdsnew(ttys[1]);
  ctx->ttys[2] = sdsnew(ttys[2]);
  global_run_data.upcoming_context = ctx;

  pid_t child = fork();
  if (child == -1) {
    FAIL("fork failed: %s", strerror(errno));
    sd_bus_message_unref(msg);
    return -errno;
  } else if (child == 0) {
    sd_bus_message_unref(msg);
    longjmp(global_run_data.return_to_loop, 1);
  } else {
    return sd_bus_reply_method_return(msg, "x", child);
  }
}

static const sd_bus_vtable service_vtable[] = {
  SD_BUS_VTABLE_START(0),
  SD_BUS_METHOD("Run", UPROCD_DBUS_RUN_ARGUMENTS, UPROCD_DBUS_RUN_RETURN,
                service_method_run, SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_VTABLE_END
};

UPROCD_EXPORT void uprocd_on_exit(uprocd_exit_handler func, void *userdata) {
  global_run_data.exit_handler = func;
  global_run_data.exit_handler_userdata = userdata;
}

UPROCD_EXPORT uprocd_context * uprocd_run() {
  int rc;
  sd_bus *bus = NULL;
  sd_bus_slot *slot = NULL;

  if (setjmp(global_run_data.return_to_loop) != 0) {
    sd_bus_slot_unref(slot);
    sd_bus_unref(bus);
    return global_run_data.upcoming_context;
  }

  rc = sd_bus_open_user(&bus);
  if (rc < 0) {
    FAIL("sd_bus_open_user failed: %s", strerror(-rc));
    goto failure;
  }

  sds service, object;
  get_bus_params(global_run_data.module, &service, &object);
  rc = sd_bus_add_object_vtable(bus, &slot, object, service, service_vtable, NULL);
  if (rc < 0) {
    FAIL("sd_bus_add_object_vtable failed: %s", strerror(-rc));
    goto failure;
  }

  rc = sd_bus_request_name(bus, service, 0);
  if (rc < 0) {
    FAIL("sd_bus_request_name failed: %s", strerror(-rc));
    goto failure;
  }

  for (;;) {
    rc = sd_bus_process(bus, NULL);
    if (rc < 0) {
      FAIL("sd_bus_process failed: %s", strerror(-rc));
      goto failure;
    } else if (rc > 0) {
      free(global_run_data.upcoming_context);
      global_run_data.upcoming_context = NULL;
      continue;
    }

    rc = sd_bus_wait(bus, (uint64_t)-1);
    if (rc < 0) {
      FAIL("sd_bus_wait failed: %s", strerror(-rc));
      goto failure;
    }
  }

  failure:
  if (slot) {
    sd_bus_slot_unref(slot);
  }
  if (bus) {
    sd_bus_unref(bus);
  }
  if (global_run_data.exit_handler) {
    uprocd_exit_handler handler = global_run_data.exit_handler;
    handler(global_run_data.exit_handler_userdata);
  }
  longjmp(global_run_data.return_to_main, 1);
}
