#pragma once

#include "queue.hpp"

#include <atomic>
#include <chrono>
#include <forward_list>
#include <functional>
#include <mutex>
#include <random>
#include <variant>

#include <iostream>
namespace jbo
{

    template<typename ... Ts> struct overload : Ts ... { using Ts::operator() ...; };
    template<class... Ts> overload(Ts...) -> overload<Ts...>;

    struct timer
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
            enabled = true;
        }

        void
        stop()
        {
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

        task_t task;
        std::chrono::milliseconds current;
        bool enabled = false;
        std::variant<periodic_constant, periodic_uniform, singleshot> data;

        template<typename RandomGenerator>
        void
        arm(RandomGenerator& rng)
        {
            std::visit(
                overload{
                    [this](timer::periodic_constant& td) {
                        current = td.interval;
                    },
                    [this, &rng](timer::periodic_uniform& td) {
                        current = std::chrono::milliseconds{td.distribution(rng)};
                    },
                    [this](timer::singleshot& td) {
                        enabled = false;   // ToDo: Remove from timers list
                    }
                },
                data
            );
        }
    };

    /**
     * A manager to manage timers.
     *
     * @details This implementation uses two queues: One to manage timeouts and one to manage actual timer tasks. The
     *          benefit of this approach is that timer tasks are not being executed in the same thread as the tick()
     *          function. This provides better timer accuracy.
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
            for (timer& t : m_timers.list)
                t.stop();

            // Stop task workers
            m_stop.test_and_set();

            // Clear pending tasks
            // ToDo
            //m_pending_tasks.clear();
        }

        template<typename F, typename ...Args>
        void
        periodic(std::chrono::milliseconds interval, F&& f, Args&& ...args)
        {
            add(
            timer::periodic_constant{ .interval = interval },
            std::forward<F>(f), std::forward<Args>(args)...
            );
        }

        template<typename F, typename ...Args>
        void
        periodic(std::chrono::milliseconds min, std::chrono::milliseconds max, F&& f, Args&& ...args)
        {
            add(
            timer::periodic_uniform{ .distribution = decltype(timer::periodic_uniform::distribution)(min.count(), max.count())},
                std::forward<F>(f), std::forward<Args>(args)...
            );
        }

        // ToDo: This does currently not work because timer::arm() immediately disables the timer
        template<typename F, typename ...Args>
        void
        single_shot(std::chrono::milliseconds interval, F&& f, Args&& ...args)
        {
            add(
                timer::singleshot{ },
                std::forward<F>(f), std::forward<Args>(args)...
            );
        }

        template<
            typename TimerData,
            typename F, typename ...Args
        >
        void
        add(TimerData&& td, F&& f, Args&& ...args)
        {
            timer t;
            t.data = std::move(td);
            t.task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
            t.enabled = true;
            t.arm(m_random_generator);

            std::scoped_lock lock(m_timers.mutex);
            m_timers.list.push_front(std::move(t));
        }

        /**
         * @param d Duration since previous call of this function.
         */
        void
        tick(std::chrono::milliseconds d)
        {
            // Iterate over each timer
            std::scoped_lock lock(m_timers.mutex);
            for (timer& t : m_timers.list) {
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

        // Executes actual timer tasks.
        // Should be called in a thread.
        // ToDo: Make sure this is thread safe (i.e. having more than one worker thread)
        void
        worker(const std::atomic_flag& stop)
        {
            while (!stop.test()) {
                // Get task
                timer::task_t task;
                {
                    // Get the task
                    if (!m_pending_tasks.try_pop(task))
                        continue;
                }

                // Run task
                task();
            }

        }

    private:
        struct {
            std::mutex mutex;
            std::forward_list<timer> list;
        } m_timers;
        queue<timer::task_t> m_pending_tasks;
        std::atomic_flag m_stop;

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
    };

    void
    setup_timer_manager(thread_pool& tp, std::chrono::milliseconds tick_resolution)
    {
        using clock_t = std::chrono::steady_clock;
        auto& tm = timer_manager::instance();

        // Ticker
        std::ignore = tp.enqueue([&tp, tick_resolution]{
            static auto t_prev = clock_t::now();
            while (!tp.stop_token().test()) {
                const auto t_now = clock_t::now();
                jbo::timer_manager::instance().tick(std::chrono::duration_cast<std::chrono::milliseconds>(t_now - t_prev));
                t_prev = t_now;

                std::this_thread::sleep_for(tick_resolution);
            }
        });

        // Worker
        // ToDo: Support more than one worker thread
        std::ignore = tp.enqueue([&tp]{
            while (!tp.stop_token().test()) {
                jbo::timer_manager::instance().worker(tp.stop_token());
            }
        });
    }

}
