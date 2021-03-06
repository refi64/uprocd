# cgrmvd.policy -- cgrmvd policy configuration format

## SYNOPSIS

policy.policy

## DESCRIPTION

A cgrmvd policy is a simple configuration file ending in .policy.

## SYNTAX

Each line of the file must be either empty, a comment, or a policy.

Comments are lines that being with a pound sign (**#**).

Policy lines follow this format:

```
copier : origin1 origin2 origin3...
```

The colon in the middle must be surrounded by whitespace on both sides.

## EXAMPLE

```
# This policy file declares that /usr/bin/foo can be moved to the cgroups of either
# /usr/bin/bar or /usr/bin/baz.
# Notice the format: copier : origin1 origin2 origin3...
/usr/bin/foo : /usr/bin/bar /usr/bin/baz
```

```
# This is the policy file uprocd uses. It declares that uprocd can be moved to the
# cgroups of uprocctl processes.
/usr/share/uprocd/bin/uprocd : /usr/bin/uprocctl
```
