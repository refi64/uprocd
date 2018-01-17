/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef UPROCD_H
#define UPROCD_H

#define UPROCD_EXPORT __attribute__((visibility("default")))

UPROCD_EXPORT const char * uprocd_module_directory();
UPROCD_EXPORT char * uprocd_module_path(const char *path);
UPROCD_EXPORT void uprocd_module_path_free(char *path);

UPROCD_EXPORT int uprocd_config_list_size(const char *key);
UPROCD_EXPORT double uprocd_config_number(const char *key);
UPROCD_EXPORT double uprocd_config_number_at(const char *list, int index);
UPROCD_EXPORT const char * uprocd_config_string(const char *key);
UPROCD_EXPORT const char * uprocd_config_string_at(const char *list, int index);

typedef struct uprocd_context uprocd_context;

UPROCD_EXPORT void uprocd_context_get_args(uprocd_context *ctx, int *pargc,
                                           char ***pargv);
UPROCD_EXPORT const char ** uprocd_context_get_env(uprocd_context *ctx);
UPROCD_EXPORT const char * uprocd_context_get_cwd(uprocd_context *ctx);
UPROCD_EXPORT void uprocd_context_free(uprocd_context *ctx);

UPROCD_EXPORT void uprocd_context_enter(uprocd_context *ctx);

typedef void (*uprocd_exit_handler)(void *userdata);
UPROCD_EXPORT void uprocd_on_exit(uprocd_exit_handler func, void *userdata);
UPROCD_EXPORT uprocd_context * uprocd_run();

typedef void (*uprocd_module_entry_type)();
UPROCD_EXPORT void uprocd_module_entry();

#endif
