/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "common.h"

#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>

#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>

void _fail(sds message) {
  int errno_ = errno;

  fprintf(stderr, SD_CRIT "%.*s\n", (int)sdslen(message), message);
  sd_notifyf(0, "STATUS=\"Failure: %s\"", message);
  sdsfree(message);

  errno = errno_;
}

void _busfail(sd_bus_error *err, sds message) {
  _fail(sdsdup(message));
  sd_bus_error_set(err, "com.refi64.cgrmvd.Error", message);
  sdsfree(message);
}

#define FMT(...) sdscatfmt(sdsempty(), __VA_ARGS__)
#define FAIL(...) _fail(FMT(__VA_ARGS__))
#define BUSFAIL(err, ...) _busfail(err, FMT(__VA_ARGS__))

typedef struct policy_origins {
  sds *origins;
  int len;
} policy_origins;

table g_policies;

void free_policy(policy_origins *policy) {
  sdsfreesplitres(policy->origins, policy->len);
}

void read_policy(sds path) {
  FILE *fp = fopen(path, "r");
  if (fp == NULL) {
    FAIL("Error opening %S: %s", path, strerror(errno));
    return;
  }

  sds line = NULL;
  int rc = 0, lineno = 0;

  while ((rc = readline(fp, &line)) == 0 && line != NULL) {
    lineno++;
    sdstrim(line, "\t ");

    size_t len = sdslen(line);
    if (len == 0 || line[0] == '#') {
      goto parse_end;
    }

    char *mid = strstr(line, " : ");
    if (mid == NULL) {
      FAIL("Error parsing %S:%d.");
      goto parse_end;
    }

    sds copier = sdsdup(line), origins = sdsdup(line);
    sdsrange(copier, 0, mid - line - 1);
    sdsrange(origins, mid - line + 3, -1);

    policy_origins *policy = new(policy_origins);
    policy->origins = sdssplitlen(origins, sdslen(origins), " ", 1, &policy->len);

    policy_origins *original = table_swap(&g_policies, copier, policy);
    if (original != NULL) {
      FAIL("WARNING: Copier %s has multiple origin values", copier);
      free_policy(original);
    }

    sdsfree(copier);
    sdsfree(origins);

    parse_end:
    sdsfree(line);
  }

  fclose(fp);
}

void reload_policies() {
  table_free(&g_policies);
  table_init(&g_policies);

  DIR *dir;
  struct dirent *entry;

  char *root = "/usr/share/cgrmvd/policies";
  dir = opendir(root);
  if (dir == NULL) {
    FAIL("Error opening %s: %s", root, strerror(errno));
    return;
  }

  for (;;) {
    errno = 0;
    entry = readdir(dir);
    if (entry == NULL) {
      break;
    }

    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    sds name = sdsnew(entry->d_name);
    int len = sdslen(name);
    if (len < 8 || strcmp(name + len - 7, ".policy") != 0) {
      FAIL("Invalid file path (expected .policy): %S", name);
      goto loop_end;
    }

    sds path = sdscatfmt(sdsnew(root), "/%S", name);
    read_policy(path);
    sdsfree(path);

    loop_end:
    sdsfree(name);
  }

  closedir(dir);
}

int readlink_bus(sds path, sds *out, sd_bus_error *err) {
  char buf[PATH_MAX + 1];
  ssize_t sz = readlink(path, buf, sizeof(buf) - 1);

  if (sz == -1) {
    BUSFAIL(err, "Error reading link behind %S: %s", path, strerror(errno));
    sdsfree(path);
    return -errno;
  }

  *out = sdsnewlen(buf, sz);
  sdsfree(path);

  struct stat st;
  if (lstat(*out, &st) == -1) {
    FAIL("WARNING: lstat on %S from readlink_bus failed: %s", *out, strerror(errno));
  } else {
    if (S_ISLNK(st.st_mode)) {
      return readlink_bus(sdsdup(*out), out, err);
    }
  }

  return 0;
}

int fopen_bus(sds path, FILE **out, char *mode, sd_bus_error *err) {
  *out = fopen(path, mode);

  if (*out == NULL) {
    BUSFAIL(err, "Error reading %S: %s", path, strerror(errno));
    sdsfree(path);
    return -errno;
  }

  sdsfree(path);
  return 0;
}

int verify_policy(int64_t copier, int64_t origin, sd_bus_error *err) {
  sds copier_exe, origin_exe;
  int rc;

  rc = readlink_bus(sdscatfmt(sdsempty(), "/proc/%I/exe", copier), &copier_exe, err);
  if (rc < 0) {
    return rc;
  }

  rc = readlink_bus(sdscatfmt(sdsempty(), "/proc/%I/exe", origin), &origin_exe, err);
  if (rc < 0) {
    sdsfree(copier_exe);
    return rc;
  }

  policy_origins *policy = table_get(&g_policies, copier_exe);
  if (policy == NULL) {
    BUSFAIL(err, "Policy for %S does not exist.", copier_exe);
    return -EPERM;
  }

  int origin_match = 0;
  for (int i = 0; i < policy->len; i++) {
    if (strcmp(origin_exe, policy->origins[i]) == 0) {
      origin_match = 1;
      break;
    }
  }

  if (!origin_match) {
    BUSFAIL(err, "Policy for %S does not include origin %S.", copier_exe, origin_exe);
    return -EPERM;
  }

  return 0;
}

