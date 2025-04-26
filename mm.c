#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1 << 12)

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
    ""
};

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

#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

static char *heap_listp = NULL;
static void *free_listp = NULL;
static void *last_bp = NULL;

//////////////////////////////////////////////////////////

// insert at head
static void insert_node(void *bp) {
    if (!bp) return;
    SUCC(bp) = free_listp;
    PRED(bp) = NULL;
    if (free_listp) {
        PRED(free_listp) = bp;
    }
    free_listp = bp;
}

// remove node
static void remove_node(void *bp) {
    if (!bp) return;
    if (PRED(bp)) {
        SUCC(PRED(bp)) = SUCC(bp);
    } else {
        free_listp = SUCC(bp);
    }
    if (SUCC(bp)) {
        PRED(SUCC(bp)) = PRED(bp);
    }
    PRED(bp) = NULL;
    SUCC(bp) = NULL;
}

// coalesce
static void *coalesce(void *bp) {
    size_t prev_alloc = (HDRP(PREV_BLKP(bp)) < (char *)mem_heap_lo()) ? 1 : GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = (HDRP(NEXT_BLKP(bp)) > (char *)mem_heap_hi()) ? 1 : GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {
        insert_node(bp);
        return bp;
    }
    else if (prev_alloc && !next_alloc) {
        void *next = NEXT_BLKP(bp);
        remove_node(next);
        size += GET_SIZE(HDRP(next));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        insert_node(bp);
    }
    else if (!prev_alloc && next_alloc) {
        void *prev = PREV_BLKP(bp);
        remove_node(prev);
        size += GET_SIZE(HDRP(prev));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(prev), PACK(size, 0));
        bp = prev;
        insert_node(bp);
    }
    else {
        void *prev = PREV_BLKP(bp);
        void *next = NEXT_BLKP(bp);
        remove_node(prev);
        remove_node(next);
        size += GET_SIZE(HDRP(prev)) + GET_SIZE(FTRP(next));
        PUT(HDRP(prev), PACK(size, 0));
        PUT(FTRP(next), PACK(size, 0));
        bp = prev;
        insert_node(bp);
    }

    return bp;
}

// extend_heap
static void *extend_heap(size_t words) {
    char *bp;
    size_t size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

    if ((long)(bp = mem_sbrk(size)) == -1) return NULL;

    PUT(HDRP(bp), PACK(size, 0));            // Free block header
    PUT(FTRP(bp), PACK(size, 0));            // Free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));    // New epilogue header

    return coalesce(bp);
}

// find_fit (next fit)
static void *find_fit(size_t asize) {
    void *bp = last_bp ? last_bp : free_listp;

    if (bp == NULL) return NULL;
    void *start_bp = bp;

    do {
        if (!GET_ALLOC(HDRP(bp)) && GET_SIZE(HDRP(bp)) >= asize) {
            last_bp = bp;
            return bp;
        }
        bp = SUCC(bp);
        if (bp == NULL) {
            bp = free_listp;
        }
    } while (bp != start_bp && bp != NULL);

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
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }

    last_bp = NULL;  // next_fit 신뢰성 위해
}

//////////////////////////////////////////////////////////

// mm_init
int mm_init(void) {
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1) return -1;
    PUT(heap_listp, 0);                            // Alignment padding
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));  // Prologue header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));  // Prologue footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));      // Epilogue header
    heap_listp += (2 * WSIZE);

    free_listp = NULL;
    last_bp = NULL;

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) return -1;
    return 0;
}

// mm_malloc
void *mm_malloc(size_t size) {
    size_t asize;
    size_t extendsize;
    char *bp;

    if (size == 0) return NULL;
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) return NULL;
    place(bp, asize);
    return bp;
}

// mm_free
void mm_free(void *bp) {
    if (!bp) return;
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

// mm_realloc
void *mm_realloc(void *ptr, size_t size) {
    if (ptr == NULL) return mm_malloc(size);
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    void *newptr = mm_malloc(size);
    if (newptr == NULL) return NULL;

    size_t oldsize = GET_SIZE(HDRP(ptr)) - DSIZE;
    if (size < oldsize) oldsize = size;
    memcpy(newptr, ptr, oldsize);
    mm_free(ptr);
    return newptr;
}
