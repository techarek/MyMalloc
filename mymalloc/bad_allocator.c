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

#include <stdio.h>
#include <stdlib.h>

#include "./allocator_interface.h"
#include "./memlib.h"

#ifdef BAD_ALIGNMENT
#define SIZE 4101
#else
#define SIZE 4096
#endif

// Don't call libc malloc!
#define malloc(...) (USE_BAD_MALLOC)
#define free(...) (USE_BAD_FREE)
#define realloc(...) (USE_BAD_REALLOC)

#ifndef ALIGNMENT
#define ALIGNMENT 8
#endif

#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

// Store previously allocated pointer to fail on overlap.
#ifdef BAD_OVERLAP
void* prev;
#endif

// bad_init - Does nothing.
int bad_init() {
  #ifdef BAD_OVERLAP
  prev = NULL;
  #endif

  return 0;
}

// bad_check - No checker.
int bad_check() { return 1; }

// bad_malloc - Allocate a block by incrementing the brk pointer.
// Always allocate a block whose size is not a multiple of the alignment,
// and may not fit the requested allocation size.
void* bad_malloc(size_t size) {
  // Fail writing outside the heap by overwriting size.
  #ifdef BAD_SIZE
  size = SIZE;
  #endif

  // Fail on bad overlap by returning a previously returned pointer.
  #ifdef BAD_OVERLAP
  if (prev) {
    return prev;
  }
  #endif

  #ifdef BAD_ALIGNMENT
  void* p = mem_sbrk(size);
  #else
  void* p = mem_sbrk(ALIGN(size));
  #endif

  if (p == (void*)-1) {
    // The heap is probably full, so return NULL.
    return NULL;
  } else {
    #ifdef BAD_OVERLAP
    prev = p;
    #endif
    return p;
  }
}

// bad_free - Freeing a block does nothing.
void bad_free(void* ptr) {
  // Do nothing.
}

// bad_realloc - Implemented simply in terms of bad_malloc and bad_free, but
// lacks copy step.
void* bad_realloc(void* ptr, size_t size) {
  void* newptr;

  // Allocate a new chunk of memory, and fail if that allocation fails.
  newptr = bad_malloc(size);

  if (NULL == newptr) {
    return NULL;
  }

  // Release the old block.
  bad_free(ptr);

  // Return a pointer to the new block.
  return newptr;
}

// call mem_reset_brk.
void bad_reset_brk() { mem_reset_brk(); }

// call mem_heap_lo
void* bad_heap_lo() { return mem_heap_lo(); }

// call mem_heap_hi
void* bad_heap_hi() { return mem_heap_hi(); }
