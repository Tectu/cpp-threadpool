#include <chrono>
#include <iostream>

#include <thread_pool.hpp>

int
main()
{
    std::cout << "initializing..." << std::endl;
    jbo::thread_pool tp(4);

    std::cout << "enqueuing..." << std::endl;
    std::vector<std::future<int>> results;
    for (std::size_t i = 0; i < 10; i++) {

        auto future = tp.enqueue([i]() -> int {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            return i * i;
        });

        results.emplace_back(std::move(future));
    }

    std::cout << "waiting..." << std::endl;
    for (auto& result : results)
        std::cout << "result: "  << result.get() << std::endl;

    return EXIT_SUCCESS;
}
