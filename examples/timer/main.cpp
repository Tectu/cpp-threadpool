#include <thread_pool.hpp>
#include <timer.hpp>

#include <chrono>
#include <iostream>

int
main()
{
    using namespace std::chrono_literals;
    using clock_t = std::chrono::steady_clock;

    std::cout << "starting threadpool..." << std::endl;
    jbo::thread_pool tp;
    tp.start(4);

    jbo::setup_timer_manager(tp, 10ms);

    const auto start = clock_t::now();
#if 1
    jbo::timer_manager::instance().periodic(100ms, [start]{
        static std::size_t i = 0;
        std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - start).count() << " | periodic 1: (" << i++ << ") --- thread: " << std::this_thread::get_id() << std::endl;
    });
#endif
#if 1
    jbo::timer_manager::instance().periodic(100ms, 1000ms, [start]{
        std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - start).count() << " | periodic 2: !" << std::endl;
    });
#endif
#if 1
    jbo::timer_manager::instance().single_shot(1000ms, [start]{
        std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - start).count() << " | Single Shot!" << std::endl;
    });
#endif

    std::this_thread::sleep_for(10s);

    std::cout << "stopping timer_manager..." << std::endl;
    jbo::timer_manager::instance().stop();
    std::cout << "stopping threadpool..." << std::endl;
    tp.stop();

    std::cout << "done" << std::endl;

    return EXIT_SUCCESS;
}
