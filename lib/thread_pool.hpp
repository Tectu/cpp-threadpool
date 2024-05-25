#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <thread>
#include <vector>
#include <type_traits>

namespace jbo
{

    /**
     * A blocking queue implementation.
     *
     * @details This uses an std::shared_mutex to facilitate read vs. write blocking in a simple manner.
     *          Certain actions, such as checking the size can be done by multiple threads simultaneously as long as no
     *          write operations are happening. Only one write operation may occur at any given time.
     */
    template<
        std::movable T,
        typename Queue = std::queue<T>
    >
    class queue
    {
    public:
        [[nodiscard]]
        bool
        empty() const
        {
            read_lock lock(m_mtx);
            return m_queue.empty();
        }

        [[nodiscard]]
        std::size_t
        size() const
        {
            read_lock lock(m_mtx);
            return std::size(m_queue);
        }

        void
        clear()
        {
            write_lock lock(m_mtx);
            m_queue.clear();
        }

        template<typename ...Args>
        void
        emplace(Args&& ...args)
        {
            write_lock lock(m_mtx);
            m_queue.emplace(std::forward<Args>(args)...);
        }

        [[nodiscard]]
        bool
        try_pop(T& t)
        {
            write_lock lock(m_mtx);

            if (m_queue.empty())
                return false;

            t = std::move(m_queue.front());
            m_queue.pop();

            return true;
        }

    private:
        using read_lock = std::scoped_lock<std::shared_mutex>;
        using write_lock = std::shared_lock<std::shared_mutex>;

        Queue m_queue;
        mutable std::shared_mutex m_mtx;
    };

    /**
     * A simple to use, robust and flexible C++ thread pool.
     */
    // ToDo: Use std::shared_mutex to make stat reads faster
    // ToDo: Cache pool size?
    class thread_pool
    {
    public:
        enum class worker_state
        {
            idle,
            working,
            stopped,
        };

    private:
        struct worker
        {
            std::thread thread;     // ToDo: Do we have any benefits of using std::jthread in this scenario?
            std::atomic<enum worker_state> state;

            worker() = default;

            worker(const worker&) = delete;

            worker(worker&& other) noexcept :
                thread{ std::move(other.thread) },
                state{ other.state.load() }
            {
            }

            // ToDo: concept
            template<typename Func, typename ...Args>
            worker(Func&& f, Args&& ...args) :
                thread{ std::forward<Func>(f), std::forward<Args>(args)... },
                state{ worker_state::idle }
            {
            }
        };

    public:
        struct status
        {
            std::size_t pool_size  = 0;
            std::size_t queue_size = 0;
            std::vector<enum worker_state> thread_states;
        };

        thread_pool() = default;
        thread_pool(const thread_pool&) = delete;
        thread_pool(thread_pool&&) noexcept = delete;

        ~thread_pool()
        {
            stop();
        }

        thread_pool& operator=(const thread_pool&) = delete;
        thread_pool& operator=(thread_pool&&) noexcept = delete;

        /**
         * Starts the threadpool.
         *
         * @param size The number of worker threads to spawn.
         */
        void
        start(const std::size_t size)
        {
            // Prevent re-init
            static bool init_done = false;
            if (init_done)
                return;
            init_done = true;

            // Create workers
            m_workers.reserve(size);
            for (std::size_t i = 0; i < size; i++)
                m_workers.emplace_back(std::bind(&thread_pool::work, this, i));
        }

        /**
         * Stops the thread pool.
         *
         * @details All remaining tasks in the queue will be executed prior to stopping. This function will return once
         *          all tasks were completed.
         *
         * @note Calling enqueue() after stop() is illegal (currently: will throw).
         */
        // ToDo: Return future?
        void
        stop()
        {
            // Signal threads that they should stop.
            m_stop.test_and_set();
            m_cv.notify_all();

            // Wait for threads to finish.
            // Note: No need to set worker state here as that happens in the worker function
            for (auto& w : m_workers) {
                if (w.thread.joinable())
                    w.thread.join();
            }

            m_workers.clear();
        }

