# uprocd.module -- uprocd module configuration format

## SYNOPSIS

module.module

## DESCRIPTION

A module file with the extension .module contains the specification for a uprocd module.
This page lists the format of a uprocd module, as well as several examples. For guidance
on creating a native module library, see uprocd.h(3).

## BASIC SYNTAX

uprocd modules use an INI-inspired syntax, with some tweaks to make module authoring
easier.

Sections are still declared using [SectionName], and properties within a section using
PropertyName=Value. If a line begins with a #, it will be considered a comment.

If the line after a Key= line is indented, then the indented line will be considered
a multi-line key value.

```
# My uprocd module.
[Section]
Key=Value
Other=
  This is part of the Other key.
  Same here.

  This will continue until a line isn't indented.
NormalKey=Value
```

## MODULE TYPES

There are two types of modules: native modules and derived modules.

Native modules consist of a .module file and shared object library (ending in .so) that
contains a symbol uprocd_module_entry(3) that runs the module code. These modules can
specify properties that can be passed to them from derived modules.

Derived modules are extensions of native modules. These consist of only a .module file,
and the file will give values to the properties that the native module requires.

The module type is declared using the [NativeModule] or [DerivedModule] config section,
which must be the very first section in the file.

## MODULE DECLARATIONS

[NativeModule] or [DerivedModule] sections may specify the following properties:

**ProcessName=<string>**

> The process name. uprocctl will rename itself to this name when running a module.

**Description=<string>**

> A description for the module.

[NativeModule] sections may specify the following properties:

**NativeLib=<string>**

> By default, uprocd will look for a .so file with the same name as the .module file.
> This will change the name of the .so file to look for.

A [DerivedModule] section **must** specify the following properties:

**Base=<string>**

> The name of the native module that this derived module is extending.

## MODULE PROPERTIES

Native modules may ask for properties that a derived module must pass. These are
declared via the [Properties] section. Each key in the section will be a property name,
and each value will be the type of property. The following types are supported:

**string**

> A string value.

**number**

> A number value. This may be either a floating point number or an integer.

**list string**

> A space-seperated list of strings.

**list number**

> A space-seperated list of numbers.

In additions, the module should declare default values for all properties that a
derived module does not have to specify. This is done in the [Defaults] section. Each
key is a property name, and each value is a default value for that property.

In a derived module, the property values will be assigned in the [DerivedModule]
section.

## EXAMPLE MODULES

A native module:

```
[NativeModule]
Description=My native module.
# ProcessName will default to the module's name.
ProcessName=custom-process-name

[Properties]
LuckyNumbers=list number
LuckyNames=list string

[Defaults]
LuckyNumbers=1 2 3 4 5
# Because LuckyNames is not given a default, derived modules will be FORCED to specify it.
```

A derived module:

```
[DerivedModule]
Description=My derived module.
Base=native-module
ProcessName=custom-process-name

LuckyNames=Noctis Luna
```

## SEE ALSO

uprocd.index(7), uprocd.h(3), uprocd(7)
