/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "common.h"

#include <systemd/sd-bus.h>

#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
int64_t target_pid = -1;

#define STATUS_USAGE "status [-h] module"
#define RUN_USAGE "run [-h] module [args...]"
#define U_USAGE "[-h] module [args...]"

void _fail(sds message) {
  fprintf(stderr, "uprocctl: %s\n", message);
  sdsfree(message);
}

#define FAIL(...) _fail(sdscatfmt(sdsempty(), __VA_ARGS__))

char * last_path_component(char *path) {
  char *p = strrchr(path, '/');
  return p ? p + 1 : path;
}

void usage() {
  puts("usage: uprocctl -h");
  puts("       uprocctl " STATUS_USAGE);
  puts("       uprocctl " RUN_USAGE);
  puts("       u " U_USAGE);
}

void status_usage() {
  puts("usage: uprocctl " STATUS_USAGE);
}

void run_usage() {
  puts("usage: uprocctl " RUN_USAGE);
}

void u_usage() {
  puts("usage: u " U_USAGE);
}

void help() {
  puts("uprocctl allows you to communicate with uprocd modules.");
  puts("");
  puts("Commands:");
  puts("");
  puts("  status      Show the status of a uprocd module.");
  puts("  run         Run a command through a uprocd module.");
  puts("");
  puts("The u command is a shortcut for uprocctl run.");
}

void status_help() {
  puts("uprocctl status shows the status of the given uprocd module.");
  puts("");
  puts("  -h       Show this screen.");
  puts("  module   The uprocd module to retrieve information for.");
}

void run_help_base(int u) {
  puts("uprocctl run allows you to spawn commands via the uprocd modules.");
  if (u) {
    puts("u is a shortcut for uprocctl run.");
  }
  puts("");
  puts("  -h          Show this screen.");
  puts("  module      The uprocd module to run.");
  puts("  [args...]   Command line arguments to pass to the module.");
}

void run_help() {
  run_help_base(0);
}

void u_help() {
  run_help_base(1);
}

void killme(int sig) {
  signal(sig, SIG_DFL);
  if (raise(sig) != 0) {
    FAIL("raise failed: %s", strerror(errno));
    abort();
  }
}

int status(const char *module) {
  sd_bus *bus = NULL;
  sd_bus_message *msg = NULL, *reply = NULL;
  sd_bus_error err = SD_BUS_ERROR_NULL;
  int rc;

  rc = sd_bus_open_user(&bus);
  if (rc < 0) {
    FAIL("sd_bus_open_user failed: %s", strerror(-rc));
    goto end;
  }

  sds service, object;
  get_bus_params(module, &service, &object);

  rc = sd_bus_message_new_method_call(bus, &msg, service, object, service, "Status");
  if (rc < 0) {
    FAIL("sd_bus_message_new_method_call failed: %s", strerror(-rc));
    goto end;
  }

  rc = sd_bus_call(bus, msg, 0, &err, &reply);
  if (rc < 0) {
    FAIL("sd_bus_call failed: %s", err.message);
    FAIL("Are you sure the uprocd module has been started?");
    goto end;
  }

  char *name, *description;
  rc = sd_bus_message_read(reply, "ss", &name, &description);
  if (rc < 0) {
    FAIL("uprocd process bus failed to return status information.");
    goto end;
  }

  printf("Name:          %s\n", name);
  printf("Description:   %s\n", description);

  end:
  sd_bus_error_free(&err);
  if (msg) {
    sd_bus_message_unref(msg);
  }
  if (bus) {
    sd_bus_unref(bus);
  }

  if (rc < 0) {
    return 1;
  } else {
    return 0;
  }
}

int wait_for_process() {
  if (ptrace(PTRACE_SEIZE, target_pid, NULL,
             (void*)(PTRACE_O_EXITKILL | PTRACE_O_TRACEEXIT)) == -1) {
    FAIL("Error seizing %I via ptrace: %s", target_pid, strerror(errno));
    return 1;
  }

  for (;;) {
    int wstatus;
    if (waitpid(target_pid, &wstatus, 0) == -1) {
      FAIL("waitpid: %s", strerror(errno));
      return 1;
    }

    if (wstatus >> 8 == (SIGTRAP | (PTRACE_EVENT_EXIT << 8))) {
      unsigned long pstatus;
      if (ptrace(PTRACE_GETEVENTMSG, target_pid, NULL, &pstatus) == -1) {
        FAIL("PTRACE_GETEVENTMSG failed: %s", strerror(errno));
        return 0;
      }

      return WEXITSTATUS(pstatus);
    } else if (WIFSTOPPED(wstatus) || WIFSIGNALED(wstatus)) {
      killme(WIFSTOPPED(wstatus) ? WSTOPSIG(wstatus) : WTERMSIG(wstatus));
      return 0;
    } else if (WIFEXITED(wstatus)) {
      return WEXITSTATUS(wstatus);
    } else {
      FAIL("waitpid gave unexpected status: %d\n", wstatus);
    }
  }
}

void forward_signal(int sig) {
  if (target_pid < 1) {
    FAIL("target_pid == %I in forward_signal", target_pid);
    abort();
  }
  kill(target_pid, sig);
}

