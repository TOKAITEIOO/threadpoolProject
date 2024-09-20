#include "threadpool.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>

/*
    ��Щ������ϣ����ȡ�߳�ִ������ķ���ֵ��

    thread1 :
    thread2 :
    thread3 :
    main thread �����ÿһ���̷߳����������䣬���ȴ��������귵�صĽ�����ϲ����յĽ��

*/

using uLong = unsigned long long;

class MyTask : public Task {

public:
    MyTask(uLong begin, uLong end)
        : begin_(begin)
        , end_(end)
    {}
    // Q1��������run�����ķ���ֵ���Ա�ʾ���������
    // JAVA python  ����һ��Object ���ͣ�Object ���������������͵Ļ���
    // C++17 Any ����

    Any run() { // run�������վ����̳߳ط�����߳���ȥ��ִ����
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





// ���:���̳߳�ThreadPool������������ʱ����ʱ�����������������������ǵ�����ȫ��ִ������ٽ������ǲ�ִ��ʣ�µ�����
int main() {
    {
        // ����ThreadPool�����������Ժ���ô�����ܰ��̳߳���ص��߳���Դȫ������
        ThreadPool pool;
        // �û��Լ������̳߳صĹ���ģʽ
        pool.setMode(PoolMode::MODE_CACHED);
        // ��ʼ�����̳߳�
        pool.start(2);

        //��linux �ϣ� ��Щrusult����Ҳ�Ǿֲ�����Ҳ�ᱻ����
        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        //ulong sum1 = res1.get().cast_<ulong>(); // ������һ��Any ����, ���ת�ɾ����������
        //std::cout << sum1 << std::endl;
        

    } // ����Result ����Ҳ�ᱻ���� ������visual stdio ���������������������Ӧ����Դ����g++�����������������������Ӧ����Դ
    std::cout << "main() is over" << std::endl;

#if 0
    {
        ThreadPool pool;
        // �û��Լ������̳߳صĹ���ģʽ
        pool.setMode(PoolMode::MODE_CACHED);
        // ��ʼ�����̳߳�
        pool.start(4);
        // 

        // �ύ����
        // Q2�������������result������
        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        pool.submitTask(std::make_shared<MyTask>(1, 100000000));

        //���ź���ʵ�֣�mutex + ��������ʵ��
        ulong sum1 = res1.get().cast_<ulong>(); // ������һ��Any ����, ���ת�ɾ����������
        ulong sum2 = res2.get().cast_<ulong>(); // ������һ��Any ����, ���ת�ɾ����������
        ulong sum3 = res3.get().cast_<ulong>(); // ������һ��Any ����, ���ת�ɾ����������
        std::cout << (sum1 + sum2 + sum3) << std::endl;
    }

    getchar();
#endif
}