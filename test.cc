#include "threadpool.h"
#include <chrono>

class MyTask : public Task 
{
public:
	Any run() override
	{
		std::cout << "tid: " << std::this_thread::get_id() << " begin!" << std::endl;
		int sum = 0;
		for (int i = begin_; i < end_; ++i)
		{
			sum += i;
		}
		std::cout << "tid: " << std::this_thread::get_id() << " end!" << std::endl;
		return sum;
	}
private:
	int begin_;
	int end_;
};

int main()
{
	ThreadPool pool;
	pool.start();

	pool.submitTask(std::make_shared<MyTask>());

	getchar();
}