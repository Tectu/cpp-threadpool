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

namespace jbo
{

    template<typename ... Ts> struct overload : Ts ... { using Ts::operator() ...; };
    template<class... Ts> overload(Ts...) -> overload<Ts...>;

    struct timer_data;

    /**
     * @note Although this is called timer, it's actually just a handle. We do this to hide implementation complexity
     *       from the user.
     */
    struct timer
    {
        timer(timer_data& t) :
            m_timer{ t }
        {
        }

        void
        start();

        void
        stop();

    private:
        timer_data& m_timer;
    };

    /**
     * The actual timer data.
     */
    struct timer_data
    {
        /**
         * The task type.
         *
         * @note: Using type-erasure here.
         */
        using task_t = std::function<void()>;

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
        friend struct timer_manager;

        struct periodic_constant {
            std::chrono::milliseconds interval;
        };
        struct periodic_uniform {
            std::uniform_int_distribution<std::chrono::milliseconds::rep> distribution;
        };
        struct singleshot {
        };

        std::mutex m_mutex;
        task_t task;
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
                    [this](timer_data::periodic_constant& td) {
                        current = td.interval;
                    },
                    [this, &rng](timer_data::periodic_uniform& td) {
                        current = std::chrono::milliseconds{td.distribution(rng)};
                    },
                    [this](timer_data::singleshot& td) {
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
        m_timer.start();
    }

    inline
    void
    timer::stop()
    {
        m_timer.stop();
    }

    /**
     * A manager to manage timers.
     *
     * @details This maintains two queues: One containing the actual timers to process ticks & timeouts and one to queue
     *          up timer tasks for execution when a timer expires.
     *          The benefit of this approach is that timer tasks are not (necessarily) executed in the same thread as
     *          the tick() function. This provides better timer accuracy.
     */
    struct timer_manager
    {
    private:
        using resolution = std::chrono::milliseconds;

    public:
        [[nodiscard]]
        static
        timer_manager&
        instance()
        {
            static timer_manager i;
            return i;
        }

        void
        stop()
        {
            // Disable all timers
            std::scoped_lock lock(m_timers.mutex);
            for (timer_data& t : m_timers.list)
                t.stop();

            // Clear pending tasks
            // ToDo
            //m_pending_tasks.clear();
        }

        template<typename F, typename ...Args>
        timer
        periodic(std::chrono::milliseconds interval, F&& f, Args&& ...args)
        {
            return add(
            timer_data::periodic_constant{ .interval = interval },
            std::forward<F>(f), std::forward<Args>(args)...
            );
        }

        template<typename F, typename ...Args>
        timer
        periodic(std::chrono::milliseconds min, std::chrono::milliseconds max, F&& f, Args&& ...args)
        {
            return add(
            timer_data::periodic_uniform{ .distribution = decltype(timer_data::periodic_uniform::distribution)(min.count(), max.count())},
                std::forward<F>(f), std::forward<Args>(args)...
            );
        }

        // ToDo: This does currently not work because timer::arm() immediately disables the timer
        template<typename F, typename ...Args>
        timer
        single_shot(std::chrono::milliseconds interval, F&& f, Args&& ...args)
        {
            return add(
                timer_data::singleshot{ },
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
            for (timer_data& t : m_timers.list) {
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
            timer_data::task_t task;
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
            std::forward_list<timer_data> list;      // ToDo: Should we use std::priority_queue to keep timers with shorter interval in the front?
        } m_timers;
        queue<timer_data::task_t> m_pending_tasks;

        std::default_random_engine m_random_generator;

        timer_manager()
        {
            // Generator
            // ToDo: Better seed
            m_random_generator = decltype(m_random_generator)(std::chrono::system_clock::now().time_since_epoch().count());
        }

        timer_manager(const timer_manager&) = delete;
        timer_manager(timer_manager&&) = delete;

        virtual
        ~timer_manager() = default;

        timer_manager&
        operator=(const timer_manager&) = delete;

        timer_manager&
        operator=(timer_manager&&) = delete;

        template<
            typename TimerData,
            typename F, typename ...Args
        >
        timer
        add(TimerData&& td, F&& f, Args&& ...args)
        {
            std::scoped_lock lock(m_timers.mutex);

            timer_data& t = m_timers.list.emplace_front();
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
    timer_executor
    {
        using clock_type = std::chrono::steady_clock;

        timer_executor(timer_manager& tm, std::chrono::milliseconds tick_interval) :
            m_tm{ tm },
            m_tick_interval{ tick_interval }
        {
        }

        virtual
        ~timer_executor()
        {
            stop();
        }

        void
        start()
        {
            m_stop.clear();

            m_task_thread = std::thread(&timer_executor::task_worker, this);
            m_ticker_thread = std::thread(&timer_executor::tick_worker, this);
        }

        void
        stop()
        {
            m_stop.test_and_set();

            m_tm.stop();

            m_ticker_thread.join();
            m_task_thread.join();
        }

    private:
        timer_manager& m_tm;
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
                jbo::timer_manager::instance().tick(std::chrono::duration_cast<std::chrono::milliseconds>(t_now - t_prev));
                t_prev = t_now;

                std::this_thread::sleep_for(m_tick_interval);
            }
        }

        void
        task_worker()
        {
            while (!m_stop.test()) {
                jbo::timer_manager::instance().execute_task();
            }
        }
    };

}
