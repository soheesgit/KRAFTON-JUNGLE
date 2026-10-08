#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
// suzefnf ALIGNMENT의 단위(8)에 맞게 올림하는 코드
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define MAX_HEAP (20*(1<<20))  /* 20 MB */

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<12) // 4KB
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc))

#define GET(p) (*(unsigned int *)(p)) //p에서 4byte만큼 GET해와
#define PUT(p, val) (*(unsigned int *)(p) = (val)) // p에서 4byte만큼 가져와서 val을 저장

#define GET_SIZE(p) (GET(p) & ~0x7) // 블록 크기 가져옴
#define GET_ALLOC(p) (GET(p) & 0x1) // 할당 여부

#define HDRP(bp) ((char *)(bp) - WSIZE)// 현재 블록의 payload 포인터(bp)로부터 header 주소를 구함
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) // 현재 블록의 footer 주소를 구함

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE((char *)(bp) - WSIZE)) // 다음 블록의 payload 시작 주소를 구함
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE)) // 이전 블록의 payload 시작 주소를 구함

static char *heap_listp;
static void *coalesce(void *ptr);
static void *rover; // 다음 탐색을 시작할 위치


static size_t realloc_case1_count = 0;
static size_t realloc_case2_count = 0;
static size_t realloc_case3_count = 0;

static size_t malloc_extend_count = 0;
static size_t malloc_extend_bytes = 0;

static int debug_registered = 0;

static void print_debug_stats(void) {
    printf("\n===== DEBUG STATS =====\n");
    printf("realloc case 1 (current reuse) : %zu\n", realloc_case1_count);
    printf("realloc case 2 (merge next)    : %zu\n", realloc_case2_count);
    printf("realloc case 3 (malloc+copy)   : %zu\n", realloc_case3_count);

    printf("malloc heap extend count       : %zu\n", malloc_extend_count);
    printf("malloc heap extend bytes       : %zu\n", malloc_extend_bytes);
    printf("=======================\n");
}

// words만큼 힙 확장 
static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    // 8의 배수가 나오도록 size 지정
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1) return NULL;

    PUT(HDRP(bp), PACK(size, 0)); // 새 free 블록의 헤더
    PUT(FTRP(bp), PACK(size, 0)); // 새 free 블록의 풋터
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // 확장된 힙의 가장 마지막에 새로운 에필로그 헤더 생성

    return coalesce(bp); // 앞의 블록이 free라면 병합
}

static void *find_fit(size_t asize) {
    void *ptr;

    for (ptr = heap_listp; GET_SIZE(HDRP(ptr)) > 0; ptr = NEXT_BLKP(ptr)) {
        if (!GET_ALLOC(HDRP(ptr)) && (asize <= GET_SIZE(HDRP(ptr)))) {
            return ptr;
        }
    }
    return NULL;
}

static void *next_fit(size_t asize) {
    void *ptr;

    // 최근 위치에서 시작해서 끝까지 돈다
    for (ptr = rover; GET_SIZE(HDRP(ptr)) > 0; ptr = NEXT_BLKP(ptr)) {
        if (!GET_ALLOC(HDRP(ptr)) && (asize <= GET_SIZE(HDRP(ptr)))) {
            rover = ptr;
            return ptr;
        }
    }

    // 끝까지 돌았는데도 할당하지 못했을 경우에, 처음부터 rover 위치까지 탐색한다.
    for (ptr = heap_listp; ptr != rover; ptr = NEXT_BLKP(ptr)) {
            if (!GET_ALLOC(HDRP(ptr)) && (asize <= GET_SIZE(HDRP(ptr)))) {
                rover = ptr;
            return ptr;
        }
    }
    return NULL;
}

static void *best_fit(size_t asize) {
    void *ptr;
    void *bestptr = NULL;
    size_t bestSize = (size_t)-1;

    for (ptr = heap_listp; GET_SIZE(HDRP(ptr)) > 0; ptr = NEXT_BLKP(ptr)) {
        size_t nowSize = GET_SIZE(HDRP(ptr));
        if (!GET_ALLOC(HDRP(ptr)) && (asize <= nowSize) && (bestSize > nowSize)) {
            bestptr = ptr;
            bestSize = nowSize; 
        }
    }

    return bestptr;
}

// 할당하는 함수
static void place(void *ptr, size_t asize) {
    size_t csize = GET_SIZE(HDRP(ptr)); // 현재 free 블록의 전체 크기를 가져온다

    if ((csize - asize) >= (2*DSIZE)) { // 남는 공간이 16바이트 이상이면 쪼갠다
        // 현재 헤더와 풋터에 블록 크기를 적고, 할당 상태라고 표시
        PUT(HDRP(ptr), PACK(asize, 1)); 
        PUT(FTRP(ptr), PACK(asize, 1));

        // 다음 포인터
        ptr = NEXT_BLKP(ptr);

        // 쪼갠 나머지 블록은,새로운 free 블록으로 만듬
        PUT(HDRP(ptr), PACK(csize - asize, 0));
        PUT(FTRP(ptr), PACK(csize - asize, 0));
    }
    else {
        // 쪼개기엔 너무 작으니 전체 할당
        PUT(HDRP(ptr), PACK(csize, 1));
        PUT(FTRP(ptr), PACK(csize, 1));
    }
}
    
/* 
 * mm_init에서는 초기 힙 영역을 확보하는 등의 필요한 초기화 작업을 수행해야 합니다.
 * 초기화 과정에서 문제가 발생하면 -1을 반환하고, 그렇지 않으면 0을 반환해야 합니다.
 */
