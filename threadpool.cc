#include "threadpool.h"

// 线程池实现
ThreadPool::ThreadPool()
	: initThreadSize_(0)
	, idleThreadSize_(0)
	, curThreadSize_(0)
	, threadMaxThreshHold_(THREAD_MAX_THRESHHOLD)
	, taskSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
{}

ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;

	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [this]() -> bool {
		return threads_.empty();
		});

}

// 设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

// 设置线程池cached模式下线程阈值
void ThreadPool::setThreadMaxThreshHold(int threshhold)
{
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED)
		threadMaxThreshHold_ = threshhold;
}

// 设置task任务队列上限阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	if (checkRunningState())
		return;
	taskQueMaxThreshHold_ = threshhold;
}

// 给线程池提交任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// 获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	// 线程通信 等待任务队列有空余
	// 用户提交代码，最长不能阻塞一秒，超过则提交任务失败
	if (!notFull_.wait_for(lock, std::chrono::seconds(1), [this]() ->bool {
		return taskQue_.size() < taskQueMaxThreshHold_;
		}))
	{
		// 表示notFull_等待1s，条件依然没有满足
		std::cerr << "task queue is full, submit task fail." << std::endl;
		return Result(sp, false);
	}

		// 如果有空余 把任务放到任务队列中
		taskQue_.emplace(sp);
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


		return Result(sp);
}

// 开启线程池
void ThreadPool::start(int initThreadSize)
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

// 定义线程函数
void ThreadPool::threadFunc(int threadId)
{
	auto lastTime = std::chrono::high_resolution_clock().now();


	while (true)
	{
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
		auto task = taskQue_.front();
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
		task->exec();
		lastTime = std::chrono::high_resolution_clock().now();

		idleThreadSize_++; // 空闲线程数量++
	}

}

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}


/*
	Thread线程实现
*/
int Thread::generateId_ = 0;

Thread::Thread(ThreadFunc func)
	: func_(func)
	, threadId_(generateId_++)
{

}
Thread::~Thread()
{}

// 启动线程
void Thread::start()
{
	std::thread t(func_, threadId_);
	t.detach();	// 该线程与主线程分离
}

int Thread::getId() const
{
	return threadId_;
}

/*
	Task 实现
*/

void Task::exec()
{
	if (result_ != nullptr)
		result_->setVal(run());
}

void Task::setResult(Result* res)
{
	result_ = res;
}

/*
	Result任务的返回值类型实现
*/
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: isValid_(isValid)
	, task_(task)
{
	task_->setResult(this);
}

void Result::setVal(Any any)
{
	// 存储task的返回值
	this->any_ = std::move(any);
	sem_.post(); // 已经获取任务的返回值，增加信号量资源
}

Any Result::get()
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait();  // task任务没有执行完，阻塞用户的线程
	return std::move(any_);
}

