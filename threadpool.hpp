#pragma once
#pragma optimize( "g", on ) 
/*
	版本二
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
#include <future>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_SIZE = \
std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 4;
const int THREAD_MAX_THRESHHOLD = THREAD_SIZE * 10;
const int THREAD_MAX_IDLE_TIME = 60; // 单位:秒

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

	Thread(ThreadFunc func)
		: func_(func)
		, threadId_(generateId_++)
	{

	}
	~Thread() = default;

	// 启动线程
	void start()
	{
		std::thread t(func_, threadId_);
		t.detach();	// 该线程与主线程分离
	}

	// 获取线程id
	int getId() const
	{
		return threadId_;
	}
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;			// 保存线程id
};

int Thread::generateId_ = 0;

// 线程池类型
class ThreadPool
{
public:
	ThreadPool()
		: initThreadSize_(0)
		, idleThreadSize_(0)
		, curThreadSize_(0)
		, threadMaxThreshHold_(THREAD_MAX_THRESHHOLD)
		, taskSize_(0)
		, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
		, poolMode_(PoolMode::MODE_FIXED)
		, isPoolRunning_(false)
	{}

	~ThreadPool()
	{
		isPoolRunning_ = false;

		std::unique_lock<std::mutex> lock(taskQueMtx_);
		notEmpty_.notify_all();
		exitCond_.wait(lock, [this]() -> bool {
			return threads_.empty();
			});
	}

	// 设置线程池的工作模式
	void setMode(PoolMode mode)
	{
		if (checkRunningState())
			return;
		poolMode_ = mode;
	}

	// 设置线程池cached模式下线程阈值
	void setThreadMaxThreshHold(int threshhold)
	{
		if (checkRunningState())
			return;
		if (poolMode_ == PoolMode::MODE_CACHED)
			threadMaxThreshHold_ = threshhold;
	}

	// 设置task任务队列上限阈值
	void setTaskQueMaxThreshHold(int threshhold)
	{
		if (checkRunningState())
			return;
		taskQueMaxThreshHold_ = threshhold;
	}

	// 给线程池提交任务
	// 使用可变参模板编程，让submitTask可以接受任意任务函数和任意数量的参数
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		// 打包任务，放到任务队列
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> result = task->get_future();

		// 获取锁
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		// 用户提交任务，最长不能阻塞一秒钟，否则提交任务失败，返回
		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[this]() -> bool {
				return taskQue_.size() < taskQueMaxThreshHold_;
			}))
		{
			std::cerr << "task queue is full, submit task fail" << std::endl;
			// 任务失败了返回一个空的
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]() -> RType {
					return RType();
				});
			(*task)();
			return task->get_future();
		}

			// 如果有空余，把任务放到任务队列
			taskQue_.emplace([task]() {
				(*task)();
				});
			taskSize_++;

			// 因为新放了任务，任务队列肯定不为空，再用notEmpty_通知
			notEmpty_.notify_all();

			// cached模式下，如果任务数量超过了空闲线程的数量并且当前线程的数量是小于我们设置的线程最大数量，就会创建新的线程去执行任务
			if (poolMode_ == PoolMode::MODE_CACHED
				&& taskSize_ > idleThreadSize_
				&& curThreadSize_ < threadMaxThreshHold_)
			{
				std::cout << ">>> create new thread threadId: " << std::this_thread::get_id() << std::endl;
				auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
				int threadId = ptr->getId();
				threads_.emplace(threadId, std::move(ptr));
				threads_[threadId]->start();
				curThreadSize_++;
			}
			return result;
	}

	// 开启线程池
	void start(int initThreadSize = THREAD_SIZE)
	{
		isPoolRunning_ = true;
		initThreadSize_ = initThreadSize;
		curThreadSize_ = initThreadSize;

		for (int i = 0; i < initThreadSize; ++i)
		{
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			curThreadSize_++;
			idleThreadSize_++;
		}

		for (auto& [threadId, thread] : threads_)
		{
			thread->start();
		}
	}

	// 禁止拷贝和赋值
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// 定义线程函数
	void threadFunc(int threadId)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();


		while (true)
		{
			Task task;
			// 先获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id() << " 尝试获取任务..." << std::endl;


			while (taskQue_.empty())
			{
				// 回收线程资源
				if (!isPoolRunning_)
				{
					threads_.erase(threadId);
					exitCond_.notify_all();
					return;
				}

				// 如果是cached模式，会根据现在空闲线程等待的时间，如果超过设定的60s，会自动回收线程
				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					// 表明等待超时
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_)
						{
							// 回收线程资源
							threads_.erase(threadId);
							// 修改其他属性
							curThreadSize_--;
							idleThreadSize_--;
							std::cout << "threadid: " << std::this_thread::get_id() << " exit" << std::endl;
							return;
						}
					}
				}
				else
				{
					// 等待
					notEmpty_.wait(lock);
				}

			}

			idleThreadSize_--;

			std::cout << "tid: " << std::this_thread::get_id() << " 获取任务成功!" << std::endl;

			// 从任务队列取一个任务
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// 取出任务，继续通知其他线程继续提交任务
			if (taskQue_.size() > 0)
			{
				notFull_.notify_all();
			}

			// 执行任务前，应该把锁释放
			lock.unlock();

			// 当前线程负责执行这个任务
			task();

			lastTime = std::chrono::high_resolution_clock().now();

			idleThreadSize_++; // 空闲线程数量++
		}
	}

	// 检查pool的运行状态
	bool checkRunningState() const
	{
		return isPoolRunning_;
	}
private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;		// 线程列表
	size_t initThreadSize_;											// 初始的线程数量
	std::atomic_int curThreadSize_;									// 记录当前线程池的线程数量
	std::atomic_int idleThreadSize_;								// 空闲线程的数量
	size_t threadMaxThreshHold_;									// 线程数量上限的阈值

	using Task = std::function<void()>;								// 任务类型 
	std::queue<Task> taskQue_;										// 任务队列
	std::atomic_uint taskSize_;										// 任务的数量
	size_t taskQueMaxThreshHold_;									// 任务数量上限的阈值

	std::mutex taskQueMtx_;											// 保证任务队列的线程安全
	std::condition_variable notFull_;								// 保证任务队列不满
	std::condition_variable notEmpty_;								// 保证任务队列不空
	std::condition_variable exitCond_;								// 保证线程资源全部回收

	PoolMode poolMode_;												// 当前线程池的工作模式
	std::atomic_bool isPoolRunning_;								// 表示线程池的启动状态
};