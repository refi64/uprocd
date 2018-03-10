# uprocd_config_string_at -- Return the string at the given index of a list

## SYNOPSIS

```c
#include <uprocd.h>

UPROCD_EXPORT const char * uprocd_config_string_at(const char *list, int index);
```

## DESCRIPTION

This function will retrieve the string at the given index of the given list. To retrieve
a string property, use uprocd_config_string(3).

## RETURN VALUE

The string value, or NULL if the list is not present or index is invalid. It is
undefined to call this function on a non-string list value.

## EXAMPLE

Module config:

```ini
[NativeModule]

[Properties]
Present=list string
NotPresent=list string

[Defaults]
Present=first second third
```

Source code:

```c
const char *s;

s = uprocd_config_string_at("Present", 0);
printf("Present[0]: %s\n", s); // Present[0]: first

s = uprocd_config_string_at("Present", 20);
printf("Present[20]: %s\n", s); // Present[20]: 0

s = uprocd_config_string_at("NotPresent", 0);
printf("NotPresent[0]: %s\n", s); // NotPresent[0]: 0
```

## SEE ALSO

uprocd.index(7), uprocd.h(3), uprocd_config_string_at(3)
