# ::system()的陷阱

## Linux手册中对`::system()`的说明

> system() executes a command specified in command by calling /bin/sh -c command, and returns after the command has been completed. During execution of the command, SIGCHLD will be blocked, and SIGINT and SIGQUIT will be ignored.

> The value returned is -1 on error (e.g., fork(2) failed), and the return status of the command otherwise. This latter return status is in the format specified in wait(2). Thus, the exit code of the command will be WEXITSTATUS(status). In case /bin/sh could not be executed, the exit status will be that of a command that does exit(127). If the value of command is NULL, system() returns nonzero if the shell is available, and zero if not.
system() does not affect the wait status of any other children.

## 注意点

`::system()`的实现流程是fork->exec->wait，因此：

- 系统原因根本没有执行cmd（比如`fork()`失败）则返回`-1`，此时需要`errno`记录错误信息
- 传给system的cmd为null，则返回`1`
- 由于`/bin/sh`无法执行，则返回`127`
- 其他情况下**返回`wait`的返回值，注意与cmd的返回值并不同**

因此只有在`::system()`执行成功，且cmd也执行并返回`0`时，`::system()`才会返回`0`

**cmd若失败，需要通过`WIFEXITED(status)`判断子进程是否正常退出，通过`WEXITSTATUS(status)`来获取cmd的返回值**

`::system()`的cmd返回值是通过`wait`来获得的，而`wait`依赖`SIGCHLD`信号，如果在程序中错误设置了`SIGCHLD`信号的`handler`，就会出现`wait`无法找到`::system()`运行cmd的子进程，导致无法得知cmd的返回情况，此时会返回`-1`，例如（注意在WSL下无法复现这种情况）：

```cpp
signal(SIGCHLD, SIG_IGN);        // 忽略SIGCHLD命令
int r = ::system("ls .");        // 实际会执行成功，输出当前目录下所有文件
cout << "return of system: "
     << r << endl;               // -1
cout << "return of child: " 
     << WEXITSTATUS(r) << endl;  // 255
cout << "errno: " << errno       // 10 ECHILD, No Child Process
     << " explain: " << strerror(errno) << endl; 
return 0;
```

因此使用`::system()`时，保险起见需要提前对`signal(SIGCHLD, SIG_DFL)`进行恢复默认的`handler`，处理结束后再回到原先的`handler`，例如：

```cpp
struct sigaction act, old_act;
act.sa_handler = SIG_DFL;              // 默认的SIGCHLD处理函数
sigemptyset(&act.sa_mask);
act.sa_flags = 0;
sigaction(SIGCHLD, &act, &old_act);    // 保存旧handler，设置为默认handler
int r = ::system("ls .");
sigaction(SIGCHLD, &old_act, nullptr); // 恢复为旧handler
cout << "return of system: "
     << r << endl;                     // 0
cout << "return of child: " 
     << WEXITSTATUS(r) << endl;        // 0
cout << "errno: " << errno             // 0 OK, Success
     << " explain: " << strerror(errno) << endl; 
```

另外**推荐使用`::popen()`函数而不是`::system()`函数**

## 其他补充

子进程的终止状态判断涉及到的宏，设进程终止状态为`status`：

- `WIFEXITED(status)`：如果子进程正常结束则为非0值
- `WEXITSTATUS(status)`：取子进程`exit()`返回的结束码，一般会先用`WIFEXITED`来判断是否正常结束才使用此宏
- `WIFSIGNALED(status)`：如果子进程是因为信号而结束则此宏值为真
- `WTERMSIG(status)`：取得子进程因信号而中止的信号码，一般会先用`WIFSIGNALED`来判断后才使用此宏
- `WIFSTOPPED(status)`：如果子进程处于暂停执行情况则此宏值为真，一般只有使用`WUNTRACED`时才会有此情况
- `WSTOPSIG(status)`：取得引发子进程暂停的信号码，一般会先用`WIFSTOPPED`来判断后才使用此宏

[参考此处](https://blog.csdn.net/yangzhenzhen/article/details/51505176)
