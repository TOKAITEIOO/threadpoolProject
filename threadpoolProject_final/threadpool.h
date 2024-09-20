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


const int TASK_MAX_THRESHOLD = 2; //�̳߳�������ֵ
const int THREAD_MAX_THRESHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 60; //��λΪ��


// ö����
enum class PoolMode {
    MODE_FIXED, //�̶��������߳�
    MODE_CACHED, //�߳������ɶ�̬����
};


//�߳�����
class Thread {
public:
    // �̺߳�����������
    using ThreadFunc = std::function<void(size_t)>;
    // �̹߳���
    Thread(ThreadFunc func)
        : func_(func)
        , threadId_(generateId_++)
    {}
    // �߳�����
    ~Thread() = default;
    // �����߳�
    void start() {
        // ����һ���߳���ִ��һ���̺߳���
        std::thread t(func_, threadId_); // C++11��˵�����̶߳���t,���̺߳���func_
        t.detach();// ����Ϊ�����߳�
    }
    // ��ȡ�߳�id
    size_t getId() const {
        // ��ȡ�߳�id
        return threadId_;
    }
private:
    ThreadFunc func_; //��һ����������
    static size_t generateId_;
    size_t threadId_; //�洢һ���̵߳�id
};

size_t Thread::generateId_ = 0; //��̬������Ҫ�������ʼ��

// �̳߳�����
class ThreadPool {
public:
    // �̳߳ع���
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

    // �̳߳�����
    ~ThreadPool() {
        isPoolRunning_ = false;
        //�ȴ��̳߳��������е��̷߳��أ�������״̬: һ��������������ִ��������
        std::unique_lock<std::mutex> lock(taskQueMtx_);
        notEmpty_.notify_all();
        exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
    }

    // �����̳߳ع���ģʽ
    void setMode(PoolMode mode) {
        if (checkRunningState() == true) {
            return;
        }
        poolMode_ = mode;
    }
    // ����task���������������
    void setTaskQueMaxThreadHold(size_t threadhold) {
        if (checkRunningState() == true) {
            return;
        }
        taskQueMaxThreadHold_ = threadhold;
    }
    // �����̳߳�Cachedģʽ���̵߳���ֵ
    void setThreadSizeThresHold(size_t threshold) {
        if (checkRunningState() == true) {
            return;
        }
        if (poolMode_ == PoolMode::MODE_CACHED) { //��Cachedģʽ�²Ž�������
            threadSizeThreshold_ = threshold;
        }
    }


    // ���̳߳��ύ����
    // ʹ�ÿɱ��ģ���̣���submitTask���Խ������������������������Ĳ���
    // pool.submitTask(sum1, 10, 20)
    //CSDN ���ؿ�����ֵ���ú������۵�ԭ��
    template<typename Func, typename... Args>
    auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> 
    {
        // ������񣬷��������������
        using Rtype = decltype(func(args...)); //�Ƶ�������һ�����ͣ��޷�����һ���������ʲ���ʹ��auto,Ӧ��ʹ��using
        auto task = std::make_shared<std::packaged_task<Rtype()>>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
        //������������Ϊͨ�������������뺯�������а�
        std::future<Rtype> result = task->get_future();


        // ��ȡ��
        std::unique_lock<std::mutex> lock(taskQueMtx_);
        // �û��ύ��������ܳ���1�룬�����ж��ύ����ʧ�ܣ�����
        if (!notFull_.wait_for(lock, std::chrono::seconds(1),
            [&]()->bool {return taskQue_.size() < taskQueMaxThreadHold_; })) {
            // ���ΪTrue, ��ʾnotFull_�ȴ�1s,������Ȼû������
            std::cerr << "Task queue is full, submit task fail." << std::endl;
            auto task = std::make_shared<std::packaged_task<Rtype()>>(
                []()->Rtype {return Rtype(); });
            (*task)();
                return task->get_future();
        }
        
        
        //�����Ƿ������⣿
        taskQue_.emplace([=]() {(*task)();});//��һ��lambda���ʽ���з���ֵ�ĺ�����װ����,ȥִ�����������     
        taskSize_++;
        // ��Ϊ�·���������������п϶������ˣ�notEmpty_ ����֪ͨ
        notEmpty_.notify_all();

        // chchedģʽ��������ȽϽ�����������С�����������Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��̳߳���

        if (poolMode_ == PoolMode::MODE_CACHED
            && taskSize_ > idleThreadSize_
            && curThreadSize_ < threadSizeThreshold_) {

            std::cout << "create new thread..." << std::endl;
            // �������߳�
            // ��Ҫ���ӵ��߳�����
            auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
            size_t threadId = ptr->getId();
            threads_.emplace(threadId, std::move(ptr));
            // �����߳�
            threads_[threadId]->start();
            // �޸��̸߳�����صĲ���
            idleThreadSize_++;
            curThreadSize_++;
        }

        //���������Result ����
        return result; // Task -> Result


    }

