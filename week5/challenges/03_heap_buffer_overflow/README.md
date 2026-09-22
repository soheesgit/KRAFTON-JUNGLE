처음에 코드를 읽어보고 오류를 찾으려고 했는데, 바로 원인을 찾기는 어려웠다.

1. 너무 큰 값을 넣는 데다가 계속 2배씩 곱해주니까 오버플로우 오류가 발생했나? 하고 추측했다.
2. `realloc()`이 메모리의 크기를 조절하는 함수이기 때문에 여기서 오류가 발생한 게 아닐까 추측했다. 왜냐하면 다른 코드들은 비교적 평범해 보였기 때문이다.

### C언어의 `realloc()`은?

`realloc()` 함수는 이미 할당받은 동적 메모리(Heap Memory)의 크기를 늘리거나 줄일 때 사용한다.

즉, `malloc()`으로 이미 할당한 메모리의 크기를 다시 바꾸는 함수이다.

그런데 그렇다면 `realloc()`을 사용하고 있으니 정상적으로 잘 돌아가야 하는 것 아닌가?

```c
/* [Thinking Point]
 * 개수/크기를 담는 len, cap 을 왜 int 가 아니라 size_t 로 선언할까?
 *   tip 1. size_t 는 "이 플랫폼에서 표현 가능한 가장 큰 객체 크기"를 담도록 만든
 *          부호 없는(unsigned) 정수 타입이다. malloc/sizeof/strlen 의 타입도 size_t 다.
 *   tip 2. int 는 보통 32비트라 약 21억(2^31-1)에서 넘치고, 음수도 가능하다.
 *          원소가 그보다 많아지거나 cap*sizeof(int) 계산이 커지면 int 는 오버플로된다.
 *   생각해보기: 크기를 int 로 두면 어떤 버그가 생길 수 있을까?
 */
```

처음에는 여기서 오버플로우와 관련이 있나 생각했다.

`int` 같은 signed 정수는 표현 가능한 범위를 넘어가면 문제가 발생한다.  
C에서 signed integer overflow는 단순히 음수로 돌아간다고 보장되는 것이 아니라 **undefined behavior**이다.

반면 `size_t`는 unsigned 타입이고, 범위를 넘어가면 다시 작은 값으로 돌아가는 방식으로 동작한다.

그리고 메모리의 크기나 배열의 길이를 나타낼 때는 `malloc()`, `sizeof()` 등에서도 `size_t`를 사용하기 때문에 `len`, `cap` 역시 `size_t`를 사용하는 것이 적절하다.

즉, 이번 문제는 단순히 `int` 오버플로우 때문에 발생한 문제는 아닌 것 같았다.

---

### GDB를 통해 확인해보자

```gdb
(gdb) run
```

실행 결과:

```text
Starting program: /work/build/03_heap_buffer_overflow
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/lib/aarch64-linux-gnu/libthread_db.so.1".

realloc(): invalid next size

Program received signal SIGABRT, Aborted.
0x0000fffff7e774d8 in ?? () from /lib/aarch64-linux-gnu/libc.so.6
```

`realloc(): invalid next size`라는 오류가 발생했다.

역시 `realloc()` 근처에서 문제가 발생한 것 같았다.

다만 이 메시지는 단순히 `realloc()`이 메모리를 할당하지 못했다는 의미라기보다는, **이전에 Heap 메모리가 잘못 사용되어 내부 메모리 정보가 깨졌을 가능성**도 있다는 뜻이다.

그러면 정확히 어디에서 문제가 시작됐는지 확인하기 위해 `bt`를 사용했다.

```gdb
(gdb) bt
```

결과:

