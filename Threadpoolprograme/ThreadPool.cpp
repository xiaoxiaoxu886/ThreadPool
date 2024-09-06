#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THERAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60;
//线程池构造
ThreadPool::ThreadPool()
	:initThreadSize_(0)
	, taskSize_(0)
	, idleThreadSize_(0)
	,curThreadSize_(0)
	, threadSizeThreshHold_(THERAD_MAX_THRESHHOLD)
	,taskQueMaxThreadHold_(TASK_MAX_THRESHHOLD)
	,poolMode_(PoolMode::MODE_FIXED)
	,isPoolRuning_(false)
{}
//线程池析构
ThreadPool::~ThreadPool()
{
	isPoolRuning_ = false;

	
	//等待线程池里面的所有的线程返回
	std::unique_lock<std::mutex>lock(taskQueMtx_);

	notEmpty_.notify_all();

	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

void ThreadPool::setMode(PoolMode mode)
{
	if (checkRuningState())
		return;
	poolMode_ = mode;
}

//设置线程池cached模式下线程的阈值
void ThreadPool::setThreadSizeTHeashHold(int theashHold)
{
	if (checkRuningState())
		return;
	if(poolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeThreshHold_ = theashHold;
	}
}

//设置task任务队列上限的阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshHold)
{
	if (checkRuningState())
		return;
	taskQueMaxThreadHold_ = threshHold;
}

//给线程池提交任务   用户调用该接口，传入任务，任务生产
Result ThreadPool::subMitTask(std::shared_ptr<Task> sp)
{
	//获取锁
	std::unique_lock<std::mutex>lock(taskQueMtx_);
	//线程通信，等待任务队列有空余
	/*while (taskQue_.size() == taskQueMaxThreadHold_)
	{
		notFull_.wait(lock);
	}*/
	//用户提交任务，最长不能阻塞超过1秒，否则判断提交任务失败，返回
	//wait     wait_for设置了一个时间片段    wait_until设置了一个时间终止节点
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() <(size_t) taskQueMaxThreadHold_; }))
	{
		//表示notFull_等待1s,条件依然没有满足
		std::cerr<<"task queue is full,submit task fail"<<std::endl;
		return Result(sp,false);
	}
	//如果有空余，把任务放入任务队列中
	taskQue_.emplace(sp);
	taskSize_++;
	//因为新放了任务，任务队列肯定不空，notEmpty_上通知
	notEmpty_.notify_all();
	auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		
	//cached模式任务处理比较紧急，场景：小而块的任务  需要根据任务数量和空闲线程的数量，判断是都需要创建显得
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ < threadSizeThreshHold_)
	{

		std::cout << ">>>create new threads..." << std::endl;
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		//启动线程
		threads_[threadId]->start();
		//修改线程个数相关的变量
		curThreadSize_++;
		idleThreadSize_++;
	}



	//返回任务得到result对象
	return Result(sp);
}

//开启线程池
void ThreadPool::start(int initThreadSize)
{
	//设置线程池的运行状态
	isPoolRuning_ = true;
	//记录初始线程个数
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	//创建线程对象
	for (int i = 0;i < (int)initThreadSize_; i++)
	{
		//当创建thread线程对象的时候，把线程函数给到thread线程对象
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
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

//定义线程函数  线程池的所有线程从任务队列里消费任务
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();
	//所有任务必须执行完成，线程池才可以回收所有线程资源

	for (;;)
	{
		std::shared_ptr<Task> task;
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
				//线程池要结束回收线程资源
			/*	if (!isPoolRuning_)
				{
					threads_.erase(threadid);
					std::cout << "threadID:" << std::this_thread::get_id() <<"exit!" << std::endl;
					exitCond_.notify_all();
					return;
				}
					*/
			}
			/*if (!isPoolRuning_)
			{
				break;
			}*/
	
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
		}
		
		//当前线程负责执行这个任务
		if (task != nullptr)
		{
			//task->run();//执行任务，把任务返回值setval给到Result
			task->exec();
		}
	
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now(); //更新线程执行完任务的时间
	}
}
//------------------线程方法实现--------------------------------

bool ThreadPool::checkRuningState() const
{
	return isPoolRuning_;
}
int Thread::generateId_ = 0;

//线程构造
Thread::Thread(ThreadFunc func)
	:func_(func)
	,threadId_(generateId_++)
{}
//线程析构
Thread::~Thread(){}


//启动线程
void Thread::start()
{
	//创建一个线程来执行一个线程函数
	std::thread t(func_,threadId_);//C++来说，线程对象t，和线程函数func_
	t.detach();//设置分离线程
}

/////------------Task方法实现--------------
int Thread::getId() const
{

	return threadId_;
}
Task::Task()
	:result_(nullptr)
{}

void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run());//这里发生多态调用
	}	
}
void Task::setResult(Result* res)
{
	result_ = res;
}


////------------Result方法实现-------------------
Result::Result(std::shared_ptr<Task>task, bool isvalid)
	:task_(task)
	, isValid_(isvalid)
{
	task_->setResult(this);
}

Any Result::get()//用户调用
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait();//task任务如果没有执行完，这里会阻塞用户的线程
	return std::move(any_);
}

void Result::setVal(Any any) //
{
	//存储task的返回值
	this->any_ = std::move(any);
	sem_.post();//已经获取的任务返回值，增加信号量资源
}
