<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-20-blue.svg" alt="standard"/>
  <img src="https://img.shields.io/badge/License-BSD-blue.svg" alt="license"/>
</p>


# Overview

A simple to use, robust and flexible thread pool written in C++20.

Features:
- Written in modern C++20
- Granular locking to improve mixed read/write loads
- High-level status interface
- Generic enqueuing interface allow any callable, not just functions

# Usage

Basic usage looks like this:
```cpp
// Initialize threadpool with four workers (threads)
jbo::thread_pool tp(4);

// Enqueue long running tasks
std::vector<std::future<int>> results;
for (std::size_t i = 0; i < 10; i++) {

    // Enqueue using lambda
    auto future = tp.enqueue([i]() -> int {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return i * i;
    });

    results.emplace_back(std::move(future));
}

// Collect results
for (auto& result : results)
    std::cout << "result: "  << result.get() << std::endl;
```