```text
#0  0x0000fffff7e774d8 in ?? () from /lib/aarch64-linux-gnu/libc.so.6
#1  0x0000fffff7e2cb3c in raise () from /lib/aarch64-linux-gnu/libc.so.6
#2  0x0000fffff7e17e00 in abort () from /lib/aarch64-linux-gnu/libc.so.6
#3  0x0000fffff7e6aac4 in ?? () from /lib/aarch64-linux-gnu/libc.so.6
#4  0x0000fffff7e81fdc in ?? () from /lib/aarch64-linux-gnu/libc.so.6
#5  0x0000fffff7e85ec8 in ?? () from /lib/aarch64-linux-gnu/libc.so.6
#6  0x0000fffff7e8728c in realloc () from /lib/aarch64-linux-gnu/libc.so.6
#7  0x0000aaaaaaaa0a90 in list_ensure (l=0xfffffffff020, need=17)
    at challenges/03_heap_buffer_overflow/bug.c:68
#8  0x0000aaaaaaaa0b28 in list_push (l=0xfffffffff020, x=16)
    at challenges/03_heap_buffer_overflow/bug.c:76
#9  0x0000aaaaaaaa0c80 in main ()
    at challenges/03_heap_buffer_overflow/bug.c:98
```

`#0 ~ #6`은 libc 내부에서 `realloc()`의 문제를 감지하고 프로그램을 종료하는 과정이고,

내 코드에서는 다음 순서로 호출된 것을 확인할 수 있다.

```text
main
→ list_push
→ list_ensure
→ realloc
```

특히 여기서

```text
#7 list_ensure (... need=17)
#8 list_push (... x=16)
```

이 눈에 들어왔다.

즉, `x = 16`을 넣으려고 하는 순간 공간이 부족해져서 `need = 17`인 상태로 `list_ensure()`가 호출된 것이다.

`list_ensure()` 안에는 다음 코드가 있었다.

```c
int *p = realloc(l->data, l->cap * sizeof(int));
```

그리고 `list_push()`에서는

```c
if (l->len == l->cap)
    list_ensure(l, l->cap + 1);
```

을 사용하고 있었다.

처음에는 `realloc()`이 문제를 일으킨 것은 확인했는데, 그러면 정확한 원인이 무엇인지 더 확인해야 했다.

---

### frame 7로 이동

처음에는 그냥 현재 위치에서

```gdb
(gdb) p l
(gdb) p need
```

같은 명령을 입력했는데 제대로 나오지 않았다.

이유는 현재 GDB가 `#0`, 즉 libc 내부 함수의 stack frame을 보고 있었기 때문이다.

`l`, `need`, `newcap`은 `list_ensure()` 안의 변수이므로 `frame 7`로 이동해야 했다.

```gdb
(gdb) frame 7
```

이후 값을 확인했다.

```gdb
(gdb) p l->cap
$5 = 16

(gdb) p newcap
$6 = 32
```

`l->cap`의 값은 16인데, 새로 계산된 `newcap`은 32였다.

즉 현재 공간 16개로는 부족하기 때문에 새로운 공간을 32개로 늘리려고 하고 있었다.

그런데 코드를 다시 보니:

```c
int *p = realloc(l->data, l->cap * sizeof(int));
```

`newcap`은 32로 계산했는데, 실제 `realloc()`에서는 여전히 기존 값인 `l->cap`, 즉 16을 사용하고 있었다.

그러면서 아래에서는

```c
l->cap = newcap;
```

으로 `cap`을 32로 바꾸고 있었다.

즉 실제 메모리는 16개 크기인데, 프로그램은 32개 크기의 공간이 있다고 생각하게 되는 것이다.

그래서 실제 Heap 메모리 범위를 넘어 계속 데이터를 쓰게 되고, 결국 Heap Buffer Overflow가 발생한 것이었다.

---

### 수정

그래서 `realloc()`에서도 기존 `l->cap`이 아니라 새로 계산한 `newcap`을 사용하도록 변경했다.

기존 코드:

```c
int *p = realloc(l->data, l->cap * sizeof(int));
```

수정:

```c
int *p = realloc(l->data, newcap * sizeof(int));
```

이렇게 해봤더니!

```text
len=2000000 cap=2097152 sum=99000000
```

정상적으로 출력됐다. 

결국 문제는 `realloc()` 자체가 아니라, 실제로 할당된 메모리 크기 ≠ 프로그램이 알고 있는 cap 값이 서로 달랐던 것이었다.

동적 배열의 크기를 늘릴 때는 `newcap`으로 계산한 새로운 용량과 실제 `realloc()`으로 확보하는 메모리 크기가 항상 같아야 한다.