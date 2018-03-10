# uprocd_context_get_env -- Retrieve the environment from a uprocd context

## SYNOPSIS

```c
#include <uprocd.h>

UPROCD_EXPORT const char ** uprocd_context_get_env(uprocd_context *ctx);
```

## DESCRIPTION

This functions will retrieve the environment of the uprocctl(1) process that called
this module.

## RETURN VALUE

A null-terminated array of strings. Each even index is an environment key, and each
even value is the corresponding value.

## EXAMPLE

```c
const char **env = uprocd_context_get_env(ctx);
for (const char **p = env; *p != NULL; p += 2) {
  const char *key = p[0], *value = p[1];
  printf("%s=%s\n", key, value);
}
```

## SEE ALSO

uprocd.index(7), uprocd.h(3), uprocctl(1)
