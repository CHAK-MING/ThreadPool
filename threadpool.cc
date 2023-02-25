#include "threadpool.h"



ThreadPool::ThreadPool()
	: initThreadSize_(0)
	, taskSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
{}

ThreadPool::~ThreadPool()
{}

// �����̳߳صĹ���ģʽ
void ThreadPool::setMode(PoolMode mode)
{
	poolMode_ = mode;
}


// ����task�������������ֵ
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	taskQueMaxThreshHold_ = threshhold;
}

// ���̳߳��ύ����
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// ��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	// �߳�ͨ�� �ȴ���������п���
	// �û��ύ���룬���������һ�룬�������ύ����ʧ��
	if (!notFull_.wait_for(lock, std::chrono::seconds(1), [this]() ->bool {
		return taskQue_.size() < taskQueMaxThreshHold_;
		}))
	{
		// ��ʾnotFull_�ȴ�1s��������Ȼû������
		std::cerr << "task queue is full, submit task fail." << std::endl;
		return Result(sp, false);
	}
	
	// ����п��� ������ŵ����������
	taskQue_.emplace(sp);
	taskSize_++;

	// ��Ϊ�·�������������п϶���Ϊ�գ�����notEmpty_֪ͨ
	notEmpty_.notify_all();
	return Result(sp);
}

// �����̳߳�
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

// �����̺߳���
void ThreadPool::threadFunc()
{
	for (;;)
	{
		// �Ȼ�ȡ��
		std::unique_lock<std::mutex> lock(taskQueMtx_);

		std::cout << "tid: " << std::this_thread::get_id() << " ���Ի�ȡ����..." << std::endl;


		// �ȴ�notEmpty_����
		notEmpty_.wait(lock, [this]() -> bool {
			return taskQue_.size() > 0;
		});

		std::cout << "tid: " << std::this_thread::get_id() << " ��ȡ����ɹ�!" << std::endl;

		// ���������ȡһ������
		auto task = taskQue_.front();
		taskQue_.pop();
		taskSize_--;

		// ȡ�����񣬼���֪ͨ�����̼߳����ύ����
		if (taskQue_.size() > 0)
		{
			notFull_.notify_all();
		}

		// ִ������ǰ��Ӧ�ð����ͷ�
		lock.unlock();

		// ��ǰ�̸߳���ִ���������
		task->exec();
	}
}


/*	
	�̷߳���ʵ��
*/
Thread::Thread(ThreadFunc func)
	: func_(func)
{

}
Thread::~Thread()
{}

// �����߳�
void Thread::start()
{
	std::thread t(func_);
	t.detach();	// ���÷����߳�
}

/*
	Task ����ʵ��
*/

void Task::exec()
{
	run();
}

/*
	Result����ֵ���ͷ���ʵ��
*/
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: isValid_(isValid)
	, task_(task)
{}

void Result::setVal(Any any)
{
	// �洢task�ķ���ֵ
	this->any_ = std::move(any);
	sem_.post(); // �Ѿ���ȡ����ķ���ֵ�������ź�����Դ
}

Any Result::get()
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait(); // task����û��ִ���꣬�����û����߳�
	return std::move(any_);
}

