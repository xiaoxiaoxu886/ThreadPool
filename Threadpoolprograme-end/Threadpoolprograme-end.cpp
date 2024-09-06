﻿// Threadpoolprograme-end.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include <chrono>
#include "threadpool.h"
using namespace std;

int sum1(int a, int b)
{
	this_thread::sleep_for(chrono::seconds(2));
	return a + b;
}

int sum2(int a, int b,int c)
{
	this_thread::sleep_for(chrono::seconds(3));
	return a + b + c;
}



int main()
{
	ThreadPool pool;
	pool.setMode(PoolMode::MODE_CACHED);
	pool.start(1);
	future<int> r1=pool.submitTask(sum1, 1, 2);
	future<int> r2 = pool.submitTask(sum2, 1, 2,3);
	future<int> r3 = pool.submitTask([](int b, int e)->int {
		int sum = 0;
		for (int i = b; i <= e; i++)
			sum += i;
		return sum;
		},1,100);
	future<int> r4 = pool.submitTask([](int b, int e)->int {
		int sum = 0;
		for (int i = b; i <= e; i++)
			sum += i;
		return sum;
		}, 1, 100);
	future<int> r5 = pool.submitTask([](int b, int e)->int {
		int sum = 0;
		for (int i = b; i <= e; i++)
			sum += i;
		return sum;
		}, 1, 100);
	//future<int> r4 = pool.submitTask(sum1, 1, 2);

	cout << r1.get() << endl;
	cout << r2.get() << endl;
	cout << r3.get() << endl;
	cout << r4.get() << endl;
	cout << r5.get() << endl;
	getchar();
}
