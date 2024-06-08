#pragma once

#include "queue.hpp"

#include <atomic>
#include <chrono>
#include <forward_list>
#include <functional>
#include <mutex>
#include <random>
#include <thread>
#include <variant>

namespace jbo::timers
{

    template<typename ... Ts> struct overload : Ts ... { using Ts::operator() ...; };
    template<class... Ts> overload(Ts...) -> overload<Ts...>;

    struct data;

    /**
     * A timer handle for the user to operate on.
     *
     * @note Although this is called timer, it's actually just a handle. We do this to hide implementation complexity
     *       from the user.
     */
    struct timer
    {
        timer() = delete;

        timer(data& d) :
            m_data{ d }
        {
        }

        timer(const timer& other) = default;

        timer(timer&& other) = default;

        virtual
        ~timer() = default;

        timer&
        operator=(const timer& other) = default;

        timer&
        operator=(timer&& other) = default;

        /**
         * Start the timer.
         */
        void
        start();

        /**
         * Stop the timer.
         */
        void
        stop();

    private:
        data& m_data;
    };

    /**
     * The actual timer data.
     */
    struct data
    {
        /**
         * The task type.
         *
         * @note: Using type-erasure here.
         */
        using task_type = std::function<void()>;

        void
        start()
        {
            std::scoped_lock lock(m_mutex);

            enabled = true;
        }

        void
        stop()
        {
            std::scoped_lock lock(m_mutex);

            enabled = false;
        }

    private:
        friend struct manager;

        struct periodic_constant {
            std::chrono::milliseconds interval;
        };
        struct periodic_uniform {
            std::uniform_int_distribution<std::chrono::milliseconds::rep> distribution;
        };
        struct singleshot {
        };

        std::mutex m_mutex;
        task_type task;
        std::chrono::milliseconds current;      // ToDo: Should this be atomic?
        bool enabled = false;
        std::variant<periodic_constant, periodic_uniform, singleshot> data;

        [[nodiscard]]
        timer
        make_handle()
        {
            return timer{*this};
        }

        template<typename RandomGenerator>
        void
        arm(RandomGenerator& rng)
        {
            std::scoped_lock lock(m_mutex);

            std::visit(
                overload{
                    [this](data::periodic_constant& td) {
                        current = td.interval;
                    },
                    [this, &rng](data::periodic_uniform& td) {
                        current = std::chrono::milliseconds{td.distribution(rng)};
                    },
                    [this](data::singleshot& td) {
                        enabled = false;   // ToDo: Remove from timers list
                    }
                },
                data
            );
        }
    };

    inline
    void
    timer::start()
    {
        m_data.start();
    }

    inline
    void
    timer::stop()
    {
        m_data.stop();
    }

    /**
     * A manager to manage timers.
     *
     * @details This maintains two queues: One containing the actual timers to process ticks & timeouts and one to queue
     *          up timer tasks for execution when a timer expires.
     *          The benefit of this approach is that timer tasks are not (necessarily) executed in the same thread as
     *          the tick() function. This provides better timer accuracy.
     */
    struct manager
    {
    private:
        using resolution = std::chrono::milliseconds;

    public:
        [[nodiscard]]
        static
        manager&
        instance()
        {
            static manager i;
            return i;
        }

        void
        stop()
        {
            // Disable all timers
            std::scoped_lock lock(m_timers.mutex);
            for (data& d : m_timers.list)
                d.stop();

            // Clear pending tasks
            // ToDo
            //m_pending_tasks.clear();
        }

        template<typename F, typename ...Args>
        timer
        periodic(std::chrono::milliseconds interval, F&& f, Args&& ...args)
        {
            return add(
                data::periodic_constant{ .interval = interval },
                std::forward<F>(f), std::forward<Args>(args)...
            );
        }

        template<typename F, typename ...Args>
        timer
        periodic(std::chrono::milliseconds min, std::chrono::milliseconds max, F&& f, Args&& ...args)
        {
            return add(
            data::periodic_uniform{ .distribution = decltype(data::periodic_uniform::distribution)(min.count(), max.count())},
                std::forward<F>(f), std::forward<Args>(args)...
            );
        }

        // ToDo: This does currently not work because timer::arm() immediately disables the timer
        template<typename F, typename ...Args>
        timer
        single_shot(std::chrono::milliseconds interval, F&& f, Args&& ...args)
        {
            return add(
                data::singleshot{ },
                std::forward<F>(f), std::forward<Args>(args)...
            );
        }

