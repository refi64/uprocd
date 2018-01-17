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

user_value *config_get(const char *key) {
  return table_get(&global_run_data.config, key);
}

UPROCD_EXPORT int uprocd_config_list_size(const char *key) {
  return config_get(key)->list.len;
}

UPROCD_EXPORT double uprocd_config_number(const char *key) {
  return config_get(key)->number;
}

UPROCD_EXPORT double uprocd_config_number_at(const char *list,
                                             int index) {
  return config_get(list)->list.items[index]->number;
}

UPROCD_EXPORT const char *uprocd_config_string(const char *key) {
  return config_get(key)->string;
}

UPROCD_EXPORT const char *uprocd_config_string_at(const char *list,
                                                  int index) {
  return config_get(list)->list.items[index]->string;
}

struct uprocd_context {
  int argc;
  sds *argv, *env;
  sds cwd;
  int fds[3];
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

  close(0);
  close(1);
  close(2);
  dup2(ctx->fds[0], 0);
  dup2(ctx->fds[1], 1);
  dup2(ctx->fds[2], 2);

  setsid();
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

int service_method_status(sd_bus_message *msg, void *data, sd_bus_error *buserr) {
  char *name = global_run_data.module;
  char *description = global_run_data.description ? global_run_data.description :
                      "<none>";

  return sd_bus_reply_method_return(msg, "ss", name, description);
}

int service_method_run(sd_bus_message *msg, void *data, sd_bus_error *buserr) {
  char *title = global_run_data.process_name ? global_run_data.process_name :
                global_run_data.module;

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

  int argc = 1;
  sds *argv = new(sds);
  argv[0] = sdsnew(title);

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
  int fds[3];
  int64_t pid;
  rc = sd_bus_message_read(msg, "s(hhh)x", &cwd, &fds[0], &fds[1], &fds[2], &pid);
  if (rc < 0) {
    goto read_end;
  }

  read_end:
  if (rc < 0) {
    FAIL("Error parsing bus message: %s", strerror(-rc));
    return rc;
  }

  int wait_for_set_ptracer[2];
  if (pipe(wait_for_set_ptracer) == -1) {
    FAIL("Error creating pipe to wait for prctl: %s", strerror(errno));
    return -errno;
  }

  sds *entries = convert_env_to_api_format(&env);

  uprocd_context *ctx = new(uprocd_context);
  ctx->argc = argc;
  ctx->argv = argv;
  ctx->env = entries;
  ctx->cwd = sdsnew(cwd);
  ctx->fds[0] = dup(fds[0]);
  ctx->fds[1] = dup(fds[1]);
  ctx->fds[2] = dup(fds[2]);
  global_run_data.upcoming_context = ctx;

  pid_t child = fork();
  if (child == -1) {
    FAIL("fork failed: %s", strerror(errno));
    sd_bus_message_unref(msg);
    return -errno;
  } else if (child == 0) {
    prctl(PR_SET_PTRACER, pid, 0, 0);
    prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0);
    write(wait_for_set_ptracer[1], "", 1);
    close(wait_for_set_ptracer[0]);
    close(wait_for_set_ptracer[1]);

    setproctitle("-uprocd@%s", global_run_data.module);

    sd_bus_message_unref(msg);
    longjmp(global_run_data.return_to_loop, 1);
  } else {
    char byte;
    read(wait_for_set_ptracer[0], &byte, 1);
    close(wait_for_set_ptracer[0]);
    close(wait_for_set_ptracer[1]);

    return sd_bus_reply_method_return(msg, "xs", child, title);
  }
}

static const sd_bus_vtable service_vtable[] = {
  SD_BUS_VTABLE_START(0),
  // Status() -> String name, String description
  SD_BUS_METHOD("Status", "", "ss", service_method_status,
                SD_BUS_VTABLE_UNPRIVILEGED),
  // Run(Array<DictEntry<String>> env, Array<String> argv, String cwd,
  //     Tuple<Fd, Fd, Fd> ttys, Int64 uprocctl_pid) -> Int64 pid, String name
  SD_BUS_METHOD("Run", "a{ss}ass(hhh)x", "xs",
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
    if (rc < 0 && rc != -EINTR) {
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
