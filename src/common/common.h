/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef COMMON_H
#define COMMON_H

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sds.h>

// Run(Array<DictEntry<String>> env, Array<String> argv, String cwd,
//     Tuple<String, String, String> ttys, Int64 uprocctl_pid) -> Int64 pid, String name
#define UPROCD_DBUS_RUN_ARGUMENTS "a{ss}ass(sss)x"
#define UPROCD_DBUS_RUN_RETURN "xs"

void * alloc(size_t sz);
void * ralloc(void *p, size_t sz);
#define newa(ty, n) ((ty*)alloc(sizeof(ty) * n))
#define new(ty) newa(ty, 1)

void get_bus_params(const char *module, sds *pservice, sds *pobject);

extern void *__setproctitle_mem;
void __setproctitle_init(char **argv);
#define setproctitle_init(argc, argv, ...) __setproctitle_init(argv)
void setproctitle(const char *fmt, ...);

#endif
