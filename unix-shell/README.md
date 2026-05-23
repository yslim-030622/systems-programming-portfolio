# Unix Shell in C

`wsh`는 C로 구현한 작은 Unix-style shell입니다. 사용자가 입력한 command line을 parsing하고, built-in command인지 external program인지 판단한 뒤, process를 생성하거나 pipeline을 연결합니다.

`wsh` is a compact Unix-style shell implemented in C. It focuses on the mechanics behind everyday shell behavior: parsing commands, launching child processes, wiring pipelines, and maintaining shell state.

## Why I Built It

shell은 OS process API를 연습하기 좋은 프로젝트입니다. `fork`, `exec`, `wait`, `pipe`, `dup2` 같은 system call을 각각 따로 쓰는 것이 아니라, 실제 command execution flow 안에서 함께 다뤄야 하기 때문입니다.

Building a shell is a practical way to understand process control and file descriptor management.

## What It Does

- Interactive mode and batch mode
- Command parsing with single-quote handling
- Built-ins: `exit`, `alias`, `unalias`, `which`, `path`, `cd`, and `history`
- Alias expansion on the first token
- Executable lookup through `PATH`
- External command execution with `fork`, `execv`, and `waitpid`
- Multi-stage pipelines with `pipe`, `dup2`, and descriptor cleanup
- Dynamic array for command history
- Hash map for aliases

## Implementation Notes

The shell keeps aliases in a hash map and command history in a dynamic array. External commands are resolved against `PATH`, then executed in a child process. Pipelines create `N - 1` pipes for `N` stages, connect each child with `dup2`, close unused descriptors, and use the last stage's exit status as the pipeline result.

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

- `wsh.c`: shell loop, parser, built-ins, alias expansion, process execution, and pipelines
- `hash_map.c` / `hash_map.h`: alias table
- `dynamic_array.c` / `dynamic_array.h`: command history storage
- `utils.c` / `utils.h`: shared helpers
- `wsh.h`: constants and public declarations