int parse_cgroup_path(int64_t pid, FILE *fp, sds *path, sd_bus_error *err) {
  sds line = NULL;
  int rc = 0, nparts = 0;
  sds *parts;

  rc = readline(fp, &line);
  if (rc < 0) {
    BUSFAIL(err, "Error reading line from /proc/%I/cgroup: %s", pid, strerror(errno));
    return -errno;
  }

  if (line == NULL) {
    *path = NULL;
    return 0;
  }

  parts = sdssplitlen(line, sdslen(line), ":", 1, &nparts);
  if (nparts != 3) {
    BUSFAIL(err, "Invalid line in /proc/%I/cgroup: %S", pid, line);
    rc = -EINVAL;
    goto end;
  }

  if (strncmp(parts[1], "name=", 5) == 0) {
    sdsrange(parts[1], 5, -1);
  }

  if (sdslen(parts[1]) == 0) {
    sdsfree(parts[1]);
    parts[1] = sdsnew("unified");
  }

  *path = sdscatfmt(sdsempty(), "/sys/fs/cgroup/%S%S", parts[1], parts[2]);
  if ((*path)[sdslen(*path) - 1] == '/') {
    sdsrange(*path, 0, -2);
  }

  end:
  sdsfree(line);

  if (parts) {
    sdsfreesplitres(parts, nparts);
  }
  return rc;
}

int move_cgroups(int64_t copier, int64_t origin, sd_bus_error *err) {
  FILE *copier_fp = NULL, *origin_fp = NULL;
  int rc = 0;

  rc = fopen_bus(sdscatfmt(sdsempty(), "/proc/%I/cgroup", copier), &copier_fp, "r", err);
  if (rc < 0) {
    goto end;
  }

  rc = fopen_bus(sdscatfmt(sdsempty(), "/proc/%I/cgroup", origin), &origin_fp, "r", err);
  if (rc < 0) {
    goto end;
  }

  for (;;) {
    sds copier_path = NULL, origin_path = NULL;

    rc = parse_cgroup_path(origin, origin_fp, &origin_path, err);
    if (rc < 0 || origin_path == NULL) {
      goto end;
    }

    rc = parse_cgroup_path(copier, copier_fp, &copier_path, err);
    if (rc < 0 || copier_path == NULL) {
      sdsfree(origin_path);
      goto end;
    }

    if (strcmp(origin_path, copier_path) == 0) {
      goto loop_end;
    }

    sds target = sdscat(sdsdup(origin_path), "/tasks");
    if (access(target, W_OK) != 0) {
      sdsfree(target);
      target = sdscat(sdsdup(origin_path), "/cgroup.procs");
      if (access(target, W_OK) != 0) {
        sdsfree(target);
        BUSFAIL(err, "Neither %S/tasks or %S/cgroup.procs are writable.", copier_path,
                copier_path);
        rc = -EACCES;
        goto loop_end;
      }
    }

    FILE *target_fp;
    rc = fopen_bus(target, &target_fp, "wa", err);
    if (rc < 0) {
      goto loop_end;
    }

    fprintf(target_fp, "%ld\n", copier);
    fclose(target_fp);

    loop_end:
    sdsfree(copier_path);
    sdsfree(origin_path);
    if (rc < 0) {
      break;
    }
  }

  end:
  if (copier_fp) {
    fclose(copier_fp);
  }
  if (origin_fp) {
    fclose(origin_fp);
  }
  return rc;
}

int service_method_move_cgroup(sd_bus_message *msg, void *data, sd_bus_error *err) {
  int64_t copier, origin;
  int rc;

  rc = sd_bus_message_read(msg, "xx", &copier, &origin);
  if (rc < 0) {
    FAIL("Error parsing bus message: %s", strerror(-rc));
    return rc;
  }

  rc = verify_policy(copier, origin, err);
  if (rc < 0) {
    return rc;
  }

  rc = move_cgroups(copier, origin, err);
  if (rc < 0) {
    return rc;
  }

  return sd_bus_reply_method_return(msg, "");
}

static const sd_bus_vtable service_vtable[] = {
  SD_BUS_VTABLE_START(0),
  // MoveCgroup(Int64 copier_pid, Int64 origin_pid)
  SD_BUS_METHOD("MoveCgroup", "xx", "", service_method_move_cgroup,
                SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_VTABLE_END
};

void bus_loop() {
  // XXX: This code is similar to uprocd/bus.c.
  int rc;
  sd_bus *bus = NULL;
  sd_bus_slot *slot = NULL;

  rc = sd_bus_open_system(&bus);
  if (rc < 0) {
    FAIL("sd_bus_open_user failed: %s", strerror(-rc));
    goto end;
  }

  rc = sd_bus_add_object_vtable(bus, &slot, "/com/refi64/uprocd/Cgrmvd",
                                "com.refi64.uprocd.Cgrmvd", service_vtable, NULL);
  if (rc < 0) {
    FAIL("sd_bus_add_object_vtable failed: %s", strerror(-rc));
    goto end;
  }

  rc = sd_bus_request_name(bus, "com.refi64.uprocd.Cgrmvd", 0);
  if (rc < 0) {
    FAIL("sd_bus_request_name failed: %s", strerror(-rc));
    goto end;
  }

  for (;;) {
    rc = sd_bus_process(bus, NULL);
    if (rc < 0) {
      FAIL("sd_bus_process failed: %s", strerror(-rc));
      goto end;
    } else if (rc > 0) {
      continue;
    }

    rc = sd_bus_wait(bus, (uint64_t)-1);
    if (rc < 0 && rc != -EINTR) {
      FAIL("sd_bus_wait failed: %s", strerror(-rc));
      goto end;
    }
  }

  end:
  sd_bus_slot_unref(slot);
  sd_bus_unref(bus);
}

int main(int argc, char **argv) {
  table_init(&g_policies);

  signal(SIGHUP, reload_policies);
  reload_policies();

  bus_loop();
}
