#include "threadpool.h"



ThreadPool::ThreadPool()
	: initThreadSize_(0)
	, taskSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
{}

ThreadPool::~ThreadPool()
{}

// 设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{
	poolMode_ = mode;
}


// 设置task任务队列上限阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
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
	return Result(sp);
}

// 开启线程池
void ThreadPool::start(int initThreadSize)
{
	initThreadSize_ = initThreadSize;

	for (int i = 0; i < initThreadSize; ++i)
	{
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
		threads_.emplace_back(std::move(ptr));
	}

	for (auto& th : threads_)
	{
		th->start();
	}
}

// 定义线程函数
void ThreadPool::threadFunc()
{
	for (;;)
	{
		// 先获取锁
		std::unique_lock<std::mutex> lock(taskQueMtx_);

		std::cout << "tid: " << std::this_thread::get_id() << " 尝试获取任务..." << std::endl;


		// 等待notEmpty_条件
		notEmpty_.wait(lock, [this]() -> bool {
			return taskQue_.size() > 0;
		});

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
	}
}


/*	
	线程方法实现
*/
Thread::Thread(ThreadFunc func)
	: func_(func)
{

}
Thread::~Thread()
{}

// 启动线程
void Thread::start()
{
	std::thread t(func_);
	t.detach();	// 设置分离线程
}

/*
	Task 方法实现
*/

void Task::exec()
{
	run();
}

/*
	Result返回值类型方法实现
*/
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: isValid_(isValid)
	, task_(task)
{}

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
	sem_.wait(); // task任务没有执行完，阻塞用户的线程
	return std::move(any_);
}

