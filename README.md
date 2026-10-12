# star-log

单头文件极简日志（glog 风格），只负责把日志打出来。

## 用法

```cpp
#include "star_log.hpp"

int main() {
    LOG(DEBUG)   << "调试";        // 默认不输出
    LOG(INFO)    << "hello " << 42 << " " << 3.14;
    LOG(WARNING) << "注意";
    LOG(ERROR)   << "出错";
    LOG(FATAL)   << "挂了";        // 打印完 abort()

    star_log::set_log_file("app.log");                    // 改成追加写文件
    star_log::set_level(star_log::LOG_WARNING);           // 只留 WARNING 以上
    star_log::set_level(star_log::LOG_DEBUG);             // 连 DEBUG 一起打
}
```

默认输出到 **stderr**。

## 行格式

```
I20261012 12:34:56.789012 12345 main.cc:42] hello 42
│└──┬───┘└──────┬───────┘└─┬─┘└───┬────┘
│ 年月日     时分秒.微秒   tid   文件:行号
└ 等级 D/I/W/E/F
```

## 接口

| 函数 | 说明 |
| --- | --- |
| `LOG(DEBUG/INFO/WARNING/ERROR/FATAL)` | 打日志，`<<` 可串联任意支持 `ostream` 的类型 |
| `star_log::set_level(sev)` | 最低输出等级，默认 `LOG_INFO`（即 DEBUG 默认关） |
| `star_log::set_log_file(path)` | 追加写入指定文件（不再写 stderr），打不开则退回 stderr |
| `star_log::set_log_to_stderr()` | 切回 stderr |
| `star_log::flush()` | 立即落盘 |

## 说明

- 依赖 C++11，Linux（`tid` 取内核线程号），头文件只有一个。
- **非线程安全**：单线程用，多线程请自行加锁。
- 热路径无堆分配（复用一块格式化缓冲），被等级过滤时几乎零开销。
- `LOG` / `LOG_INFO` 这些名字和 `<syslog.h>` 撞车（syslog.h 里也定义了 `LOG_INFO` 等）。
  万一同一个编译单元里 include 了 syslog.h，把 `enum Level` 的 5 个名字和
  `STAR_LOG` 宏里的 `LOG_##sev` 一起换成 `STAR_LOG_` 前缀就行。

