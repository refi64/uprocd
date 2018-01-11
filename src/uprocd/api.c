/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "uprocd.h"
#include "api/uprocd.h"

#include <systemd/sd-bus.h>

#include <unistd.h>

extern char **environ;

struct uprocd_context {
  sds *env;
  sds cwd;
  int64_t pid;
};

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
  sdsfree(ctx->cwd);
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

  rc = sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "{ss}");
  if (rc < 0) {
    goto read_end;
  }

  while ((rc = sd_bus_message_enter_container(msg, SD_BUS_TYPE_DICT_ENTRY, "ss")) > 0) {
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

  char *cwd;
  int64_t pid;
  rc = sd_bus_message_read(msg, "sx", &cwd, &pid);
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
  ctx->env = entries;
  ctx->cwd = sdsnew(cwd);
  ctx->pid = pid;
  global_run_data.upcoming_context = ctx;

  pid_t child = fork();
  if (child == -1) {
    FAIL("fork failed: %s", strerror(errno));
    return -errno;
  } else if (child == 0) {
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

  sds service = sdscatfmt(sdsnew(UPROCD_SERVICE_PREFIX), ".%s", global_run_data.module),
      object = sdscatfmt(sdsnew(UPROCD_OBJECT_PREFIX), "/%s", global_run_data.module);
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
