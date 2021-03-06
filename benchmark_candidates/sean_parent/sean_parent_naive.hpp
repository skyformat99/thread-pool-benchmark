
/* 
 * thread-pool-benchmark, a C++ Thread Pool Colosseum
 * Copyright (C) 2018  Red-Portal
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace sparent_naive
{
    namespace internal{
        using lock_t = std::unique_lock<std::mutex>; 

        class notification_queue {
            std::deque<std::function<void()>> _q;
            bool _done = false;
            std::mutex _mutex;
            std::condition_variable _ready;

        public:
            inline void
            done()
            {
                {
                    lock_t lock{_mutex};
                    _done = true;
                }
                _ready.notify_all();
            }

            inline bool
            pop(std::function<void()>& x)
            {
                lock_t lock{_mutex};
                while(_q.empty() && !_done)
                {
                    _ready.wait(lock);
                }
                if(_q.empty())
                    return false;
                x = move(_q.front());
                _q.pop_front();
                return true;
            }

            template<typename F>
            inline void
            push(F&& f)
            {
                {
                    lock_t lock{_mutex};
                    _q.emplace_back(std::forward<F>(f));
                }
                _ready.notify_one();
            }
        };

        class task_system
        {
            unsigned const _count;
            std::vector<std::thread> _threads;
            notification_queue _q;

            inline void
            run(unsigned i)
            {
                (void)i;
                while(true)
                {
                    std::function<void()> f;
                    if(!f && !_q.pop(f))
                        break;
                    f();
                }
            }

        public:
            inline task_system()
                : _count(std::thread::hardware_concurrency()),
                  _threads(),
                  _q()
            {
                for(unsigned n = 0; n != _count; ++n)
                    _threads.emplace_back([this, n]{ run(n); });
            }

            inline ~task_system()
            {
                _q.done();
                for(auto& e : _threads)
                    e.join();
            }

            inline void
            push(std::function<void()>&& f)
            {
                _q.push(std::move(f));
            }
        };

        void
        push_queue(std::function<void()>&& f);
    }
    
    template<typename F, typename... Args>
    decltype(auto)
    async(F&& f, Args&&... args)
    {
        using result_type = std::result_of_t<std::decay_t<F>(std::decay_t<Args>...)>;
        using packaged_type = std::packaged_task<result_type()>;

        auto ptr = new packaged_type(std::bind(std::forward<F>(f),
                                               std::forward<Args>(args)...));
        auto result = ptr->get_future();

        internal::push_queue([ptr]
                             {
                                 (*ptr)();
                                 delete ptr;
                             });
        return result;
    }
}

