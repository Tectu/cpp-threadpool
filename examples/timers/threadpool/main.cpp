#include <timer.hpp>
#include <thread_pool.hpp>

#include <chrono>
#include <iostream>

void
print_time(std::ostream& out)
{
    using clock_type = std::chrono::steady_clock;

    static const auto start = clock_type::now();

    out << std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - start).count();
}

int
main()
{
    using namespace std::chrono_literals;

    std::cout << "starting thread pool..." << std::endl;
    jbo::thread_pool tp;
    tp.start(4);

    std::cout << "starting timer executor..." << std::endl;
    jbo::timers::executors::thread_pool te(jbo::timers::manager::instance(), tp, 1ms);
    te.start();

    // A periodic timer
    jbo::timers::manager::instance().periodic(10ms, []{
        static std::size_t i = 0;
        print_time(std::cout);
        std::cout << " | periodic 1: (" << i++ << ") --- thread: " << std::this_thread::get_id() << std::endl;
    });

    std::this_thread::sleep_for(20s);

    // Stop everything
    std::cout << "stopping timer executor..." << std::endl;
    te.stop();
    std::cout << "stopping thread pool..." << std::endl;
    tp.stop();

    std::cout << "done" << std::endl;

    return EXIT_SUCCESS;
}
