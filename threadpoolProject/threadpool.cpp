#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>
#include <chrono>
const int TASK_MAX_THRESHOLD = 1024; //�̳߳�������ֵ
const int THREAD_MAX_THRESHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 10; //��λΪ��

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

// �̳߳�����
ThreadPool::~ThreadPool() {
    isPoolRunning_ = false;
    //�ȴ��̳߳��������е��̷߳��أ�������״̬: һ��������������ִ��������
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    notEmpty_.notify_all();
    exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}


// �����̳߳ع���ģʽ
void ThreadPool::setMode(PoolMode mode) {
    if (checkRunningState() == true) {
        return;
    }
    poolMode_ = mode;
}

// ����task���������������
void ThreadPool::setTaskQueMaxThreadHold(size_t threadHold) {
    if (checkRunningState() == true) {
        return;
    }
    taskQueMaxThreadHold_ = threadHold;
}

// �����̳߳�Cachedģʽ���̵߳���ֵ
void ThreadPool::setThreadSizeThresHold(size_t threshold) {
    if (checkRunningState() == true) {
        return;
    }
    if (poolMode_ == PoolMode::MODE_CACHED) { //��Cachedģʽ�²Ž�������
        threadSizeThreshold_ = threshold;
    }
}


// �����̳߳�
void ThreadPool::start(size_t initThreadsize) {
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


// ������������ģ�� ��
// ���̳߳��ύ���� �û����øýӿڴ������������������
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
    // ��ȡ��
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    // �̵߳�ͨ�� �ȴ���������п��� wait wait_for wait_until
    //wait ��Ҫ��������������һ�������lambda��û�еĻ���Ҫ���������ѭ��
    //wait_for �����ȴ���ʱ��
    //wait_until һ��ʱ��ڵ�
    // �û��ύ��������ܳ���1�룬�����ж��ύ����ʧ�ܣ�����
    // ֻҪ�������㣬���̷���
    if (!notFull_.wait_for(lock, std::chrono::seconds(1),
        [&]()->bool {return taskQue_.size() < taskQueMaxThreadHold_; })) {
        // ���ΪTrue, ��ʾnotFull_�ȴ�1s,������Ȼû������
        std::cerr << "Task queue is full, submit task fail." << std::endl;
        //return   task->getResult(); //���ش���      Task <- Result �߳�ִ����task����task����ͱ��������ˣ�pop��
        return Result(sp, false); // Task -> Result
    }

    // ����п���Ļ���������������������
    taskQue_.emplace(sp);
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
    // return task->getResult();
    return Result(sp); // Task -> Result
}
// �����̺߳��� �̳߳ص������̴߳������������������
void ThreadPool::threadFunc(size_t threadid) { // �̺߳���ִ���귵�أ���Ӧ���߳�Ҳ������
    auto lastTime = std::chrono::high_resolution_clock().now(); // ��¼��һ�����е�ʱ��
    
    // ����������ɱ���ִ����ɣ��̳߳زſ��Ի������е��߳���Դ
    for (;;) {
        std::shared_ptr<Task> task;
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
                                threads_.erase(threadid); // ����std::thread::get_id()
                                // ��¼�߳�������ر�����ֵ�޸�
                                idleThreadSize_--;
                                curThreadSize_--;
                                std::cout << "threadid :" << std::this_thread::get_id() << "exit!" << std::endl;
                                return;
                                // ���̶߳�����߳��б�������ɾ��  û�а취ƥ��threadFunc  ��Ӧ��һ�� thread����

                                // threadid => thread ���� => ����ɾ��

                            }
                        }
                    } else {
                        // �ȴ�notEmpty����
                        notEmpty_.wait(lock);
                    }
                    //// �̳߳ؽ���Ҫ������Դ
                    //if (!isPoolRunning_) {
                    //    // ���յ�ǰ���߳�
                    //    threads_.erase(threadid); // ����std::thread::get_id()
                    //    // ��¼�߳�������ر�����ֵ�޸�
                    //    std::cout << "threadid :" << std::this_thread::get_id() << "exit!" << std::endl;
                    //    exitCond_.notify_all();
                    //    return; // �����̺߳��������ǽ�����ǰ�߳�
                    //}

                }
            // // �ȴ�notEmpty����
            // notEmpty_.wait(lock, [&]()->bool {return taskQue_.size() > 0;});
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
            task->exec(); // ִ�����񣻰�����ķ���ֵͨ��setValue()�������ظ�Result
        }
        idleThreadSize_++; // ���⴦�����ʱ�߳�������1
        auto lastTime = std::chrono::high_resolution_clock().now(); // �����߳�ִ����ɵ�ʱ��
    }

    // ִ��������߳̽�����������Դ
    threads_.erase(threadid);
    std::cout << "threadid :" << std::this_thread::get_id() << "exit!" << std::endl;
    exitCond_.notify_all();
}

bool ThreadPool::checkRunningState() const {
    return isPoolRunning_;
}



///////////////////TASK����ʵ��/////////////////////
Task::Task()
    :result_(nullptr)
{}


void Task::exec() {
    // exec��������run����
    if (result_ != nullptr) {
        result_->setValue(run());// ���﷢����̬�ĵ���
    }
}


void Task::setResult(Result* res) {
    // ��һ��Result������и�ֵ
    result_ = res;
}





/////////////////// �̷߳���ʵ�� ////////////////////
size_t Thread::generateId_ = 0;

// ���캯��
Thread::Thread(ThreadFunc func)
    : func_(func)
    , threadId_(generateId_++)
{}

// ��������
Thread::~Thread() {}

//�����߳�
void Thread::start() {
    // ����һ���߳���ִ��һ���̺߳���
    std::thread t(func_, threadId_); // C++11��˵�����̶߳���t,���̺߳���func_
    t.detach();// ����Ϊ�����߳�
}

size_t Thread::getId() const {
    // ��ȡ�߳�id
    return threadId_;
}





///////////////////Result��ʵ��/////////////////////////
Result::Result(std::shared_ptr<Task> task, bool isValid)
    : task_(task)
    , isValid_(isValid)
{
    task_->setResult(this); //��result ���潫Result ���� task����
}


Any Result::get() {
    if (!isValid_) {
        return "";
    }
    sem_.wait(); //�û�����get�����taskû��ִ���꣬����������û����߳�
    //����ִ�����ˣ������û�����Դ��������any����
    return std::move(any_);
}

void Result::setValue(Any any) {
    // �ȴ洢task�ķ���ֵ����ǰthis
    this->any_ = std::move(any);
    sem_.post();// �Ѿ���ȡ����ķ���ֵ�������ź�������Դ
}
