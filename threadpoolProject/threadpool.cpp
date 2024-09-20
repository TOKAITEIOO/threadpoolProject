#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>
#include <chrono>
const int TASK_MAX_THRESHOLD = 1024; //线程池上限阈值
const int THREAD_MAX_THRESHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 10; //单位为秒

ThreadPool::ThreadPool()
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
ThreadPool::~ThreadPool() {
    isPoolRunning_ = false;
    //等待线程池里面所有的线程返回，有两种状态: 一种是阻塞，正在执行任务中
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    notEmpty_.notify_all();
    exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}


// 设置线程池工作模式
void ThreadPool::setMode(PoolMode mode) {
    if (checkRunningState() == true) {
        return;
    }
    poolMode_ = mode;
}

// 设置task任务队列数量上限
void ThreadPool::setTaskQueMaxThreadHold(size_t threadHold) {
    if (checkRunningState() == true) {
        return;
    }
    taskQueMaxThreadHold_ = threadHold;
}

// 设置线程池Cached模式下线程的阈值
void ThreadPool::setThreadSizeThresHold(size_t threshold) {
    if (checkRunningState() == true) {
        return;
    }
    if (poolMode_ == PoolMode::MODE_CACHED) { //在Cached模式下才进行设置
        threadSizeThreshold_ = threshold;
    }
}


// 开启线程池
void ThreadPool::start(size_t initThreadsize) {
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


// 生产者消费者模型 ：
// 给线程池提交任务 用户调用该接口传入任务对象，生产任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
    // 获取锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    // 线程的通信 等待任务队列有空余 wait wait_for wait_until
    //wait 需要加上条件参数，一般可以用lambda，没有的话需要在外面加上循环
    //wait_for 持续等待的时间
    //wait_until 一个时间节点
    // 用户提交任务，最长不能超过1秒，否则判断提交任务失败，返回
    // 只要条件满足，立刻返回
    if (!notFull_.wait_for(lock, std::chrono::seconds(1),
        [&]()->bool {return taskQue_.size() < taskQueMaxThreadHold_; })) {
        // 如果为True, 表示notFull_等待1s,条件依然没有满足
        std::cerr << "Task queue is full, submit task fail." << std::endl;
        //return   task->getResult(); //返回错误      Task <- Result 线程执行完task对象，task对象就被析构掉了（pop）
        return Result(sp, false); // Task -> Result
    }

    // 如果有空余的话，把任务放入任务队列中
    taskQue_.emplace(sp);
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
    // return task->getResult();
    return Result(sp); // Task -> Result
}
// 定义线程函数 线程池的所有线程从任务队列里消费任务
void ThreadPool::threadFunc(size_t threadid) { // 线程函数执行完返回，相应的线程也结束了
    auto lastTime = std::chrono::high_resolution_clock().now(); // 记录上一次运行的时间
    
    // 所有任务完成必须执行完成，线程池才可以回收所有的线程资源
    for (;;) {
        std::shared_ptr<Task> task;
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
                                threads_.erase(threadid); // 不是std::thread::get_id()
                                // 记录线程数量相关变量的值修改
                                idleThreadSize_--;
                                curThreadSize_--;
                                std::cout << "threadid :" << std::this_thread::get_id() << "exit!" << std::endl;
                                return;
                                // 把线程对象从线程列表容器中删除  没有办法匹配threadFunc  对应哪一个 thread对象

                                // threadid => thread 对象 => 将其删除

                            }
                        }
                    } else {
                        // 等待notEmpty条件
                        notEmpty_.wait(lock);
                    }
                    //// 线程池结束要回收资源
                    //if (!isPoolRunning_) {
                    //    // 回收当前的线程
                    //    threads_.erase(threadid); // 不是std::thread::get_id()
                    //    // 记录线程数量相关变量的值修改
                    //    std::cout << "threadid :" << std::this_thread::get_id() << "exit!" << std::endl;
                    //    exitCond_.notify_all();
                    //    return; // 结束线程函数，就是结束当前线程
                    //}

                }
            // // 等待notEmpty条件
            // notEmpty_.wait(lock, [&]()->bool {return taskQue_.size() > 0;});
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
            task->exec(); // 执行任务；把任务的返回值通过setValue()方法返回给Result
        }
        idleThreadSize_++; // 问题处理完成时线程数量加1
        auto lastTime = std::chrono::high_resolution_clock().now(); // 更新线程执行完成的时间
    }

    // 执行任务的线程结束，回收资源
    threads_.erase(threadid);
    std::cout << "threadid :" << std::this_thread::get_id() << "exit!" << std::endl;
    exitCond_.notify_all();
}

bool ThreadPool::checkRunningState() const {
    return isPoolRunning_;
}



///////////////////TASK方法实现/////////////////////
Task::Task()
    :result_(nullptr)
{}


void Task::exec() {
    // exec方法调用run方法
    if (result_ != nullptr) {
        result_->setValue(run());// 这里发生多态的调用
    }
}


void Task::setResult(Result* res) {
    // 将一个Result对象进行赋值
    result_ = res;
}





/////////////////// 线程方法实现 ////////////////////
size_t Thread::generateId_ = 0;

// 构造函数
Thread::Thread(ThreadFunc func)
    : func_(func)
    , threadId_(generateId_++)
{}

// 析构函数
Thread::~Thread() {}

//启动线程
void Thread::start() {
    // 创建一个线程来执行一个线程函数
    std::thread t(func_, threadId_); // C++11来说，有线程对象t,和线程函数func_
    t.detach();// 设置为分离线程
}

size_t Thread::getId() const {
    // 获取线程id
    return threadId_;
}





///////////////////Result类实现/////////////////////////
Result::Result(std::shared_ptr<Task> task, bool isValid)
    : task_(task)
    , isValid_(isValid)
{
    task_->setResult(this); //在result 里面将Result 赋给 task对象
}


Any Result::get() {
    if (!isValid_) {
        return "";
    }
    sem_.wait(); //用户调用get，如果task没有执行完，这里会阻塞用户的线程
    //任务执行完了，消耗用户的资源，并返回any类型
    return std::move(any_);
}

void Result::setValue(Any any) {
    // 先存储task的返回值，当前this
    this->any_ = std::move(any);
    sem_.post();// 已经获取任务的返回值，增加信号量的资源
}
