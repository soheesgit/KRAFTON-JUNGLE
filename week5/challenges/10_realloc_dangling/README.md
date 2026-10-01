# Challenge 10 — realloc 후 옛 포인터 사용 (심화: undo 스냅샷 댕글링)

## `realloc()`이란?

`realloc()`은 이미 할당된 메모리의 크기를 변경한다.

- 이때 기존 공간을 그대로 확장할 수도 있지만, 공간이 부족하면 새로운 메모리 영역으로 이동할 수도 있다.
- `realloc()` 실패 시 기존 메모리가 해제되는 것은 아니다.
- 하지만 `ptr = realloc(ptr, ...)`처럼 바로 대입하면 반환된 `NULL` 때문에 기존 주소를 잃어버릴 수 있으므로 임시 포인터를 사용하는 것이 안전하다.

## 코드를 보고 오류를 추측해보기

- 처음 코드를 봤을 때는 정확한 원인을 찾지 못했다.
- 제목에 `realloc`이 들어가 있으므로 `realloc()`의 특성과 관련된 문제일 것이라고 생각했지만, 코드만 보고는 어떤 과정에서 문제가 발생하는지 알기 어려웠다.

## GDB를 통해 오류 찾기

```text
(gdb) run
len=4003 cap=4096 head=0 tail=3999
free(): double free detected in tcache 2
```

Backtrace를 확인했다.

```text
(gdb) bt
#7  0x0000aaaaaaaa0c40 in eb_free (e=0xffffffffef10) at challenges/10_realloc_dangling/bug.c:85
#8  0x0000aaaaaaaa0d48 in main () at challenges/10_realloc_dangling/bug.c:104
```

문제는 `eb_free()`의 다음 코드에서 발생했다.

```c
free(e->undo[i]);
```

로그상 오류는 `double free`다.

하지만 `eb_free()` 코드를 봤을 때 같은 주소를 두 번 해제하는 부분이 바로 보이지는 않았다. 따라서 `free()`가 실행된 위치만 보는 것이 아니라, **해당 포인터가 어디서 만들어졌고 이전에 이미 해제된 적이 있는지**를 코드와 GDB를 통해 추적해보기로 했다.

## 1. 실제 사용 중인 `undo` 확인

```text
(gdb) p i
$3 = 0

(gdb) p e->undo[0]
$6 = (int *) 0xaaaaaaac12a0

(gdb) p e->undo[1]
$7 = (int *) 0x0
```

`undo[1]`, `undo[2]`도 처음에는 의심했지만:

```text
e->undo_n = 1
```

현재 실제로 사용 중인 snapshot은 하나뿐이다.

따라서 조사 대상은 `undo[0]` 하나로 좁힐 수 있다.

## 2. `undo[0]`은 어디서 만들어졌는가?

```c
static void eb_snapshot(EditBuffer *e) {
    if (e->undo_n < MAX_UNDO)
        e->undo[e->undo_n++] = e->data;
}
```

처음에는 `undo_n == 0`이므로 사실상 다음 코드가 실행된다.

```c
e->undo[0] = e->data;
e->undo_n = 1;
```

여기서 중요한 점은 데이터를 복사한 것이 아니라 **포인터의 주소값이 복사된다는 것**이다.

이 부분은 처음 코드를 봤을 때 헷갈렸다.

`e->data`의 자료형은 `int *`이고, `e->undo[0]`의 자료형 역시 `int *`이다.

즉,

```c
e->undo[0] = e->data;
```

는 `e->data`가 가리키고 있는 배열의 내용을 복사하는 것이 아니라, `e->data` 안에 저장되어 있는 **주소값 자체를 `e->undo[0]`에 복사하는 것**이다.

예를 들어 `e->data`에 `0x1000`이라는 주소가 들어 있었다면 실행 이후에는 다음과 같은 상태가 된다.

```text
e->data    = 0x1000
e->undo[0] = 0x1000
```

따라서 두 포인터는 서로 다른 데이터를 가지고 있는 것이 아니라 **같은 메모리 공간을 가리키고 있다.**

반대로 포인터가 가리키는 실제 값을 가져오고 싶다면 역참조 연산자 `*`를 사용한다.

```c
int value = *e->data;
```

이 경우에는 `e->data`라는 주소가 복사되는 것이 아니라, `e->data`가 가리키는 위치에 저장된 `int` 값 하나가 `value`에 복사된다.

즉 다음과 같이 구분할 수 있다.

```c
int *p2 = p1;      // 주소값 복사
int value = *p1;   // p1이 가리키는 실제 값 복사
```

그리고 현재 `data` 배열 전체를 독립적인 스냅샷으로 만들고 싶다면 단순히 포인터를 대입하는 것으로는 부족하다. 별도의 메모리를 할당한 뒤 내용을 복사해야 한다.

```c
int *copy = malloc(e->len * sizeof(int));
memcpy(copy, e->data, e->len * sizeof(int));
```

이렇게 하면 `e->data`와 `copy`는 서로 다른 주소를 가리키지만, 내부 데이터의 내용은 동일한 상태가 된다.

따라서 현재의 `eb_snapshot()`은 실제 데이터를 복사해서 저장하는 스냅샷이 아니라, **현재 `data`의 주소만 `undo` 배열에 기록하고 있는 상태**라고 볼 수 있다.

### GDB에서 확인하기

