# Unix Shell in C

`wsh` is a small Unix-style shell written in C. It focuses on the mechanics behind everyday shell behavior: parsing a command line, deciding whether a command is built in or external, launching child processes, wiring pipelines, and preserving shell state such as aliases and history.

## What It Implements

- Interactive and batch execution modes
- Built-ins: `exit`, `alias`, `unalias`, `which`, `path`, `cd`, and `history`
- Alias expansion on the first command token
- External command lookup through `PATH`
- Process execution with `fork`, `execv`, and `waitpid`
- Multi-stage pipelines with `pipe`, `dup2`, and descriptor cleanup
- Dynamic arrays and a hash map for shell state
- Strict C build flags with optimized and debug builds

## Build

```sh
make
./wsh
```

For a debug build:

```sh
make wsh-dbg
```

## Files

- `wsh.c` - shell loop, parser, built-ins, alias expansion, process execution, and pipelines
- `hash_map.c` / `hash_map.h` - alias table
- `dynamic_array.c` / `dynamic_array.h` - command history storage
- `utils.c` / `utils.h` - shared helpers
- `wsh.h` - constants and public shell declarations
