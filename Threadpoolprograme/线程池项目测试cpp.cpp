#include <iostream>
#include "threadpool.h"
#include <chrono>
#include <thread>

using ULong = unsigned long long;

class myTask : public Task
{
public:
	myTask(int begin, int end)
		:begin_(begin)
		, end_(end)
	{}
	Any run()
	{
		std::cout << "Begin threadFunc tid" << std::this_thread::get_id() << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));
		ULong sum = 0;
		for (ULong i = begin_; i <= end_; i++)
		{
			sum += i;
		}
		std::cout << "end threadFunc tid" << std::this_thread::get_id() << std::endl;
		return sum;
	}
private:
	int begin_;
	int end_;
};



int main()
{
	{
		ThreadPool pool;
		pool.setMode(PoolMode::MODE_CACHED);
		pool.start(1);
		Result res1 = pool.subMitTask(std::make_shared<myTask>(1, 100000000));
		Result res2 = pool.subMitTask(std::make_shared<myTask>(100000001, 200000000));
		ULong sum1 = res1.get().cast_<ULong>();
		std::cout << sum1 << std::endl;		
	}
	std::cout << "mainover!" << std::endl;
	getchar();

#if 0
	
	ThreadPool pool;
	//设置线程的工作模式
	pool.setMode(PoolMode::MODE_CACHED);
	pool.start(4);
	
	Result res1 = pool.subMitTask(std::make_shared<myTask>(1,100000000));
	Result res2 = pool.subMitTask(std::make_shared<myTask>(100000001, 200000000));
	Result res3 = pool.subMitTask(std::make_shared<myTask>(200000001, 300000000));
	pool.subMitTask(std::make_shared<myTask>(200000001, 300000000));

	pool.subMitTask(std::make_shared<myTask>(200000001, 300000000));
	pool.subMitTask(std::make_shared<myTask>(200000001, 300000000));

	ULong sum1 = res1.get().cast_<ULong>(); //get返回了一个Any类型，怎么转成具体类型
	ULong sum2 = res2.get().cast_<ULong>();
	ULong sum3 = res3.get().cast_<ULong>();
	std::cout << sum1 + sum2 + sum3 << std::endl;

	getchar();
	//system("pause");
	return 0;
#endif
}