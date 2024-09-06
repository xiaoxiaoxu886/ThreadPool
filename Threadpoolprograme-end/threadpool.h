#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <thread>
#include <future>
#include <iostream>


const int TASK_MAX_THRESHHOLD = 2;//INT32_MAX;
const int THERAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60;

//线程池支持的模式
enum class PoolMode
{
	MODE_FIXED,//固定数量的线程
	MODE_CACHED,//线程数量可动态增长
};

//线程类型
class Thread {
public:
	//线程函数对象类型
	using ThreadFunc = std::function<void(int)>;

	//线程构造
	Thread(ThreadFunc func)
		:func_(func)
		, threadId_(generateId_++)
	{}
	//线程析构
	~Thread() = default;
	//启动线程
	void start()
	{
		//创建一个线程来执行一个线程函数
		std::thread t(func_, threadId_);//C++来说，线程对象t，和线程函数func_
		t.detach();//设置分离线程
	}

	//获取线程id
	int getId() const
	{
		return threadId_;
	}
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;//保存线程id
};
int Thread::generateId_ = 0;

//线程池类型
class ThreadPool
{
public:
	//线程池构造
	ThreadPool()
		:initThreadSize_(0)
		, taskSize_(0)
		, idleThreadSize_(0)
		, curThreadSize_(0)
		, threadSizeThreshHold_(THERAD_MAX_THRESHHOLD)
		, taskQueMaxThreadHold_(TASK_MAX_THRESHHOLD)
		, poolMode_(PoolMode::MODE_FIXED)
		, isPoolRuning_(false)
	{}
	//线程池析构
	~ThreadPool()
	{
		isPoolRuning_ = false;


		//等待线程池里面的所有的线程返回
		std::unique_lock<std::mutex>lock(taskQueMtx_);

		notEmpty_.notify_all();

		exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
	}

	//设置线程池的工作模式
	void setMode(PoolMode mode)
	{
		if (checkRuningState())
			return;
		poolMode_ = mode;
	}

	//设置task任务队列上限的阈值
	void setTaskQueMaxThreshHold(int threshHold)
	{
		if (checkRuningState())
			return;
		taskQueMaxThreadHold_ = threshHold;
	}

	//设置线程池cache的模式下线程阈值
	void setThreadSizeTHeashHold(int theashHold)
	{
		if (checkRuningState())
			return;
		if (poolMode_ == PoolMode::MODE_CACHED)
		{
			threadSizeThreshHold_ = theashHold;
		}
	}