    // �����̳߳�
    void start(size_t initThreadsize) {
        // �����̳߳ص�����״̬
        isPoolRunning_ = true;
        // ��¼��ʼ�̵߳ĸ���
        initThreadSize_ = initThreadsize;
        curThreadSize_ = initThreadsize;
        // �����̶߳��󣬰��̺߳�������thread�̶߳���
        for (int i = 0; i < initThreadSize_; i++) {
            //Ҫ��һ������ռλ��
            auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
            // threads_.emplace_back(std::move(ptr));
            size_t threadId = ptr->getId();
            threads_.emplace(threadId, std::move(ptr));

        }
        // ���������߳�
        for (int i = 0; i < initThreadSize_; i++) {
            threads_[i]->start(); // ���������߳�
            idleThreadSize_++; // ��¼��ʼ�����̵߳�����
        }
    }

    // ��ֹ�û����п�������͸�ֵ����
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
private:
    // �����̺߳���
    void threadFunc(size_t threadid) {
        // �̺߳���ִ���귵�أ���Ӧ���߳�Ҳ������
        auto lastTime = std::chrono::high_resolution_clock().now(); // ��¼��һ�����е�ʱ��

        // ����������ɱ���ִ����ɣ��̳߳زſ��Ի������е��߳���Դ
        for (;;) {
            Task task;
            {
                // �Ȼ�ȡ��
                std::unique_lock<std::mutex> lock(taskQueMtx_);
                std::cout << "tid:" << std::this_thread::get_id() << "���Ի�ȡ����..." << std::endl;

                //cached ģʽ�£��п����Ѿ������˺ܶ���̣߳����ǿ���ʱ�䳬��60s,
                //��ΰѶ�����߳̽������գ�(����initThreadSize_�������߳���Ҫ���л���)
                // ��ǰʱ�� - ��һ���߳�ִ�е�ʱ�� > 60s
                    //ÿһ���ӷ���һ�Σ����ص�ʱ��������ֳ�ʱ���ػ����������ִ�з���

                    // ��+˫���ж�
                while (taskQue_.size() == 0) {
                    if (!isPoolRunning_) {
                        threads_.erase(threadid); // ����std::thread::get_id()
                        // ��¼�߳�������ر�����ֵ�޸�
                        std::cout << "threadid :" << std::this_thread::get_id() << "exit!" << std::endl;
                        exitCond_.notify_all();
                        return; // �̺߳����������߳̽���
                    }


                    if (poolMode_ == PoolMode::MODE_CACHED) {
                        if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
                            // ��ʱ������
                            auto now = std::chrono::high_resolution_clock().now();
                            auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                            if (dur.count() >= THREAD_MAX_IDLE_TIME
                                && curThreadSize_ > initThreadSize_) {
                                // ���յ�ǰ���߳�
                                threads_.erase(threadid); 
                                // ����std::thread::get_id()
                                // ��¼�߳�������ر�����ֵ�޸�
                                idleThreadSize_--;
                                curThreadSize_--;
                                std::cout << "threadid :" << std::this_thread::get_id() << "exit!" << std::endl;
                                return;
                                // ���̶߳�����߳��б�������ɾ��  û�а취ƥ��threadFunc  ��Ӧ��һ�� thread����
                                // threadid => thread ���� => ����ɾ��

                            }
                        }
                    }
                    else {
                        // �ȴ�notEmpty����
                        notEmpty_.wait(lock);
                    }
                }
                // �ȴ�notEmpty����
                idleThreadSize_--; // ��������ʱ�߳�������1
                std::cout << "tid:" << std::this_thread::get_id() << "��ȡ����ɹ���" << std::endl;

                // �����������ȡһ���������
                task = taskQue_.front();
                taskQue_.pop();
                taskSize_--;

            }// �������򽫵�ǰ�����ͷ�

            // �����Ȼ��ʣ�����񣬼���֪ͨ�����߳�ִ������
            if (taskQue_.size() > 0) {
                notEmpty_.notify_all();
            }

            // ȡ��һ������ִ��һ��֪ͨ,֪ͨ���Խ����ύ��������
            notFull_.notify_all();

            // ��ǰ�̸߳���ִ���������
            if (task != nullptr) {
                // ִ������function<void()>
                task(); 
            }
            idleThreadSize_++; // ���⴦�����ʱ�߳�������1
            auto lastTime = std::chrono::high_resolution_clock().now(); // �����߳�ִ����ɵ�ʱ��
        }

        // ִ��������߳̽�����������Դ
        threads_.erase(threadid);
        std::cout << "threadid :" << std::this_thread::get_id() << "exit!" << std::endl;
        exitCond_.notify_all();
    }
    // ���pool������״̬
    bool checkRunningState() const {
        return isPoolRunning_;
    }
private:
    // û�а취���̺߳�����threadFunc����������ҵ��̶߳���,�������һ���߳�id����ӳ�䣩
    //std::vector<std::unique_ptr<Thread>> threads_;  //�߳��б�
    std::unordered_map<size_t, std::unique_ptr<Thread>> threads_; //�߳��б�

    size_t initThreadSize_; //��ʼ���߳�����
    size_t threadSizeThreshold_; // �߳�����������ֵ
    std::atomic_int idleThreadSize_;// ��¼�����̵߳�����
    std::atomic_int curThreadSize_;// ��¼��ǰ�̳߳����������

    //QUESTION:������һ����ʱ���������,�п��ܻ�洢һ���������Ķ��������񽫶����������ڱ��浽run����ִ�����,��˱���Ҫʹ������ָ��

    //Task �������һ���������� -> 
    using Task = std::function<void()>;
    std::queue<Task> taskQue_; //�������
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
