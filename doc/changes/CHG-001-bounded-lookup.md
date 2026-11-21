# CHG-001：继承查找的有界遍历

状态：需求与设计先行的工程试点；非认证数据，不指定 DAL。

## 范围与问题

仅修改 `src/lleaveslib.c` 中的类成员查找与 `isa` 显式遍历。
当前成员视图遍历没有步数上限；父类公开字段回退会再次触发类 `__index`，
导致重复搜索与 C 递归，正常深继承的缺失字段可能耗尽 C 调用栈。
不改变 Lua VM、字节码、GC、构造器、静态链及实例链的原生查找实现。

## 需求（实现前确定）

| 标识 | 可验证需求 | 预期结果 / 测试 |
| --- | --- | --- |
| BND-001 | 正常类的成员优先级、false 值、实例/静态隔离保持不变 | `bounded.leav` 的 priority 场景及既有 objects/updates 测试 |
| BND-002 | 父类公开字段回退不递归调用 Leaves 类 `__index` | 300 层正常继承：缺失字段为 nil，祖先公开字段可达，覆盖值优先 |
| BND-003 | 每次显式原始链遍历最多检查 1000 个表节点；需要第 1001 个节点时报受控 Lua 错误 | 1000 节点边界可查；1001 节点查询超界；错误包含 `inheritance chain is too deep` |
| BND-004 | 异常视图父链接或类父链接形成环时，受影响的显式遍历须在上述上限内报错 | 定向故障注入，不挂死、不崩溃；不声明支持任意修改内部元表 |
| BND-005 | 一次查询异常不得改变其他正常类的查询结果 | 环测试后恢复链接，重新执行正常查询 |
| BND-006 | 代码保持 ISO C，默认/严格 C89/C99/内部断言构建通过，新增用例支持解释及字节码执行 | 构建矩阵、测试报告；测试进程配置超时 |

## 设计与影响分析

1. 定义一个内部常量 `MAX_INHERITANCE = 1000`。视图、父类字段、isa 的
   显式遍历分别使用本地计数器；进入每个表节点前检查配额。
2. 保留类表自身字段优先级（由 Lua 原始表访问负责）。`class_index` 仍先
   查完整静态链，再查完整实例视图链。二者均未找到后，只需依次原始查询
   父类表自身字段：前两阶段已经搜索了所有继承静态与实例方法。
3. 回退不再用 `lua_gettable(parent)` 重入类元方法，改为迭代 `rawget`。
   该等价关系只适用于正常内部链；自定义父类 `__index` 等内部元表替换
   不在兼容承诺内。异常内部结构只承诺本次定向加固覆盖的终止行为。
4. 使用固定数量的 Lua C API 临时栈位，不按继承深度增加 C 调用栈或堆数组。
   每个节点弹掉旧节点，只保留当前节点与结果；不添加 registry 根或缓存。
5. `isa` 统一按访问表节点计数，边界不再依赖遍历结束后的隐含加一。
6. 普通 Lua 语法与其表查找没有修改。Lua 原生 `__index` 表链仍使用 Lua
   自身的保护界限；1000 仅约束本次列明的显式原始链遍历，不是全系统
   最大类深度，不是最坏执行时间保证。

## 追踪与验证方法

| 需求 | 实现位置 | 测试位置 |
| --- | --- | --- |
| BND-001 | class_index 的三阶段查找 | bounded.leav: priority；objects.leav；updates.leav |
| BND-002 | class_index 父表迭代回退 | bounded.leav: deep |
| BND-003 | 遍历配额辅助函数与三个显式循环 | bounded.leav: boundary |
| BND-004 | 视图/父表循环的节点前检查 | bounded.leav: cycles |
| BND-005 | 无缓存、无结构写入的查询路径 | bounded.leav: recovery |
| BND-006 | ISO C 实现；run.py 超时保护 | test/leaves/run.py；CI 矩阵 |

新增测试的预期结果由上述查找规则与计数定义确定，不以优化前程序输出为判定标准。
超时只用于测试运行器防止挂死，不作为运行时遍历界限的实现。

