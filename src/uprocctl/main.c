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
char *prog;
int64_t target_pid = -1;

#define RUN_USAGE "run [-h] module [args...]"

void _fail(sds message) {
  fprintf(stderr, "uprocctl: %s\n", message);
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

int wait_for_process() {
  if (ptrace(PTRACE_SEIZE, target_pid, NULL, (void*)PTRACE_O_TRACEEXIT) == -1) {
    FAIL("Error seizing %I via ptrace: %s", target_pid, strerror(errno));
    return 1;
  }

  siginfo_t sig;
  if (waitid(P_PID, target_pid, &sig, WEXITED) == -1) {
    FAIL("Error waiting for %I: %s", target_pid, strerror(errno));
    return 1;
  }

  for (;;) {
    if (sig.si_code == CLD_EXITED) {
      return sig.si_status;
    } else if (sig.si_code == CLD_KILLED || sig.si_code == CLD_DUMPED) {
      return sig.si_status + 128;
    } else if (sig.si_code == CLD_TRAPPED) {
      if (sig.si_status == (SIGTRAP | (PTRACE_EVENT_EXIT<<8))) {
        unsigned long status;
        if (ptrace(PTRACE_GETEVENTMSG, target_pid, NULL, &status) == -1) {
          FAIL("Error retrieving exit code via ptrace: %s", strerror(errno));
          return 1;
        }
        return status >> 8;
      } else {
        if (ptrace(PTRACE_CONT, target_pid, NULL, (void*)(long)sig.si_status) == -1) {
          /* puts("Failed"); */
          return sig.si_status + 128;
        } else {
          /* puts("This one worked"); */
          continue;
        }
      }
    } else {
      FAIL("Unexpected sig.si_code: %i (si_status: %i)", sig.si_code, sig.si_status);
      return 1;
    }
  }
}

void forward_signal(int sig) {
  if (target_pid < 1) {
    FAIL("target_pid == %I in forward_signal", target_pid);
    abort();
  }
  /* printf("%d\n", sig); */
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
    FAIL("sd_bus_call failed: %s", err.message);
    FAIL("Are you sure the uprocd module has been started?");
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
      signal(sig, forward_signal);
    }

    return wait_for_process();
  }
}

int main(int argc, char **argv) {
  setproctitle_init(argc, argv);
  prog = argv[0];

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
