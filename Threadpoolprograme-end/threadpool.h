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

//�̳߳�֧�ֵ�ģʽ
enum class PoolMode
{
	MODE_FIXED,//�̶��������߳�
	MODE_CACHED,//�߳������ɶ�̬����
};

//�߳�����
class Thread {
public:
	//�̺߳�����������
	using ThreadFunc = std::function<void(int)>;

	//�̹߳���
	Thread(ThreadFunc func)
		:func_(func)
		, threadId_(generateId_++)
	{}
	//�߳�����
	~Thread() = default;
	//�����߳�
	void start()
	{
		//����һ���߳���ִ��һ���̺߳���
		std::thread t(func_, threadId_);//C++��˵���̶߳���t�����̺߳���func_
		t.detach();//���÷����߳�
	}

	//��ȡ�߳�id
	int getId() const
	{
		return threadId_;
	}
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;//�����߳�id
};
int Thread::generateId_ = 0;

//�̳߳�����
class ThreadPool
{
public:
	//�̳߳ع���
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
	//�̳߳�����
	~ThreadPool()
	{
		isPoolRuning_ = false;


		//�ȴ��̳߳���������е��̷߳���
		std::unique_lock<std::mutex>lock(taskQueMtx_);

		notEmpty_.notify_all();

		exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
	}

	//�����̳߳صĹ���ģʽ
	void setMode(PoolMode mode)
	{
		if (checkRuningState())
			return;
		poolMode_ = mode;
	}

	//����task����������޵���ֵ
	void setTaskQueMaxThreshHold(int threshHold)
	{
		if (checkRuningState())
			return;
		taskQueMaxThreadHold_ = threshHold;
	}

	//�����̳߳�cache��ģʽ���߳���ֵ
	void setThreadSizeTHeashHold(int theashHold)
	{
		if (checkRuningState())
			return;
		if (poolMode_ == PoolMode::MODE_CACHED)
		{
			threadSizeThreshHold_ = theashHold;
		}
	}

	//���̳߳��ύ����
	//ʹ�ÿɱ��ģ���̣���submittask���Խ������������������������Ĳ���
	//Result subMitTask(std::shared_ptr<Task> sp);
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args)->std::future<decltype(func(args...))>
	{
		//������񣬷����������
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
			);
		std::future<RType> result = task->get_future();

		//��ȡ��
		std::unique_lock<std::mutex>lock(taskQueMtx_);
		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreadHold_; }))
		{
			//��ʾnotFull_�ȴ�1s,������Ȼû������
			std::cerr << "task queue is full,submit task fail" << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType {return RType(); });
			(*task)();
			return task->get_future();
		}
		//����п��࣬������������������
		//taskQue_.emplace(sp);
		//using Task = std::function<void()>;
		taskQue_.emplace([task]() {(*task)(); });//������task
		taskSize_++;
		//��Ϊ�·�������������п϶����գ�notEmpty_��֪ͨ
		notEmpty_.notify_all();
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));

		//cachedģʽ������ȽϽ�����������С���������  ��Ҫ�������������Ϳ����̵߳��������ж��Ƕ���Ҫ�����Ե�
		if (poolMode_ == PoolMode::MODE_CACHED
			&& taskSize_ > idleThreadSize_
			&& curThreadSize_ < threadSizeThreshHold_)
		{

			std::cout << ">>>create new threads..." << std::endl;
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			//�����߳�
			threads_[threadId]->start();
			//�޸��̸߳�����صı���
			curThreadSize_++;
			idleThreadSize_++;
		}



		//��������õ�result����
		return result;
	}

	//�����̳߳�
	void start(int initThreadSize = std::thread::hardware_concurrency())
	{
		//�����̳߳ص�����״̬
		isPoolRuning_ = true;
		//��¼��ʼ�̸߳���
		initThreadSize_ = initThreadSize;
		curThreadSize_ = initThreadSize;

		//�����̶߳���
		for (int i = 0; i < (int)initThreadSize_; i++)
		{
			//������thread�̶߳����ʱ�򣬰��̺߳�������thread�̶߳���
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
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

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	//�����̺߳���
	void threadFunc(int threadid)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();
		//�����������ִ����ɣ��̳߳زſ��Ի��������߳���Դ

		for (;;)
		{
			Task task;
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

				}
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
			

				//��ǰ�̸߳���ִ���������
				if (task != nullptr)
				{
					//function<void()>;//ִ�����񣬰����񷵻�ֵsetval����Result
					task();//ִ��function<void()>
				}

				idleThreadSize_++;
				lastTime = std::chrono::high_resolution_clock().now(); //�����߳�ִ���������ʱ��
			}
		}
	}
		//���pool������״̬
	bool checkRuningState() const
		{
			return isPoolRuning_;
		}

private:
	std::unordered_map<int, std::unique_ptr<Thread>>threads_;//�߳��б�
	size_t initThreadSize_;//��ʼ�߳�����+
	std::atomic_int curThreadSize_;//��¼��ǰ�̳߳����̵߳�������
	int threadSizeThreshHold_;//�߳�����������ֵ
	std::atomic_int idleThreadSize_;//�����̵߳�����

	using Task = std::function<void()>;
	std::queue<Task>taskQue_;//�������
	std::atomic_int taskSize_;//������������и���ֵ
	int taskQueMaxThreadHold_; //��������������޵���ֵ

	std::mutex taskQueMtx_;//��֤������е��̰߳�ȫ
	std::condition_variable notFull_;//��ʾ������в���
	std::condition_variable notEmpty_;//��ʾ������в���

	std::condition_variable exitCond_;//�ȴ��߳���Դȫ������

	PoolMode poolMode_;//��ǰ�̳߳صĹ���ģʽ
	std::atomic_bool isPoolRuning_;//��ʾ��ǰ�̳߳ص�����״̬


};
#endif