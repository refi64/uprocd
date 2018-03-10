# uprocd_config_present -- Determine if the given property is present

## SYNOPSIS

```c
#include <uprocd.h>

UPROCD_EXPORT int uprocd_config_present(const char *key);
```

## DESCRIPTION

This function will check if the given key is present in the module's properties.

## RETURN VALUE

1 if it is present, 0 if it isn't.

## SEE ALSO

uprocd.index(7), uprocd.h(3)
