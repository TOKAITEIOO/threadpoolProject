#ifndef THREADPOOL_H_
#define THREADPOOL_H_


#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <unordered_map>
#include <thread>
#include <future>


const int TASK_MAX_THRESHOLD = 2; //线程池上限阈值
const int THREAD_MAX_THRESHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 60; //单位为秒


// 枚举类
enum class PoolMode {
    MODE_FIXED, //固定数量的线程
    MODE_CACHED, //线程数量可动态增长
};


//线程类型
class Thread {
public:
    // 线程函数对象类型
    using ThreadFunc = std::function<void(size_t)>;
    // 线程构造
    Thread(ThreadFunc func)
        : func_(func)
        , threadId_(generateId_++)
    {}
    // 线程析构
    ~Thread() = default;
    // 启动线程
    void start() {
        // 创建一个线程来执行一个线程函数
        std::thread t(func_, threadId_); // C++11来说，有线程对象t,和线程函数func_
        t.detach();// 设置为分离线程
    }
    // 获取线程id
    size_t getId() const {
        // 获取线程id
        return threadId_;
    }
private:
    ThreadFunc func_; //存一个函数对象
    static size_t generateId_;
    size_t threadId_; //存储一个线程的id
};

size_t Thread::generateId_ = 0; //静态变量需要在类外初始化

// 线程池类型
class ThreadPool {
public:
    // 线程池构造
    ThreadPool()
        : initThreadSize_(4)
        , taskSize_(0)
        , taskQueMaxThreadHold_(TASK_MAX_THRESHOLD)
        , poolMode_(PoolMode::MODE_FIXED)
        , isPoolRunning_(false)
        , idleThreadSize_(0)
        , threadSizeThreshold_(THREAD_MAX_THRESHOLD)
        , curThreadSize_(0)
    {}

    // 线程池析构
    ~ThreadPool() {
        isPoolRunning_ = false;
        //等待线程池里面所有的线程返回，有两种状态: 一种是阻塞，正在执行任务中
        std::unique_lock<std::mutex> lock(taskQueMtx_);
        notEmpty_.notify_all();
        exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
    }

    // 设置线程池工作模式
    void setMode(PoolMode mode) {
        if (checkRunningState() == true) {
            return;
        }
        poolMode_ = mode;
    }
    // 设置task任务队列数量上限
    void setTaskQueMaxThreadHold(size_t threadhold) {
        if (checkRunningState() == true) {
            return;
        }
        taskQueMaxThreadHold_ = threadhold;
    }
    // 设置线程池Cached模式下线程的阈值
    void setThreadSizeThresHold(size_t threshold) {
        if (checkRunningState() == true) {
            return;
        }
        if (poolMode_ == PoolMode::MODE_CACHED) { //在Cached模式下才进行设置
            threadSizeThreshold_ = threshold;
        }
    }


    // 给线程池提交任务
    // 使用可变参模板编程，让submitTask可以接收任意任务函数和任意数量的参数
    // pool.submitTask(sum1, 10, 20)
    //CSDN 大秦坑王右值引用和引用折叠原理
    template<typename Func, typename... Args>
    auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> 
    {
        // 打包任务，放入任务队列里面
        using Rtype = decltype(func(args...)); //推到出来是一个类型，无法赋给一个变量，故不能使用auto,应该使用using
        auto task = std::make_shared<std::packaged_task<Rtype()>>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
        //不带参数是因为通过绑定器将参数与函数名进行绑定
        std::future<Rtype> result = task->get_future();


        // 获取锁
        std::unique_lock<std::mutex> lock(taskQueMtx_);
        // 用户提交任务，最长不能超过1秒，否则判断提交任务失败，返回
        if (!notFull_.wait_for(lock, std::chrono::seconds(1),
            [&]()->bool {return taskQue_.size() < taskQueMaxThreadHold_; })) {
            // 如果为True, 表示notFull_等待1s,条件依然没有满足
            std::cerr << "Task queue is full, submit task fail." << std::endl;
            auto task = std::make_shared<std::packaged_task<Rtype()>>(
                []()->Rtype {return Rtype(); });
            (*task)();
                return task->get_future();
        }
        
        
        //这里是否有问题？
        taskQue_.emplace([=]() {(*task)();});//用一个lambda表达式将有返回值的函数封装起来,去执行下面的任务     
        taskSize_++;
        // 因为新放入了任务，任务队列肯定不空了，notEmpty_ 进行通知
        notEmpty_.notify_all();

        // chched模式，任务处理比较紧急，场景：小而快的任务，需要根据任务数量和空闲线程的数量，判断是否需要创建新的线程出来

        if (poolMode_ == PoolMode::MODE_CACHED
            && taskSize_ > idleThreadSize_
            && curThreadSize_ < threadSizeThreshold_) {

            std::cout << "create new thread..." << std::endl;
            // 创建新线程
            // 需要增加的线程数量
            auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
            size_t threadId = ptr->getId();
            threads_.emplace(threadId, std::move(ptr));
            // 启动线程
            threads_[threadId]->start();
            // 修改线程个数相关的参数
            idleThreadSize_++;
            curThreadSize_++;
        }

        //返回任务的Result 对象
        return result; // Task -> Result


    }

