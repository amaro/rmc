#pragma once

#include <memory>

#include "utils.h"

/*
 * fixed-size queue based on
 * https://embeddedartistry.com/blog/2017/05/17/creating-a-circular-buffer-in-c-and-c/
 */
template <class T, uint32_t N> class CircularBuffer {
public:
  explicit CircularBuffer()
      : buffer(std::unique_ptr<T[]>(new T[N])) {}

  /* pushes at the head and increments head */
  void push(T item) {
    buffer[head] = std::move(item);

    if (isfull)
      DIE("circular buffer is full");

    inc_with_wraparound(head, N);
    isfull = head == tail;
  }

  T pop_front() {
    if (empty())
      DIE("circular buffer is empty");

    // Read data and advance the tail (we now have a free space)
    auto val = std::move(buffer[tail]);
    isfull = false;
    inc_with_wraparound(tail, N);

    return val;
  }

  T pop_back() {
    if (empty())
      DIE("circular buffer is empty");

    // decrement head (so it points to data), and read data
    // this implies we are not full
    dec_with_wraparound(head, N);
    auto val = std::move(buffer[head]);
    isfull = false;

    return val;
  }

  void reset() {
    head = tail;
    isfull = false;
  }

  bool empty() const {
    // if head and tail are equal, we are empty
    return (!isfull && (head == tail));
  }

  bool full() const {
    // If tail is ahead the head by 1, we are full
    return isfull;
  }

  uint32_t capacity() const { return N; }

  uint32_t size() const {
    uint32_t size = N;

    if (!isfull) {
      if (head >= tail)
        size = head - tail;
      else
        size = N + head - tail;
    }

    return size;
  }

private:
  std::unique_ptr<T[]> buffer;
  uint32_t head = 0;
  uint32_t tail = 0;
  bool isfull = false;
};
