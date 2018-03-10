# uprocd_context_get_cwd -- Retrieve the working directory from a uprocd context

## SYNOPSIS

```c
#include <uprocd.h>

UPROCD_EXPORT const char * uprocd_context_get_cwd(uprocd_context *ctx)
```

## DESCRIPTION

This function will return the working directory of the uprocctl(1) process that
called this module.

## EXAMPLE

```c
const char *cwd = uprocd_context_get_cwd(ctx);
printf("uprocctl was called from %s\n", cwd);
```

## SEE ALSO

uprocd.index(7), uprocd.h(3), uprocctl(1)
