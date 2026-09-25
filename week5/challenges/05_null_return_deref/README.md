# NULL 반환값 역참조로 인한 SIGSEGV 디버깅

처음에는 아래 `memcpy()` 부분을 의심했다.

```c
size_t kl = (size_t)(end - (p + 2));

if (kl >= sizeof key)
    kl = sizeof key - 1;

memcpy(key, p + 2, kl);
```

`memcpy(key, p + 2, kl)`은 `p + 2` 주소부터 `kl`바이트를 `key`에 복사하는 코드이다.

또한 `kl`이 `key`의 크기를 넘지 않도록 제한하고 있으므로, 실제 오류 위치를 GDB로 확인했다.

## GDB 확인

```text
(gdb) run
Starting program: /work/build/05_null_return_deref 
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/lib/aarch64-linux-gnu/libthread_db.so.1".

Program received signal SIGSEGV, Segmentation fault.
0x0000fffff7e92ed0 in ?? () from /lib/aarch64-linux-gnu/libc.so.6
(gdb) bt
#0  0x0000fffff7e92ed0 in ?? () from /lib/aarch64-linux-gnu/libc.so.6
#1  0x0000aaaaaaaa0b64 in expand (c=0xffffffffed90, tmpl=0xaaaaaaaa0d70 "http://${host}:${port}/${path}/index.html", out=0xffffffffee98 "[http://example.com:8080/](http://example.com:8080/)", outcap=256)
   at challenges/05_null_return_deref/bug.c:68
#2  0x0000aaaaaaaa0ce0 in main () at challenges/05_null_return_deref/bug.c:102
```

`expand()` 함수 내부에서 오류가 발생했다.

```text
(gdb) frame 1
#1  0x0000aaaaaaaa0b64 in expand (c=0xffffffffed90, tmpl=0xaaaaaaaa0d70 "http://${host}:${port}/${path}/index.html", out=0xffffffffee98 "[http://example.com:8080/](http://example.com:8080/)", outcap=256)
   at challenges/05_null_return_deref/bug.c:68
warning: Source file is more recent than executable.
68                  size_t vl = strlen(v);                 
```

예상했던 `memcpy()`가 아니라 다음 코드에서 SIGSEGV가 발생했다.

```c
size_t vl = strlen(v);
```

현재 처리 중인 key를 확인했다.

```text
(gdb) print key
$3 = "path", '\000' <repeats 12 times>, "P\367\377\367\377\377\000\000\000\360\375\367\377\377\000"
```

즉 `${path}`를 처리하던 중이었다.

## 원인

`v`는 `cfg_get()`의 반환값이다.

```c
static const char *cfg_get(const Config *c, const char *k) {
    for (int i = 0; i < c->n; i++)
        if (strcmp(c->keys[i], k) == 0) return c->vals[i];
    return NULL;
}
```

`strcmp()`는 주소가 아니라 문자열 내용을 비교한다.

GDB로 확인하면:

```text
(gdb) p c->keys[0]
$11 = 0xaaaaaaaa0d58 "host"
(gdb) p c
$12 = (const Config *) 0xffffffffed90
(gdb) p key
$13 = "path", '\000' <repeats 12 times>, "P\367\377\367\377\377\000\000\000\360\375\367\377\377\000"
```

설정에 `"path"`가 없으면 `cfg_get()`은 `NULL`을 반환한다.

그 상태에서

```c
const char *v = cfg_get(c, key);
size_t vl = strlen(v);
```

를 실행하면서 사실상 `strlen(NULL)`이 호출되어 SIGSEGV가 발생한 것이다.

## 수정

`NULL`일 경우 빈 문자열로 처리했다.

```c
const char *v = cfg_get(c, key);
if (v == NULL) v = "";

size_t vl = strlen(v);
```

수정 후:

```text
(gdb) run
Starting program: /work/build/05_null_return_deref 
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/lib/aarch64-linux-gnu/libthread_db.so.1".
url = [http://example.com:8080//index.html](http://example.com:8080//index.html)
[Inferior 1 (process 43697) exited normally]
(gdb) 
```

정상 종료되었다.

### 정리

```text
${path}
   ↓
cfg_get(c, "path")
   ↓
path 없음
   ↓
NULL 반환
   ↓
strlen(NULL)
   ↓
SIGSEGV
```

NULL을 반환할 수 있는 함수의 결과는 사용하기 전에 반드시 확인해야 한다.