int run(char *module, int argc, char **argv) {
  sd_bus *bus = NULL;
  sd_bus_message *msg = NULL, *reply = NULL;
  sd_bus_error err = SD_BUS_ERROR_NULL;
  int rc;
  char *title;

  char *cwd = getcwd(NULL, 0);
  if (cwd == NULL) {
    FAIL("Error retrieving current working directory: %s", strerror(errno));
    rc = -errno;
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
    goto write_end;
  }

  for (char **p = environ; *p; p++) {
    rc = sd_bus_message_open_container(msg, 'e', "ss");
    if (rc < 0) {
      goto write_end;
    }

    sds env = sdsnew(*p), key = sdsdup(env), value = sdsdup(env);
    sds eq = strchr(env, '=');
    sdsrange(key, 0, eq - env - 1);
    sdsrange(value, eq - env + 1, -1);

    rc = sd_bus_message_append(msg, "ss", key, value);
    sdsfree(key);
    sdsfree(value);
    if (rc < 0) {
      goto write_end;
    }

    rc = sd_bus_message_close_container(msg);
    if (rc < 0) {
      goto write_end;
    }
  }

  rc = sd_bus_message_close_container(msg);
  if (rc < 0) {
    goto write_end;
  }

  rc = sd_bus_message_open_container(msg, 'a', "s");
  if (rc < 0) {
    goto write_end;
  }

  for (int i = 0; i < argc; i++) {
    rc = sd_bus_message_append_basic(msg, 's', argv[i]);
    if (rc < 0) {
      goto write_end;
    }
  }

  rc = sd_bus_message_close_container(msg);
  if (rc < 0) {
    goto write_end;
  }

  rc = sd_bus_message_append(msg, "s(hhh)x", cwd, dup(0), dup(1), dup(2), getpid());
  if (rc < 0) {
    goto write_end;
  }

  write_end:
  if (rc < 0) {
    FAIL("Error writing bus message: %s", strerror(-rc));
    goto end;
  }

  rc = sd_bus_call(bus, msg, 0, &err, &reply);
  if (rc < 0) {
    if (strcmp(err.name, SD_BUS_ERROR_SERVICE_UNKNOWN) == 0) {
      FAIL("Failed to locate %s's D-Bus service.", module);
      FAIL("Are you sure it has been started? (Try systemctl --user status uprocd@%s.)",
           module);
    } else {
      FAIL("sd_bus_call failed: %s", err.message);
      FAIL("Are you sure the uprocd module has been started?");
    }
    goto end;
  }

  rc = sd_bus_message_read(reply, "xs", &target_pid, &title);
  if (rc < 0) {
    FAIL("uprocd process bus failed to return the new PID.");
    goto end;
  }

  setproctitle("-%s", title);

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

  if (rc < 0) {
    return 1;
  } else {
    for (int sig = 0; sig < 31; sig++) {
      if (sig == SIGCHLD) {
        continue;
      }
      signal(sig, forward_signal);
    }

    return wait_for_process();
  }
}

int check_command(const char *command, int argc, char **argv, int exact,
                  void (*usage)(), void (*help)()) {
  int correct_number = exact ? argc == 1 : argc >= 1;
  if (!correct_number) {
    if (exact) {
      FAIL("%s requires exactly one argument.", command);
    } else {
      FAIL("%s requires at least one argument.", command);
    }
    usage();
    return -1;
  }

  int is_help = strcmp(argv[0], "-h") == 0;
  if (argv[0][0] == '-' && !is_help) {
    FAIL("Invalid argument: %s.", argv[0]);
    usage();
    return -1;
  }

  if (is_help) {
    usage();
    putchar('\n');
    help();
    return 1;
  }

  return 0;
}

int main(int argc, char **argv) {
  setproctitle_init(argc, argv);

  char *name = last_path_component(argv[0]);

  if (strcmp(name, "u") == 0) {
    int rc = check_command("u", argc - 1, argv + 1, 0, u_usage, u_help);
    if (rc) {
      return rc < 0 ? 1 : 0;
    }
    return run(argv[1], argc - 2, argv + 2);
  } else if (strncmp(name, "u", 1) == 0 && strcmp(name, "uprocctl") != 0) {
    return run(name + 1, argc - 1, argv + 1);
  } else {
    if (strcmp(name, "uprocctl") != 0) {
      FAIL("WARNING: argv[0] is not a recognized uprocd symlink. Assuming uprocctl...");
    }

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
    } else if (strcmp(argv[1], "status") == 0) {
      int rc = check_command("status", argc - 2, argv + 2, 1, status_usage, status_help);
      if (rc) {
        return rc < 0 ? 1 : 0;
      }
      return status(argv[2]);
    } else if (strcmp(argv[1], "run") == 0) {
      int rc = check_command("run", argc - 2, argv + 2, 0, run_usage, run_help);
      if (rc) {
        return rc < 0 ? 1 : 0;
      }
      return run(argv[2], argc - 3, argv + 3);
    } else {
      FAIL("Invalid command.");
      usage();
      return 1;
    }
  }
}