	//给线程池提交任务
	//使用可变惨模版编程，让submittask可以接受任意任务函数和任意数量的参数
	//Result subMitTask(std::shared_ptr<Task> sp);
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args)->std::future<decltype(func(args...))>
	{
		//打包任务，放入任务队列
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
			);
		std::future<RType> result = task->get_future();

		//获取锁
		std::unique_lock<std::mutex>lock(taskQueMtx_);
		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreadHold_; }))
		{
			//表示notFull_等待1s,条件依然没有满足
			std::cerr << "task queue is full,submit task fail" << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType {return RType(); });
			(*task)();
			return task->get_future();
		}
		//如果有空余，把任务放入任务队列中
		//taskQue_.emplace(sp);
		//using Task = std::function<void()>;
		taskQue_.emplace([task]() {(*task)(); });//解引用task
		taskSize_++;
		//因为新放了任务，任务队列肯定不空，notEmpty_上通知
		notEmpty_.notify_all();
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));

		//cached模式任务处理比较紧急，场景：小而块的任务  需要根据任务数量和空闲线程的数量，判断是都需要创建显得
		if (poolMode_ == PoolMode::MODE_CACHED
			&& taskSize_ > idleThreadSize_
			&& curThreadSize_ < threadSizeThreshHold_)
		{

			std::cout << ">>>create new threads..." << std::endl;
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			//启动线程
			threads_[threadId]->start();
			//修改线程个数相关的变量
			curThreadSize_++;
			idleThreadSize_++;
		}



		//返回任务得到result对象
		return result;
	}

	//开启线程池
	void start(int initThreadSize = std::thread::hardware_concurrency())
	{
		//设置线程池的运行状态
		isPoolRuning_ = true;
		//记录初始线程个数
		initThreadSize_ = initThreadSize;
		curThreadSize_ = initThreadSize;

		//创建线程对象
		for (int i = 0; i < (int)initThreadSize_; i++)
		{
			//当创建thread线程对象的时候，把线程函数给到thread线程对象
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
		}
		//启动所有线程
		for (int i = 0; i < (int)initThreadSize_; i++)
		{
			threads_[i]->start();//需要去执行一个线程函数
			idleThreadSize_++;//记录初始空闲线程的数量
		}
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	//定义线程函数
	void threadFunc(int threadid)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();
		//所有任务必须执行完成，线程池才可以回收所有线程资源

		for (;;)
		{
			Task task;
			{
				//先获取锁
				std::unique_lock<std::mutex>lock(taskQueMtx_);
				//等待notempty_条件
				std::cout << "Begin threadFunc tid" << std::this_thread::get_id()
					<< "尝试获取任务。。。" << std::endl;

				//线程空闲60秒进行回收


				//每一秒钟返回一次  怎么区分超时返回，还是有任务待执行返回
				while (taskQue_.size() == 0)
				{
					if (!isPoolRuning_)
					{
						threads_.erase(threadid);
						std::cout << "threadID:" << std::this_thread::get_id() << "exit!" << std::endl;
						exitCond_.notify_all();
						return;
					}

					if (poolMode_ == PoolMode::MODE_CACHED)
					{
						//条件变量，超时返回
						if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
						{
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							if (dur.count() >= THREAD_MAX_IDLE_TIME
								&& curThreadSize_ > initThreadSize_)
							{
								//开始回收当前线程							
								//记录线程数量的相关变量的值修改
								//把线程对象从线程列表容器中删除
								//线程id,找ID删除线程
								threads_.erase(threadid);
								curThreadSize_--;
								idleThreadSize_--;
								std::cout << "threadID:" << std::this_thread::get_id() << "exit!" << std::endl;
								return;
							}
						}
					}
					else
					{
						notEmpty_.wait(lock);
					}

				}
				idleThreadSize_--;
				//从任务队列中取一个任务出来
				std::cout << "Begin threadFunc tid" << std::this_thread::get_id()
					<< "获取任务成功。。。" << std::endl;
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;

				//如果依然有剩余任务，继续通知其他的线程执行任务
				if (taskQue_.size() > 0)
				{
					notEmpty_.notify_all();
				}
				//取出一个任务进行通知,通知可以继续提交生产任务
				notFull_.notify_all();
			

				//当前线程负责执行这个任务
				if (task != nullptr)
				{
					//function<void()>;//执行任务，把任务返回值setval给到Result
					task();//执行function<void()>
				}

				idleThreadSize_++;
				lastTime = std::chrono::high_resolution_clock().now(); //更新线程执行完任务的时间
			}
		}
	}
		//检查pool的运行状态
	bool checkRuningState() const
		{
			return isPoolRuning_;
		}

private:
	std::unordered_map<int, std::unique_ptr<Thread>>threads_;//线程列表
	size_t initThreadSize_;//初始线程数量+
	std::atomic_int curThreadSize_;//记录当前线程池里线程的总数量
	int threadSizeThreshHold_;//线程数量上限阈值
	std::atomic_int idleThreadSize_;//空闲线程的数量

	using Task = std::function<void()>;
	std::queue<Task>taskQue_;//任务队列
	std::atomic_int taskSize_;//任务的数量，有个阈值
	int taskQueMaxThreadHold_; //任务队列数量上限的阈值

	std::mutex taskQueMtx_;//保证任务队列的线程安全
	std::condition_variable notFull_;//表示任务队列不满
	std::condition_variable notEmpty_;//表示任务队列不空

	std::condition_variable exitCond_;//等待线程资源全部回收

	PoolMode poolMode_;//当前线程池的工作模式
	std::atomic_bool isPoolRuning_;//表示当前线程池的启动状态


};
#endif