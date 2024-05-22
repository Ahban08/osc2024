#include "memory.h"
#include "u_list.h"
#include "uart1.h"

extern char _heap_top;
static char* htop_ptr = &_heap_top;

//lab2
void* malloc(unsigned int size) {
    // -> htop_ptr
    // htop_ptr + 0x08:  heap_block size
    // htop_ptr + 0x10 ~ htop_ptr + 0x10 * k:
    //            { heap_block }
    // -> htop_ptr

    // 0x10 for heap_block header
    char* r = htop_ptr + 0x10;
    // size paddling to multiple of 0x10
    size = 0x10 + size - size % 0x10;
    *(unsigned int*)(r - 0x8) = size;
    htop_ptr += size;
    return r;
}

//lab4
static frame_t*           frame_array;                    // store memory's statement and page's corresponding index
static list_head_t        frame_freelist[FRAME_MAX_IDX];  // store available block for page

void init_allocator()
{
    frame_array = malloc(MAX_PAGES * sizeof(frame_t));

    // init frame_array to FRAME_IDX_FINAL order
    for (int i = 0; i < MAX_PAGES; i++)
    {
        if (i % (1 << FRAME_IDX_FINAL) == 0)
        {
            frame_array[i].order = FRAME_IDX_FINAL; // 設為最大的frame size
            frame_array[i].used = FRAME_FREE;
        }
    }

    //init frame freelist
    for (int i = FRAME_IDX_0; i <= FRAME_IDX_FINAL; i++)
    {
        INIT_LIST_HEAD(&frame_freelist[i]);
    }

    for (int i = 0; i < MAX_PAGES; i++)
    {
        // init listhead for each frame
        INIT_LIST_HEAD(&frame_array[i].listhead);
        frame_array[i].idx = i;
        frame_array[i].cache_order = CACHE_NONE;

        // add init frame (FRAME_IDX_FINAL) into freelist
        if (i % (1 << FRAME_IDX_FINAL) == 0)
        {
            list_add(&frame_array[i].listhead, &frame_freelist[FRAME_IDX_FINAL]);
        }
    }

    }

void* page_malloc(unsigned int size){
    uart_sendline("    [+] Allocate page - size : %d(0x%x)\r\n", size, size);
    uart_sendline("        Before\r\n");
    dump_page_info();

    int order;
    // Check the size and determine the appropriate order
    for (int i = FRAME_IDX_0; i <= FRAME_IDX_FINAL; i++) 
    {

        if (size <= (PAGESIZE << i))
        {
            order = i;
            uart_sendline("        block size = 0x%x\n", PAGESIZE << i);
            break;
        }

        if ( i == FRAME_IDX_FINAL)
        {
            uart_sendline("[!] The case of out-of-memory!\r\n");
            return (void*)0; // Return null pointer to indicate failure
        }

    }

    // Find the smallest larger frame in the free list
    int target_order;
    for (target_order = order; target_order <= FRAME_IDX_FINAL; target_order++)
    {
        // freelist does not have 2**i order frame, going for next order
        if (!list_empty(&frame_freelist[target_order]))
            break; //found!
    }
    if (target_order > FRAME_IDX_FINAL)
    {
        uart_sendline("[!] No available frame in freelist. Request failed!\r\n");
        return (void*)0;
    }

    // Get the available frame from the free list
    frame_t *target_frame_ptr = (frame_t*)frame_freelist[target_order].next;
    list_del_entry((struct list_head *)target_frame_ptr);
    

    // Release redundant memory block to separate into pieces
    for (int j = target_order; j > order; j--) // ex: 10000 -> 01111
    {
        release_redundant(target_frame_ptr);
        uart_sendline("        split order:%d from 0x%x~0x%x to 0x%x~0x%x and 0x%x~0x%x.\n", j,
                                BUDDY_MEMORY_BASE + (PAGESIZE*(target_frame_ptr->idx)),
                                BUDDY_MEMORY_BASE + (PAGESIZE*(target_frame_ptr->idx + (1<<j))),
                                BUDDY_MEMORY_BASE + (PAGESIZE*(target_frame_ptr->idx)),
                                BUDDY_MEMORY_BASE + (PAGESIZE*(target_frame_ptr->idx + (1<<(j -1)))),
                                BUDDY_MEMORY_BASE + (PAGESIZE*(target_frame_ptr->idx + (1<<(j -1)))),
                                BUDDY_MEMORY_BASE + (PAGESIZE*(target_frame_ptr->idx + (1<<j))));
    }

    // Allocate it
    target_frame_ptr->used = FRAME_ALLOCATED;
    uart_sendline("        physical address : 0x%x\n", BUDDY_MEMORY_BASE + (PAGESIZE*(target_frame_ptr->idx)));
    uart_sendline("        After\r\n");
    dump_page_info();

    return (void *) BUDDY_MEMORY_BASE + (PAGESIZE * (target_frame_ptr->idx));
}


