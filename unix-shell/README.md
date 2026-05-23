# Unix Shell in C

## 한국어

`wsh`는 C로 만든 작은 Unix-style shell입니다. 명령어를 읽고, built-in인지 외부 프로그램인지 판단하고, 필요한 경우 child process를 만들어 실행합니다.

### 지원 기능

- interactive mode와 batch mode
- single quote를 포함한 command parsing
- `cd`, `history`, `alias`, `which`, `path` 같은 built-in command
- alias expansion과 command history
- Unix process API 기반 external command 실행
- file descriptor redirection 기반 multi-stage pipeline

### 빌드

```sh
make
./wsh
```

debug build:

```sh
make wsh-dbg
```

## English

`wsh` is a compact shell written in C. It reads commands, handles built-ins, launches external programs, and connects pipelines when needed.

### What It Supports

- interactive mode and batch mode
- command parsing with single quotes
- built-ins such as `cd`, `history`, `alias`, `which`, and `path`
- alias expansion and command history
- external command execution through Unix process APIs
- multi-stage pipelines with file descriptor redirection

### Build

```sh
make
./wsh
```

Debug build:

```sh
make wsh-dbg
```