        /**
         * Perform a tick.
         *
         * @details This function needs to be called periodically.
         *
         * @note As of the current implementation, this must not be called by multiple threads.
         *
         * @param d Duration since previous call of this function.
         */
        void
        tick(std::chrono::milliseconds d)
        {
            // Iterate over each timer
            std::scoped_lock lock(m_timers.mutex);
            for (data& t : m_timers.list) {
                // Acquire mutex lock
                //std::scoped_lock lock(t.m_mutex);

                // Skip disabled timers
                if (!t.enabled)
                    continue;

                // Decrement (prevent overflow)
                if (t.current - d > decltype(t.current)::zero())
                    t.current -= d;
                else
                    t.current = decltype(t.current)::zero();

                // Check for timeout
                if (t.current == decltype(t.current)::zero()) {
                    // Re-arm
                    t.arm(m_random_generator);

                    // Push task
                    m_pending_tasks.emplace(t.task);
                }
            }

        }

        /**
         * Executes actual timer tasks.
         *
         * @details Executes one timer task and returns. Returns immediately if currently no timer task needs to be
         *          executed.
         *
         * @note This function should be called much more frequently than tick() to prevent the timer task execution
         *       queue from filling up.
         */
        // ToDo: Make sure this is thread safe (i.e. having more than one worker thread)
        void
        execute_task()
        {
            // Get task
            data::task_type task;
            {
                // Get the task
                if (!m_pending_tasks.try_pop(task))
                    return;
            }

            // Run task
            task();
        }

    private:
        struct {
            std::mutex mutex;
            std::forward_list<data> list;      // ToDo: Should we use std::priority_queue to keep timers with shorter interval in the front?
        } m_timers;
        queue<data::task_type> m_pending_tasks;

        std::default_random_engine m_random_generator;

        manager()
        {
            // Generator
            // ToDo: Better seed
            m_random_generator = decltype(m_random_generator)(std::chrono::system_clock::now().time_since_epoch().count());
        }

        manager(const manager&) = delete;
        manager(manager&&) = delete;

        virtual
        ~manager() = default;

        manager&
        operator=(const manager&) = delete;

        manager&
        operator=(manager&&) = delete;

        template<
            typename TimerData,
            typename F, typename ...Args
        >
        timer
        add(TimerData&& td, F&& f, Args&& ...args)
        {
            std::scoped_lock lock(m_timers.mutex);

            data& t = m_timers.list.emplace_front();
            t.data = std::move(td);
            t.task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
            t.enabled = true;
            t.arm(m_random_generator);

            return t.make_handle();
        }
    };

    /**
     * Executor to act on a timer manager.
     */
    // ToDo: Allow executing tasks via a threadpool. Note that this requires that timer tasks can be executed in parallel.
    //       Can we protect against this or does the user have to do that themselves in their task executor function?
    // ToDo: Better name?
    struct
    executor
    {
        using clock_type = std::chrono::steady_clock;

        executor(manager& tm, std::chrono::milliseconds tick_interval) :
            m_tm{ tm },
            m_tick_interval{ tick_interval }
        {
        }

        virtual
        ~executor()
        {
            stop();
        }

        void
        start()
        {
            m_stop.clear();

            m_task_thread = std::thread(&executor::task_worker, this);
            m_ticker_thread = std::thread(&executor::tick_worker, this);
        }

        void
        stop()
        {
            m_stop.test_and_set();

            m_tm.stop();

            if (m_ticker_thread.joinable())
                m_ticker_thread.join();
            if (m_task_thread.joinable())
                m_task_thread.join();
        }

    private:
        manager& m_tm;
        const std::chrono::milliseconds m_tick_interval;
        std::thread m_ticker_thread;
        std::thread m_task_thread;
        std::atomic_flag m_stop;

        void
        tick_worker()
        {
            static auto t_prev = clock_type::now();
            while (!m_stop.test()) {
                const auto t_now = clock_type::now();
                manager::instance().tick(std::chrono::duration_cast<std::chrono::milliseconds>(t_now - t_prev));
                t_prev = t_now;

                std::this_thread::sleep_for(m_tick_interval);
            }
        }

        void
        task_worker()
        {
            while (!m_stop.test()) {
                manager::instance().execute_task();
            }
        }
    };

}