## 非目标与限制

- 不提供完整内部元数据验证、类伪造防护、安全沙箱或对用户元方法的终止保证。
- 不处理内存耗尽注入、MC/DC、目标机覆盖或全部上游 Lua 回归。
- 超过遍历配额可能由长链或环导致，错误不声称精确诊断环。
- 不以微基准或 ASan 代替需求验证，不宣称 DO-178C 符合性。

## 验证记录

### 本地执行结果

源码基线：`f117d415718643fb7260ad3085ad6fcd99b6d2b3`，在当前工作分支上应用本次补丁。
环境：Linux x86_64；GCC 12.2.0（Debian 12.2.0-14+deb12u1）；Python 3.11.2。
记录反映本次本地执行，不代表远端 CI 已运行或目标机验证完成。

| 验证 | 实际结果 |
| --- | --- |
| 基线运行新增用例，进程超时设为 5 秒 | 在 deep 场景返回非零：`C stack overflow`，未达到环测试 |
| 默认 Linux：`make clean && make linux -j4`，运行回归 | 49 项检查通过 |
| `make clean && make iso-c89 -j4`，运行回归 | 49 项检查通过 |
| `make clean && make iso-c99 -j4`，运行回归 | 49 项检查通过 |
| `make clean && make linux -j4 MYCFLAGS='-DLUAI_ASSERT -DLUA_USE_APICHECK'`，运行回归 | 49 项检查通过 |
| 默认构建下 `python3 -O test/leaves/run.py` | 49 项检查通过；运行器核心检查不依赖 Python assert |
| ASan/UBSan + 内部断言/API 检查，7 个运行时用例 | 全部通过，无 sanitizer 报告 |
| `git diff --check` | 通过 |

回归命令为 `python3 test/leaves/run.py`。新增用例分别解释执行，并编译为
普通/去调试信息字节码后执行；每个进程有 30 秒超时，信号终止不能被当作
预期的普通错误。超时是测试基础设施保护，不替代节点配额。
严格构建之后恢复了默认 Linux 产物，未提交二进制。

Sanitizer 构建可按以下命令复现（与本地执行使用相同源文件集合与选项）：

```sh
mkdir -p .work/asan
gcc -std=c99 -pedantic-errors -DLUA_USE_JUMPTABLE=0 \
  -DLUAI_ASSERT -DLUA_USE_APICHECK -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I src \
  $(find src -maxdepth 1 -name '*.c' ! -name luac.c ! -name onelua.c ! -name ltests.c | sort) \
  -lm -o .work/asan/leav
for name in objects locals constructors updates coroutines parents bounded; do
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    .work/asan/leav test/leaves/$name.leav || exit 1
done
```

验证时关键输入的 SHA-256（避免仅用“测试通过”识别被测对象）：

```text
f128a70cc12392b092da3ad865516da850ee2ac7676b1e86b98036c6435b476c  src/lleaveslib.c
3522aae2a46a4fe7353baed40c956b44e6c9fd1552295c979423538015adf8ba  test/leaves/bounded.leav
68beb1142917764a7de4db77ae83139d083b3fbb70903047c161bb2ff2bf9309  test/leaves/run.py
```

原始构建/执行日志保存在忽略的 `.work/CHG-001-*` 中，仅供当前工作区调试，
不是受控长期证据库。本文件保存可追踪摘要和复现步骤；正式证据归档仍未建立。
其他源码通过上述基线及本次补丁识别，未对完整工具链或所有依赖作内容寻址。

### 自检与开放事项

- 自检：正常链的静态/视图结果已先搜索，祖先原始字段回退不重复进入类元方法；
  两个查找阶段之间清理临时栈，每次循环只保留当前节点，false 不当作缺失值。
- 正常深链、精确边界、单节点/双节点环、失败后恢复以及既有回归均通过。
- 改变了超深显式链的失败方式；该限制已写入对象模型说明。
- 未实施结构覆盖、目标机验证、OOM 故障注入或性能承诺；不声称证明全程序终止。
- **独立人工评审尚未完成**；自动执行和实现者自检不等同于独立批准。
