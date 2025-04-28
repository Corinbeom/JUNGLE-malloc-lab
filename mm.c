#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"

team_t team = {
    "ateam",
    "Harry Bovik",
    "bovik@cs.cmu.edu",
    "",
    ""
};

#define ALIGNMENT 8
#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1 << 12)
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

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

#define NEXT_FREE(bp) (*(void **)(bp))
#define PREV_FREE(bp) (*(void **)((char *)(bp) + WSIZE))
#define PUT_NEXT_ADDRESS(bp, addr) (*(void **)(bp) = (addr))
#define PUT_PREV_ADDRESS(bp, addr) (*(void **)((char *)(bp) + WSIZE) = (addr))

#define NUM_CLASS 16   // 🔥 클래스 더 세분화!

static void *heap_listp = NULL;
static void *segregated_free_lists[NUM_CLASS];

static int get_class(size_t size) {
    int class_idx = 0;
    while (class_idx < NUM_CLASS - 1 && size > 1) {
        size >>= 1;
        class_idx++;
    }
    return class_idx;
}

static void insert_node(void *bp) {
    int class_idx = get_class(GET_SIZE(HDRP(bp)));
    void *head = segregated_free_lists[class_idx];

    if (head != NULL)
        PUT_PREV_ADDRESS(head, bp);

    PUT_NEXT_ADDRESS(bp, head);
    PUT_PREV_ADDRESS(bp, NULL);
    segregated_free_lists[class_idx] = bp;
}

static void remove_node(void *bp) {
    int class_idx = get_class(GET_SIZE(HDRP(bp)));

    if (PREV_FREE(bp)) {
        PUT_NEXT_ADDRESS(PREV_FREE(bp), NEXT_FREE(bp));
    } else {
        segregated_free_lists[class_idx] = NEXT_FREE(bp);
    }

    if (NEXT_FREE(bp))
        PUT_PREV_ADDRESS(NEXT_FREE(bp), PREV_FREE(bp));
}

static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {
        insert_node(bp);
        return bp;
    }
    if (prev_alloc && !next_alloc) {
        remove_node(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc) {
        remove_node(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else {
        remove_node(PREV_BLKP(bp));
        remove_node(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    insert_node(bp);
    return bp;
}

static void *extend_heap(size_t words) {
    size_t size = ALIGN(words * WSIZE);
    char *bp = mem_sbrk(size);

    if ((long)bp == -1)
        return NULL;

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    return coalesce(bp);
}

int mm_init(void) {
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0);
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));
    heap_listp += (2 * WSIZE);

    for (int i = 0; i < NUM_CLASS; i++)
        segregated_free_lists[i] = NULL;

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    return 0;
}

// 🔥 best-fit within class
static void *find_fit(size_t asize) {
    int class_idx = get_class(asize);
    void *bp = NULL;
    void *best_fit = NULL;

    for (; class_idx < NUM_CLASS; class_idx++) {
        for (bp = segregated_free_lists[class_idx]; bp != NULL; bp = NEXT_FREE(bp)) {
            if (GET_SIZE(HDRP(bp)) >= asize) {
                if (best_fit == NULL || GET_SIZE(HDRP(bp)) < GET_SIZE(HDRP(best_fit))) {
                    best_fit = bp;
                }
            }
        }
        if (best_fit != NULL)
            break;
    }
    return best_fit;
}

static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    remove_node(bp);

    if ((csize - asize) >= (2 * DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        void *next_bp = NEXT_BLKP(bp);
        PUT(HDRP(next_bp), PACK(csize - asize, 0));
        PUT(FTRP(next_bp), PACK(csize - asize, 0));
        coalesce(next_bp);
    }
    else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

void *mm_malloc(size_t size) {
    size_t asize;
    size_t extendsize;
    char *bp;

    if (size == 0)
        return NULL;

    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

void mm_free(void *bp) {
    if (bp == NULL) return;

    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

void *mm_realloc(void *bp, size_t size) {
    if (bp == NULL) return mm_malloc(size);
    if (size == 0) {
        mm_free(bp);
        return NULL;
    }

    size_t oldsize = GET_SIZE(HDRP(bp));
    size_t asize = (size <= DSIZE) ? (2 * DSIZE) : DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    if (asize <= oldsize)
        return bp;

    void *next = NEXT_BLKP(bp);
    if (!GET_ALLOC(HDRP(next)) && (oldsize + GET_SIZE(HDRP(next))) >= asize) {
        remove_node(next);
        size_t newsize = oldsize + GET_SIZE(HDRP(next));
        PUT(HDRP(bp), PACK(newsize, 1));
        PUT(FTRP(bp), PACK(newsize, 1));
        return bp;
    }

    void *newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;

    size_t copySize = oldsize - DSIZE;
    if (size < copySize)
        copySize = size;

    memcpy(newptr, bp, copySize);
    mm_free(bp);

    return newptr;
}