// Split the frame
// e.g. Request for order = 2, target_order = 4
// j = 4
// 0     1     2     3     4     5     6     7     8     9     10    11    12    13    14    15
// ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
// │  4  │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │
// └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
//             　
// j = 3         　                                ↓
// ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
// │  3  │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │  3  │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │
// └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
//                                                  ↑↑↑↑↑  change this frame to order = 3
//
// j = 2                   ↓
// ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
// │  2  │ <F> │ <F> │ <F> │  2  │ <F> │ <F> │ <F> │  3  │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │
// └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
//                          ↑↑↑↑↑  change this frame to order = 2
//
// finish split, get first frame and set order = 2 
// ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
// │  2  │ <F> │ <F> │ <F> │  2  │ <F> │ <F> │ <F> │  3  │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │
// └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
//  ↑↑↑↑↑  return this frame
//
frame_t* release_redundant(frame_t *frame)
{
    // order -1 -> add its buddy to free list (frame itself will be used in master function)
    frame->order -= 1;

    frame_t *buddyptr = get_buddy(frame);
    buddyptr->order = frame->order;
    list_add(&buddyptr->listhead, &frame_freelist[buddyptr->order]);
    return frame;
}

// e.g. get_buddy() = frame_array[ 8 ^ (1 << 3)] = frame_array[0]
//                                                 frame-> order = 3
//                                                 ↓
// ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
// │  3  │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │  3  │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │
// └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
//                                                 ↑ frame-> idx = 8
// e.g. get_buddy() = frame_array[ 8 ^ (1 << 2)] = frame_array[12]
//
//                                                 frame-> order = 2
//                                                 ↓
// ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
// │  3  │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │ <F> │  2  │ <F> │ <F> │ <F> │  2  │ <F> │ <F> │ <F> │
// └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
//                                                 ↑ frame-> idx = 8
frame_t* get_buddy(frame_t *frame)
{
    // XOR(idx, order)
    return &frame_array[frame->idx ^ (1 << frame->order)];
}

int coalesce(frame_t **frame_ptr_ptr)
{
    frame_t *frame_ptr = *frame_ptr_ptr;
    frame_t *buddy = get_buddy(frame_ptr);
    // frame is the boundary
    if (frame_ptr->order == FRAME_IDX_FINAL)
        return -1;

    // Order must be the same: 2**i + 2**i = 2**(i+1)
    if (frame_ptr->order != buddy->order)
        return -1;

    // buddy is in used
    if (buddy->used == FRAME_ALLOCATED)
        return -1;

    //check buddy first part
    // uart_sendline("buddy address 0x%x & frame_ptr address 0x%x \r\n", buddy, &frame_ptr_ptr);
    if( buddy->idx < frame_ptr->idx){
        
        frame_t *tmp = frame_ptr;
        frame_ptr = buddy;
        *frame_ptr_ptr = buddy;
        buddy = tmp;
        list_del_entry((struct list_head *)*frame_ptr_ptr);
        uart_sendline("    swap buddy & frame_ptr \r\n");
    }else{
        list_del_entry((struct list_head *)buddy);
    }

    
    frame_ptr->order += 1;
    uart_sendline("    coalesce detected, merging 0x%x, 0x%x, -> order = %d\r\n", BUDDY_MEMORY_BASE + (PAGESIZE*(frame_ptr->idx)), BUDDY_MEMORY_BASE + (PAGESIZE*(buddy->idx)), frame_ptr->order);
    return 0;
}

void dump_page_info(){
    unsigned int exp2 = 1;
    uart_sendline("        ----------------- [  Number of Available Page Blocks  ] -----------------\r\n        | ");
    for (int i = FRAME_IDX_0; i <= FRAME_IDX_FINAL; i++)
    {
        uart_sendline("%4dKB(%1d) ", 4*exp2, i);
        exp2 *= 2;
    }
    uart_sendline("|\r\n        | ");
    for (int i = FRAME_IDX_0; i <= FRAME_IDX_FINAL; i++)
        uart_sendline("     %4d ", list_size(&frame_freelist[i]));
    uart_sendline("|\r\n");
}

void page_free(void* ptr)
{
    frame_t *target_frame_ptr = &frame_array[((unsigned long long)ptr - BUDDY_MEMORY_BASE) >> 12]; // MAX_PAGES * 64bit -> 0x1000 * 0x10000000
    uart_sendline("    [+] Free page: 0x%x, order = %d\r\n",ptr, target_frame_ptr->order);
    uart_sendline("        Before\r\n");
    dump_page_info();
    target_frame_ptr->used = FRAME_FREE;
    while(coalesce(&target_frame_ptr)==0); // merge buddy iteratively
    list_add(&target_frame_ptr->listhead, &frame_freelist[target_frame_ptr->order]);
    uart_sendline("        After\r\n");
    dump_page_info();
}

void *kmalloc(unsigned int size)
{
    uart_sendline("\n\n");
    uart_sendline("================================\r\n");
    uart_sendline("[+] Request kmalloc size: %d\r\n", size);
    uart_sendline("================================\r\n");
    //For page
    if (size > (32 << CACHE_IDX_FINAL))
    {
        void *r = page_malloc(size);
        return r;
    }
}

void kfree(void *ptr)
{
    uart_sendline("\n\n");
    uart_sendline("==========================\r\n");
    uart_sendline("[+] Request kfree 0x%x\r\n", ptr);
    uart_sendline("==========================\r\n");
    //For page
    if ((unsigned long long)ptr % PAGESIZE == 0 && frame_array[((unsigned long long)ptr - BUDDY_MEMORY_BASE) >> 12].cache_order == CACHE_NONE)
    {
        page_free(ptr);
        return;
    }
}