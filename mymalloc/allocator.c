/**
 * Copyright (c) 2015 MIT License by 6.172 Staff
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 **/

#include "./allocator.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "./allocator_interface.h"
#include "./memlib.h"

// Don't call libc malloc!
#define malloc(...) (USE_MY_MALLOC)
#define free(...) (USE_MY_FREE)
#define realloc(...) (USE_MY_REALLOC)

// The max possible allocation size is 2^31-1,
// and we use bin x to represent sizes between
// 2^(x+3) and 2^(x+4), so 29 bins is sufficient.
#define BINS 28

typedef unsigned int header;
#define HEADER_SIZE (sizeof(header))

// We will use bit 31 in the header to store
// whether or not this block is free. We can use this bit
// because we know that size < 2^31 for all blocks,
// and that block sizes are always divisible by 8,
// so we will store size / 8 in the rest of the header.
#define FREE_FLAG (1 << 31)

// Each block in our freelist needs at least 24 bytes
// (4 for header, 8 each for prev/next, 4 for footer)
// so we need all blocks to be >=24 bytes.
#define MIN_BLOCK_SIZE 24

// Threshold (bytes) at which to split a block during malloc
#ifndef SPLIT_THRESHOLD
#define SPLIT_THRESHOLD 64
#endif

static inline int floor_log2(header x) {
  assert(x > 0);
  // 31 - # leading zeros = position of most significant 1
  //    = floor_log2
  return 31 - __builtin_clz(x);
}

// Freelist (fixed size blocks)
struct __attribute__((packed)) freelist_item {
  // The MSB of hdr is a flag, storing 1 if free and 0 if used.
  // The rest of hdr is (size of block) / 8.
  header hdr;
  // Pointers to prev and next items in the linked list
  struct freelist_item* prev;
  struct freelist_item* next;
};

// Local pointer to end of heap that is in use.
// This points to the last byte outside the heap.
static void* heap_end;

// Uses (29 bins) * (64-bit addr) = 232 bytes
// Binned free list, where bin x contains free blocks
//  where 2^(x+3) <= size < 2^(x+4). Each bin is a linked list.
static struct freelist_item* b_freelist[BINS];

// Track lowest and highest nonempty bins to save some iteration.
static int highest_nonempty_bin;
static int lowest_nonempty_bin;

// Remove a free block at p from b_freelist[bin].
static inline void freelist_remove(int bin, void* p) {
  // mem_heap_hi() points to last byte in the heap, so can be equal
  assert(p >= mem_heap_lo() && p <= mem_heap_hi());

  struct freelist_item* pointer = (struct freelist_item*)p;

  if (pointer->prev) {
    pointer->prev->next = pointer->next;
  } else {
      b_freelist[bin] = pointer->next;
  }
  if (pointer->next) {
    pointer->next->prev = pointer->prev;
  }

  // Update nonempty bins if needed
  if (!b_freelist[highest_nonempty_bin]) {
    // Defaults to -1 if we don't find any nonempty bins
    for (int i = highest_nonempty_bin - 1; i >= -1; --i) {
      if (i == -1 || b_freelist[i]) {
        // found nonempty bin, update
        highest_nonempty_bin = i;
        break;
      }
    }
  }
  if (!b_freelist[lowest_nonempty_bin]) {
    // Defaults to BINS if we don't find any nonempty bins
    for (int i = lowest_nonempty_bin + 1; i <= BINS; ++i) {
      if (i == BINS || b_freelist[i]) {
        // found nonempty bin, update
        lowest_nonempty_bin = i;
        break;
      }
    }
  }
}

// Add block at p to the head of b_freelist[bin].
static inline void freelist_add(int bin, void* p) {
  assert(p >= mem_heap_lo() && p <= mem_heap_hi());

  struct freelist_item* cur_head = b_freelist[bin];

  if (cur_head) {
    cur_head->prev = (struct freelist_item*)p;
  }
  ((struct freelist_item*)p)->next = cur_head;
  b_freelist[bin] = p;
  ((struct freelist_item*)p)->prev = NULL;

  // Update nonempty bins if needed
  if (bin > highest_nonempty_bin) highest_nonempty_bin = bin;
  if (bin < lowest_nonempty_bin) lowest_nonempty_bin = bin;
}

