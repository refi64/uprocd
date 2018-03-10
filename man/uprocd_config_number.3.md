# uprocd_config_number -- Return the number at the given property

## SYNOPSIS

```c
#include <uprocd.h>

UPROCD_EXPORT double uprocd_config_number(const char *key);
```

## DESCRIPTION

This function will retrieve the number at the given property. To retrieve a number from
a list, use uprocd_config_number_at(3).

## RETURN VALUE

The number's value, or 0 if it is not present. It is undefined to call this function
on a non-number value.

To distinguist between 0 values and non-present properties, use
uprocd_config_present(3).

## EXAMPLE

Module config:

```ini
[NativeModule]

[Properties]
Present=number
NotPresent=number

[Defaults]
Present=10
```

Source code:

```c
double num;

num = uprocd_config_number("Present");
printf("Present: %d\n", num); // Present: 10

num = uprocd_config_number("NotPresent");
printf("NotPresent: %d\n", num); // NotPresent: 0
```

## SEE ALSO

uprocd.index(7), uprocd.h(3), uprocd_config_present(3), uprocd_config_number_at(3)
