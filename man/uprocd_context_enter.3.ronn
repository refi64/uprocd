# uprocd_context_enter -- Enter a uprocd context object

## SYNOPSIS

```c
#include <uprocd.h>

UPROCD_EXPORT void uprocd_context_enter(uprocd_context *ctx);
```

## DESCRIPTION

This function will "enter" the given context. The following operations will be
performed:

1. The current environment will be matched to the context's environment.
2. The current working directory will be changed to the context's working directory.
3. The current process will be attached to the context's standard I/O and terminal.

## SEE ALSO

uprocd.index(7), uprocd.h(3), uprocd(7)