// check - This checks our invariants:
//  - all blocks in freelist are marked free
//  - headers and footers match
//  - header size either points to next block or end of heap
//  - number of blocks marked free = number of blocks in freelist
int my_check() {
  // heap_end points to first byte outside heap while mem_heap_hi()
  // points to last byte in heap
  assert(heap_end <= mem_heap_hi() + 1);

  char* p;
  char* lo = (char*)mem_heap_lo() + 4;
  char* hi = (char*)heap_end;
  header size = 0;

  // Find length of each freelist bin
  int lengths[BINS];
  // count number of found free blocks for each bin
  int found_lengths[BINS];
  int found_lowest_nonempty = BINS;
  int found_highest_nonempty = -1;
  for (int i = 0; i < BINS; i++) {
    found_lengths[i] = 0;
    lengths[i] = 0;
    struct freelist_item* cur = b_freelist[i];
    while (cur) {
      if ((void*)cur < heap_end && (cur->hdr & FREE_FLAG) == 0) {
        printf("Free block not marked as free!\n");
        printf("p=%p, hdr=%d\n", cur, cur->hdr);
        assert(0);
        return -1;
      }
      header size = cur->hdr & ~FREE_FLAG;
      // Size should be between 2^(bin) and 2^(bin+1)
      if (size < (1 << i) || size >= (1 << (i+1))) {
        printf("Free block has invalid size!\n");
        printf("p=%p, bin=%d, size=%d\n", cur, i, size);
        assert(0);
        return -1;
      }
      lengths[i]++;
      cur = cur->next;
    }

    // Update lowest_nonempty and highest_nonempty
    if (lengths[i] > 0) {
      if (found_lowest_nonempty == BINS) {
        found_lowest_nonempty = i;
      }
      found_highest_nonempty = i;
    }
  }

  // Check that lowest_nonempty_bin and highest_nonempty_bin
  // are consistent with freelist.
  if (lowest_nonempty_bin != found_lowest_nonempty) {
    printf("lowest_nonempty_bin is incorrect!\n");
    printf("lowest_nonempty_bin=%d, found lowest nonempty=%d\n",
            lowest_nonempty_bin, found_lowest_nonempty);
    assert(0);
    return -1;
  }
  if (highest_nonempty_bin != found_highest_nonempty) {
    printf("highest_nonempty_bin is incorrect!!\n");
    printf("highest_nonempty_bin=%d, found highest nonempty=%d\n",
            highest_nonempty_bin, found_highest_nonempty);
    assert(0);
    return -1;
  }

  p = lo;
  while (lo <= p && p < hi) {
    header hdr = *(header*)p;
    // Clear the free flag and multiply by 8 to get size
    size = (*(header*)p & ~FREE_FLAG) << 3;
    assert(size >= MIN_BLOCK_SIZE);
    if (hdr & FREE_FLAG) {
      found_lengths[floor_log2(size) - 3]++;
    }
    // header and footer should be the same
    header ftr = *(header*)(p + size - HEADER_SIZE);
    if (hdr != ftr) {
      printf("Header and footer do not match\n");
      printf("p=%p, size=%u, hdr=%u, ftr=%d\n", p, size, hdr, ftr);
      assert(0);
      return -1;
    }
    // Size includes header and footer, so add size to get to next
    p += size;
  }

  for (int i = 0; i < BINS; i++) {
    int fail = 0;
    if (lengths[i] != found_lengths[i]) {
      printf("Number of free blocks does not match length of freelist!\n");
      printf("Bin %d, freelist length: %d, free blocks: %d\n",
              i, lengths[i], found_lengths[i]);
      fail = 1;
    }
    if (fail) {
      assert(0);
      return -1;
    }
  }

  if (p != hi) {
    printf("Bad headers did not end at heap_hi!\n");
    printf("heap_lo: %p, heap_hi: %p, size: %u, p: %p\n", lo, hi, size, p);
    assert(0);
    return -1;
  }

  return 0;
}

// init - Initialize the malloc package.  Called once before any other
// calls are made.  Since this is a very simple implementation, we just
// return success.
int my_init() {
  highest_nonempty_bin = -1;
  lowest_nonempty_bin = BINS;

  for (int i = 0; i < BINS; i++)
    b_freelist[i] = NULL;

  // We will always allocate sizes of multiples of 8. But since the
  // pointer we return to the user is (p + HEADER_SIZE), to allow
  // for the header, we need to start at HEADER_SIZE = 4 off-alignment
  // so that all returned pointers are 8-byte aligned.
  mem_sbrk(4);

  heap_end = mem_heap_hi() + 1;
  return 0;
}

// Wrapper function for mem_sbrk. We keep track of
// a local pointer heap_end, so if there is unused memory
// between heap_end and the actual end of the heap, we can
// use that instead of asking for more memory.
static inline void* my_sbrk(long size) {
  assert(size > 0);
  if (heap_end + size <= mem_heap_hi() + 1) {
    heap_end += size;
    return heap_end - size;
  } else {
    int extra_size = heap_end + size - mem_heap_hi() - 1;
    mem_sbrk(extra_size);
    heap_end += size;
    assert(heap_end == mem_heap_hi() + 1);
    return heap_end - size;
  }
}

