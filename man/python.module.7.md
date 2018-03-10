# python.module -- The uprocd Python module

## SYNOPSIS

python.module

## DESCRIPTION

This is the Python native module for uprocd.

## PROPERTIES

**Preload=<string>**

    Code to run during the initialization phase.

**Run=<string>**

    Code to run when the module is forked.

## EXAMPLE

```ini
[DerivedModule]
Base=python
# This code is to initialize the module.
Preload=import IPython
# This will be called when the module is run.
Run=
  from IPython import start_ipython
  sys.exit(start_ipython())
```
