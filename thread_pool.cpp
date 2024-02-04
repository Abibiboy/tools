#include <iostream>
#include <unistd.h>
#include <string>
#include <vector>
#include <memory>
#include <queue>
#include <functional>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>

class ThreadPool {
private:
    using TaskFunc = std::function<void()>;
    struct TaskQueue {
        // 任务队列
        std::queue<TaskFunc>                        _tasks;
        int                                         _max_tasks_num;
        // 保护任务队列
        std::mutex                                  _mtx;  
        std::condition_variable                     _cv;

        // 提供任务队列的接口
    public:
        TaskQueue(int num) 
            : _max_tasks_num(num)
        {}

        void push(TaskFunc f) {
            {
                std::unique_lock<std::mutex> lock(_mtx);
                _cv.wait(lock, [this](){
                    return _tasks.size() < _max_tasks_num;
                });

                _tasks.push(std::move(f));
                _cv.notify_one();
            }
        }
        
        // 没任务就弹出空
        TaskFunc pop() {
            TaskFunc f;
            {
                std::unique_lock<std::mutex> lock(_mtx);

                if (_tasks.empty())
                    return f;

                f = _tasks.front();
                _tasks.pop();
            }
            return f;
        }
    };

private:
    static constexpr int MAX_TASKS_NUM          = 20;               // 默认最大 20 个任务

    // 所有线程
    int                                         _threads_num;
    std::vector<std::unique_ptr<std::thread>>   _threads;
    std::unique_ptr<std::atomic<bool>>          _is_stop;

    TaskQueue                                   _taskq;
    std::mutex                                  _mtx;
    std::condition_variable                     _cv;
public:
    // 每个线程应该做的事情
    void thread_do() {
        //  先取一次，后面都会自动取
        auto f = _taskq.pop();
        bool is_pop = static_cast<bool>(f);     // 是否是一个有效任务

        while (true) {
            while (is_pop == true) {
                f();                                // 执行任务
                f = _taskq.pop();                   // 弹出下一个任务
                is_pop = static_cast<bool>(f);
            }

            // 任务队列暂时为空, 可以阻塞等待
            {
                std::unique_lock<std::mutex> lock(_mtx);
                _cv.wait(lock, [this, &f, &is_pop]() {
                    return *_is_stop == true || (f = _taskq.pop());                // 如果线程池暂停了，那么就结束
                });
                if (*_is_stop == true)       return;
                if (f)                       is_pop = true;
            }
        }
    }

    ThreadPool(int threads_num, int max_tasks_num = MAX_TASKS_NUM)
        : _threads_num(threads_num)
        , _taskq(max_tasks_num)
        , _is_stop(new std::atomic<bool>(false))
    {
        _threads.resize(_threads_num);
        for (int i = 0; i < _threads_num; i ++) {
            _threads[i].reset(new std::thread(&ThreadPool::thread_do, this));
        }
    }

    // 提交一个任务
    void submit(TaskFunc f) {
        _taskq.push(f);
        std::unique_lock<std::mutex> lock(_mtx);
        _cv.notify_one();                           // 唤醒一个线程
    }

    // 终止
    void stop() {
        *_is_stop = true;
        {
            std::unique_lock<std::mutex> lock(_mtx);
            _cv.notify_all();
        }

        for (int i = 0; i < _threads_num; i ++) {
            if (_threads[i]->joinable()) {
                _threads[i]->join();
            }
        }
    }

};

int main() {
    ThreadPool pool(2);
    int i = 0;
    while (i < 10) {
        pool.submit([&i]() { 
            std::cout << std::this_thread::get_id() << " hello " << std::to_string(i ++) << std::endl; 
        });
        sleep(1);
    }
    pool.stop();
    return 0;
}