// Allocate block of size alloc_size from this free block, splitting if
// the leftover is over the split threshold and updating the
// headers/footers. Returns the pointer to the allocated block.
static inline void* split_and_alloc(size_t alloc_size, struct freelist_item* free_block, int bin) {
  // Block has free flag set and valid size
  assert(free_block->hdr | FREE_FLAG);
  header size = free_block->hdr & ~FREE_FLAG;
  // Header and footer are the same
  assert(free_block->hdr == *(header*)((char*)free_block + (size << 3) - HEADER_SIZE));
  // alloc_size should be aligned
  assert(alloc_size == ALIGN(alloc_size));

  header leftover_mem = (size << 3) - alloc_size;
  assert(leftover_mem >= 0);
  if (leftover_mem <= SPLIT_THRESHOLD) {
    // No splitting needed, just remove this from the freelist
    // and hand back to the user
    void* p = free_block;
    freelist_remove(bin, p);

    // clear free flag from header and footer
    *(header*)p &= ~(FREE_FLAG);
    *(header*)(p + (size << 3) - HEADER_SIZE) &= ~(FREE_FLAG);
    assert(*(header*)p == size);
    return (void*)((char*)p + HEADER_SIZE);
  }

  // Split off the part that we need and put the rest
  // back into the correct bin
  int new_bin = floor_log2(leftover_mem) - 3;
  assert(new_bin > 0);
  assert(new_bin <= bin);

  void* p = free_block;  // pointer to return to user

  void* newp = p + alloc_size;  // new start of free block

  // Update header and footer in the new free block
  ((struct freelist_item*)newp)->hdr = (leftover_mem >> 3) | FREE_FLAG;
  *(header*)(newp + leftover_mem - HEADER_SIZE) = (leftover_mem >> 3) | FREE_FLAG;

  // Remove from this bin
  freelist_remove(bin, p);

  // Put remaining free part back into correct bin
  freelist_add(new_bin, newp);

  // Set header and footer for this block (size/8)
  *(header*)p = alloc_size >> 3;
  *(header*)(p + alloc_size - HEADER_SIZE) = alloc_size >> 3;
  assert((*(header*)p & FREE_FLAG) == 0);

  return (void*)((char*)p + HEADER_SIZE);
}

//  malloc - Allocate a block.
//  Always allocate a block whose size is a multiple of the alignment.
void* my_malloc(size_t size) {
  // Largest block size we can handle is 2^(BINS + 3)
  if (size > (1UL << (BINS + 3)))
    return NULL;

  // We allocate a little bit of extra memory so that we can store
  // a header before it and a footer after it.
  long aligned_size = ALIGN(size + HEADER_SIZE * 2);

  // Our free list item takes up 24 bytes, so the size of the block
  // needs to be at least 24 bytes right now. (Including the footer
  // which is not included in the struct)
  if (aligned_size < MIN_BLOCK_SIZE) aligned_size = MIN_BLOCK_SIZE;

  // Look for a block in the freelist that we can reuse
  int lowest_bin = floor_log2(aligned_size) - 3;
  assert(lowest_bin > 0 && lowest_bin < BINS);

  // Start at lowest nonempty bin if greater than lowest_bin
  int start_bin = (lowest_bin > lowest_nonempty_bin) ? lowest_bin : lowest_nonempty_bin;

  for (int bin = start_bin; bin <= highest_nonempty_bin ; bin++) {
    struct freelist_item* cur = b_freelist[bin];
    while (cur) {
      // Block has free flag set and valid size
      assert(cur->hdr | FREE_FLAG);
      header size = cur->hdr & ~FREE_FLAG;
      assert(size >= (1 << bin));
      assert(size < (1 << (bin+1)));
      // Header and footer are the same
      assert(cur->hdr == *(header*)((char*)cur + (size << 3) - HEADER_SIZE));

      if ((size << 3) >= aligned_size) {
        // This block is large enough, use it
        return split_and_alloc(aligned_size, cur, bin);
      }

      cur = cur->next;
    }
  }

  // Expands the heap by the given number of bytes and returns a pointer to
  // the newly-allocated area.  This is a slow call, so you will want to
  // make sure you don't wind up calling it on every malloc.
  void* p = my_sbrk(aligned_size);

  if (p == (void*)-1) {
    // Whoops, an error of some sort occurred.  We return NULL to let
    // the client code know that we weren't able to allocate memory.
    return NULL;
  } else {
    // We store the size/8 in the header and footer.
    *(header*)p = aligned_size >> 3;
    *(header*)(p + aligned_size - HEADER_SIZE) = aligned_size >> 3;
    assert((*(header*)p & FREE_FLAG) == 0);

    // Then, we return a pointer to the rest of the block of memory,
    // which is at least size bytes long.  We have to cast to uint8_t
    // before we try any pointer arithmetic because voids have no size
    // and so the compiler doesn't know how far to move the pointer.
    // Since a uint8_t is always one byte, adding HEADER_SIZE after
    // casting advances the pointer by HEADER_SIZE bytes.
    return (void*)((char*)p + HEADER_SIZE);
  }
}

