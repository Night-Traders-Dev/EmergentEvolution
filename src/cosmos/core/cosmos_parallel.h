#pragma once

#include <algorithm>
#include <cstddef>
#include <thread>
#include <vector>

template <typename Fn>
static void run_parallel_chunks(size_t count, size_t workers, Fn&& fn) {
    if (count == 0) return;
    workers = std::max<size_t>(1, std::min(workers, count));
    if (workers <= 1) {
        fn(0, count);
        return;
    }

    std::vector<std::thread> pool;
    pool.reserve(workers - 1);
    for (size_t t = 1; t < workers; ++t) {
        const size_t begin = (count * t) / workers;
        const size_t end = (count * (t + 1)) / workers;
        pool.emplace_back([&, begin, end]() { fn(begin, end); });
    }
    fn(0, count / workers);
    for (auto& worker : pool) worker.join();
}
