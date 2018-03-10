# uprocd.h -- uprocd native module API index

## SYNOPSIS

```c
#define UPROCD_EXPORT __attribute__((visibility("default")))

typedef void (*uprocd_module_entry_type)();
UPROCD_EXPORT void uprocd_module_entry();

UPROCD_EXPORT const char * uprocd_module_directory();
UPROCD_EXPORT char * uprocd_module_path(const char *path);
UPROCD_EXPORT void uprocd_module_path_free(char *path);

typedef void (*uprocd_exit_handler)(void *userdata);
UPROCD_EXPORT void uprocd_on_exit(uprocd_exit_handler func, void *userdata);

UPROCD_EXPORT int uprocd_config_present(const char *key);
UPROCD_EXPORT int uprocd_config_list_size(const char *key);
UPROCD_EXPORT double uprocd_config_number(const char *key);
UPROCD_EXPORT double uprocd_config_number_at(const char *list, int index);
UPROCD_EXPORT const char * uprocd_config_string(const char *key);
UPROCD_EXPORT const char * uprocd_config_string_at(const char *list, int index);

typedef struct uprocd_context uprocd_context;

UPROCD_EXPORT uprocd_context * uprocd_run();
UPROCD_EXPORT void uprocd_context_enter(uprocd_context *ctx);
UPROCD_EXPORT void uprocd_context_free(uprocd_context *ctx);

UPROCD_EXPORT void uprocd_context_get_args(uprocd_context *ctx, int *pargc,
                                           char ***pargv);
UPROCD_EXPORT const char * uprocd_context_get_cwd(uprocd_context *ctx);
UPROCD_EXPORT const char ** uprocd_context_get_env(uprocd_context *ctx);
```

## DESCRIPTION

This is the header file containing the API to be used by native modules.

## GETTING STARTED

uprocd(7) - Ensure you've already read how uprocd works

## MODULE BASICS

uprocd_module_entry(3) - Entry point for uprocd modules

uprocd_module_directory(3) - Retrieve the path to the current uprocd module

uprocd_module_path(3) - Retrieve the path to a file next to the current uprocd module

uprocd_module_path_free(3) - Free a uprocd_module_path(3) return value

uprocd_on_exit(3) - Set a handler to be called on uprocd_run(3) failure

## ACCESSING MODULE PROPERTIES

uprocd_config_present(3) - Determine if the given property is present

uprocd_config_list_size(3) - Return the size of the list at the given property

uprocd_config_number(3) - Return the number at the given property

uprocd_config_number_at(3) - Return the number at the given index of a list

uprocd_config_string(3) - Return the string at the given property

uprocd_config_string_at(3) - Return the string at the given index of a list

## USING CONTEXTS

uprocd_run(3) - Enter the main uprocd daemon and fork the process

uprocd_context_enter(3) - Enter a uprocd context object

uprocd_context_free(3) - Free a uprocd context

uprocd_context_get_args(3) - Retrieve arguments from a uprocd context

uprocd_context_get_cwd(3) - Retrieve the working directory from a uprocd context

uprocd_context_get_env(3) - Retrieve the environment from a uprocd context