        /**
         * Checks whether the threadpool is running
         * @return
         */
        [[nodiscard]]
        bool
        is_running() const
        {
            return std::size(m_workers) > 0;
        }

        [[nodiscard]]
        std::size_t
        pool_size() const
        {
            // ToDo: Needs to be protected when we support dynamic sizing
            return std::size(m_workers);
        }

        [[nodiscard]]
        std::size_t
        queue_size() const
        {
            return std::size(m_queue);
        }

        [[nodiscard]]
        status
        get_status() const
        {
            status ret;

            ret.pool_size  = pool_size();
            ret.queue_size = queue_size();

            // Thread status
            ret.thread_states.reserve(std::size(m_workers));
            for (const auto& w : m_workers)
                ret.thread_states.emplace_back(w.state);

            return ret;
        }

        template<typename F, typename... Args>
        [[nodiscard]]
        std::future<typename std::invoke_result<F, Args...>::type>
        enqueue(F&& f, Args&& ...args)
        {
            using return_t = typename std::invoke_result<F, Args...>::type;
            using future_t = std::future<return_t>;
            using task_t   = std::packaged_task<return_t()>;

            // Create task
            // Note: Using shared_ptr to keep lambda captures & arguments alive
            auto task = std::make_shared<task_t>(std::move(std::bind(std::forward<F>(f), std::forward<Args>(args)...)));

            // Get future for later returning
            future_t res = task->get_future();

            // Enqueue
            {
                // Necessary as per std::condition_variable documentation
                std::scoped_lock lock(m_mtx);

                // Ensure that we're not queueing up new tasks when we're supposed to stop
                // ToDo: Throwing is not really nice.
                if (m_stop.test())
                    throw std::runtime_error("enqueuing task on thread pool that is no longer running.");

                m_queue.emplace([task]{ (*task)(); });
            }

            // Notify one thread
            m_cv.notify_one();

            return res;
        }

    private:
        /**
         * The actual pool of threads.
         */
        std::vector<worker> m_workers;

        /**
         * Queue of pending tasks.
         *
         * Note: Type erasure via std::function<void()>
         */
        queue<std::function<void()>> m_queue;

        std::mutex m_mtx;   // ToDo: Consider using std::shared_mutex (needs std::condition_variable_any)

        // ToDo: Does this make sense? We need the mutex above anyway for std::condition_variable. Maybe this makes the atomic superfluous.
        std::atomic_flag m_stop;    // ToDo: Use std::stop_token?

        /**
         * Condition variable to signal threads.
         *
         * @details Threads that are currently sleeping need to be signaled/awakened when they are supposed to do some work
         *          or stop (return).
         */
        std::condition_variable m_cv;

        /**
         * The function each worker thread in the pool will execute.
         */
        void
        work(const std::size_t idx)
        {
            worker& w = m_workers[idx];

            while (true) {

                // Currently we're idling
                w.state = worker_state::idle;

                // Get task
                std::function<void()> task;
                {
                    std::unique_lock lock(m_mtx);

                    // Wait until:
                    //   - We were notified (when there is work in the queue)
                    //   - The threadpool stop flag is set
                    //   - The queue isn't empty (there is pending work)
                    // ToDo: Optimize by performing queue::try_pop() and checking returned boolean in here?
                    m_cv.wait(lock, [this]{ return m_stop.test() || !m_queue.empty(); });

                    // If the thread pool is supposed to stop AND the queue is empty, this thread won't be needed anymore.
                    if (m_stop.test() && m_queue.empty()) {
                        w.state = worker_state::stopped;
                        return;
                    }

                    // Get the task
                    if (!m_queue.try_pop(task))
                        continue;
                }

                // Run task
                w.state = worker_state::working;
                task();
            }
        }
    };

}
