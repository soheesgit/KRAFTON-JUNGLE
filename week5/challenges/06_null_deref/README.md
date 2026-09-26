# 콜론이 없는 헤더 처리 중 NULL 역참조

처음 코드를 훑어봤을 때는 큰 문제가 없어 보여서 원인을 찾기 어려웠다.

하지만 `main`의 `raw` 데이터를 다시 확인해 보니 다음과 같이 `:`이 없는 줄이 있었다.

```text
Connection
```

반면 파싱 코드는 모든 줄에 `:`이 있다고 가정하고 있었다.

```c
char *colon = strchr(line, ':');
char *val = skip_ws(colon + 1);
```

`strchr()`는 찾는 문자가 없으면 `NULL`을 반환한다.

따라서 `Connection`처럼 `:`이 없는 줄에서는

```c
colon == NULL
```

이 되고, 이후 `colon + 1`을 사용하면서 잘못된 주소를 참조할 가능성이 있다고 예상했다.

## GDB 확인

실제로 실행해 보니 `parse_headers()`에서 SIGSEGV가 발생했다.

```text
Program received signal SIGSEGV, Segmentation fault.
0x0000aaaaaaaa0998 in parse_headers (text=0xfffffffff010 "Host", h=0xffffffffee08) at challenges/06_null_deref/bug.c:52
warning: Source file is more recent than executable.
52
```

문제가 발생한 시점의 값을 확인했다.

```text
(gdb) p colon
$1 = 0x0
(gdb) p line
$2 = 0xfffffffff02e "Connection"
```

`line`은 `"Connection"`이고 `colon`은 `0x0`, 즉 `NULL`이었다.

따라서 `:`이 없는 줄에서 `strchr()`가 `NULL`을 반환했고, 이후 이 값을 그대로 사용하면서 SIGSEGV가 발생한 것을 확인할 수 있었다.

## 수정

문제 조건에 따라 `:`이 없는 줄은 건너뛰도록 처리했다.

```c
char *colon = strchr(line, ':');

// line 안에 ':'가 없다면 건너뛴다.
if (colon == NULL)
    continue;

char *val = skip_ws(colon + 1);
```

## 수정 후 실행 결과

```text
parsed 3 headers
  Host = example.com
  Accept = */*
  User-Agent = memdbg-cli
[Inferior 1 (process 50074) exited normally]
```

정상적으로 종료되었다.

### 정리

```text
"Connection"
    ↓
strchr(line, ':')
    ↓
NULL 반환
    ↓
colon + 1 사용
    ↓
SIGSEGV
```

`strchr()`처럼 실패 시 `NULL`을 반환할 수 있는 함수는 반환값을 확인한 뒤 사용해야 한다.