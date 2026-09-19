## TODO

```c
Widget *w = malloc(sizeof *w);
```

`w`는 `Widget *` 타입이므로 `*w`의 타입은 `Widget`이다.  
`sizeof`는 일반적으로 값을 실제로 평가하지 않고 타입의 크기만 계산하므로, 아직 `w`가 유효한 객체를 가리키지 않아도 `sizeof *w`를 사용할 수 있다.

#### sizeof(Widget) 대신 sizeof *w 로 쓰면 어떤 장점이 있을까?

`sizeof *w`를 쓰면 타입 이름을 직접 반복하지 않아도 된다.
따라서 포인터의 타입이 바뀌어도 sizeof 부분을 따로 수정할 필요가 없어
유지보수가 쉽고 타입 불일치 실수를 줄일 수 있다.

---

## 문제 풀이 과정 — Use After Free

프로그램 실행 중 다음 위치에서 `SIGSEGV`가 발생했다.

```c
w->vtbl->render(w);
```

먼저 각 포인터가 정상인지 확인했다.

```gdb
p w
p w->vtbl
p w->vtbl->render
```

`w->vtbl`에 비정상적인 주소가 들어 있었고, 실제 `DIALOG_VT` 주소와도 달랐다.

```gdb
p &DIALOG_VT
```

어떤 객체인지 확인해 보니 문제가 발생한 `w`는 `items[2]`의 Dialog였다.

```gdb
p i
p s->items[i]
p w
```

코드를 확인해 보니 Dialog가 닫힐 때 자기 자신을 바로 `free()`하고 있었다.

```c
static void dialog_on_event(Widget *self, int code) {
    if (code == 1) {
        self->closed = 1;
        widget_destroy(self);
    }
}
```

가설은 다음과 같았다.

> Dialog는 이미 `free()`됐지만 `Screen.items[2]`는 여전히 같은 주소를 가지고 있어서 다음 렌더링에서 다시 접근한다.

`break widget_destroy`를 걸어 확인해 보니 실제로 크래시 때 사용된 주소와 `free()`된 주소가 같았다.

즉 원인은 **Use After Free**였다.

### 수정

Dialog가 직접 `free()`하지 않고 닫힘 상태만 표시하도록 변경했다.

```c
self->closed = 1;
```

이후 `Screen`이 닫힌 객체를 직접 정리하도록 수정했다.

```c
if (w->closed) {
    widget_destroy(w);
    s->items[i] = NULL;
}
```

렌더링할 때는 `NULL`인 항목을 건너뛴다.

```c
if (w == NULL) continue;
```

수정 후에는 `SIGSEGV` 없이 정상적으로 실행되었다.