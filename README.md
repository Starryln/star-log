# star-log

单头文件极简日志（glog 风格），只负责把日志打出来。

## 用法

```cpp
#include "star_log.hpp"

int main() {
    star_log::SetLogToFile("test.log");
    LOG(DEBUG)   << "调试";        // 默认不输出
    LOG(INFO)    << "hello " << 42 << " " << 3.14;
    LOG(WARNING) << "注意";
    LOG(ERROR)   << "出错";
    LOG(FATAL)   << "挂了";        // 打印完 abort()
}
```

默认输出到 **stderr**。

## 行格式

```
[INFO ][2026-10-13 13:48:23.845][848016][main.cpp:6] hello 42
 └─┬─┘  └────┬───┘ └─────┬────┘  └──┬──┘└────┬────┘
  等级     年-月-日   时:分:秒.微秒   tid     文件:行号
```

## 接口

| 函数 | 说明 |
| --- | --- |
| `LOG(DEBUG/INFO/WARNING/ERROR/FATAL)` | 打日志，`<<` 可串联任意支持 `ostream` 的类型 |
| `star_log::SetMinLogLevel(level)` | 最低输出等级，默认 `DEBUG` |
| `star_log::SetLogToFile(path)` | 追加写入指定文件 |
| `star_log::SetLogToStderr()` | 是否输出 stderr |
| `star_log::flush()` | 立即落盘 |

## 说明

- 依赖 C++11，Linux（`tid` 取内核线程号），头文件只有一个。
- **非线程安全**：单线程用，多线程请自行加锁。
- 热路径无堆分配（复用一块格式化缓冲），被等级过滤时几乎零开销。


