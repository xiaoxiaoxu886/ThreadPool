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

//Any���ͣ��ɽ����������ݵ�����
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	//���������Any�����������͵���������
	template<typename T>
	Any(T data) :base_(std::make_unique<Derive<T>>(data))
	{}

	//�������������ANy�е�����������ȡ����
	template<typename T>
	T cast_()
	{
		//������ô��base_�ҵ�����ָ���Derive����c��������ȡ����data��Ա����
		//����ָ��=����ת��������ָ��  RTTI
		Derive<T> *pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr)
		{
			throw "type is unmatch!";
		}
		return pd->data_;
	}
private:
	//��������
	class Base
	{
	public:
		virtual ~Base() = default;//= default�൱��{}
	};
	//����������
	template<typename T>
	class Derive : public Base
	{
	public:
		Derive(T data) :data_(data)
		{}
		T data_;//�������������������
	};
private:
	//����һ������ָ��
	std::unique_ptr<Base> base_;
};


//ʵ��һ���ź�����
class Semphore
{
public:
	Semphore(int resLimit = 0) :resLimit_(resLimit) 
	{}
	~Semphore() = default;
	//��ȡһ���ź�����Դ
	void wait()
	{
		std::unique_lock<std::mutex>lock(mtx_);
		//�ȴ��ź�����Դ��û����Դ�Ļ�����������ǰ�߳�
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}
	//����һ���ź�����Դ
	void post()
	{
		std::unique_lock<std::mutex>lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}
private:
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};

class Task;//Task���͵�ǰ������
//ʵ�ֽ����ύ���̳߳ص�task����ִ����ɺ�ķ���ֵ����result
class Result
{
public:
	Result(std::shared_ptr<Task>task, bool isvalid=true);
	~Result() = default;

	//setval��������ȡ����ִ����ķ���ֵ
	void setVal(Any any);
	//get�������û��������������ȡtask�ķ���ֵ
	Any get();
private:
	Any any_;//�洢����ķ���ֵ
	Semphore sem_;//�߳�ͨ���ź���
	std::shared_ptr<Task> task_;//ָ���Ӧ��ȡ����ֵ���������
	std::atomic_bool isValid_;//����ֵ�Ƿ���Ч
};

//����������
class Task {
public:

	Task();
	~Task() = default;

	void exec();
	void setResult(Result* res);
	//�û������Զ��������������ͣ���Task�̳У���дrun������ʵ���Զ���������
	virtual Any run() = 0;
private:
	Result* result_;//result������������ڴ���Task����
};

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
	Thread(ThreadFunc func);
	//�߳�����
	~Thread();
	//�����߳�
	void start();

	//��ȡ�߳�id
	int getId() const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;//�����߳�id
};


//�̳߳�����
class ThreadPool
{
public:
	//�̳߳ع���
	ThreadPool();
	//�̳߳�����
	~ThreadPool();

	//�����̳߳صĹ���ģʽ
	void setMode(PoolMode mode);

	//����task����������޵���ֵ
	void setTaskQueMaxThreshHold(int threshHold);

	//�����̳߳�cache��ģʽ���߳���ֵ
	void setThreadSizeTHeashHold(int theashHold);

	//���̳߳��ύ����
	Result subMitTask(std::shared_ptr<Task> sp);

	//�����̳߳�
	void start(int initThreadSize= std::thread::hardware_concurrency());

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	//�����̺߳���
	void threadFunc(int threadid);
	//���pool������״̬
	bool checkRuningState() const;

private:
	std::unordered_map<int,std::unique_ptr<Thread>>threads_;//�߳��б�
	size_t initThreadSize_;//��ʼ�߳�����+
	std::atomic_int curThreadSize_;//��¼��ǰ�̳߳����̵߳�������
	int threadSizeThreshHold_;//�߳�����������ֵ
	std::atomic_int idleThreadSize_;//�����̵߳�����

	std::queue<std::shared_ptr<Task>>taskQue_;//�������
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
