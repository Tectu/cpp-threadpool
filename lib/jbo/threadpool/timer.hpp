#pragma once

#include "queue.hpp"
#include "thread_pool.hpp"

#include <atomic>
#include <array>
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
        timer() = default;

        explicit
        timer(data& d) :
            m_data{ &d }
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
        data* m_data = nullptr;
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

            m_enabled = true;
        }

        void
        stop()
        {
            std::scoped_lock lock(m_mutex);

            m_enabled = false;
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
        std::mutex m_task_mutex;
        task_type m_task;
        std::atomic_flag m_task_pending;
        std::chrono::milliseconds m_current;      // ToDo: Should this be atomic?
        bool m_enabled = false;
        std::variant<periodic_constant, periodic_uniform, singleshot> m_data;

        [[nodiscard]]
        timer
        make_handle()
        {
            return timer(*this);
        }

        template<typename RandomGenerator>
        void
        arm(RandomGenerator& rng)
        {
            std::scoped_lock lock(m_mutex);

            std::visit(
                overload{
                    [this](data::periodic_constant& td) {
                        m_current = td.interval;
                    },
                    [this, &rng](data::periodic_uniform& td) {
                        m_current = std::chrono::milliseconds{td.distribution(rng)};
                    },
                    [this](data::singleshot& td) {
                        //
                        // Note: arm() is essentially called twice for a single-shot timer: The first time just when
                        //       the timer was created and the second time just after the timer fired/expired.
                        //       As such, we have to keep record of whether we are calling arm() the first time or the
                        //       second time. If we don't have this check in place, the timer would never fire as the first
                        //       call to arm() happens immediately at/after timer creation which would immediately disable
                        //       and remove the timer.

                        static bool first_invocation = true;

                        if (!first_invocation)
                            m_enabled = false;   // ToDo: Remove from timers list

                        first_invocation = false;
                    }
                },
                m_data
            );
        }

        /**
         * Execute the timer task.
         *
         * @note This will return immediately if the task is currently already being executed.
         */
        void
        execute_task()
        {
            // Do not execute if already executing
            if (m_task_pending.test())
                return;
            m_task_pending.test_and_set();

            // Execute task
            std::scoped_lock lock(m_task_mutex);
            m_task();

            // Ready to go again
            m_task_pending.clear();
        }
    };

    inline
    void
    timer::start()
    {
        if (m_data)
            m_data->start();
    }

    inline
    void
    timer::stop()
    {
        if (m_data)
            m_data->stop();
    }

    /**
     * A manager to manage timers.
     *
     * @details This maintains two queues: One containing the actual timers to process ticks & timeouts and one to queue
     *          up timer tasks for execution when a timer expires.
     *          The benefit of this approach is that timer tasks are not (necessarily) executed in the same thread as
     *          the tick() function. This provides better timer accuracy.
     *
     * @note There's an internal locking mechanism in place to prevent simultaneous execution of the timer task from
     *       multiple threads.
     */
    struct manager
    {
    private:
        using resolution = std::chrono::milliseconds;

    public:
        manager(const manager&) = delete;
        manager(manager&&) = delete;

        virtual
        ~manager() = default;

        manager&
        operator=(const manager&) = delete;

        manager&
        operator=(manager&&) = delete;

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
            m_pending_tasks.clear();
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
                if (!t.m_enabled)
                    continue;

                // Decrement (prevent overflow)
                if (t.m_current - d > decltype(t.m_current)::zero())
                    t.m_current -= d;
                else
                    t.m_current = decltype(t.m_current)::zero();

                // Check for timeout
                if (t.m_current == decltype(t.m_current)::zero()) {
                    // Re-arm
                    t.arm(m_random_generator);

                    // Push task
                    m_pending_tasks.emplace(std::bind(&data::execute_task, &t));
                }
            }

        }

        /**
         * Attempts to pop a task from the pending tasks queue.
         *
         * @details This function can be used to retrieve a timer task from the queue (if any). This function does not
         *          actually execute the task.
         *
         * @note This function is thread-safe.
         * @note This should be called more frequently than tick() to prevent the task queue from filling up.
         *
         * @return Whether a task was popped from the queue.
         */
        [[nodiscard]]
        bool
        pop_task(std::function<void()>& task)
        {
            return m_pending_tasks.try_pop(task);
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

        template<
            typename TimerData,
            typename F, typename ...Args
        >
        timer
        add(TimerData&& td, F&& f, Args&& ...args)
        {
            std::scoped_lock lock(m_timers.mutex);

            data& t = m_timers.list.emplace_front();
            t.m_data = std::move(td);
            t.m_task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
            t.m_enabled = true;
            t.arm(m_random_generator);

            return t.make_handle();
        }
    };

    namespace executors
    {

        /**
         * Standalone executor.
         *
         * @details This executor spawns 1+N threads: One thread to run the timer ticks and N threads to execute
         *          timer tasks.
         */
        template<std::size_t NumTaskExecutors>
        struct
        standalone
        {
            using clock_type = std::chrono::steady_clock;

            standalone(manager& tm, std::chrono::milliseconds tick_interval) :
                m_tm{ tm },
                m_tick_interval{ tick_interval }
            {
            }

            virtual
            ~standalone()
            {
                stop();
            }

            void
            start()
            {
                m_stop.clear();

                for (auto& t : m_task_thread)
                    t = std::thread(&standalone::task_worker, this);

                m_ticker_thread = std::thread(&standalone::tick_worker, this);
            }

            void
            stop()
            {
                m_stop.test_and_set();

                m_tm.stop();

                if (m_ticker_thread.joinable())
                    m_ticker_thread.join();

                for (auto& t : m_task_thread) {
                    if (t.joinable())
                        t.join();
                }
            }

        private:
            manager& m_tm;
            const std::chrono::milliseconds m_tick_interval;
            std::thread m_ticker_thread;
            std::array<std::thread, NumTaskExecutors> m_task_thread;
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
                    std::function<void()> task;
                    if (!manager::instance().pop_task(task))
                        continue;

                    if (task)
                        task();
                }
            }
        };

        /**
         * Executor using thread_pool for timer task execution.
         *
         * @details This executor maintains two threads: One for the timer ticker and one to push timer tasks to the
         *          provided threadpool.
         */
        // ToDo: Lots of code duplication with standalone executor.
        struct thread_pool
        {
            using clock_type = std::chrono::steady_clock;

            thread_pool(manager& tm, jbo::thread_pool& tp, std::chrono::milliseconds tick_interval) :
                m_tm{ tm },
                m_tp{ tp },
                m_tick_interval{ tick_interval }
            {
            }

            virtual
            ~thread_pool()
            {
                stop();
            }

            void
            start()
            {
                m_stop.clear();

                m_task_thread = std::thread(&thread_pool::task_worker, this);
                m_ticker_thread = std::thread(&thread_pool::tick_worker, this);
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
            jbo::thread_pool& m_tp;
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
                    std::function<void()> task;
                    if (!manager::instance().pop_task(task))
                        continue;

                    if (task)
                        std::ignore = m_tp.enqueue(std::move(task));
                }
            }
        };

    }

}
