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

    jbo::timers::executors::standalone<1> te(jbo::timers::manager::instance(), 10ms);
    te.start();

    // Create periodic timer
    auto t1 = jbo::timers::manager::instance().periodic(1s, []{
        static std::size_t i = 0;
        print_time(std::cout);
        std::cout << " | periodic 1: (" << i++ << ") --- thread: " << std::this_thread::get_id() << std::endl;
    });

    std::this_thread::sleep_for(5s);

    // Modify interval
    std::cout << "Modifying interval" << std::endl;
    t1.set_interval(250ms);

    std::this_thread::sleep_for(5s);

    // Modify interval
    std::cout << "Modifying interval" << std::endl;
    t1.set_interval(2500ms);

    std::this_thread::sleep_for(5s);

    std::cout << "stopping timer executor..." << std::endl;
    te.stop();

    std::cout << "done" << std::endl;

    return EXIT_SUCCESS;
}
