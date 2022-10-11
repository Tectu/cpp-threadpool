#include <chrono>
#include <iostream>

#include "thread_pool.hpp"

using namespace jbo;

int
long_running_task()
{
    using namespace std::chrono_literals;

    std::this_thread::sleep_for(5s);

    return 4;
}

void
print_state(const thread_pool::status& s, std::ostream& out = std::cout)
{
    std::cout << "pool size : " << s.pool_size << "\n";
    std::cout << "queue size: " << s.queue_size << "\n";
    for (std::size_t i = 0; i < s.thread_states.size(); i++)
        std::cout << "thread [" << i << "] state: " << static_cast<int>(s.thread_states[i]) << "\n";
}

int
main()
{
    thread_pool tp(4);

    std::vector<std::future<int>> results;
    for (std::size_t i = 0; i < 10; i++) {

        auto future = tp.enqueue([i]() -> int {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            return i * i;
        });

        results.emplace_back(std::move(future));
    }

    for (int i = 0; i < 10; i++) {
        print_state(tp.get_status());
        std::cout << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    std::cout << "waiting..." << std::endl;
    for (auto& result : results)
        std::cout << "result: "  << result.get() << std::endl;

    while (true)
        std::this_thread::sleep_for(std::chrono::seconds(1));
    return EXIT_SUCCESS;
}
