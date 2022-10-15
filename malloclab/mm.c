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

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */ //line:vm:mm:beginconst
#define DSIZE       8       /* Double word size (bytes) */
#define CHUNKSIZE  (1<<12)  /* Extend heap by this amount (bytes) */  //line:vm:mm:endconst 

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))      
#define PUT(p, val)  (*(unsigned int *)(p) = (val))    

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)                 
#define GET_ALLOC(p) (GET(p) & 0x1)                    

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((void *)(bp) - WSIZE)                
#define FTRP(bp)       ((void *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) 

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((void *)(bp) + GET_SIZE(((void *)(bp) - WSIZE))) 
#define PREV_BLKP(bp)  ((void *)(bp) - GET_SIZE(((void *)(bp) - DSIZE))) 
/* $end mallocmacros */

/* Global variables */
static void *heap_listp = 0;  /* Pointer to first block */  

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static int get_index(size_t asize);
static void list_add(void *bp);
static void list_del(void *bp);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(12 * WSIZE)) == (void*)-1) {
        return -1;
    }

    PUT(heap_listp, 0);
    PUT(heap_listp + (1 * WSIZE), PACK(5 * DSIZE, 1));
    PUT(heap_listp + (2 * WSIZE), 0);
    PUT(heap_listp + (3 * WSIZE), 0);
    PUT(heap_listp + (4 * WSIZE), 0);
    PUT(heap_listp + (5 * WSIZE), 0);
    PUT(heap_listp + (6 * WSIZE), 0);
    PUT(heap_listp + (7 * WSIZE), 0);
    PUT(heap_listp + (8 * WSIZE), 0);
    PUT(heap_listp + (9 * WSIZE), 0);
    PUT(heap_listp + (10 * WSIZE), PACK(5 * DSIZE, 1));
    PUT(heap_listp + (11 * WSIZE), PACK(0, 1));
    heap_listp += (2 * WSIZE);

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) {
        return -1;
    }

    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;      
    size_t extendsize; 
    void *bp;      

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
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)  
        return NULL;

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
    size_t oldsize;
    void *newptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0) {
        mm_free(ptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if(ptr == NULL) {
        return mm_malloc(size);
    }

    newptr = mm_malloc(size);

    /* If realloc() fails the original block is left untouched  */
    if(!newptr) {
        return 0;
    }

    /* Copy the old data. */
    oldsize = GET_SIZE(HDRP(ptr));
    if(size < oldsize) oldsize = size;
    memcpy(newptr, ptr, oldsize);

    /* Free the old block. */
    mm_free(ptr);

    return newptr;
}

static void *extend_heap(size_t words) {
    void *bp;
    size_t size;

    size = (words & 1) ? (words + 1) * WSIZE : words * WSIZE;

    if ((bp = mem_sbrk(size)) == (void*)-1) {
        return NULL;
    } 

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    PUT(bp, 0);

    return coalesce(bp);
}

static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {
        list_add(bp);
    } else if (prev_alloc && !next_alloc) {
        list_del(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        list_add(bp);
    } else if (!prev_alloc && next_alloc) {
        list_del(PREV_BLKP(bp));
        bp = PREV_BLKP(bp);
        size += GET_SIZE(HDRP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        list_add(bp);
    } else {
        list_del(PREV_BLKP(bp));
        list_del(NEXT_BLKP(bp));
        size += (GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp))));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        list_add(bp);
    }

    return bp;
}

static void place(void *bp, size_t asize)
{
    list_del(bp);
    size_t csize = GET_SIZE(HDRP(bp));   

    if ((csize - asize) >= (2 * DSIZE)) { 
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-asize, 0));
        PUT(FTRP(bp), PACK(csize-asize, 0));
        list_add(bp);
    }
    else { 
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}


static void* find_fit(size_t asize) {
    int index = get_index(asize);

    for (; index <= 7; ++index) {
        void *ptr = GET(heap_listp + (index * WSIZE));

        while (ptr) {
            if (GET_SIZE(HDRP(ptr)) >= asize) {
                return ptr;
            }
            ptr = GET(ptr);
        }
    }

    return NULL;
}

static int get_index(size_t asize) {
    if (asize <= 32) {
        return 0;
    }

    if (asize <= 64) {
        return 1;
    }

    if (asize <= 128) {
        return 2;
    }

    if (asize <= 256) {
        return 3;
    }

    if (asize <= 512) {
        return 4;
    }

    if (asize <= 1024) {
        return 5;
    }

    if (asize <= 2048) {
        return 6;
    }

    return 7;
}

static void list_add(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    int index = get_index(size);

    void *pre = heap_listp + (index * WSIZE);
    void *ptr = GET(pre);

    while (ptr && GET_SIZE(HDRP(ptr)) < size) {
        pre = ptr;
        ptr = GET(ptr);
    }

    if (ptr == NULL) {
        PUT(pre, bp);
        PUT(bp, 0);
        return;
    }

    PUT(bp, ptr);
    PUT(pre, bp);

    return;
}

static void list_del(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    int index = get_index(size);

    void *pre = heap_listp + (index * WSIZE);
    void *ptr = GET(pre);

    while (ptr) {
        if (ptr == bp) {
            PUT(pre, GET(ptr));
            PUT(ptr, 0);
            return;
        }
        pre = ptr;
        ptr = GET(ptr);
    }

    return;
}