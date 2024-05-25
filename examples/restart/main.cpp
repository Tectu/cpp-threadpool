#include <chrono>
#include <iostream>

#include <thread_pool.hpp>

int
main()
{
    constexpr static const std::size_t num_tasks = 10;
    static_assert(num_tasks % 2 == 0);

    std::cout << "initializing..." << std::endl;
    jbo::thread_pool tp;
    tp.start(4);

    std::cout << "enqueuing..." << std::endl;
    std::vector<std::future<int>> results;
    for (std::size_t i = 0; i < num_tasks; i++) {

        auto result = tp.enqueue([i]() -> int {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            return i * i;
        });

        results.emplace_back(std::move(result));
    }

    std::cout << "waiting... (1/2)" << std::endl;
    for (std::size_t i = 0; i < num_tasks/2; i++)
        std::cout << "result: "  << results[i].get() << std::endl;

    std::cout << "stopping..." << std::endl;
    tp.stop();

    std::cout << "sleeping..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));

    std::cout << "starting..." << std::endl;
    tp.start(4);

    std::cout << "waiting... (2/2)" << std::endl;
    for (std::size_t i = num_tasks/2; i < num_tasks; i++)
        std::cout << "result: "  << results[i].get() << std::endl;

    return EXIT_SUCCESS;
}
