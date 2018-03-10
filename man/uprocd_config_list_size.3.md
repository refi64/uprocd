# uprocd_config_list_size -- Return the size of the list at the given property

## SYNOPSIS

```c
#include <uprocd.h>

UPROCD_EXPORT int uprocd_config_list_size(const char *key);
```

## DESCRIPTION

This function will retrieve the size of the list at the given key in the module's
properties.

## RETURN VALUE

The list's length, or -1 if it is not present. It is undefined to call this function
on a non-list value.

## EXAMPLE

Module config:

```ini
[NativeModule]

[Properties]
Present=list string
NotPresent=list string

[Defaults]
Present=a b c
```

Source code:

```c
int len;

len = uprocd_config_list_size("Present");
printf("Size of Present: %d\n", len); // Size of Present: 3

len = uprocd_config_list_size("NotPresent");
printf("Size of NotPresent: %d\n", len); // Size of NotPresent: -1
```

## SEE ALSO

uprocd.index(7), uprocd.h(3), uprocd_config_string_at(3), uprocd_config_number_at(3)
