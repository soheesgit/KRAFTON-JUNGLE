# 미초기화 행 포인터 역참조

처음 코드를 확인했을 때 다음 `memset()` 부분이 수상하다고 생각했다.

```c
memset(scratch, 0xAB, ROWS * sizeof(int *));
```

`memset()`은 두 번째 인자로 받은 값을 바이트 단위로 메모리에 채운다.

따라서 위 코드는 `ROWS * sizeof(int *)` 크기의 메모리를 `0xAB`로 채운다.

처음에는 `0xAB`로 초기화하는 것이 문제라고 생각했지만, 다시 확인해 보니 이 코드는 의도적으로 힙에 쓰레기 값을 남기기 위한 코드였다.

```c
static void dirty_heap(void) {
    void *scratch = malloc(ROWS * sizeof(int *));

    if (scratch) {
        memset(scratch, 0xAB, ROWS * sizeof(int *));
        free(scratch);
    }
}
```

즉 `dirty_heap()`에서 메모리를 `0xAB`로 채운 뒤 `free()`하고, 이후 같은 크기의 메모리를 다시 할당했을 때 이전 값이 남아 있는 상황을 재현하고 있었다.

문제는 `memset()` 자체가 아니라, 이후 `rows`에서 초기화되지 않은 포인터를 사용하고 있을 가능성이 있다고 생각했다.

## GDB 확인

실제로 실행해 보니 `row_sum()`에서 SIGSEGV가 발생했다.

```text
Program received signal SIGSEGV, Segmentation fault.
0x0000aaaaaaaa0a24 in row_sum (rows=0xaaaaaaac12a0, nrows=32)
    at challenges/08_uninitialized_read/bug.c:93
93                  total += rows[i][j];
```

호출 스택을 확인했다.

```text
(gdb) bt
#0  0x0000aaaaaaaa0a24 in row_sum (rows=0xaaaaaaac12a0, nrows=32)
    at challenges/08_uninitialized_read/bug.c:93
#1  0x0000aaaaaaaa0aac in main ()
    at challenges/08_uninitialized_read/bug.c:105
```

현재까지 계산된 합을 확인했다.

```text
(gdb) p total
$1 = 6
```

그리고 현재 접근하려던 값을 확인했다.

```text
(gdb) p rows[i][j]
Cannot access memory at address 0x0
```

`total`이 `6`이라는 것은 첫 번째 행은 정상적으로 처리되었다는 의미이다.

```text
rows[0] = {0, 1, 2, 3}

0 + 1 + 2 + 3 = 6
```

따라서 첫 번째 행을 계산한 뒤 다음 행을 접근하는 과정에서 문제가 발생했다고 판단했다.

각 행 포인터의 값을 직접 확인했다.

```text
(gdb) p rows[0]
$6 = (int *) 0xaaaaaaac13b0

(gdb) p rows[1]
$8 = (int *) 0x0

(gdb) p rows[2]
$9 = (int *) 0xaaaaaaac13d0

(gdb) p rows[3]
$10 = (int *) 0xabababababababab

(gdb) p rows[4]
$11 = (int *) 0xaaaaaaac13f0

(gdb) p rows[5]
$12 = (int *) 0xabababababababab
```

짝수 인덱스에는 정상적인 주소가 들어 있었지만, 홀수 인덱스에는 `NULL` 또는 `0xabababababababab` 같은 값이 들어 있었다.

`make_matrix()`를 확인해 보니 짝수 행에만 메모리를 할당하고 있었다.

```c
for (int i = 0; i < ROWS; i += 2) {
    int *r = malloc(COLS * sizeof(int));

    for (int j = 0; j < COLS; j++)
        r[j] = i * COLS + j;

    rows[i] = r;
}
```

따라서 구조는 다음과 같았다.

```text
rows[0] -> 할당됨
rows[1] -> 초기화되지 않음
rows[2] -> 할당됨
rows[3] -> 초기화되지 않음
rows[4] -> 할당됨
rows[5] -> 초기화되지 않음
...
```

`rows` 자체를 `malloc()`으로 할당했기 때문에 값을 넣지 않은 홀수 행이 자동으로 `NULL`이 되는 것은 아니었다.

```c
int **rows = malloc(ROWS * sizeof(int *));
```

`rows[3]`, `rows[5]`에서 확인한 `0xabababababababab`은 `dirty_heap()`에서 남겨 둔 값이었다.

`rows[1]`이 `0x0`으로 보인 것도 프로그램에서 명시적으로 `NULL`로 초기화한 결과는 아니며, 메모리 할당자의 내부 동작에 의해 해당 값이 남은 결과이다.

즉 초기화하지 않은 포인터의 값은 신뢰할 수 없었다.

## 잘못된 수정

처음에는 `i += 2`가 문제라고 생각하여 다음과 같이 수정해 보았다.

```c
for (int i = 0; i < ROWS; i++)
```

수정 후에는 정상적으로 실행되었다.

```text
summing 32x4 matrix...
sum = 8128
[Inferior 1 (process 75959) exited normally]
```

`i++`로 변경하면 `rows[0]`부터 `rows[31]`까지 모든 행에 메모리가 할당되므로 미초기화 포인터가 사라진다.

하지만 이 수정은 원래 의도였던 희소 행렬 구조를 없애고 모든 행을 할당한 것이었다.

따라서 `i += 2` 자체가 문제의 원인은 아니었다.

문제는

```text
비어 있는 행의 포인터를 초기화하지 않은 상태에서
모든 rows[i]를 무조건 역참조한 것
```

이었다.

## 수정

희소 행렬 구조는 그대로 유지하고, 사용하지 않는 행을 명확하게 `NULL` 상태로 만들도록 수정했다.

기존 코드에서는 `malloc()`을 사용하고 있었다.

```c
int **rows = malloc(ROWS * sizeof(int *));
```

이를 `calloc()`으로 변경했다.

```c
int **rows = calloc(ROWS, sizeof(int *));
```

이 실습 환경에서는 `rows`의 각 포인터가 처음에 `NULL` 상태가 된다.

이후 기존과 동일하게 짝수 행만 할당하면 다음과 같은 구조가 된다.

```text
rows[0] -> 실제 행
rows[1] -> NULL
rows[2] -> 실제 행
rows[3] -> NULL
rows[4] -> 실제 행
rows[5] -> NULL
...
```

그리고 `row_sum()`에서 `NULL`인 행은 접근하지 않고 건너뛰도록 수정했다.

```c
static long row_sum(int **rows, int nrows) {
    long total = 0;

    for (int i = 0; i < nrows; i++) {
        if (rows[i] == NULL)
            continue;

        for (int j = 0; j < COLS; j++) {
            total += rows[i][j];
        }
    }

    return total;
}
```

## 수정 후 실행 결과

```text
summing 32x4 matrix...
sum = 3936
```

짝수 행에만 데이터가 존재하는 희소 행렬 구조를 유지하면서 정상적으로 합산되었다.

### 정리

```text
rows를 malloc()으로 할당
    ↓
짝수 행에만 실제 주소 저장
    ↓
홀수 행의 포인터는 초기화되지 않음
    ↓
row_sum()에서 모든 rows[i] 접근
    ↓
NULL 또는 쓰레기 포인터 역참조
    ↓
SIGSEGV
```

`i += 2`가 문제였던 것이 아니라, 사용하지 않는 행의 포인터를 초기화하지 않고 접근한 것이 문제였다.

포인터 배열에서 사용하지 않는 원소가 있다면 `calloc()` 또는 명시적인 초기화를 통해 `NULL` 상태로 만들고, 역참조하기 전에 `NULL` 여부를 확인해야 한다.