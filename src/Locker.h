#ifndef locker_h__
#define locker_h__

//�ź��������������������������װ
//windows��ʹ�ü��ݵ�pthreads-win32
//�������ṩ��ͳһposix�߳̽ӿڣ�ֻ��������

#include<pthread.h>
#include <semaphore.h>
#ifdef WIN32
#include <time.h>
#else
#include<sys/time.h>
#endif

//�ź���
class Sem
{
private:
	sem_t sem_;
public:
	Sem(int value);
	~Sem();
	bool wait();
	bool waitForSeconds(int seconds);
	bool post();
};

//������
class Locker
{
private:
	pthread_mutex_t mutex_;
public:
	Locker();
	~Locker();
	bool lock();
	bool trylock();
	bool timelock(int iSecond);
	bool unlock();
	pthread_mutex_t* GetPthreadMutex();
};

//������
class LockerGuard
{
private:
	Locker& locker_;
public:
	LockerGuard(Locker& locker);
	~LockerGuard();
};

//��������
class Cond
{
private:
	pthread_cond_t cond_;
	pthread_mutex_t mutex_;
public:
	Cond();
	~Cond();
	bool wait();
	bool waitForSeconds(int seconds);
	bool signal();
	bool signalAll();
};

class Condition
{
private:
	pthread_cond_t cond_;
	Locker& mutex;
public:
	Condition(Locker& locker);
	~Condition();
	bool wait();
	bool waitForSeconds(int seconds);
	bool signal();
	bool signalAll();
};
#endif // locker_h__
