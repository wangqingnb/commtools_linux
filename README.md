# CommTools (Linux)

面向 Linux 的 C/C++ 通信与报文工具库，提供 epoll TCP、ISO8583、加密摘要、并发队列、INI 配置与日志等基础设施。

可用 **makefile** 在 Linux 上构建；也可用 Visual Studio 打开 [`CommTools.sln`](CommTools.sln) 做 Linux 远程开发。

## 模块一览

| 模块 | 说明 |
|------|------|
| **TCPClient / TCPServer** | 非阻塞 socket + epoll（ET）异步收发 |
| **pkg8583 / pkg8583Parser** | ISO8583 组包 / 解包 |
| **PtrQueue** | 线程安全指针环形队列（可阻塞） |
| **DES / MD5 / MAC** | 加解密与摘要 |
| **iniparser / dictionary** | INI 配置解析 |
| **FileLog / MemLog(CSysLog)** | 文件日志、内存队列日志 |
| **commroute** | Hex/BCD、时间戳、路径等通用工具 |

产物名：`libCommtools.a`（静态库）、`libCommtools.so`（动态库）。

## 环境要求

- Linux（已在 CentOS 7 / gcc 4.8+ 验证）
- `g++`（C++11）、`make`、`ar`、`pthread`
- 源码编码：**UTF-8（无 BOM）**，换行：**LF**

## 构建

在 `CommTools/` 目录下：

```bash
# Debug（默认）
make clean
make

# Release（-O3）
make clean DBGF=release
make DBGF=release
```

输出：

```text
CommTools/output/Debug/libCommtools.a|.so
CommTools/output/Release/libCommtools.a|.so
```

若存在上级 `lib/` 目录，makefile 还会复制到：

```text
../lib/<Debug|Release>/static/
../lib/<Debug|Release>/share/
```

链接业务程序时增加头文件路径 `CommTools/`，并链接 `-lCommtools -pthread`（按需设置 `-L` / `rpath`）。

## 单元测试

测试位于 [`tests/`](tests/)，覆盖：

- `commroute`、`md5`、`des`
- `PtrQueue`（含多生产者/多消费者）
- `CSysLog`（含多线程写读）
- `ISO8583` 组解包与边界检查
- `iniparser`

```bash
# 先构建库（Debug）
cd CommTools && make

# 再构建并运行测试
cd ../tests
make
./test_commtools
```

## 目录结构

```text
commtools_linux/
  CommTools.sln           # VS Linux 工程
  CommTools/              # 库源码 + makefile
  tests/                  # makefile 测试用例
  .editorconfig
  .gitattributes
```

## 与 Windows 版关系

Windows 侧对应能力见仓库 [RockyClass_windows](https://github.com/wangqingnb/RockyClass_windows) 中的 CommTools 模块；本仓库为 **Linux** 实现（epoll/pthread），接口与模块划分相近，但不可直接混链。

## 许可

私有仓库，未另行声明许可协议前，仅限授权使用。
