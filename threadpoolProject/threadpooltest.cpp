#include "threadpool.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>

/*
    有些场景是希望获取线程执行任务的返回值的

    thread1 :
    thread2 :
    thread3 :
    main thread 负责给每一个线程分配计算的区间，并等待他们算完返回的结果，合并最终的结果

*/

using uLong = unsigned long long;

class MyTask : public Task {

public:
    MyTask(uLong begin, uLong end)
        : begin_(begin)
        , end_(end)
    {}
    // Q1：如何设计run函数的返回值可以表示任意的类型
    // JAVA python  都有一个Object 类型，Object 是其他所有类类型的基类
    // C++17 Any 类型

    Any run() { // run方法最终就在线程池分配的线程中去做执行了
        std::cout << "tid:" << std::this_thread::get_id() << "begin!" << std::endl;

        uLong sum = 0;
        for (uLong i = begin_; i <= end_; i++) {
            sum += i;
        }


        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::cout << "tid:" << std::this_thread::get_id() << "end!" << std::endl;

        return sum;
    }
private:
    uLong begin_;
    uLong end_;
};





// 设计:当线程池ThreadPool出作用域析构时，此时任务队列里面如果还有任务，是等任务全部执行完成再结束还是不执行剩下的任务，
int main() {
    {
        // 问题ThreadPool对象析构了以后，怎么样才能把线程池相关的线程资源全部回收
        ThreadPool pool;
        // 用户自己设置线程池的工作模式
        pool.setMode(PoolMode::MODE_CACHED);
        // 开始启动线程池
        pool.start(2);

        //在linux 上， 这些rusult对象也是局部对象，也会被析构
        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        //ulong sum1 = res1.get().cast_<ulong>(); // 返回了一个Any 类型, 如何转成具体的类型呢
        //std::cout << sum1 << std::endl;
        

    } // 这里Result 对象也会被析构 ！，在visual stdio 下条件变量析构会回收相应的资源，在g++下条件变量析构不会回收相应的资源
    std::cout << "main() is over" << std::endl;

#if 0
    {
        ThreadPool pool;
        // 用户自己设置线程池的工作模式
        pool.setMode(PoolMode::MODE_CACHED);
        // 开始启动线程池
        pool.start(4);
        // 

        // 提交任务
        // Q2：如何设计这里的result机制呢
        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        pool.submitTask(std::make_shared<MyTask>(1, 100000000));

        //用信号量实现，mutex + 条件变量实现
        ulong sum1 = res1.get().cast_<ulong>(); // 返回了一个Any 类型, 如何转成具体的类型呢
        ulong sum2 = res2.get().cast_<ulong>(); // 返回了一个Any 类型, 如何转成具体的类型呢
        ulong sum3 = res3.get().cast_<ulong>(); // 返回了一个Any 类型, 如何转成具体的类型呢
        std::cout << (sum1 + sum2 + sum3) << std::endl;
    }

    getchar();
#endif
}