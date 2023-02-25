#pragma once

/*
	版本一
*/

#include <iostream>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <unordered_map>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_SIZE = \
	std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 4;
const int THREAD_MAX_THRESHHOLD = THREAD_SIZE * 10;
const int THREAD_MAX_IDLE_TIME = 60; // 单位:秒

// Any类型：可以接受任意类型的数据
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

	// 获取Any的data数据
	template<typename T>
	T cast_()
	{
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
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

	// 派生类类型
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

// 信号量
class Semaphore
{
public:
	Semaphore(int limit = 0)
		: resLimit_(limit)
		, isExit_(false)
	{}
	~Semaphore()
	{
		isExit_ = true;
	}

	void wait()
	{
		// 如果当前的线程量资源已经被回收，就直接返回
		if (isExit_)
			return;
		std::unique_lock<std::mutex> lock(mtx_);
		cond_.wait(lock, [&]() -> bool {
			return resLimit_ > 0;
		});
		resLimit_--;
	}

	void post()
	{
		if (isExit_)
			return;
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}

private:
	int resLimit_;

	std::mutex mtx_;
	std::condition_variable cond_;
	std::atomic_bool isExit_; // 因为在linux中，条件变量和互斥量的资源不会在析构函数中就去释放其资源，设置是否退出
};

class Task;
// 实现接受提交到线程池的task任务执行完成后的返回值类型
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	void setVal(Any any);

	Any get();
private:
	Any any_;		// 存储任务的返回值
	Semaphore sem_; // 线程通信信号量
	std::shared_ptr<Task> task_; // 指向对应获取返回值的任务对象
	std::atomic_bool isValid_; // 返回值是否有效
};

// 任务抽象基类
class Task
{
public:
	Task() = default;
	~Task() = default;
	void exec();
	void setResult(Result* res);
	// 用户可以自定义任意类型的任务，从Task继承，重写run方法
	virtual Any run() = 0; 
private:
	Result* result_ = nullptr;
};

// 线程池支持的模式
enum class PoolMode
{
	MODE_FIXED, // 固定数量的线程池
	MODE_CACHED // 线程数量可以动态增长
};

// 线程类型
class Thread
{
public:
	// 线程函数对象类型
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func);
	~Thread();

	// 启动线程
	void start();

	// 获取线程id
	int getId() const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;			// 保存线程id
};

// 线程池类型
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();

	// 设置线程池的工作模式
	void setMode(PoolMode mode);

	// 设置线程池cached模式下线程阈值
	void setThreadMaxThreshHold(int threshhold);

	// 设置task任务队列上限阈值
	void setTaskQueMaxThreshHold(int threshhold);

	// 给线程池提交任务
	Result submitTask(std::shared_ptr<Task> sp);

	// 开启线程池
	void start(int initThreadSize = THREAD_SIZE);

	// 禁止拷贝和赋值
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// 定义线程函数
	void threadFunc(int threadId);

	// 检查pool的运行状态
	bool checkRunningState() const;
private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;		// 线程列表
	size_t initThreadSize_;											// 初始的线程数量
	std::atomic_int curThreadSize_;									// 记录当前线程池的线程数量
	std::atomic_int idleThreadSize_;								// 空闲线程的数量
	size_t threadMaxThreshHold_;									// 线程数量上限的阈值
	
	std::queue<std::shared_ptr<Task>> taskQue_;						// 任务队列
	std::atomic_uint taskSize_;										// 任务的数量
	size_t taskQueMaxThreshHold_;									// 任务数量上限的阈值

	std::mutex taskQueMtx_;											// 保证任务队列的线程安全
	std::condition_variable notFull_;								// 保证任务队列不满
	std::condition_variable notEmpty_;								// 保证任务队列不空
	std::condition_variable exitCond_;								// 保证线程资源全部回收

	PoolMode poolMode_;												// 当前线程池的工作模式
	std::atomic_bool isPoolRunning_;								// 表示线程池的启动状态
};