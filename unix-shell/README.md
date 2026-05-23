# Unix Shell in C

`wsh`는 C로 만든 작은 Unix-style shell입니다. 명령어를 읽고, built-in인지 외부 프로그램인지 판단하고, 필요한 경우 child process를 만들어 실행합니다.

`wsh` is a compact shell written in C.

## What It Supports

- interactive mode and batch mode
- command parsing with single quotes
- built-ins such as `cd`, `history`, `alias`, `which`, and `path`
- alias expansion and command history
- external command execution through Unix process APIs
- multi-stage pipelines with file descriptor redirection

## Build

```sh
make
./wsh
```

Debug build:

```sh
make wsh-dbg
```

## Files

- `wsh.c`: shell loop, parsing, built-ins, execution, and pipelines
- `hash_map.c`, `hash_map.h`: aliases
- `dynamic_array.c`, `dynamic_array.h`: history
- `utils.c`, `utils.h`: helper code
