# uprocd_context_free -- Free a uprocd context

## SYNOPSIS

```c
#include <uprocd.h>

UPROCD_EXPORT void uprocd_context_free(uprocd_context *ctx);
```

## DESCRIPTION

Frees the memory associated with the given context. Any values previously retrieved
from the context will become invalidated.

## SEE ALSO

uprocd.index(7), uprocd.h(3), uprocd_module_entry(3)
