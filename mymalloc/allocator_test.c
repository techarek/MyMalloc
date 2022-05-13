/**
 * Copyright (c) 2020 MIT License by 6.172 Staff
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

/**
 * allocator_test.c
 *
 * This file provides a skeleton for writing your own tests, benchmarks, or
 * whatever you want. It currently contains a couple of simple examples. You can
 * compile this file with `make allocator_test`, and run it as
 * `./allocator_test`.
 *
 * You can call anything declared in allocator.h here, as well as your my_init,
 * my_malloc, my_free, and my_init functions.
 **/

#include "./allocator.h"

#include <stdio.h>

#include "./allocator_interface.h"
#include "./fasttime.h"
#include "./memlib.h"

// This function will fail with the skeleton code with a "Ran out of memory..."
// error, since free is currently a no-op.
#define NUM_ALLOCS 17
#define NUM_ITERATIONS 1 << 17

void benchmark_my_malloc() {
  mem_init();  // call necessary for your malloc interface to work

  my_init();

  void* allocs[NUM_ALLOCS];

  fasttime_t begin = gettime();
  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    for (int i = 0; i < NUM_ALLOCS; i++) {
      allocs[i] = my_malloc(1 << i);
    }
    for (int i = 0; i < NUM_ALLOCS; i++) {
      my_free(allocs[i]);
    }
  }
  fasttime_t end = gettime();

  mem_deinit();  // call necessary if mem_init() called above

  printf("total runtime: %fs\n", tdiff(begin, end));
  printf("total mem usage: %d bytes\n", (int)mem_heapsize());
}

void check_size_t(size_t actual, size_t expected) {
  if (actual != expected) {
    printf("Check failed. Expected %zu, got %zu\n", expected, actual);
  } else {
    printf("Check passed. %zu == %zu\n", expected, actual);
  }
}

void test_align() {
  assert(ALIGNMENT == 8);

  printf("Testing ALIGN...\n");
  check_size_t(ALIGN(16), 16);
  check_size_t(ALIGN(18), 24);
  check_size_t(ALIGN(39), 40);
}

int main() {
  test_align();
  // benchmark_my_malloc();
}
