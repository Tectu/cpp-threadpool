#pragma once

#include <concepts>
#include <queue>
#include <mutex>
#include <shared_mutex>

namespace jbo
{

    /**
     * A blocking queue implementation.
     *
     * @details All functions are thread-safe.
     *
     * @details This uses an std::shared_mutex to facilitate read vs. write blocking in a simple manner.
     *          Certain actions, such as checking the size can be done by multiple threads simultaneously as long as no
     *          write operations are happening. Only one write operation may occur at any given time.
     */
    template<
        std::movable T,
        typename Queue = std::queue<T>
    >
    struct queue
    {
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
            m_queue = { };
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
        using read_lock = std::shared_lock<std::shared_mutex>;
        using write_lock = std::scoped_lock<std::shared_mutex>;

        Queue m_queue;
        mutable std::shared_mutex m_mtx;
    };

}