```text
(gdb) break eb_snapshot
Breakpoint 1 at 0xaaaaaaaa0a94: file challenges/10_realloc_dangling/bug.c, line 64.

(gdb) run
64          if (e->undo_n < MAX_UNDO) e->undo[e->undo_n++] = e->data;

(gdb) p e->data
$12 = (int *) 0xaaaaaaac12a0

(gdb) next
65      }

(gdb) p e->undo[0]
$14 = (int *) 0xaaaaaaac12a0
```

둘 다:

```text
0xaaaaaaac12a0
```

을 가리키고 있다.

## 3. 이후 `realloc()`이 발생한다

초기 `cap`은 작기 때문에 데이터를 계속 `push`하면 `eb_grow()`가 호출되고, 내부에서 `realloc()`이 실행된다.

```text
(gdb) break eb_grow
Breakpoint 2 at 0xaaaaaaaa0aec: file challenges/10_realloc_dangling/bug.c, line 68.

(gdb) continue
68          size_t nc = e->cap;

(gdb) p e->data
$15 = (int *) 0xaaaaaaac12a0
```

`realloc()` 실행 전의 주소를 확인한 뒤, `realloc()`이 실행된 이후 반환된 `p`를 확인했다.

```text
(gdb) p p
$17 = (int *) 0xaaaaaaac12e0
```

주소가 달라졌다.

```text
A = 0xaaaaaaac12a0
B = 0xaaaaaaac12e0
```

즉 `realloc()` 과정에서 메모리가 **A → B로 이동한 것**이다.

성공한 `realloc()`이 다른 위치로 메모리를 옮기면 기존 메모리 A는 더 이상 유효하지 않다.

하지만 `undo[0]`은 자동으로 B로 변경되지 않는다.

`undo[0]`에는 단순히 주소값 A가 복사되어 있었기 때문이다.

따라서 상태는 다음과 같다.

```text
e->data    = B    // 현재 정상적으로 사용하는 메모리
e->undo[0] = A    // 이미 해제되어 더 이상 유효하지 않은 메모리
```

이처럼 이미 유효하지 않은 메모리를 계속 가리키는 포인터를 **dangling pointer**라고 한다.

## 4. 결국 `double free`

마지막 `eb_free()`에서:

```c
free(e->data);
```

현재 `e->data == B`이므로 B를 정상적으로 해제한다.

이후:

```c
for (int i = 0; i < e->undo_n; i++) {
    free(e->undo[i]);
}
```

`undo_n == 1`이므로:

```c
free(e->undo[0]);
```

이 실행된다.

하지만:

```text
e->undo[0] = A
```

이고 A는 이미 `realloc()`이 다른 위치로 이동하는 과정에서 해제된 메모리다.

따라서 이미 해제된 A를 다시 `free()`하게 되어 `double free`가 발생한다.

## 코드 수정

기존에는 스냅샷을 저장할 때 원본 데이터의 주소만 `undo`에 저장했다.

이를 수정하여 별도의 메모리 공간을 할당하고, 현재 데이터를 복사한 뒤 그 복사본의 주소를 `undo`에 저장하도록 변경했다.

```c
static void eb_snapshot(EditBuffer *e) {
    if (e->undo_n < MAX_UNDO) {
        int *copy = malloc(e->len * sizeof(int));
        if (!copy) {
            perror("malloc");
            exit(1);
        }

        memcpy(copy, e->data, e->len * sizeof(int));
        e->undo[e->undo_n++] = copy;
    }
}
```

`memcpy()`는 지정한 크기만큼의 메모리 내용을 다른 메모리 공간으로 복사하는 함수이다.

```c
memcpy(copy, e->data, e->len * sizeof(int));
```

따라서 위 코드는 `e->data`가 가리키는 메모리에서 `e->len * sizeof(int)` 바이트만큼의 내용을 `copy`가 가리키는 새 메모리 공간으로 복사한다.

이번 수정의 핵심은 **스냅샷이 원본 `data`와 같은 메모리를 가리키지 않도록 별도의 메모리를 할당한 것**이다.

```text
e->data
   ↓
A [0][1][2]

e->undo[0]
   ↓
C [0][1][2]
```

두 메모리는 내용은 같지만 서로 다른 주소를 가진다.

따라서 이후 `realloc()`으로 `e->data`가 다른 위치로 이동하더라도 `undo[0]`에 저장된 복사본에는 영향을 주지 않는다.

수정 후 다시 실행했다.

```text
(gdb) run
Starting program: /work/build/10_realloc_dangling
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/lib/aarch64-linux-gnu/libthread_db.so.1".
len=4003 cap=4096 head=0 tail=3999
done
[Inferior 1 (process 132591) exited normally]
```

`double free` 없이 `done`까지 출력되며 정상 종료되는 것을 확인했다.

## 핵심

문제의 시작은 다음 코드였다.

```c
e->undo[e->undo_n++] = e->data;
```

이 코드는 `data`의 내용을 복사하는 것이 아니라 **`data`가 가지고 있던 주소값만 복사한다.**

따라서 `e->data`와 `undo[0]`이 같은 메모리를 가리키게 된다.

그 상태에서 `realloc()`이 `data`를 다른 주소로 이동시키면서 기존 메모리는 해제되었지만, `undo[0]`에는 이전 주소가 그대로 남았다.

결국 `undo[0]`은 dangling pointer가 되었고, 마지막 `eb_free()`에서 이미 해제된 주소를 다시 `free()`하면서 `double free`가 발생했다.

이를 해결하기 위해 스냅샷을 저장할 때 별도의 메모리를 할당하고 `memcpy()`로 데이터를 복사하여, `undo`가 독립적인 복사본을 소유하도록 수정했다.