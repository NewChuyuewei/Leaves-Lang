# Leaves 对象模型与运行时约定

本文描述当前实现，不包含尚未实现的接口或专有私有字段语法。

## 1. 声明、继承与创建

```lua
class Base
  function init(self, value)
    self.value = value
  end
  function get(self)
    return self.value
  end
end

local function choose_base()
  return Base
end

class Child extends choose_base() end
local item = Child(5)
```

- 类名是词法局部变量，类体局部变量另有独立作用域。
- 父类表达式求值一次，使用其单个结果；可以是变量、字段或函数调用等表达式。
- 父类必须是 Leaves 类；实例、普通表等非类值会报错。当前显式 `extends nil` 与未提供父类一样，创建根类。
- 省略 `init` 时沿实例方法继承链查找；仍未找到则直接返回空实例。
- 本类直接存储的 `init` 优先于实例方法视图中的同名方法。非 nil 且不可调用的 `init` 会按 Lua 调用规则报错。
- 构造器参数保留 Lua 的顺序、nil 和可变参数语义；所有构造器返回值均被忽略，最终只返回实例。
- 父构造器不自动调用，可写 `Base.init(self, ...)` 显式调用。
- 类体 `local` 每次执行声明时创建一次，由该次创建的类的实例共享；这不是每实例字段。

## 2. 成员查找顺序

实例 `obj.key`：

1. 实例表自身字段。
2. 本类实例方法视图。
3. 父类实例方法视图，逐层继承。

静态方法不在实例视图中，因而不会通过实例查找获得。给实例自己赋同名字段仍是普通 Lua 表操作。

类 `Cls.key`：

1. 类表自身字段，包括本类实例方法的公开副本和 `isa` 等辅助成员。
2. 静态表：本类优先，然后沿静态表继承链查找。
3. 实例方法视图：本类优先，然后沿父视图查找。
4. 父类的类表查找，用于继承其他公开类字段。

因此，当同名静态方法与实例方法混用时，`Cls.key` 和 `obj.key` 不一定相同。
本类的公开实例方法副本会先于静态查找命中；没有本类原始字段时，继承静态方法会先于继承实例方法命中。
这是当前实现的兼容规则，建议避免对两类方法使用同名成员。

实例方法不是 Lua 运算符元方法的自动注册入口。例如声明名为 `__add` 的实例方法，不会自动赋予实例加法行为。

## 3. 同步更新实例方法

```lua
local previous = item.get

leaves.setmethod(Base, "get", function(self)
  return self.value * 2
end)

assert(item:get() == 10)
assert(previous(item) == 5)

-- 删除本类方法，重新显露继承方法；没有继承方法时查找为 nil。
leaves.setmethod(Child, "get", nil)
```

`leaves.setmethod(Cls, name, fn_or_nil)`：

- 第一个参数必须是类，不接受实例；第二个参数必须是字符串。
- 第三个参数必须显式提供，且为函数或 nil。不接受可调用表。
- 同时写入类的公开方法副本和实例方法视图，返回传入的类。
- 更新影响已有实例、以后创建的实例以及未覆盖该方法的子类；子类自己的覆盖不受影响。
- 实例自身的同名字段仍然优先；之前保存的函数引用或已开始执行的方法不会被替换。
- 删除只删除本类定义，不屏蔽继承方法，也不删除同名静态方法。
- 更新 `init` 只改变未来构造的行为，不会重新初始化已有实例。
- 不引入查找缓存；方法更新后无需手动失效缓存。
- 拒绝以 `__leaves_` 开头的保留名称。
- 这是普通实例方法更新 API，不是静态方法声明或更新 API。

`Cls.method = fn` 依然是合法的普通表赋值，但只改变类表，不会同步实例视图。需要一致的方法替换语义时应使用 `leaves.setmethod`。
通过 `leaves.class` 和 `leaves.setmethod` 编程无需启用 Leaves 语法，普通 Lua 模式下也可使用。

## 4. 构造器与协程

在可挂起的 Lua 协程中，`Cls(...)` 和 `leaves.new(Cls, ...)` 都允许构造器调用 `coroutine.yield`：

```lua
class Task
  function init(self)
    self.value = coroutine.yield("waiting")
  end
end

local co = coroutine.create(function()
  return leaves.new(Task)
end)
local ok, phase = coroutine.resume(co)
assert(ok and phase == "waiting")
local done, task = coroutine.resume(co, 42)
assert(done and task.value == 42)
```

- 挂起值与恢复参数由 Lua 协程规则决定。
- 实例保留在 C 调用栈的可追踪槽位中，挂起期间不会因 GC 被提前释放；无需 registry 引用。
- 恢复完成后返回同一个实例，构造器返回值仍被忽略。
- 挂起后出现的错误正常传播，可由外部 Lua `pcall` 捕获。
- `coroutine.close` 按 Lua 规则清理挂起构造器中的 `<close>` 资源，不等于 Leaves 提供了对象析构器。
- 主线程及不可挂起的 C 调用边界仍禁止 yield。API 支持挂起不代表嵌入式宿主的任意调用路径都可挂起。
- 构造器如果主动将 `self` 暴露给外部，外部可能看到未完成初始化的实例；系统不自动回滚已发生的副作用。

## 5. 实现边界与保留字段

类、实例和视图均使用 Lua 表。所有 `__leaves_*` 字段、内部视图元表及继承链均为实现细节，应用代码不要直接修改或伪造。

- 对内部结构的任意修改、继承环和伪造类不属于稳定支持范围。
- 不保证手动替换内部元表后仍有正确的构造、更新或类型查询行为。
- `debug` 库可以访问闭包与元表；词法封装不是安全沙箱。
- 不保证内存不足错误发生时多表更新具有事务回滚语义。
- 当前不提供类冻结、对象析构器、接口检查或专有 `private` 字段语法。

## 6. 验证入口

`make check` 包含对象、词法封装、构造器、动态更新、父类表达式与协程回归，并验证新增用例的普通和去调试信息字节码执行。
CI 还启用 Lua 内部断言和 C API 检查，以捕获常规构建中不易显现的栈与寄存器错误。
严格 C89 / C99 构建请先 `make clean` 后选择对应构建目标，再运行 `python3 test/leaves/run.py`。
