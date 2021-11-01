#pragma once

#include <iostream>
#include "utils/utils.h"

// Manages a log in local or remote memory.
class Log {
public:
  void init(uintptr_t base_addr, uint32_t available_bytes) {
    data_base_addr = base_addr;
    capacity = available_bytes / sizeof(uint64_t);
  }

  uintptr_t append() {
    if (size == capacity) 
      die("Log overflow");

    uintptr_t addr = elem_addr(end);
    end = (end + 1) % capacity;
    size += 1;

    return addr;
  }

  uintptr_t tail() {
    if (size == 0)
      die("Reading an empty log");

    return elem_addr(start);
  }

  void trim(uint32_t num_elems) {
    if (num_elems > size)
      die("Trimming overflow");

    start = (start + num_elems) % capacity;
    size -= num_elems;
  }

  uint32_t get_capacity() {
    return capacity;
  }

  uint32_t get_size() {
    return size;
  }

private:
  uint32_t start = 0;
  uint32_t end = 0;

  uint32_t capacity = 0;
  uint32_t size = 0;

  uintptr_t data_base_addr = 0;

  uintptr_t elem_addr(uint32_t index) {
    return data_base_addr + index * sizeof(uint64_t);
  }
};
