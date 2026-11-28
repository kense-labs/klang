<a id="call-location-src_file-and-src_line"></a>
# 调用位置：`src_file()` 与 `src_line()`

状态：已实现。

一条日志语句需要知道写出这条日志的调用所在的文件与行号。解析器已经在调用节点上保存了这些信息（`Line`，来自 `Parser::CurLine`）。代码生成阶段会将其作为常量发出，因此即使内联了接收该值的函数，它也不会发生移动。这里没有宏，也不需要栈回溯。

<a id="defaults"></a>
## 默认值

默认值在声明它的函数中解析。`AddParamDefaults` 会将该表达式克隆到调用处，而克隆体保留的是声明处的 `Line`。因此，作为默认值编写的 `src_file()` 就会给出该函数自身所在的文件。

该克隆体会在 `InParamDefault` 中进行检查，其帧记录了省略该实参的调用（`Frame::callline`）。当这次检查将某个调用解析为 `src_file` 或 `src_line` 内建函数时，`CheckBuiltin` 会根据最内层的参数默认值帧来设置该调用的 `Line`（`EachDefaultInPlace` 追踪的是调用书写的位置，而不是从默认值出发去检查的函数体）。同名的普通函数并非内建函数，因此它的行号仍保留解析时的位置。`CheckCall` 会丢弃先前插入的默认值，并在下一次检查时重新克隆，因此这一逻辑会在每次插入时运行。

```klang
fn info(text: u8[:], file: u8[:] = src_file(), row: i64 = src_line()) {}
```

位于 `fill.k` 第 4 行的 `info("filled")` 等价于 `info("filled", "fill.k", 4)`。调用方显式传入的实参会被保留。

<a id="the-file-string"></a>
## 文件字符串

`src_file()` 返回的是基本文件名（base name），即路径的最后一段。运行时中止（abort）会打印 `CodeGen::FileRef` 所存储的路径，每个源文件对应一个静态对象。日志语句则不会这样做：那个路径取决于编译器加载时的路径——可能是主目录（home directory）或某个代码检出目录（checkout）——并且会被硬编码进二进制文件中。基本文件名则是每个不同字符串对应一个静态对象（`StrRaw`），由所有引用它的调用共享。两个具有相同基本文件名的源文件不会被区分。列号仅保留在编译器内部。
