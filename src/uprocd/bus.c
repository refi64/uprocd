/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "private.h"

#include <systemd/sd-bus.h>

int service_method_status(sd_bus_message *msg, void *data, sd_bus_error *err) {
  char *name = global_run_data.module;
  char *description = global_run_data.description ? global_run_data.description :
                      "<none>";

  return sd_bus_reply_method_return(msg, "ss", name, description);
}

int service_method_run(sd_bus_message *msg, void *data, sd_bus_error *err) {
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

  int child = prepare_context_and_fork(argc, argv, &env, cwd, fds, pid);
  if (child < 0) {
    sd_bus_message_unref(msg);
    return child;
  }

  if (child == 0) {
    sd_bus_message_unref(msg);
    longjmp(global_run_data.return_to_loop, 1);
  } else {
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

struct bus_data {
  sd_bus *bus;
  sd_bus_slot *slot;
  sds service, object;
};

bus_data * bus_new() {
  int rc;
  bus_data *data = new(bus_data);

  rc = sd_bus_open_user(&data->bus);
  if (rc < 0) {
    FAIL("sd_bus_open_user failed: %s", strerror(-rc));
    goto failure;
  }

  get_bus_params(global_run_data.module, &data->service, &data->object);
  rc = sd_bus_add_object_vtable(data->bus, &data->slot, data->object, data->service,
                                service_vtable, NULL);
  if (rc < 0) {
    FAIL("sd_bus_add_object_vtable failed: %s", strerror(-rc));
    goto failure;
  }

  rc = sd_bus_request_name(data->bus, data->service, 0);
  if (rc < 0) {
    FAIL("sd_bus_request_name failed: %s", strerror(-rc));
    goto failure;
  }

  return data;

  failure:
  bus_free(data);
  return NULL;
}

int bus_pump(bus_data *data) {
  int rc;
  rc = sd_bus_process(data->bus, NULL);
  if (rc < 0) {
    FAIL("sd_bus_process failed: %s", strerror(-rc));
    return -1;
  } else if (rc > 0) {
    return 1;
  }

  rc = sd_bus_wait(data->bus, (uint64_t)-1);
  if (rc < 0 && rc != -EINTR) {
    FAIL("sd_bus_wait failed: %s", strerror(-rc));
    return -1;
  }

  return 0;
}

void bus_free(bus_data *data) {
  if (data == NULL) {
    return;
  }

  sd_bus_slot_unref(data->slot);
  sd_bus_unref(data->bus);
  if (data->service) {
    sdsfree(data->service);
  }
  if (data->object) {
    sdsfree(data->object);
  }

  free(data);
}
