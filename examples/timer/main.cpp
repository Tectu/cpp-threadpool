#include <timer.hpp>

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

    jbo::timers::executor te(jbo::timers::manager::instance(), 10ms);
    te.start();

    // A periodic timer
    jbo::timers::manager::instance().periodic(100ms, []{
        static std::size_t i = 0;
        print_time(std::cout);
        std::cout << " | periodic 1: (" << i++ << ") --- thread: " << std::this_thread::get_id() << std::endl;
    });
#if 0
    // A timer firing at random intervals
    jbo::timers::manager::instance().periodic(100ms, 1000ms, []{
        print_time(std::cout);
        std::cout << " | periodic 2: !" << std::endl;
    });

    // A single shot timer
    jbo::timers::manager::instance().single_shot(1000ms, []{
        print_time(std::cout);
        std::cout  << " | Single Shot!" << std::endl;
    });
#endif
    std::this_thread::sleep_for(5s);

    std::cout << "stopping timer executor..." << std::endl;
    te.stop();

    std::cout << "done" << std::endl;

    return EXIT_SUCCESS;
}