    // 开启线程池
    void start(size_t initThreadsize) {
        // 设置线程池的运行状态
        isPoolRunning_ = true;
        // 记录初始线程的个数
        initThreadSize_ = initThreadsize;
        curThreadSize_ = initThreadsize;
        // 创建线程对象，把线程函数给到thread线程对象
        for (int i = 0; i < initThreadSize_; i++) {
            //要留一个参数占位符
            auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
            // threads_.emplace_back(std::move(ptr));
            size_t threadId = ptr->getId();
            threads_.emplace(threadId, std::move(ptr));

        }
        // 启动所有线程
        for (int i = 0; i < initThreadSize_; i++) {
            threads_[i]->start(); // 启动所有线程
            idleThreadSize_++; // 记录初始空闲线程的数量
        }
    }

    // 禁止用户进行拷贝构造和赋值构造
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
private:
    // 定义线程函数
    void threadFunc(size_t threadid) {
        // 线程函数执行完返回，相应的线程也结束了
        auto lastTime = std::chrono::high_resolution_clock().now(); // 记录上一次运行的时间

        // 所有任务完成必须执行完成，线程池才可以回收所有的线程资源
        for (;;) {
            Task task;
            {
                // 先获取锁
                std::unique_lock<std::mutex> lock(taskQueMtx_);
                std::cout << "tid:" << std::this_thread::get_id() << "尝试获取任务..." << std::endl;

                //cached 模式下，有可能已经创建了很多的线程，但是空闲时间超过60s,
                //如何把多余的线程结束回收？(超过initThreadSize_数量的线程需要进行回收)
                // 当前时间 - 上一次线程执行的时间 > 60s
                    //每一秒钟返回一次，返回的时候如何区分超时返回还是有任务待执行返回

                    // 锁+双重判断
                while (taskQue_.size() == 0) {
                    if (!isPoolRunning_) {
                        threads_.erase(threadid); // 不是std::thread::get_id()
                        // 记录线程数量相关变量的值修改
                        std::cout << "threadid :" << std::this_thread::get_id() << "exit!" << std::endl;
                        exitCond_.notify_all();
                        return; // 线程函数结束，线程结束
                    }


                    if (poolMode_ == PoolMode::MODE_CACHED) {
                        if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
                            // 超时返回了
                            auto now = std::chrono::high_resolution_clock().now();
                            auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                            if (dur.count() >= THREAD_MAX_IDLE_TIME
                                && curThreadSize_ > initThreadSize_) {
                                // 回收当前的线程
                                threads_.erase(threadid); 
                                // 不是std::thread::get_id()
                                // 记录线程数量相关变量的值修改
                                idleThreadSize_--;
                                curThreadSize_--;
                                std::cout << "threadid :" << std::this_thread::get_id() << "exit!" << std::endl;
                                return;
                                // 把线程对象从线程列表容器中删除  没有办法匹配threadFunc  对应哪一个 thread对象
                                // threadid => thread 对象 => 将其删除

                            }
                        }
                    }
                    else {
                        // 等待notEmpty条件
                        notEmpty_.wait(lock);
                    }
                }
                // 等待notEmpty条件
                idleThreadSize_--; // 处理问题时线程数量减1
                std::cout << "tid:" << std::this_thread::get_id() << "获取任务成功！" << std::endl;

                // 从任务队列中取一个任务出来
                task = taskQue_.front();
                taskQue_.pop();
                taskSize_--;

            }// 出作用域将当前的锁释放

            // 如果依然有剩余任务，继续通知其他线程执行任务
            if (taskQue_.size() > 0) {
                notEmpty_.notify_all();
            }

            // 取出一个任务，执行一个通知,通知可以进行提交生产任务
            notFull_.notify_all();

            // 当前线程负责执行这个任务
            if (task != nullptr) {
                // 执行任务function<void()>
                task(); 
            }
            idleThreadSize_++; // 问题处理完成时线程数量加1
            auto lastTime = std::chrono::high_resolution_clock().now(); // 更新线程执行完成的时间
        }

        // 执行任务的线程结束，回收资源
        threads_.erase(threadid);
        std::cout << "threadid :" << std::this_thread::get_id() << "exit!" << std::endl;
        exitCond_.notify_all();
    }
    // 检查pool的运行状态
    bool checkRunningState() const {
        return isPoolRunning_;
    }
private:
    // 没有办法从线程函数（threadFunc这个函数中找到线程对象,因此增加一个线程id进行映射）
    //std::vector<std::unique_ptr<Thread>> threads_;  //线程列表
    std::unordered_map<size_t, std::unique_ptr<Thread>> threads_; //线程列表

    size_t initThreadSize_; //初始的线程数量
    size_t threadSizeThreshold_; // 线程数量上限阈值
    std::atomic_int idleThreadSize_;// 记录空闲线程的数量
    std::atomic_int curThreadSize_;// 记录当前线程池里的总数量

    //QUESTION:创建了一个临时的任务对象,有可能会存储一个被析构的对象，有义务将对象生命周期保存到run函数执行完毕,因此必须要使用智能指针

    //Task 任务就是一个函数对象 -> 
    using Task = std::function<void()>;
    std::queue<Task> taskQue_; //任务队列
    std::atomic_uint taskSize_; // 任务的数量
    size_t taskQueMaxThreadHold_; // 任务队列数量的上限阈值


    std::mutex taskQueMtx_; //保证任务队列的线程安全
    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
    std::condition_variable exitCond_; //等待线程资源回收

    PoolMode poolMode_; //当前线程池的工作模式
    std::atomic_bool isPoolRunning_;// 表示当前线程池的启动状态;
};
#endif