int mm_init(void)
{
    if (!debug_registered) {
    atexit(print_debug_stats);
    debug_registered = 1;
    }

    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *) - 1) return -1; // 힙 초기 공간을 확보하고, 실패하면 -1 반환

    PUT(heap_listp, 0); // 패딩 값 선언
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); // 헤더
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); // 풋터
    PUT(heap_listp + (3*WSIZE), PACK(0, 1)); // 에필로그 헤더 (맨 마지막 헤더)
    heap_listp += (2 * WSIZE); // 힙리스트가 프롤롤그 블록의 bp 위치를 가리키도록 이동
    rover = heap_listp;

    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) return -1; // 초기 힙을 확장해보고 실패하면 -1 반환

    return 0;    
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multipl    of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    
    char *ptr;

    if (size == 0) return NULL;

    // 8바이트 정렬 조건에 맞춰서 할당. 최소 16바이트 할당함
    if (size <= DSIZE)
        asize = 2 * DSIZE; 
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);   // header/footer와 8바이트 정렬을 고려한 실제 블록 크기

    if ((ptr = next_fit(asize)) != NULL) { // 기존 free block 중 들어갈 곳을 찾음.
        place(ptr, asize); // place로 할당

        return ptr;
    }

    extendsize = MAX(asize, CHUNKSIZE); // 맞는 free block이 없으면 asize와 4KB 중 큰 크기만큼 힙을 확장
    if ((ptr = extend_heap(extendsize/WSIZE)) == NULL) return NULL;

    malloc_extend_count++;
    malloc_extend_bytes += extendsize;

    place(ptr, asize);
    return ptr;
}

/*
 * mm_free - 블록을 free 상태로 변경하고,
 * 인접한 free 블록이 있다면 coalesce를 통해 병합한다.
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0)); 
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr); // 병합
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    if (ptr == NULL)
    return mm_malloc(size);

    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    size_t csize = GET_SIZE(HDRP(ptr)); // 현재 실제 슬록 크기 계산
    size_t asize; // 할당할 size의 실제 블록 크기

    // 8바이트 정렬 조건에 맞춰서 할당. 최소 16바이트 할당함
    if (size <= DSIZE)
        asize = 2 * DSIZE; 
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);   // header/footer와 8바이트 정렬을 고려한 실제 블록 크기

    // 1.현재 블록이 충분히 크다면, 이미 할당된 것이다. 그냥 return 해준다.
    if (csize >= asize) {
        realloc_case1_count++;

        place(ptr, asize);
        return ptr;
    }

    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr))); // 다음 블록의 할당 여부
    size_t sum_size = csize + GET_SIZE(HDRP(NEXT_BLKP(ptr))); // 이번 블록과 다음 블록의 크기를 합침
    // 2. 뒤의 블록이랑 합쳐서 할당할 수 있다면, 할당한다.
    if (!next_alloc && sum_size >= asize) { 
        realloc_case2_count++;
        if (rover == NEXT_BLKP(ptr)) {
        rover = ptr;
     }
        PUT(HDRP(ptr), PACK(sum_size, 1)); // 큰 범위를 allocated로 표시
        PUT(FTRP(ptr), PACK(sum_size, 1));

        place(ptr, asize);

        return ptr;
    }

    // 3.  안되면 결국 새 포인터 만들어서 할당
    realloc_case3_count++;

    void *oldptr = ptr; // 기존 포인터
    void *newptr; // 새로 할당할 포인터

    size_t copySize = csize - DSIZE; // 현재 블록에서 Payload 크기만 계산
    newptr = mm_malloc(size); // 새로운 포인터를 크기만큼 할당한다
    if (newptr == NULL) // 할당 실패 시 NULL을 return한다
      return NULL;
    
    if (size < copySize) // 기존 데이터 크기와 새로운 공간 
      copySize = size;
    memcpy(newptr, oldptr, copySize); // 실제 데이터 복사 
    mm_free(oldptr); // 이전 포인터 프리해주기
    return newptr;
}

static void *coalesce(void *ptr) {
    // 현재 free가 된 블록의 앞뒤를 확인해서, 옆 블록도 free라면 하나로 합치는 함수

    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr))); // 이전 블록의 할당 여부
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr))); // 다음 블록의 할당 여부
    size_t size = GET_SIZE(HDRP(ptr)); // 현재 크기

    if (prev_alloc && next_alloc) return ptr; // 이전과 다음이 블록이 모두 할당되었다면, 병합할 블록이 없으므로 그대로 반환

    else if (prev_alloc && !next_alloc) { // 앞은 allocated, 뒤는 free
        size += GET_SIZE(HDRP(NEXT_BLKP(ptr))); // 이번 블록과 다음 블록의 크기를 합침
        PUT(HDRP(ptr), PACK(size, 0)); // 큰 범위를 free라고 표시
        PUT(FTRP(ptr), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc) { // 앞은 free, 뒤는 allocated 
        size += GET_SIZE(HDRP(PREV_BLKP(ptr))); // 이번 블록과 앞의 블록의 크기를 합침
        PUT(FTRP(ptr), PACK(size, 0)); // 큰 범위를 free라고 표시
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }
    else {
        // 앞, 뒤 둘 다 free 블록인 경우
        size += GET_SIZE(HDRP(PREV_BLKP(ptr))) + GET_SIZE(FTRP(NEXT_BLKP(ptr))); // 이번 블록 + 앞의 블록 + 뒤의 블록 크기를 더함
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }

    // 만약 rover 포인터의 위치가 
    if ((char *)rover > (char *)ptr && (char *)rover < (char *)NEXT_BLKP(ptr)) {
    rover = ptr;
    }
    return ptr;
}