// free - Add block back to free list and coalesce with
//        left and/or right neighbors if possible.
void my_free(void* ptr) {
  // Find the header for the block by looking before the pointer
  void* hdr_ptr = (char*)ptr - HEADER_SIZE;

  // Free flag should not be not set
  assert((*(header*)hdr_ptr & FREE_FLAG) == 0);
  header orig_size = *(header*)hdr_ptr;
  header size = orig_size;

  int bin = floor_log2(size);
  assert(bin < BINS);

  // Coalesce with previous block if it is also free.
  void* prev_ftr_ptr = (char*)hdr_ptr - HEADER_SIZE;
  // Make sure this is a valid block
  if (prev_ftr_ptr >= mem_heap_lo()) {
    header prev_ftr = *(header*) prev_ftr_ptr;
    if (prev_ftr & FREE_FLAG) {
      header prev_size = prev_ftr & ~FREE_FLAG;
      int prev_bin = floor_log2(prev_size);

      void* prev_hdr_ptr = (char*)hdr_ptr - (prev_size << 3);
      assert(prev_hdr_ptr >= mem_heap_lo());
      assert(*(header*)prev_hdr_ptr == *(header*)prev_ftr_ptr);
      // Remove this block from freelist
      freelist_remove(prev_bin, prev_hdr_ptr);

      // Update size and header pointer to values for
      // the entire coalesced block
      size += prev_size;
      hdr_ptr = prev_hdr_ptr;
    }
  }

  // If we are freeing from the right edge of the heap,
  // we just decrement heap_end, our local pointer to the edge
  // of the heap. Then this memory can be reused
  if ((char*)hdr_ptr + (size << 3) == heap_end) {
    heap_end -= (size << 3);
    return;
  }

  // Coalesce with the next block if it is also free.
  // Next header will be at (ptr_to_size + size*8) since size
  //  includes the header.
  void* next_hdr_ptr = (char*)hdr_ptr + (size << 3);
  if (next_hdr_ptr < heap_end) {
    header next_hdr = *(header*) next_hdr_ptr;
    if (next_hdr & FREE_FLAG) {
      header next_size = next_hdr & ~FREE_FLAG;
      int next_bin = floor_log2(next_size);

      // First remove the next block from the freelist
      freelist_remove(next_bin, next_hdr_ptr);

      // Update size of current block
      size += next_size;
    }
  }
  // Get updated bin of possibly coalesced block
  bin = floor_log2(size);

  // Set free flag
  size |= FREE_FLAG;

  // Add this block (including the part storing the size)
  // back into b_freelist[bin]
  ((struct freelist_item*) hdr_ptr)->hdr = size;
  *(header*)(hdr_ptr + (size << 3) - HEADER_SIZE) = size;
  freelist_add(bin, hdr_ptr);
}

// realloc - Implemented simply in terms of malloc and free,
//  with a few optimizations
void* my_realloc(void* ptr, size_t size) {
  void* newptr;
  header copy_size;

  // Get the size of the old block of memory by looking in the
  // header right before the pointer.
  void* hdr_ptr = (char*)ptr - HEADER_SIZE;
  copy_size = (*(header*)hdr_ptr) << 3;

  header new_size = ALIGN(size + HEADER_SIZE);
  // If the new size is less than the size we allocated (i.e. does not
  // expand past the already-allocated block), we don't need to reallocate.
  if (new_size <= copy_size) {
    return ptr;
  }

  // If the block is at the edge of the heap, instead of reallocating
  // a new block, just extend the heap by the amount needed.
  if ((char*)heap_end - copy_size == hdr_ptr) {
    my_sbrk(new_size - copy_size);
    // update size in header & footer
    *(header*)hdr_ptr = new_size >> 3;
    *(header*)(hdr_ptr + new_size - HEADER_SIZE) = new_size >> 3;
    return ptr;
  }

  // Allocate a new chunk of memory, and fail if that allocation fails.
  newptr = my_malloc(size);
  if (NULL == newptr) {
    return NULL;
  }

  // This is a standard library call that performs a simple memory copy.
  memcpy(newptr, ptr, copy_size - HEADER_SIZE);

  // Release the old block.
  my_free(ptr);

  // Return a pointer to the new block.
  return newptr;
}
