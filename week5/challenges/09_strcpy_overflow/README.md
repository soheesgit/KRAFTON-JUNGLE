# Challenge 09 — `strcpy` 힙 오버플로

## 문제 원인

`joined_size()`에서 필요한 메모리 크기를 계산할 때 반복문이 다음과 같이 작성되어 있었다.

```c
for (int i = 0; i < n - 1; i++) {
    total += strlen(parts[i]);
}
```

`parts`의 마지막 요소는 `parts[n - 1]`인데, 위 반복문에서는 마지막 문자열의 길이가 계산에서 제외된다.

반면 실제 복사 과정에서는:

```c
for (int i = 0; i < n; i++) {
    strcpy(out + off, parts[i]);
    off += strlen(parts[i]);
}
```

마지막 문자열까지 모두 복사한다.

즉, **할당 크기에는 마지막 문자열이 포함되지 않았지만 실제 복사에서는 마지막 문자열까지 복사하면서 힙 오버플로가 발생했다.**


## GDB 확인

프로그램을 실행하면 `strcpy()`에서 `SIGSEGV`가 발생했다.

```text
Program received signal SIGSEGV, Segmentation fault.
0x0000fffff7e8d328 in strcpy ()
```

`bt`로 확인한 결과:

```text
#0  strcpy ()
#1  join (...) at bug.c:54
#2  main (...) at bug.c:70
```

`join()`의 다음 코드에서 크래시가 발생했다.

```c
strcpy(out + off, parts[i]);
```

GDB에서 값을 확인하면:

```text
need = 29
off = 28
```

앞의 세 문자열 길이 합이 `28`이고, 종료 문자 `'\0'`를 포함해 `29`바이트만 할당된 상태였다.

하지만 마지막 `body`의 길이는 `199999`바이트이므로 실제 필요한 크기는:

```text
28 + 199999 + 1 = 200028 bytes
```

이다.


## 수정

반복문 범위를 `n - 1`이 아니라 `n`까지 돌도록 수정했다.

```c
for (int i = 0; i < n; i++) {
    total += strlen(parts[i]);
}
```

이제 크기 계산과 실제 복사 범위가 동일해졌다.


## 결과

수정 후 실행 결과:

```text
joined length = 200027
[Inferior 1 exited normally]
```

프로그램이 `SIGSEGV` 없이 정상 종료했다.

## 정리

원인은 `strcpy()` 자체가 아니라 `joined_size()`의 off-by-one 오류였다.

```text
크기 계산: i < n - 1
실제 복사: i < n
```

두 반복문의 범위를 동일하게 수정하여 문제를 해결했다.