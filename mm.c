/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
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
    ""
};

/*   기본 매크로 설정   */
#define WSIZE 4  //  워드 크기. 헤더나 푸터 한 개의 크기. 보통 4바이트 (32비트)
#define DSIZE 8  //  더블 워드 크기. 페이로드 정렬을 위해 최소 블록 크기로 사용. 보통 8바이트 (밑에 ALIGN과 관련이 있는지 알아볼 것)
#define CHUNKSIZE (1 << 12)  //  초기 힙 확장 단위. 힙을 확장할 때 한 번에 이만큼 요청. 일반적으로 4096 바이트 (4KB)


#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc))  //  블록의 크기와 할당 상태(0또는 1)를 하나의 값으로 포장. 하위 비트를 alloc에 사용

#define GET(p) (*(unsigned int *)(p))  // 포인터 p가 가리키는 메모리에서 워드 단위 값 읽기
#define PUT(p, val) (*(unsigned int *)(p) = (val))  //  포인터 p가 가리키는 메모리에 워드 단위 값 쓰기

#define GET_SIZE(p) (GET(p) & ~0x7)  //  블록 크기 추출 (하위 3비트 제거)
#define GET_ALLOC(p) (GET(p) & 0x1)  //  할당 여부 추출 (하위 1비트 확인)

#define HDRP(bp) ((char *)(bp) - WSIZE)  // bp는 payload 포인터. 이 매크로는 해당 블록의 헤더 주소 계산
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)  // bp 기준으로 footer의 주소 계산. 전체 블록 크기에서 8바이트 빼는 이유는 헤더 + footer 포함했기 떄문

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))  //  현재 블록 기준으로 다음 블록의 payload 주소 계산
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))  //  현재 블록 기준으로 이전 블록의 payload 주소 계산
/*   기본 매크로 설정   */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

void *heap_listp;

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));  //  이전 블록 할당 여부
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));  //  다음 블록 할당 여부
    size_t size = GET_SIZE(HDRP(bp));  //  전체 블록 크기

    //  case 1 : 이전/다음 모두 할당 된 경우 => 병합 불가
    if (prev_alloc && next_alloc)
    {
        return bp;
    }

    // case 2 : 다음 블록만 free => 다음 블록과 병합
    else if (prev_alloc && !next_alloc)
    {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));  //  크기 확장
        PUT(HDRP(bp), PACK(size, 0));           //  header 갱신
        PUT(FTRP(bp), PACK(size, 0));           //  footer 갱신
        return bp;
    }

    // case 3 : 이전 블록만 free => 이전 블록과 병합
    else if (!prev_alloc && next_alloc)
    {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));      //  크기 확장
        PUT(FTRP(bp), PACK(size, 0));               //  footer 갱신 (현재 블록 기준)
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));    //  이전 블록의 header 갱신
        bp = PREV_BLKP(bp);         //  병합 후 위치 이동
    }

    // case 4 : 이전/다음 모두 free => 세 개 병합
    else
    {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));  // 크기 확장 (이전 + 다음 블록과 병합
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));    //  header : 이전 블록 기준
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));    //  footer : 다음 블록 기준
        bp = PREV_BLKP(bp);             //  병합된 블록의 시작 위치로 이동
    }
    return bp;
}


static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    return coalesce(bp);
}

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    //  1. 힙을 위한 최소 공간 16바이트 확보 (padding + prologue header/footer + epilogue header)
    heap_listp = mem_sbrk(4 * WSIZE);
    if (heap_listp == (void *)-1) {
        return -1;  //  sbrk 실패 시 에러 리턴
    }
    //  2. 초기 블록들 구성하는 단계
    PUT(heap_listp, 0);                                 //  Alignment padding
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));      //  Prologue header (8바이트, 할당됨)
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));      //  Prologue footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));          //  Epilogue header (0바이트, 할당됨)

    //  3. payload 기준 위치로 이동 (Prologue 블록의 payload 포인터)
    heap_listp += (2 * WSIZE);

    //  4. 살제 usable한 free block 확보
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */

// 적절한 free block을 찾는 first-fit 함수
static void *find_fit(size_t asize)
{
    void *bp = heap_listp;

    // 현재 블록이 할당중이거나, 크기가 부족하면 계속 탐색
    while (GET_ALLOC(HDRP(bp)) || GET_SIZE(HDRP(bp)) < asize)
    {
        // epilogue 까지 도달한 경우, 더 이상 블록 없음
        if (GET_SIZE(HDRP(bp)) == 0)
        {
            return NULL;
        }
        bp = NEXT_BLKP(bp);  // 다음 블록으로 이동 (curr = curr->next 이거랑 비슷한느낌)
    }

    return bp;  // 적절한 free 블록을 찾으면 bp 리턴
}

// 주어진 위치에 메모리를 배치 (필요 시 분할)
void place(void *bp, size_t asize)
{
    size_t block_size = GET_SIZE(HDRP(bp));  // 현재 블록 전체 크기

    // 현재 블록을 asize 크기로 할당 표시
    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));

    // 남는 공간이 있을 경우, 새로운 free 블록으로 분할
    if (block_size > asize)
    {
        // 다음 블록의 header/footer를 free 상태로 초기화
        PUT(HDRP(NEXT_BLKP(bp)), PACK(block_size - asize, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(block_size - asize, 0));
    }
}



void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;

    if (size == 0) {
        return NULL;
    }

    if (size <= DSIZE) {
        asize = 2*DSIZE;
    }
    else {
        asize = DSIZE * ((size + (DSIZE) + (DSIZE -1)) / DSIZE);
    }

    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL) {
        return NULL;
    }
    place(bp, asize);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    copySize = GET_SIZE(HDRP(oldptr)) - DSIZE;
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

