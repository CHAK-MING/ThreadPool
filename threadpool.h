#pragma once

#include <iostream>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>

const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_SIZE = \
	std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 4;

// Any���ͣ����Խ����������͵�����
class Any
{
public:
	Any() = default;
	~Any() = default;

	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;

	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	template<typename T>
	Any(T data) 
		: base_(std::make_unique<Derive<T>>(data))
	{}

	// ��ȡAny��data����
	template<typename T>
	T cast_()
	{
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_->get());
		if (pd == nullptr)
		{
			throw "type is unmatch!";
		}
		return pd->data_;
	}
private:
	class Base 
	{
	public:
		virtual ~Base() = default;
	};

	// ����������
	template<typename T>
	class Derive : public Base
	{
	public:
		Derive(T data) : data_(data) {}
		T data_;
	};
private:
	std::unique_ptr<Base> base_;
};

// �ź���
class Semaphore
{
public:
	Semaphore(int limit = 0)
		: resLimit_(limit)
	{}
	~Semaphore() = default;

	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		cond_.wait(lock, [&]() -> bool {
			return resLimit_ > 0;
		});
		resLimit_--;
	}

	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}

private:
	int resLimit_;

	std::mutex mtx_;
	std::condition_variable cond_;
};

class Task;
// ʵ�ֽ����ύ���̳߳ص�task����ִ����ɺ�ķ���ֵ����
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	void setVal(Any any);

	Any get();
private:
	Any any_;		// �洢����ķ���ֵ
	Semaphore sem_; // �߳�ͨ���ź���
	std::shared_ptr<Task> task_; // ָ���Ӧ��ȡ����ֵ���������
	std::atomic_bool isValid_; // ����ֵ�Ƿ���Ч
};

// ����������
class Task
{
public:
	void exec();
	// �û������Զ����������͵����񣬴�Task�̳У���дrun����
	virtual Any run() = 0; 
};

// �̳߳�֧�ֵ�ģʽ
enum class PoolMode
{
	MODE_FIXED, // �̶��������̳߳�
	MODE_CACHED // �߳��������Զ�̬����
};

// �߳�����
class Thread
{
public:
	// �̺߳�����������
	using ThreadFunc = std::function<void()>;

	Thread(ThreadFunc func);
	~Thread();

	void start();
private:
	ThreadFunc func_;
};

// �̳߳�����
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();

	// �����̳߳صĹ���ģʽ
	void setMode(PoolMode mode);

	// ����task�������������ֵ
	void setTaskQueMaxThreshHold(int threshhold);

	// ���̳߳��ύ����
	Result submitTask(std::shared_ptr<Task> sp);

	// �����̳߳�
	void start(int initThreadSize = THREAD_MAX_SIZE);

	// ��ֹ�����͸�ֵ
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// �����̺߳���
	void threadFunc();

private:
	std::vector<std::unique_ptr<Thread>> threads_;				// �߳��б�
	size_t initThreadSize_;						// ��ʼ���߳�����
	
	std::queue<std::shared_ptr<Task>> taskQue_;	// �������
	std::atomic_uint taskSize_;					// ���������
	size_t taskQueMaxThreshHold_;				// �����������޵���ֵ

	std::mutex taskQueMtx_;						// ��֤������е��̰߳�ȫ
	std::condition_variable notFull_;			// ��֤������в���
	std::condition_variable notEmpty_;			// ��֤������в���

	PoolMode poolMode_;							// ��ǰ�̳߳صĹ���ģʽ
};