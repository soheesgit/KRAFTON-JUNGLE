# Challenge 04 — Double Free

## 문제 상황

`Directory`는 하나의 `Rec` 객체를 `by_id`, `by_name` 두 배열에서 함께 참조하고 있다.

```c
static void directory_add(Directory *d, int id, const char *name) {
    Rec *r = rec_new(id, name);

    d->by_id[d->count]   = r;
    d->by_name[d->count] = r;  /* 같은 포인터를 두 인덱스에 함께 등록 */

    d->count++;
}
```

여기서 중요한 점은 `by_id`와 `by_name`이 서로 다른 객체를 가지는 것이 아니라, 같은 `Rec` 객체를 가리키는 포인터를 각각 저장한다는 것이다.

## 포인터 정렬 과정

`by_name` 정렬 코드를 보면 객체 자체를 이동시키는 것이 아니라, 포인터의 위치만 서로 교환한다.

```c
Rec *t = d->by_name[i];
d->by_name[i] = d->by_name[j];
d->by_name[j] = t;
```

동작 과정은 다음과 같다.

```text
1. by_name[i]가 가리키던 주소를 t에 저장
2. by_name[i]에 by_name[j]의 주소를 저장
3. by_name[j]에 기존 by_name[i]의 주소를 저장
```

즉 정렬 이후에도 `by_id`와 `by_name`은 여전히 같은 객체들을 가리키고 있으며, 순서만 다르다.


## 원인 추적

기존 `directory_free()`는 다음과 같이 작성되어 있었다.

```c
static void directory_free(Directory *d) {
    for (int i = 0; i < d->count; i++) {
        free(d->by_id[i]->name);
        free(d->by_id[i]);
    }

    for (int i = 0; i < d->count; i++) {
        free(d->by_name[i]);
    }

    d->count = 0;
}
```

첫 번째 반복문에서 `by_id`가 가리키는 `Rec` 객체를 이미 해제한다.

하지만 `by_name`도 같은 `Rec` 객체를 가리키고 있기 때문에 두 번째 반복문에서 같은 메모리를 다시 `free`하게 된다.

따라서 다음 코드에서 double free가 발생할 것으로 예상했다.

```c
free(d->by_name[i]);
```


## GDB 실행 결과

```text
by id:  3:carol 1:alice 4:dave 2:bob
by name: alice(1) bob(2) carol(3) dave(4)
lookup id=2 -> bob
free(): double free detected in tcache 2

Program received signal SIGABRT, Aborted.
```

백트레이스를 확인했다.

```text
(gdb) bt
#0  0x0000fffff7e774d8 in ?? () from /lib/aarch64-linux-gnu/libc.so.6
#1  0x0000fffff7e2cb3c in raise () from /lib/aarch64-linux-gnu/libc.so.6
#2  0x0000fffff7e17e00 in abort () from /lib/aarch64-linux-gnu/libc.so.6
#3  0x0000fffff7e6aac4 in ?? () from /lib/aarch64-linux-gnu/libc.so.6
#4  0x0000fffff7e81fdc in ?? () from /lib/aarch64-linux-gnu/libc.so.6
#5  0x0000fffff7e844fc in ?? () from /lib/aarch64-linux-gnu/libc.so.6
#6  0x0000fffff7e86e68 in free () from /lib/aarch64-linux-gnu/libc.so.6
#7  0x0000aaaaaaaa0f04 in directory_free (d=0xffffffffee60) at challenges/04_double_free/bug.c:108
#8  0x0000aaaaaaaa1018 in main () at challenges/04_double_free/bug.c:127
```

예상했던 `directory_free()` 내부의 두 번째 `free` 과정에서 오류가 발생했다.


## 포인터 주소 확인

두 배열이 정말 같은 객체를 가리키는지 GDB로 확인했다.

```text
(gdb) p d->by_id
$1 = {0xaaaaaaac12a0, 0xaaaaaaac12e0, 0xaaaaaaac1320, 0xaaaaaaac1360, 0x0 <repeats 12 times>}

(gdb) p d->by_name
$3 = {0xaaaaaaac12e0, 0xaaaaaaac1360, 0xaaaaaaac12a0, 0xaaaaaaac1320, 0x0 <repeats 12 times>}
```

순서는 다르지만 두 배열에 포함된 주소는 동일했다.

즉 `by_name`은 새로운 객체를 소유하는 배열이 아니라, `by_id`가 가리키는 객체들을 다른 순서로 참조하는 인덱스였다.


## 수정

객체의 소유권을 `by_id`에만 두고, `by_name`은 참조만 하도록 수정했다.

```c
static void directory_free(Directory *d) {
    for (int i = 0; i < d->count; i++) {
        free(d->by_id[i]->name);
        free(d->by_id[i]);
    }

    d->count = 0;
}
```

`by_name`에서는 더 이상 `free`하지 않는다.


## 수정 후 실행 결과

```text
(gdb) run
Starting program: /work/build/04_double_free

by id:  3:carol 1:alice 4:dave 2:bob
by name: alice(1) bob(2) carol(3) dave(4)
lookup id=2 -> bob
done

[Inferior 1 (process 34012) exited normally]
```

정상적으로 종료되었다.

## 정리

이번 문제의 원인은 `by_id`와 `by_name`이 서로 다른 객체를 소유한다고 생각하기 쉬웠지만, 실제로는 **동일한 `Rec` 객체를 가리키는 별칭(alias)** 이었다는 점이다.

```text
by_id   → 객체를 소유하고 free 담당
by_name → 같은 객체를 참조만 함
```

같은 객체를 여러 포인터가 가리킬 수는 있지만, 메모리 해제는 소유권을 가진 한 곳에서만 수행해야 한다.

핵심은 다음과 같다.

```text
같은 주소를 두 번 free하면 double free가 발생한다.
포인터가 여러 개여도 객체는 하나일 수 있다.
소유권과 참조 관계를 명확히 구분해야 한다.
```