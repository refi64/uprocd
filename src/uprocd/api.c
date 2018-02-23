/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "private.h"
#include "uprocd.h"

#include <systemd/sd-bus.h>

#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <unistd.h>

extern char **environ;

UPROCD_EXPORT const char * uprocd_module_directory() {
  return global_run_data.module_dir;
}

UPROCD_EXPORT char * uprocd_module_path(const char *path) {
  return sdscatfmt(sdsempty(), "%S/%s", global_run_data.module_dir, path);
}

UPROCD_EXPORT void uprocd_module_path_free(char *path) {
  sdsfree(path);
}

static user_value *config_get(const char *key) {
  return table_get(&global_run_data.config, key);
}

static int is_index_valid(user_value *usr, int index) {
  return index > 0 && index < usr->list.len;
}

UPROCD_EXPORT int uprocd_config_present(const char *key) {
  return config_get(key) != NULL;
}

UPROCD_EXPORT int uprocd_config_list_size(const char *key) {
  user_value *usr = config_get(key);
  return usr ? usr->list.len : -1;
}

UPROCD_EXPORT double uprocd_config_number(const char *key) {
  user_value *usr = config_get(key);
  return usr ? usr->number : 0;
}

UPROCD_EXPORT double uprocd_config_number_at(const char *list,
                                             int index) {
  user_value *usr = config_get(list);
  return usr && is_index_valid(usr, index) ? usr->list.items[index]->number : 0;
}

UPROCD_EXPORT const char *uprocd_config_string(const char *key) {
  user_value *usr = config_get(key);
  return usr ? usr->string : NULL;
}

UPROCD_EXPORT const char *uprocd_config_string_at(const char *list,
                                                  int index) {
  user_value *usr = config_get(list);
  return usr && is_index_valid(usr, index) ? usr->list.items[index]->string : NULL;
}

struct uprocd_context {
  int argc;
  sds *argv, *env;
  sds cwd;
  int fds[3], pid;
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
  close(ctx->fds[0]);
  close(ctx->fds[1]);
  close(ctx->fds[2]);
  free(ctx);
}

static void move_cgroups(int64_t origin) {
  sd_bus *bus = NULL;
  sd_bus_message *msg = NULL;
  sd_bus_error err = SD_BUS_ERROR_NULL;
  int rc;

  rc = sd_bus_open_system(&bus);
  if (rc < 0) {
    FAIL("sd_bus_open_system failed: %s", strerror(-rc));
    goto end;
  }

  int64_t pid = getpid();

  rc = sd_bus_call_method(bus, "com.refi64.uprocd.Cgrmvd",
                          "/com/refi64/uprocd/Cgrmvd", "com.refi64.uprocd.Cgrmvd",
                          "MoveCgroup", &err, &msg, "xx", pid, origin);
  if (rc < 0) {
    FAIL("Error calling com.refi64.uprocd.Cgrmvd.MoveCgroup: %s", err.message);
    goto end;
  }

  end:
  sd_bus_error_free(&err);
  sd_bus_message_unref(msg);
  sd_bus_unref(bus);
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

  if (chdir(ctx->cwd) == -1) {
    FAIL("WARNING: chdir into new cwd %S failed: %s", ctx->cwd, strerror(errno));
  }

  close(0);
  close(1);
  close(2);
  dup2(ctx->fds[0], 0);
  dup2(ctx->fds[1], 1);
  dup2(ctx->fds[2], 2);

  move_cgroups(ctx->pid);

  if (setpgrp() == -1) {
    FAIL("WARNING: setpgrp failed: %s", strerror(errno));
  }
}

sds * convert_env_to_api_format(table *penv) {
  sds *entries = newa(sds, penv->sz * 2 + 1), *current = entries;
  char *name = NULL, *value;
  while ((name = table_next(penv, name, (void**)&value))) {
    *(current++) = sdsnew(name);
    *(current++) = sdsnew(value);
  }

  *current = NULL;
  return entries;
}

int prepare_context_and_fork(int argc, char **argv, table *env, char *cwd, int *fds,
                             pid_t pid) {
  int wait_for_set_ptracer[2];
  if (pipe(wait_for_set_ptracer) == -1) {
    FAIL("Error creating pipe to wait for prctl: %s", strerror(errno));
    return -errno;
  }

  sds *entries = convert_env_to_api_format(env);

  uprocd_context *ctx = new(uprocd_context);
  ctx->argc = argc;
  ctx->argv = argv;
  ctx->env = entries;
  ctx->cwd = sdsnew(cwd);
  ctx->fds[0] = dup(fds[0]);
  ctx->fds[1] = dup(fds[1]);
  ctx->fds[2] = dup(fds[2]);
  ctx->pid = pid;
  global_run_data.upcoming_context = ctx;

  pid_t child = fork();
  if (child == -1) {
    FAIL("fork failed: %s", strerror(errno));
    return -errno;
  } else if (child == 0) {
    prctl(PR_SET_PTRACER, pid, 0, 0);
    ioctl(0, TIOCSCTTY, 1);

    write(wait_for_set_ptracer[1], "", 1);
    close(wait_for_set_ptracer[0]);
    close(wait_for_set_ptracer[1]);

    setproctitle("-uprocd:%s", global_run_data.module);
    signal(SIGINT, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);

    return 0;
  } else {
    char byte;
    read(wait_for_set_ptracer[0], &byte, 1);
    close(wait_for_set_ptracer[0]);
    close(wait_for_set_ptracer[1]);

    return child;
  }
}

UPROCD_EXPORT void uprocd_on_exit(uprocd_exit_handler func, void *userdata) {
  global_run_data.exit_handler = func;
  global_run_data.exit_handler_userdata = userdata;
}

UPROCD_EXPORT uprocd_context * uprocd_run() {
  int rc;
  bus_data *data = NULL;

  if (setjmp(global_run_data.return_to_loop) != 0) {
    bus_free(data);
    return global_run_data.upcoming_context;
  }

  data = bus_new();
  if (data == NULL) {
    goto failure;
  }

  for (;;) {
    rc = bus_pump(data);
    if (rc < 0) {
      goto failure;
    }
    else if (rc == 1) {
      free(global_run_data.upcoming_context);
      global_run_data.upcoming_context = NULL;
    }
  }

  failure:
  bus_free(data);
  if (global_run_data.exit_handler) {
    uprocd_exit_handler handler = global_run_data.exit_handler;
    handler(global_run_data.exit_handler_userdata);
  }
  longjmp(global_run_data.return_to_main, 1);
}
