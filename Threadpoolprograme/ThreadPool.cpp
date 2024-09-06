#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THERAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60;
//�̳߳ع���
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
//�̳߳�����
ThreadPool::~ThreadPool()
{
	isPoolRuning_ = false;

	
	//�ȴ��̳߳���������е��̷߳���
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

//�����̳߳�cachedģʽ���̵߳���ֵ
void ThreadPool::setThreadSizeTHeashHold(int theashHold)
{
	if (checkRuningState())
		return;
	if(poolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeThreshHold_ = theashHold;
	}
}

//����task����������޵���ֵ
void ThreadPool::setTaskQueMaxThreshHold(int threshHold)
{
	if (checkRuningState())
		return;
	taskQueMaxThreadHold_ = threshHold;
}

//���̳߳��ύ����   �û����øýӿڣ�����������������
Result ThreadPool::subMitTask(std::shared_ptr<Task> sp)
{
	//��ȡ��
	std::unique_lock<std::mutex>lock(taskQueMtx_);
	//�߳�ͨ�ţ��ȴ���������п���
	/*while (taskQue_.size() == taskQueMaxThreadHold_)
	{
		notFull_.wait(lock);
	}*/
	//�û��ύ�����������������1�룬�����ж��ύ����ʧ�ܣ�����
	//wait     wait_for������һ��ʱ��Ƭ��    wait_until������һ��ʱ����ֹ�ڵ�
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() <(size_t) taskQueMaxThreadHold_; }))
	{
		//��ʾnotFull_�ȴ�1s,������Ȼû������
		std::cerr<<"task queue is full,submit task fail"<<std::endl;
		return Result(sp,false);
	}
	//����п��࣬������������������
	taskQue_.emplace(sp);
	taskSize_++;
	//��Ϊ�·�������������п϶����գ�notEmpty_��֪ͨ
	notEmpty_.notify_all();
	auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		
	//cachedģʽ������ȽϽ�����������С���������  ��Ҫ�������������Ϳ����̵߳��������ж��Ƕ���Ҫ�����Ե�
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ < threadSizeThreshHold_)
	{

		std::cout << ">>>create new threads..." << std::endl;
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		//�����߳�
		threads_[threadId]->start();
		//�޸��̸߳�����صı���
		curThreadSize_++;
		idleThreadSize_++;
	}



	//��������õ�result����
	return Result(sp);
}

//�����̳߳�
void ThreadPool::start(int initThreadSize)
{
	//�����̳߳ص�����״̬
	isPoolRuning_ = true;
	//��¼��ʼ�̸߳���
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	//�����̶߳���
	for (int i = 0;i < (int)initThreadSize_; i++)
	{
		//������thread�̶߳����ʱ�򣬰��̺߳�������thread�̶߳���
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}
	//���������߳�
	for (int i = 0; i < (int)initThreadSize_; i++)
	{
		threads_[i]->start();//��Ҫȥִ��һ���̺߳���
		idleThreadSize_++;//��¼��ʼ�����̵߳�����
	}
}

//�����̺߳���  �̳߳ص������̴߳������������������
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();
	//�����������ִ����ɣ��̳߳زſ��Ի��������߳���Դ

	for (;;)
	{
		std::shared_ptr<Task> task;
		{
			//�Ȼ�ȡ��
			std::unique_lock<std::mutex>lock(taskQueMtx_);
			//�ȴ�notempty_����
			std::cout << "Begin threadFunc tid" << std::this_thread::get_id()
				<< "���Ի�ȡ���񡣡���" << std::endl;

			//�߳̿���60����л���
			
			
			//ÿһ���ӷ���һ��  ��ô���ֳ�ʱ���أ������������ִ�з���
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
					//������������ʱ����
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_)
						{
							//��ʼ���յ�ǰ�߳�							
							//��¼�߳���������ر�����ֵ�޸�
							//���̶߳�����߳��б�������ɾ��
							//�߳�id,��IDɾ���߳�
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
				//�̳߳�Ҫ���������߳���Դ
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
			//�����������ȡһ���������
			std::cout << "Begin threadFunc tid" << std::this_thread::get_id()
				<< "��ȡ����ɹ�������" << std::endl;
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			//�����Ȼ��ʣ�����񣬼���֪ͨ�������߳�ִ������
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}
			//ȡ��һ���������֪ͨ,֪ͨ���Լ����ύ��������
			notFull_.notify_all();
		}
		
		//��ǰ�̸߳���ִ���������
		if (task != nullptr)
		{
			//task->run();//ִ�����񣬰����񷵻�ֵsetval����Result
			task->exec();
		}
	
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now(); //�����߳�ִ���������ʱ��
	}
}
//------------------�̷߳���ʵ��--------------------------------

bool ThreadPool::checkRuningState() const
{
	return isPoolRuning_;
}
int Thread::generateId_ = 0;

//�̹߳���
Thread::Thread(ThreadFunc func)
	:func_(func)
	,threadId_(generateId_++)
{}
//�߳�����
Thread::~Thread(){}


//�����߳�
void Thread::start()
{
	//����һ���߳���ִ��һ���̺߳���
	std::thread t(func_,threadId_);//C++��˵���̶߳���t�����̺߳���func_
	t.detach();//���÷����߳�
}

/////------------Task����ʵ��--------------
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
		result_->setVal(run());//���﷢����̬����
	}	
}
void Task::setResult(Result* res)
{
	result_ = res;
}


////------------Result����ʵ��-------------------
Result::Result(std::shared_ptr<Task>task, bool isvalid)
	:task_(task)
	, isValid_(isvalid)
{
	task_->setResult(this);
}

Any Result::get()//�û�����
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait();//task�������û��ִ���꣬����������û����߳�
	return std::move(any_);
}

void Result::setVal(Any any) //
{
	//�洢task�ķ���ֵ
	this->any_ = std::move(any);
	sem_.post();//�Ѿ���ȡ�����񷵻�ֵ�������ź�����Դ
}
