#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <memory>

#include "utils.h"

/* based on https://embeddedartistry.com/blog/2017/05/17/creating-a-circular-buffer-in-c-and-c/ */
template <class T>
class CircularBuffer {
public:
	explicit CircularBuffer(uint32_t size) :
		buffer(std::unique_ptr<T[]>(new T[size])),
		max_size(size) { }

	void push(T item)
	{
		buffer[head] = std::move(item);

		if(isfull)
			DIE("circular buffer is full");

        inc_with_wraparound(head, max_size);
		isfull = head == tail;
	}

	T pop()
	{
		if(empty())
			DIE("circular buffer is empty");

		//Read data and advance the tail (we now have a free space)
		auto val = std::move(buffer[tail]);
		isfull = false;
        inc_with_wraparound(tail, max_size);

		return val;
	}

	void reset() {
		head = tail;
		isfull = false;
	}

	bool empty() const {
		//if head and tail are equal, we are empty
		return (!isfull && (head == tail));
	}

	bool full() const {
		//If tail is ahead the head by 1, we are full
		return isfull;
	}

	uint32_t capacity() const {
		return max_size;
	}

	uint32_t size() const {
		uint32_t size = max_size;

		if(!isfull) {
			if(head >= tail)
				size = head - tail;
			else
				size = max_size + head - tail;
		}

		return size;
	}

private:
	std::unique_ptr<T[]> buffer;
	uint32_t head = 0;
	uint32_t tail = 0;
	const uint32_t max_size;
	bool isfull = false;
};

#endif
