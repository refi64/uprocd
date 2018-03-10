# uprocd_module_path_free -- Free a uprocd_module_path return value

## SYNOPSIS

```c
#include <uprocd.h>

UPROCD_EXPORT void uprocd_module_path_free(char *path);
```

## DESCRIPTION

This function will free the string returned from uprocd_module_path(3).

## SEE ALSO

uprocd.index(7), uprocd.h(3), uprocd_module_directory(3), uprocd_module_path(3)
