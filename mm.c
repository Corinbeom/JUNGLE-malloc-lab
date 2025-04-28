// 🧹 malloc-lab 최종 수정 버전 (분리 가용 리스트 + first fit)

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include "mm.h"
#include "memlib.h"

team_t team = {
    "ateam", "Harry Bovik", "bovik@cs.cmu.edu", "", ""
};

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1 << 12) // 4096 bytes

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc))

#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

#define PRED(bp) (*(void **)(bp))
#define SUCC(bp) (*(void **)((char *)(bp) + WSIZE))

#define NUM_CLASSES 8

static char *heap_listp = NULL;
static void *segregated_free_lists[NUM_CLASSES];

// find size class
static int find_size_class(size_t size) {
    if (size <= 16) return 0;
    else if (size <= 32) return 1;
    else if (size <= 64) return 2;
    else if (size <= 128) return 3;
    else if (size <= 256) return 4;
    else if (size <= 512) return 5;
    else if (size <= 1024) return 6;
    else return 7;
}

// insert node
static void insert_node(void *bp) {
    int class_idx = find_size_class(GET_SIZE(HDRP(bp)));

    SUCC(bp) = segregated_free_lists[class_idx];
    PRED(bp) = NULL;
    if (segregated_free_lists[class_idx] != NULL)
        PRED(segregated_free_lists[class_idx]) = bp;
    segregated_free_lists[class_idx] = bp;
}

// remove node
static void remove_node(void *bp) {
    int class_idx = find_size_class(GET_SIZE(HDRP(bp)));

    if (PRED(bp))
        SUCC(PRED(bp)) = SUCC(bp);
    else
        segregated_free_lists[class_idx] = SUCC(bp);

    if (SUCC(bp))
        PRED(SUCC(bp)) = PRED(bp);
}

// coalesce
static void *coalesce(void *bp) {
    bool prev_alloc = (PREV_BLKP(bp) < (void *)mem_heap_lo()) || GET_ALLOC(HDRP(PREV_BLKP(bp)));
    bool next_alloc = (NEXT_BLKP(bp) > (void *)mem_heap_hi()) || GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {
        insert_node(bp);
        return bp;
    }
    else if (prev_alloc && !next_alloc) {
        remove_node(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc) {
        remove_node(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else {
        remove_node(PREV_BLKP(bp));
        remove_node(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    insert_node(bp);
    return bp;
}

// extend heap
static void *extend_heap(size_t words) {
    size_t size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    void *bp;

    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    return coalesce(bp);
}

// find fit
static void *find_fit(size_t asize) {
    for (int i = find_size_class(asize); i < NUM_CLASSES; i++) {
        void *bp = segregated_free_lists[i];
        while (bp != NULL) {
            if (GET_SIZE(HDRP(bp)) >= asize)
                return bp;
            bp = SUCC(bp);
        }
    }
    return NULL;
}

// place
static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    remove_node(bp);

    if ((csize - asize) >= (2 * DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));

        void *next_bp = NEXT_BLKP(bp);
        PUT(HDRP(next_bp), PACK(csize - asize, 0));
        PUT(FTRP(next_bp), PACK(csize - asize, 0));
        insert_node(next_bp);
    }
    else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

//////////////////////////////////////////////////////////

// mm_init
int mm_init(void) {
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0);
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));
    heap_listp += (2 * WSIZE);

    for (int i = 0; i < NUM_CLASSES; i++)
        segregated_free_lists[i] = NULL;

    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;

    return 0;
}

// mm_malloc
void *mm_malloc(size_t size) {
    size_t asize;
    size_t extendsize;
    char *bp;

    if (size == 0)
        return NULL;

    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);

    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;

    place(bp, asize);
    return bp;
}

// mm_free
void mm_free(void *bp) {
    if (bp == NULL)
        return;

    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

// mm_realloc
void *mm_realloc(void *ptr, size_t size) {
    if (ptr == NULL)
        return mm_malloc(size);  // ptr이 NULL이면 malloc과 같은 방식으로 처리
    if (size == 0) {
        mm_free(ptr);  // size가 0이면 해당 블록을 free하고 NULL 반환
        return NULL;
    }

    size_t oldsize = GET_SIZE(HDRP(ptr));  // 기존 블록의 크기 가져오기
    size_t asize = (size <= DSIZE) ? (2 * DSIZE) : DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);

    if (asize <= oldsize)
        return ptr;  // 기존 크기가 충분하면 기존 포인터 그대로 반환

    void *next = NEXT_BLKP(ptr);  // 다음 블록 주소
    if (!GET_ALLOC(HDRP(next)) && (oldsize + GET_SIZE(HDRP(next))) >= asize) {
        remove_node(next);  // 만약 옆 블록이 free이고 크기가 충분하면 병합
        size_t newsize = oldsize + GET_SIZE(HDRP(next));  // 병합 후 새로운 크기
        PUT(HDRP(ptr), PACK(newsize, 1));  // 헤더 업데이트
        PUT(FTRP(ptr), PACK(newsize, 1));  // 푸터 업데이트
        return ptr;  // 병합된 블록 반환
    }

    void *newptr = mm_malloc(size);  // 병합할 수 없다면 새로운 메모리 할당
    if (newptr == NULL)
        return NULL;  // 할당 실패하면 NULL 반환

    size_t copySize = oldsize - DSIZE;  // 기존 데이터 크기
    if (size < copySize)
        copySize = size;  // 복사할 크기를 요청된 크기로 맞춤
    memcpy(newptr, ptr, copySize);  // 데이터 복사
    mm_free(ptr);  // 기존 블록은 free
    return newptr;  // 새로운 포인터 반환
}