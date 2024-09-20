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
// 枚举类
enum class PoolMode {
    MODE_FIXED, //固定数量的线程
    MODE_CACHED, //线程数量可动态增长
};


// Any 类型，表示可以接受任意数据的类型

class Any {
public:
    Any() = default;
    ~Any() = default;
    // 左值拷贝构造和赋值应该删除，因为unique_ptr不支持
    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;
    Any(Any&&) = default;
    Any& operator=(Any&&) = default;

    // 这个构造函数可以让Any类型接受任意其他类型的数据
    template<typename T> //
    Any(T data) : base_(std::make_unique<Derive<T>>(data)) {} // Any 创建一个Any类型

    // 用这个方法可以把Any对象里面存储的data数据提取出来
    template<typename T>
    T cast_() {
        // 我们怎么从Base里面找到他所指向的Derive对象，从它里面取出data成员变量
        // 基类指针 ->转成派生类指针 RTTI
        Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
        if (pd == nullptr) {
            std::cout << "type is unmatch!" << std::endl;
        }
        return pd->data_;
    }
private:
    // 基类类型
    class Base {
    public:
        virtual ~Base() = default; //使用default 在新标准中能优化
    private:
    };
    // 派生类类型
    template <typename T>
    class Derive : public Base {
    public:
        Derive(T data) : data_(data) {}
        //这里需要是公有的
        T data_; // 保存了任意的其他类型
    };
private:
    // 定义一个基类的指针
    std::unique_ptr<Base> base_;
};

// 实现一个信号量类
class Semaphore {
public:
    Semaphore(int limit = 0)
        :resLimit_(limit)
        ,isExit_(false)
    {}
    ~Semaphore() {
        isExit_ = true;
    }
    // 获取一个信号量资源
    void wait() {
        if (isExit_) {
            return;
        }
        std::unique_lock<std::mutex> lock(mtx_);
        cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
        resLimit_--;
    }
    // 增加一个信号量资源
    void post() {
        if (isExit_) {
            return;
        }
        std::unique_lock<std::mutex> lock(mtx_);
        resLimit_++;

        // linux condition_variable 的析构函数什么都没有做，导致这里的状态已经失效，无故阻塞
        cond_.notify_all(); // 通知条件变量wait的地方可以起来干活了
    }
private:
    std::atomic_bool isExit_;
    size_t resLimit_;
    std::mutex mtx_;
    std::condition_variable cond_;
};



class Task;// Task类的前置声明
// 实现接收到提交到线程池的task任务执行完成后的返回值类型Result
class Result {
public:
    Result(std::shared_ptr<Task> task, bool isValid = true);
    ~Result() = default;

    // Q1： setValue 的方法， 获取任务执行完的返回值
    void setValue(Any any);

    // Q2： get方法，用户调用这个方法，获取task的返回值
    Any get();
private:
    Any any_; //存储任务的返回值
    Semaphore sem_; //线程通信的信号量
    std::shared_ptr<Task> task_; // 指向对应获取返回值的任务对象
    std::atomic_bool isValid_; //返回值是否有效
};



// 任务抽象基类
class Task {
public:
    Task();
    ~Task() = default;
    // 用户可以自定义任意任务类型，从Task继承而来，重写run方法，实现自定义任务处理
    virtual Any run() = 0; //纯虚函数
    void exec();
    void setResult(Result* result);
private:
    Result* result_; //Result 生命周期是要长于Task对象的

};


//线程类型
class Thread {
public:
    // 线程函数对象类型
    using ThreadFunc = std::function<void(size_t)>;
    // 线程构造
    Thread(ThreadFunc func);
    // 线程析构
    ~Thread();
    // 启动线程
    void start();
    // 获取线程id
    size_t getId() const;
private:
    ThreadFunc func_; //存一个函数对象
    static size_t generateId_;
    size_t threadId_; //存储一个线程的id
};


/*
Using Example:
Threadpool pool;
pool.start(4);

class MyTask : public Task {
public:
    void run() {
        // 线程需要执行的代码
    }
};

pool.submitTask(std::make_shared<MyTask>());


*/

// 线程池类型
class ThreadPool {
public:
    // 线程池构造
    ThreadPool();
    // 线程池析构
    ~ThreadPool();

    // 设置线程池工作模式
    void setMode(PoolMode mode);
    // 设置task任务队列数量上限
    void setTaskQueMaxThreadHold(size_t threadhold);
    // 设置线程池Cached模式下线程的阈值
    void setThreadSizeThresHold(size_t threshold);


    // 给线程池提交任务
    Result submitTask(std::shared_ptr<Task> sp);

    // 开启线程池
    void start(size_t initThreadSize = std::thread::hardware_concurrency());// 获取当前系统CPU的核心数量

    // 禁止用户进行拷贝构造和赋值构造
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
private:
    // 定义线程函数
    void threadFunc(size_t threadId);
    // 检查pool的运行状态
    bool checkRunningState() const;
private:
    // 没有办法从线程函数（threadFunc这个函数中找到线程对象,因此增加一个线程id进行映射）
    //std::vector<std::unique_ptr<Thread>> threads_;  //线程列表
    std::unordered_map<size_t, std::unique_ptr<Thread>> threads_; //线程列表


    size_t initThreadSize_; //初始的线程数量
    size_t threadSizeThreshold_; // 线程数量上限阈值
    std::atomic_int idleThreadSize_;// 记录空闲线程的数量
    std::atomic_int curThreadSize_;// 记录当前线程池里的总数量

    //QUESTION:创建了一个临时的任务对象,有可能会存储一个被析构的对象，有义务将对象生命周期保存到run函数执行完毕,因此必须要使用智能指针
    std::queue<std::shared_ptr<Task>> taskQue_; //任务队列
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