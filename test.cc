#include "threadpool.h"
#include <chrono>

using uLong = unsigned long long;

class MyTask : public Task 
{
public:
	MyTask() = default;
	~MyTask() = default;
	MyTask(uLong begin, uLong end)
		: begin_(begin), end_(end)
	{}

	Any run() override
	{
		std::cout << "tid: " << std::this_thread::get_id() << " begin!" << std::endl;
		uLong sum = 0;
		for (uLong i = begin_; i < end_; ++i)
		{
			sum += i;
		}
		std::cout << "tid: " << std::this_thread::get_id() << " end!" << std::endl;
		return sum;
	}
private:
	uLong begin_;
	uLong end_;
};
void test1()
{
	ThreadPool pool;
	// 设置线程池的工作模式
	pool.setMode(PoolMode::MODE_FIXED);
	// 启动线程池
	pool.start();

	// 提交任务
	Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 10000000));
	Result res2 = pool.submitTask(std::make_shared<MyTask>(10000001, 20000000));
	Result res3 = pool.submitTask(std::make_shared<MyTask>(20000001, 30000000));

	// 获取任务结果
	uLong sum1 = res1.get().cast_<uLong>();
	uLong sum2 = res2.get().cast_<uLong>();
	uLong sum3 = res3.get().cast_<uLong>();

	uLong sum = sum1 + sum2 + sum3;

	std::cout << "sum : " << sum << std::endl;

}

void test2()
{
	ThreadPool pool;
	// 设置线程池的工作模式
	pool.setMode(PoolMode::MODE_CACHED);
	// 启动线程池
	pool.start(4);
	pool.setThreadMaxThreshHold(10);

	// 提交任务
	Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 10000000));
	Result res2 = pool.submitTask(std::make_shared<MyTask>(10000001, 20000000));
	Result res3 = pool.submitTask(std::make_shared<MyTask>(20000001, 30000000));
	Result res4 = pool.submitTask(std::make_shared<MyTask>(30000001, 40000000));
	Result res5 = pool.submitTask(std::make_shared<MyTask>(40000001, 50000000));
	Result res6 = pool.submitTask(std::make_shared<MyTask>(50000001, 60000000));

	// 获取任务结果
// 	uLong sum1 = res1.get().cast_<uLong>();
// 	uLong sum2 = res2.get().cast_<uLong>();
// 	uLong sum3 = res3.get().cast_<uLong>();

	// uLong sum = sum1 + sum2 + sum3;

	//std::cout << "sum : " << sum << std::endl;

}

int main()
{
	test2();
}