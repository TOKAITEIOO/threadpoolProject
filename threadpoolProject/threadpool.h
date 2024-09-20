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
// ö����
enum class PoolMode {
    MODE_FIXED, //�̶��������߳�
    MODE_CACHED, //�߳������ɶ�̬����
};


// Any ���ͣ���ʾ���Խ����������ݵ�����

class Any {
public:
    Any() = default;
    ~Any() = default;
    // ��ֵ��������͸�ֵӦ��ɾ������Ϊunique_ptr��֧��
    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;
    Any(Any&&) = default;
    Any& operator=(Any&&) = default;

    // ������캯��������Any���ͽ��������������͵�����
    template<typename T> //
    Any(T data) : base_(std::make_unique<Derive<T>>(data)) {} // Any ����һ��Any����

    // ������������԰�Any��������洢��data������ȡ����
    template<typename T>
    T cast_() {
        // ������ô��Base�����ҵ�����ָ���Derive���󣬴�������ȡ��data��Ա����
        // ����ָ�� ->ת��������ָ�� RTTI
        Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
        if (pd == nullptr) {
            std::cout << "type is unmatch!" << std::endl;
        }
        return pd->data_;
    }
private:
    // ��������
    class Base {
    public:
        virtual ~Base() = default; //ʹ��default ���±�׼�����Ż�
    private:
    };
    // ����������
    template <typename T>
    class Derive : public Base {
    public:
        Derive(T data) : data_(data) {}
        //������Ҫ�ǹ��е�
        T data_; // �������������������
    };
private:
    // ����һ�������ָ��
    std::unique_ptr<Base> base_;
};

// ʵ��һ���ź�����
class Semaphore {
public:
    Semaphore(int limit = 0)
        :resLimit_(limit)
        ,isExit_(false)
    {}
    ~Semaphore() {
        isExit_ = true;
    }
    // ��ȡһ���ź�����Դ
    void wait() {
        if (isExit_) {
            return;
        }
        std::unique_lock<std::mutex> lock(mtx_);
        cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
        resLimit_--;
    }
    // ����һ���ź�����Դ
    void post() {
        if (isExit_) {
            return;
        }
        std::unique_lock<std::mutex> lock(mtx_);
        resLimit_++;

        // linux condition_variable ����������ʲô��û���������������״̬�Ѿ�ʧЧ���޹�����
        cond_.notify_all(); // ֪ͨ��������wait�ĵط����������ɻ���
    }
private:
    std::atomic_bool isExit_;
    size_t resLimit_;
    std::mutex mtx_;
    std::condition_variable cond_;
};



class Task;// Task���ǰ������
// ʵ�ֽ��յ��ύ���̳߳ص�task����ִ����ɺ�ķ���ֵ����Result
class Result {
public:
    Result(std::shared_ptr<Task> task, bool isValid = true);
    ~Result() = default;

    // Q1�� setValue �ķ����� ��ȡ����ִ����ķ���ֵ
    void setValue(Any any);

    // Q2�� get�������û����������������ȡtask�ķ���ֵ
    Any get();
private:
    Any any_; //�洢����ķ���ֵ
    Semaphore sem_; //�߳�ͨ�ŵ��ź���
    std::shared_ptr<Task> task_; // ָ���Ӧ��ȡ����ֵ���������
    std::atomic_bool isValid_; //����ֵ�Ƿ���Ч
};



// ����������
class Task {
public:
    Task();
    ~Task() = default;
    // �û������Զ��������������ͣ���Task�̳ж�������дrun������ʵ���Զ���������
    virtual Any run() = 0; //���麯��
    void exec();
    void setResult(Result* result);
private:
    Result* result_; //Result ����������Ҫ����Task�����

};


//�߳�����
class Thread {
public:
    // �̺߳�����������
    using ThreadFunc = std::function<void(size_t)>;
    // �̹߳���
    Thread(ThreadFunc func);
    // �߳�����
    ~Thread();
    // �����߳�
    void start();
    // ��ȡ�߳�id
    size_t getId() const;
private:
    ThreadFunc func_; //��һ����������
    static size_t generateId_;
    size_t threadId_; //�洢һ���̵߳�id
};


/*
Using Example:
Threadpool pool;
pool.start(4);

class MyTask : public Task {
public:
    void run() {
        // �߳���Ҫִ�еĴ���
    }
};

pool.submitTask(std::make_shared<MyTask>());


*/

// �̳߳�����
class ThreadPool {
public:
    // �̳߳ع���
    ThreadPool();
    // �̳߳�����
    ~ThreadPool();

    // �����̳߳ع���ģʽ
    void setMode(PoolMode mode);
    // ����task���������������
    void setTaskQueMaxThreadHold(size_t threadhold);
    // �����̳߳�Cachedģʽ���̵߳���ֵ
    void setThreadSizeThresHold(size_t threshold);


    // ���̳߳��ύ����
    Result submitTask(std::shared_ptr<Task> sp);

    // �����̳߳�
    void start(size_t initThreadSize = std::thread::hardware_concurrency());// ��ȡ��ǰϵͳCPU�ĺ�������

    // ��ֹ�û����п�������͸�ֵ����
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
private:
    // �����̺߳���
    void threadFunc(size_t threadId);
    // ���pool������״̬
    bool checkRunningState() const;
private:
    // û�а취���̺߳�����threadFunc����������ҵ��̶߳���,�������һ���߳�id����ӳ�䣩
    //std::vector<std::unique_ptr<Thread>> threads_;  //�߳��б�
    std::unordered_map<size_t, std::unique_ptr<Thread>> threads_; //�߳��б�


    size_t initThreadSize_; //��ʼ���߳�����
    size_t threadSizeThreshold_; // �߳�����������ֵ
    std::atomic_int idleThreadSize_;// ��¼�����̵߳�����
    std::atomic_int curThreadSize_;// ��¼��ǰ�̳߳����������

    //QUESTION:������һ����ʱ���������,�п��ܻ�洢һ���������Ķ��������񽫶����������ڱ��浽run����ִ�����,��˱���Ҫʹ������ָ��
    std::queue<std::shared_ptr<Task>> taskQue_; //�������
    std::atomic_uint taskSize_; // ���������
    size_t taskQueMaxThreadHold_; // �������������������ֵ


    std::mutex taskQueMtx_; //��֤������е��̰߳�ȫ
    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
    std::condition_variable exitCond_; //�ȴ��߳���Դ����

    PoolMode poolMode_; //��ǰ�̳߳صĹ���ģʽ
    std::atomic_bool isPoolRunning_;// ��ʾ��ǰ�̳߳ص�����״̬;



};

#endif