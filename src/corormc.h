#ifndef CORORMC_H
#define CORORMC_H

#include <coroutine>
#include "allocator.h"
#include "utils/utils.h"

template <typename T = void> class CoroRMC {
/*
based on:
    https://www.modernescpp.com/index.php/c-20-an-infinite-data-stream-with-coroutines
    https://github.com/andreasbuhr/cppcoro/blob/master/include/cppcoro/task.hpp
*/
public:
    using value_type = T;

    /* must have this name */
    struct promise_type {
        /* move assignment op */
        promise_type &operator=(promise_type &&oth) = delete;
        /* move constructor */
        promise_type(promise_type &&oth) = delete;
        /* copy constructor */
        promise_type(const promise_type &) = delete;
        /* copy assignment op */
        promise_type &operator=(const promise_type &) = delete;

        /* constructor */
        promise_type() noexcept {};
        ~promise_type() = default;

        void *operator new(size_t size) {
          if (size != 128)
            DIE("promise size is not 128, it is=" << size);

          return RMCAllocator::get_promise();
        }

        void operator delete(void * p) {
            RMCAllocator::delete_promise(p);
        }

        auto initial_suspend() { return std::suspend_always{}; }

        auto final_suspend() { return std::suspend_always{}; }

        auto get_return_object() noexcept {
          return CoroRMC{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        auto return_void() { return std::suspend_never{}; }

        auto yield_value(const value_type value) noexcept {
          current_value = value;
          return std::suspend_always{};
        }

        void unhandled_exception() noexcept { std::exit(1); }

        value_type current_value;
    };

    /* constructors */
    CoroRMC(std::coroutine_handle<promise_type> h) noexcept : coroutine(h) {}
    /* default constructor */
    CoroRMC() = delete;
    /* move assignment op */
    CoroRMC &operator=(CoroRMC &&oth) = delete;
    /* move constructor */
    CoroRMC(CoroRMC &&oth) = delete;
    /* copy constructor */
    CoroRMC(const CoroRMC &) = delete;
    /* copy assignment op */
    CoroRMC &operator=(const CoroRMC &) = delete;

    ~CoroRMC() {
        if (coroutine)
            coroutine.destroy();
    }

    void *operator new(size_t size) {
        if (size != 16)
            DIE("promise size is not 16, it is=" << size);

        return RMCAllocator::get_rmc();
    }
    void operator delete(void * p) {
        RMCAllocator::delete_rmc(p);
    }

    /* returns true if coroutine is done; false otherwise */
    bool resume() {
        /* coroutine.done() returns true if the coroutine is suspended at its final
        suspend point, or false if the coroutine is suspended at other suspend
        points. The behavior is undefined if it does not refer to a suspended
        coroutine. */
        assert(not coroutine.done());
        coroutine.resume();
        return coroutine.done();
    }

    int id;

private:
    std::coroutine_handle<promise_type> coroutine;
};

#endif
