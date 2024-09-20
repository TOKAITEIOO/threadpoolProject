// threadpoolProject_final.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include <functional>
#include <thread>
#include <future>
#include "threadpool.h"



using namespace std;

int sum1(int a, int b) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return a + b;
}

int sum2(int a, int b, int c) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return a + b + c;
}
/*
    Q1: 如何能让线程池提交任务更加方便
    pool.submitTask(sum1, 10 ,20);
    pool.submitTask(sum2, 1, 2, 3);
    submitTask: 可变参模板编程
    Q2：为了接收任务的返回值，我们自己造了一个Result以及相关的类型，代码很多
    c++11 线程库 thread， packaged_task(function函数对象)，针对thread无法获取返回值的问题，获取返回值
                            async， 功能更加强大，获得返回值
        使用future 来代替Result，节省线程池代码
*/
int main()
{
    ThreadPool pool;
    //pool.setMode(PoolMode::MODE_CACHED);
    pool.start(2);
    
    future<int> result1 = pool.submitTask(sum1, 10, 20);
    future<int> result2 = pool.submitTask(sum2, 10, 20, 30); 
    future<int> result3 = pool.submitTask([](int b, int e)->int {
        int sum = 0;
        for (int i = b; i <= e; i++)
            sum += i;
        return sum;
        },1, 100);
    future<int> result4 = pool.submitTask([](int b, int e)->int {
        int sum = 0;
        for (int i = b; i <= e; i++)
            sum += i;
        return sum;
        }, 1, 100);
    future<int> result5 = pool.submitTask([](int b, int e)->int {
        int sum = 0;
        for (int i = b; i <= e; i++)
            sum += i;
        return sum;
        }, 1, 100);
    future<int> result6 = pool.submitTask([](int b, int e)->int {
        int sum = 0;
        for (int i = b; i <= e; i++)
            sum += i;
        return sum;
        }, 1, 100);
    future<int> result7 = pool.submitTask([](int b, int e)->int {
        int sum = 0;
        for (int i = b; i <= e; i++)
            sum += i;
        return sum;
        }, 1, 100);
    future<int> result8 = pool.submitTask([](int b, int e)->int {
        int sum = 0;
        for (int i = b; i <= e; i++)
            sum += i;
        return sum;
        }, 1, 100);
    cout << result1.get() << endl;
    cout << result2.get() << endl;
    cout << result3.get() << endl;
    cout << result4.get() << endl;
    cout << result5.get() << endl;
    cout << result7.get() << endl;
    cout << result8.get() << endl;
    return 0;
}
