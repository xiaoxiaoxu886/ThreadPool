# ThreadPool
线性池项目介绍：
----------
这是一个基于可变参模板和引用折叠原理实现的线程池项目，它支持任意任务函数和任意参数的传递。\<br>
通过使用future类型定制submitTask接口，可以实现对提交任务的返回值的管理。\<br>
该项目支持fixed和cached模式的线程池定制。\<br>
并使用map和queue容器管理线程对象。\<br>
项目还实现了在Linux平台编译线性池动态库。\<br>
并基于条件变量condition_variable和互斥锁mutex实现了任务提交线程和任务执行线程间的通信机制。\<br>
