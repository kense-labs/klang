<a id="compiler-analyses-inventory-and-simplification-options"></a>
# 编译器分析：清单与简化方案

> **已弃用的语言特性：** `reusable` 和 `reusable[]`。请改用
> [`slot_pool<T>`](../stdlib.md#slot_pool) 表示槽位，以及
> [`slice_pool<T>`](../stdlib.md#slice_pool) 表示连续区间。
> 下文中的内建行为和历史示例对现有代码仍然适用；计划在剩余迁移缺口补齐后单独移除。

<details>
<summary>目录</summary>

- [1. 概述](#1-summary)
- [2. 状态追踪位置](#2-what-is-tracked-where)
- [3. 清单](#3-inventory)
- [4. 各项分析说明](#4-per-analysis-notes)
- [5. 跨模块发现与建议](#5-cross-cutting-findings-and-recommendations)
- [6. 排序与验证](#6-sequencing-and-validation)
- [7. 进展状态](#7-status)

</details>

这是打破检查器分析中“修补一个漏洞、新增一个标志位”循环的第一步。本文档列出了编译器运行的每一项分析、每项分析追踪的内容及方式、大致代码量、稳健程度，以及在何处换用不同的表示方式能更彻底地覆盖整个语言。文末附有按优先级排序的建议。

数据截至 master `bfea86b`（2026-09-24）。代码行数为函数体行数，通过对 `src/typecheck*.h`、`bce.h`、`optimize*.h` 和 `codegen*.h` 中的函数定义进行一次性粗略扫描并按函数归属统计得出，因此仅作大致参考。各项分析维护的状态单独列出，因为实际膨胀正发生在状态之中。

<a id="1-summary"></a>
## 1. 概述

* 编译器运行着大约 20 项不同的分析。生命周期相关分析族（来源溯源、回读、存储、缩容、增长、递归、返回根、特化键）约占检查器函数体代码的 5,000 行，外加约 400 行的状态声明。它几乎占了所有的代码改动：在最近的 60 个提交中，约有 30 个是在修复这些分析未能覆盖的语言结构，另有十几个是在重构同一批代码。BCE（约 2,300 行）以及优化器和代码生成中的小型分析在结构上是稳固的，很少发生改动。
* 问题不在于代码质量。代码注释非常详尽，风格也很一致。频繁改动源于四个架构选择：
  1. **引用的来源（provenance）是“单个根 + 补丁位”。** 一个可能指向多个位置的值会被折叠为它的最内层根（`InnerRoot`），折叠丢失的信息则作为附属标志位来携带（`intogs`、`cyclelocal`、`hidesclass`、`rootfrom`、`slotread`、`holderroot`……）。每一个标志位都必须在约 25 个转换点进行传播，而每新增一个语言结构就可能多一处漏掉传播的机会。这是最主要的修复来源。
  2. **函数体只检查一次，就在首次到达它的调用内部完成。** 回到递归环中的调用会到达一个尚未检查完的被调函数，因此每项过程间分析都必须有一条基于语法的“被调函数仍在检查中”的备选路径，外加收回与结算机制（环返回根预测、线程化类别、假定平衡的缩容、环调用点）。大约 1,500 行代码仅为此而存在。
  3. **三个阶段各自维护了三套存储与被调副作用模型。** 检查器（`NoteRootEvent`、`ShrinkTargets`、`RootCandidates`、`MayAliasRoots`、`NamedOutside`）、BCE（`UltOf`、`Reach`、`AffectedByWrite`、`Effects`）和代码生成（`CollectSpecs`、`RefTopsOk`、`SyncReach`）各自通过自己的调用图遍历来回答“这可能到达 / 别名 / 修改什么”。
  4. **事实在许多位置被主动推入状态，而不是从语法树中推导得出。** 持有临时值、增长日志、活性、循环内被赋值的名称以及待处理缩容都由散布在检查器各处的调用来维护；只要某个语言结构忘了调用，就会出现漏洞。
* 按投入产出比排序的首要建议（详见第 5 节）：
  1. 用显式的备选集替代单一根，并从集合中推导出那些补丁位。
  2. 通过各自一个遍历器从语法树推导“当前语句中什么是活跃的”和“此位置之后还使用了什么”，而不是维护栈结构。
  3. 将生命周期分析从类型检查中拆分出来，并作为基于摘要的按环不动点计算来运行，从而删除检查进行中（in-progress）的整套机制。
  4. 使用一套统一的存储/副作用模型（区域，regions），由检查器、BCE 和代码生成共享。
  5. 将类别比较从特化键中移出，变为调用点义务（call-site obligations）。

<a id="2-what-is-tracked-where"></a>
## 2. 状态追踪位置

下表按所属结构体分组列出了分析状态。修复通常都会改动下面这些字段。

| 所属对象 | 字段 | 用途 |
|---|---|---|
| `Prov`（ast.h，11 个字段） | `root`, `rootexact`, `rootfrom`, `writable`, `reusable`, `byteview`, `intogs`, `cyclelocal`, `hidesclass`, `slotread`, `reached` | 引用指向何处，以及合并或回读时丢失了什么信息 |
| `Val`（额外增加 13 个） | `holderroot/holderexact/holderset/holderfrom`, `lvalue`, `storagebranches`, `implicitcopy`, `nonneg`, `unsized/unsizedparam`, `strlit/emptyarr/isnull` | 持有者（holder）内容、拷贝语义、常量 |
| `VarDef`（17 个分析字段） | `ref`（一个 `Prov`）, `refrootknown`, `refprebound`, `refidentityused`, `contentroot/contentexact/contentset/contentbyteview`, `rootalias`, `classfrom`, `poolclass`, `classpool`, `growshrink`, `viewslot`, `captured`, `markof`, `nonneg` | 变量的绑定、其内容，以及类别根所代表的含义 |
| `RootArg`（14 个） | `cls`, `depth`, `depthkey`, `writable`, `reusable`, `growshrink`, `byteview`, `viewslot`, `intogs`, `exact`, `heldexact`, `concrete`, `via`, `pool` | 特化键视角下对单个参数的描述 |
| `FnSpec` 摘要（17 个） | `shrinkexternals/params`, `shrinkexternalbounds/parambounds`, `unbalancedshrink`, `liveshrinks`, `growexternals/params`, `classevents`, `eventstart`, `retroots`, `reboundoptionals`, `narrowedenv`, `needs`, `neededges`, `litadapts`, `litflows` | 函数体的行为，调用者必须重放这些行为 |
| `RetAlt`/`RetRoot`（19 个） | `alts`, `pred`, `seeded`, `predunknown`, `predlost`, `used*`（6 个）, `usedthreads`, `byteview`, `set` | 返回根以及环预测的收回状态 |
| `StoreEvent`（9 个）, `LiveShrink`（10 个）, `BoundShrink`, `ShrinkBalance` | | 存储记录以及由调用者判定的缩容对 |
| `TypeCheck` 单遍状态（≈30 个） | `heldtemps`, `shrinkrest`, `invalue`, `inreturn`, `curdst`, `constslot`, `argpath`, `storeevents`, `pendingshrinks`, `loopassigned`, `shrinkcache`, `growcache`, `cyclestores`, `threadedclasses`, `assumedshrinks/open/specs`, `livecapture`, `guessedshrink`, `cyclesites`, `growlog`, `growconflicts`, `namedoutside`, `litchecks`, `cyclecache`, `fitnode`, `fitfail` | 飞行中状态（in-flight state） |
| `BCE` | `Flow`（事实 + 代次）, `places`, `addrof/intvars/wdecl/wbad/wkinds/relpids`, `cands/ge0/lelen/mono`, `effects`, `ifaces/sites/nsites/opaquesites`, `callees/calleesfirst` | 自身的分析域、别名及摘要 |
| `Optimizer` | `facts`（每个 VarDef 的写入/取地址）, `consts`, `inlineinfo`, `postorder`, `viewed` | |
| `CodeGen` | `SpecInfo`（自由变量、全局变量、`needssp`）, `nrvo`, `toporder/growth/loopparent`, `TopCachePlan`, `cscopes` 水位线, `views` | |

`ast.h` 中声明了 119 个 `bool` 字段；上述结构体大约占了其中的一半。

<a id="3-inventory"></a>
## 3. 清单

质量评级：**A** 结构稳固，不太可能出现漏洞；**B** 可用，但存在一定堆积；**C** 补丁堆积，容易出现漏洞；**D** 结构脆弱。“信号（Signal）”指 `test/errors_tc` 中提及该规则规范章节的测试夹具数量，以及自 2026-09-12 以来涉及该规则的提交标题数量。

| # | 分析项 | 规范章节 | 主要代码 | 函数体行数 | 质量 | 信号 |
|---|---|---|---|---|---|---|
| 1 | 类型、泛型、实例化、重载、分派、内建函数、控制结构 | §3, §6, §7.1–7.7, §8 | `typecheck_types.h`, `typecheck_calls.h`（解析部分）, `typecheck_builtins.h`（`CheckBuiltin`）, `typecheck_nodes.h`, `typecheck_flow.h`（控制结构） | ~5,200 | B+ | 改动少 |
| 2 | 根来源（provenance）与合并值 | §9.2 | `Ident::Check`, `CheckRefOf`, `DecayRef`, `SlotView`, `MergeVals`, `Hides`, `InnerRoot`, `TempRoot`, `TempCopy`, `CheckBranchRoot`, `CheckBindingRoot`, `BindProv`, `RefProvOf`, `CheckRefRebindRoot`, `PrebindLoopRefs` | ~1,000（+`Prov`/`Val`/`VarDef` 状态） | C | §9.2：91 个测试夹具；22 个提交提及 roots |
| 3 | 回读根 | §9.5 | `RootCandidates`, `ReadBackRoot`, `ReadBackLVal`, `ContainerRead`, `TempContents`, `CanContain`, `ReachesThroughRefs`, `VisibleVars` | ~250 | B- | 16 个提交提及 read-backs/slots |
| 4 | 存储规则与存储记录 | §9.2 store, §5.1 | `FitsAt`（存储部分，约 100 行）, `RecordStore`, `AddStoreEvent`, `NoteContentRoot`, `ApplyCalleeStores`, `ShrinkTargets`, `ClassArgRoot` | ~250 | C+ | 11 个提交 |
| 5 | 可增可缩（grow-shrink）存储排除（污点、槽位读取） | §5.2 | `GrowShrinkTaint`, `StoredIntoGrowShrink`, `IntoGrowShrink`, `GrowShrinkCanHold`, `SlotReadable`, `SlotReadMayRetarget`, `RefMayPointInto`, `RefMayRetarget` | ~110 | C | §5.2：158 个测试夹具 |
| 6 | 函数体内的缩容检查（grow-only、grow-shrink） | §5.1, §5.2 | `CheckGrowShrink`, `GrowOnlyShrinkAt`, `CheckShrinkHolders`, `CheckHeldShrinks`, `HeldRefsMayPointInto`, `HolderMayPointInto`, `ResolvePendingShrinks`, `ShrinkThrough`, `ShrinkMayFree`, `UsedAfter`, `HoldValue`/`HoldLocation`/`HoldSequence` | ~440 | C- | §5.1：65 个测试夹具；225 个夹具文件名包含 "shrink" |
| 7 | 缩容摘要、调用者判定的配对、平衡调用 | §5.1 calls, §5.2 balanced | `NoteRootEvent`, `NoteShrink`, `ApplyCalleeShrinks`, `NoteLiveViews`, `NoteLiveShrink`, `ApplyCalleeLiveShrinks`, `ResolveCycleSites`, `ResizesToMark`, `SamePath`, `KeepShrinkChecks`, `SettleAssumedShrinks`, `SyntacticShrinks`, `ScanReceivers` | ~560（+`LiveShrink`, `AssumedShrink`, `CycleSite` 状态） | D | 14 个提交；仅 `ApplyCalleeShrinks` 就达 183 行 |
| 8 | 构造过程中的增长、整体赋值过程中的使用 | §1.3(4), §4.4 | `NoteGrow`, `CheckGrowsSince`, `ApplyCalleeGrows`, `SyntacticGrows`, `MayAliasRoots`, `ResolveGrowConflicts`, `CheckBuiltUses`, `ReachesBuilt`, `EachUse`, `FieldsApart`, `NamedOutside`, `BuiltInPlace`, `AppendedRun/Copies` | ~310 | B- | §1.3：14 个，§4.4：17 个夹具；15 个提交提及 growth |
| 9 | 特化键与根类别 | §3.4（实现说明）, §10.2 | `GetOrCreateSpec`（239 行）, `CheckSpecBody`（类别根）, `ClassDepth`, `EnvReach`, `SettleParamRootExactness`, `CanonRoot`, `UltimateRoot` | ~690（+`RootArg`） | B- | 键包含 9 个部分外加单独比较的 `depthkey`；`exact`/`concrete` 在事后按位与合并 |
| 10 | 递归：环、环返回根预测、线程化类别与池类别、环存储 | §7.8 | `typecheck_cycles.h`（全部，690 行）, `ValidateCycle`, `JoinCycle`, `ValidatePoolArgs`, `ValidateThreadArgs`, `NoteThreadedClass`, `ThreadStorable`, `RelyOnThread`, `Unthread`, `CycleStorable`, `CheckCycleInit`, `InProgressRebinds`, 每个 `ApplyCallee*` 的 `inprogress` 分支 | ~770（+约 600 行分布在其他函数中） | D | §7.8：58 个测试夹具；12 个提交 |
| 11 | 返回根、推断结果、远距离返回 | §9.2 return, §7.9 | `RecordReturn`, `RetAltVal`, `CallResult`, `CheckReturn`, `CheckInferredResult`, `ValidateNeeds`, `AddNeed` | ~360 | B- | 本周 4 个提交 |
| 12 | 流状态：确定赋值、可选类型收窄 | §3.8 | `SaveFlow/RestoreFlow/MergeFlow`, `NarrowCond`, `CollectAssignedNames`, `KillNarrowingsAssignedIn`, `FinishLoopNarrowing`, `ApplyCalleeRebinds`, `ExternalOptionals` | ~250 | B | 基于名称的循环规则；每个分支的快照开销为 O(变量数) |
| 13 | 拷贝语义与引用透明性 | §4.1, §3.8 | `CheckValue`, `DecayRef`, `KeepsRef`, `AutoRef`, `BindsRef`, `ImplicitCopy`, `CheckBranchCopy`, `BindBranchesByRef`, `UnrefForValueParam`, `IsOwnLocal`, `inreturn`, `argpath` | ~170 | B- | §4.1：35 个夹具；标志位 `argpath`, `storagebranches`, `implicitcopy`, `inreturn`, `branchcopy`, `inferred`, `synth` |
| 14 | 可写性 | §9.5 | `constslot`, `Prov::writable`, `PointeeWritable`, `WholeWritable`, `NoLetAssign`, `NoCopyWrite`, `RootArg::writable` | ~60 | A- | §9.5：62 个夹具（主要测回读，而非 const 性） |
| 15 | 相对引用与池 | §3.9 | `ResolvePools`, `PoolOf`, `ValidatePool`, `FitsAt`（相对引用部分）, `CheckSelfInit`, `RootedAtReceiver`, `classpool` | ~130 | A- | §3.9：29 个夹具，稳定 |
| 16 | 格式化重载副作用 | §3.7 | `CheckPrintable`, `CheckRenderable`, `UserFormatIn`, `heldtemps.render`, `shrinkrest` | ~110 | B- | 8 个提交 |
| 17 | 字面量参数 | §7.7 | `RecordLitAdapt`, `NoteLitArgs`, `VerifyLiterals`, `LitAdapt/LitFlow` | ~50 | B+ | 稳定 |
| 18 | 持有临时值与活性（支撑 6–8） | §5.1 | `heldtemps`, `TempScope`, `shrinkrest`, `RestScope`, `blockpos`, `UsedAfter`, `MentionsName`, `invalue`, `loopassigned` | ~120 | C+ | 在约 12 个位置压入 |
| 19 | 边界检查消除 | §10.5 | `bce.h` | ~2,300 | A- | 共 8 个提交；最近两个修复了操作数类型 |
| 20 | 优化器事实：可达性、写入/取地址、常量、内联分类、拷贝视图、基础情形及尾递归消除（TRE）资格 | §4（实现说明） | `Reach`, `Analyze`, `Scan`, `OptViewed`, `NamesStorage`, `CodeFree`, `BaseOK`, `Pure`, `RootStable`, `Rebindable` | ~230 / ~2,000 | B+ | `OptViewed` 是对树上未记录的检查器决策所做的补丁 |
| 21 | 代码生成：自由变量、全局变量与 `k_sp` 需求（不动点）、栈分配、栈顶缓存、NRVO、求值快照 | C.2, C.3, §6.10（实现说明） | `CollectSpecs`, `PlanTopClasses`, `RefTopsOk`, `PlanTopCaches`, `ExpandTopMarkers`, `SyncReach`, `DetectNrvo`, `NamedResult`, `Snapshot`, `AllocStk/SaveBase`, `HoistAggregateDecls` | ~520 / ~9,500 | B（标记展开为 C） | 17 个提交提及栈/缓存 |

除代码本身外，`docs/implementation.md` 还需要 800 行（§3.4–3.11）来描述分析 2–11，`test/errors_tc` 包含 647 个测试夹具（8,400 行），`test/lifetimes` 包含 51 个测试夹具（2,400 行）。任何重构都依赖这套测试套件。

<a id="4-per-analysis-notes"></a>
## 4. 各项分析说明

每项说明涵盖该分析追踪什么、如何实现、漏洞从何而来以及改进方案。

<a id="41-root-provenance-2"></a>
### 4.1 根来源（2）

**追踪内容。** 对每一个类引用值：限定被指向对象生命周期的变量（`root`）、该变量是否拥有它（`rootexact`），以及沿编译期调用路径的偏序关系“A 存活期长于 B 当且仅当 `depth(A) <= depth(B)`”。临时值会获得一个属于自己的 `VarDef`，其作用域更深一层。

**实现方式。** 每个值只有一个根。当分支、重绑定、调用返回或分派分支使一个值具有多个可能的根时，`InnerRoot` 会保留最内层的那个根。附加字段用于记录这一选择原本会丢失的信息：

* `intogs`：该值可能指向一个所保留的根并不持有的 grow-shrink 数组。

* `cyclelocal`：它可能以环（cycle）中不存储任何内容的位置为根。

* `hidesclass`：它可能是一个参数类别（parameter class）的被指向对象。

* `rootfrom`：不精确回读所来自的容器，用于报错诊断以及存储记录的 `src`。

* `slotread`：每一个备选项都是从存储中加载出来的。

该记录还包括 `byteview` 和 `reached`。持有者（holder）值还携带第二个根（`holderroot`），变量则携带第三个根（`contentroot`）。

**漏洞。** 每个标志位都必须穿过约 25 个转换点并得以保留：`MergeVals`、`DecayRef`、`SlotView`、`ReadBackLVal`、`DerefLValue`、`ContainerRead`、`RetAltVal`、`CallResult`、`RecordReturn`、`CheckRefRebindRoot`、`GetOrCreateSpec`（转换为 `RootArg`）、`CheckSpecBody`（转换为类别根）、`TempCopy`、`NoteLitElem`、`CheckFor`/`CheckMatch` 绑定器、`FitsAt`（整数组转切片）、`LoadSliceArgs`、`HoldValue`、`AppendedCopies`、`CheckBreak`、`TryDispatch`、`Ident::Check`、`CheckRefOf`。提交“keep what a merged value may be when only one root is kept”（365b87c）、“root a construct's by-value result at a temporary of its own”（b7c6de1）、“root a function value's result at a temporary”（bed55a1）以及 `klang-provenance-consumers` 检查清单都属于这一类漏洞。`RetRoot::alts` 和 `CycleRoots::RootSet` 已经是集合；`CallResult` 会映射每个备选项，然后再用 `MergeVals` 将它们折叠回去。

**改进方案。** 将备选集作为底层表示：`Prov` 持有一个 `{root, exact, bound-type}` 的小向量，合并即集合并集。各标志位变成在消费端对集合的查询：“可能指向 grow-shrink 数组”就是“存在某个备选项的根是该类数组”，“可环存储”就是“所有备选项都可环存储”，“隐藏了一个类别”就是“存在某个备选项是类别根”，`rootfrom` 则是该备选项自身的记录。存活期检查只需遍历所有备选项。这会消除五个标志位及其传播逻辑，任何合并值的新语言结构都能直接通过集合并集获得正确结果，无需额外代码。`holderroot` 也可以是持有者内容上的同一种集合。这是所有其他建议的前提条件。

<a id="42-read-back-roots-3"></a>
### 4.2 回读根（3）

**追踪内容。** 从容器中加载出来的引用指向何处。

**实现方式。** 根是在加载点通过枚举当前作用域中按值类型能持有该被指向对象的变量（`RootCandidates` 遍历 `CanContain`），再加上全局变量、静态数据以及引用可通达此处的参数类别根，重新推导出来的。取最深的候选者，且仅当候选者唯一时才是精确的（exact）。临时值则以其持有者根作答。

**评估。** 这项分析原理清晰且规模不大。其弱点在于它是一种在使用点查询的、基于类型的近似推断：它依据使用点时的作用域状态进行推理，而不是依据实际被存进去的东西，与此同时存储记录（4.3）又精确地记录了实际存进去的内容。两者不得不在 `RecordStore`（通过 `rootfrom` 传递 `src`）和 `HolderMayPointInto` 中手工协调。

**改进方案。** 有了根集合（4.1）和由存储记录维护的每容器内容集合后，从当前活跃帧（activation）的局部容器进行回读的结果，就是该容器的内容集合。对于当前活跃帧无法窥见内部的容器（参数、全局变量），则回退到基于类型的枚举。规则保持不变，但只保留单一事实来源。

<a id="43-store-rule-and-store-record-4"></a>
### 4.3 存储规则与存储记录（4）

**追踪内容。** 所存储的引用存活期长于其目标位置。为了服务缩容规则，它还追踪每一次向容器中存储引用或持有者的操作（`storeevents`，一个全程序范围的列表；以及每容器的 `contentroot`）。

**实现方式。** `FitsAt` 对每一个进入有类型槽位的值执行规则检查，针对一个不精确目标可能代表的每一个存储位置（`ShrinkTargets` 遍历 `Dest::reached`）。通过参数的类别进行的存储会保存在特化上（`classevents`），并在每次调用时重放（`ApplyCalleeStores`），在重放处再次拓宽。回边则保守地将每个引用参数记录为被存储进每个容器参数。

**评估。** 规则本身没问题。问题在于记录是一个扁平列表，每次查询都要线性扫描（`HolderMayPointInto`、`EachHolderRoot`），并带有用于追踪拷贝的 `src` 链。`reached` 和 `bound` 是本月新加的，用来拓宽不精确的目标。最近的两个修复（3362c37、12e291b）针对的都是不精确目标应当枚举哪些存储位置。

**改进方案。** 保留该记录，但改为每容器的内容集合（与 4.1 相同的集合类型），在存储时更新，并将拷贝记录为指向源容器集合的引用。`ShrinkTargets` 和 `RootCandidates` 随后就合并为同一个函数：“类型为 T 的根可能代表的存储位置”。

<a id="44-grow-shrink-storage-exclusion-5"></a>
### 4.4 可增可缩存储排除（5）

**追踪内容。** 确保没有任何可能指向 grow-shrink 数组的引用会被存储起来，从而使 §5.2 的缩容扫描只需查看变量即可。

**实现方式。** `GrowShrinkTaint` 先检查根，再检查 `intogs`，再检查指向切片槽位的引用。`slotread` 表示某个值来自存储，因而是干净的。`SlotReadMayRetarget` 和 `RefMayRetarget` 会重新放行那些后续循环重绑定可能使其移动的 `var` 引用。

**评估。** 这项分析规模较小，却是最近漏洞最多的一类（`slotread`：1a06b13；`viewslot`；键上的 `intogs`）。归根结底又回到了“这个值究竟有哪些备选项”。

**改进方案。** 将该分析吸收进 4.1。污点变成“某个备选项是 grow-shrink 根”，干净变成“每个备选项都是从存储中读取出来的”，两者都直接从集合推导得出。

<a id="45-shrink-checks-within-a-body-6"></a>
### 4.5 函数体内的缩容检查（6）

**追踪内容。** 在发生缩容时，追踪每一个可能指向该数组的活跃对象：当前语句中持有的临时值、作用域内的引用变量和切片变量（以及指向切片或持有者的引用所能到达的对象）、通过其存储记录追踪的持有者，以及在整套程序检查完成后通过其存储记录追踪的其他全局变量（`CheckGlobalShrinks`）。

**实现方式。** 一共有三次扫描（`CheckHeldShrinks`、遍历 `vars` 的 `GrowOnlyShrinkAt`、遍历 `VisibleVars` 的 `CheckShrinkHolders`），每次扫描都有自己的一套“是否可能指向”的分支分析（`RefMayPointInto`，以及仅自身就有八种结果的 `HeldRefsMayPointInto`）。活性来自 `UsedAfter`（语句之后的语法提及、`shrinkrest`、循环），而 `pendingshrinks` 用于处理循环体中更靠后位置发生的存储。

**评估。** 这是打补丁最多的区域。grow-only 数组和 grow-shrink 数组使用不同的扫描，字节视图（byte views）和槽位读取（slot reads）各自拥有豁免逻辑，而“通过不精确根”又把扫描乘上了 `ShrinkTargets`。

**改进方案。** 统一为一次扫描：对每一个活跃视图（变量加上该语句中更早的操作数，见 4.14），将其备选集与被缩容数组的备选集求交集。grow-only 与 grow-shrink 之间唯一的区别在于是否包含持有者的内容集合。八分支的情况分析会坍缩为集合求交加上现有的两条豁免规则（字节视图、相对引用）。

<a id="46-shrink-summaries-caller-judged-pairs-balanced-calls-7"></a>
### 4.6 缩容摘要、调用者判定的配对、平衡调用（7）

**追踪内容。** 函数体缩容了什么（按参数、外部变量或绑定类型）、哪些仍在使用的视图只有其调用者能将它们与被缩容数组区分开来（`liveshrinks`），以及哪些缩容属于平衡调整（balanced resizes）。

**实现方式。** 摘要保存在 `FnSpec` 上，并在调用点由 `ApplyCalleeShrinks`（183 行）应用：它会展开摘要、依据别名情况判定是否平衡，然后决定立即扫描或延后处理。配对会在每次调用以及环闭合时（`cyclesites`）通过参数进行映射。回边被假定为平衡的；它跳过的检查无论如何都会执行，通过捕获 `CompileError` 来捕获错误（`KeepShrinkChecks`），并在环闭合时结算。

**评估。** 这块代码在结构上是脆弱的。它包含三套相互交织的延后机制（待处理缩容 `pending shrinks`、环调用点 `cycle sites`、假定缩容 `assumed shrinks`）、基于异常的错误捕获，以及一个用于调整报错措辞的 `guessed` 标志位。`liveshrinks` 的思路是正确的（记录一条义务，交由调用者判定），但它只应用在了这单一条规则上。

**改进方案。** 将“义务”确立为通用机制（第 5 节第 5 项），并按环通过不动点计算摘要（第 3 项），从而移除所有延后机制。于是摘要只包含被缩容的区域及其平衡性，以及那些配对。

<a id="47-growth-during-construction-uses-during-whole-assignment-8"></a>
### 4.7 构造过程中的增长、整体赋值过程中的使用（8）

**追踪内容。** 在某个数组中就地构造值时没有任何操作会增长该数组，以及整体赋值（whole assignment）的右侧不会使用它正在重建的数组。

**实现方式。** 在表达式之后对照被构造的根检查每个活跃帧的日志（`growlog`）（`CheckGrowsSince`），期间使用 `MayAliasRoots`。两个参数类别之间的冲突被延后到最后处理（`growconflicts`、`ResolveGrowConflicts`）。被调函数通过 `growparams`/`growexternals` 贡献增长记录，若仍在检查中则通过其文本贡献。`CheckBuiltUses` 遍历右侧表达式以及每个被调函数的 `NamedOutside`。

**评估。** 这项分析是合理的。`MayAliasRoots` 是第四个别名判定器（另外三个是 `RefMayPointInto`、BCE 的 `AffectedByWrite`、代码生成的 `RefTopsOk`）。

**改进方案。** 基于备选集计算别名关系，并将“增长”作为共享摘要中的又一种副作用来处理（第 4 项）。

<a id="48-specialization-keys-and-root-classes-9"></a>
### 4.8 特化键与根类别（9）

**追踪内容。** 哪些调用点可以共享同一个经过检查的函数体。

**实现方式。** 键包含类型、绑定、字面量参数、函数值、收窄后的环境、`needs`，以及每个参数对应的 `RootArg`（包含类别编号、`depthkey`、可写性、池，以及四个 grow-shrink 和视图标志位）。`exact` 和 `concrete` 在事后跨调用点按位与合并，并由 `SettleParamRootExactness` 结算。回边无论其根是什么，都会复用正在检查中的函数体。

**评估。** 这部分代码注释很好，但特化键混用了两种策略：函数体为了内联回答比较所需的事前事实（`depthkey`、`growshrink`、`heldexact`、`viewslot`、`intogs`）以及事后才结算的事实（`exact`、`concrete`、`growconflicts`）。每当出现新漏洞时，就会引发一场关于该把它放在哪一侧的争论（`klang-spec-key-depth-ties`、`klang-holder-param-exactness`）。更大的键还会成倍增加需要检查的函数体数量。

**改进方案。** 即第 5 项：在键中只保留类型、可写性和池，让函数体记录关于其类别根的义务（`A outlives B`、`A ≠ B`、`A holds no grow-shrink array`），由调用点去清偿，就像今天 `liveshrinks` 所做的那样。随后就可以移除 `depthkey`/`EnvReach` 以及 `exact`/`concrete` 结算逻辑。

<a id="49-recursion-10"></a>
### 4.9 递归（10）

**追踪内容。** 哪些函数位于同一个环中；在返回尚未被检查之前，回边返回结果的根；一个环允许通过哪些参数类别进行存储（池、线程化类别）；在函数被识别为处于环中之前已经发生的存储；跨越进入环的调用期间持有的非固定局部变量。

**实现方式。** 通过对返回语句进行语法不动点迭代（`CycleRoots`，690 行）来预测根。真正的返回会对照预测结果进行检查，并带有收回规则（`ReturnConflict`、`RetRoot::used*`、`predlost`）。线程化类别通过继承者（heirs）、`relied` 和追溯性的 `Unthread` 错误来追踪。`cyclestores` 会被 `JoinCycle` 追溯性地拒绝。每个 `ApplyCallee*` 都有一个使用语法扫描的 `inprogress` 分支（`SyntacticShrinks`、`SyntacticGrows`、`InProgressRebinds`、`NamedOutside` pending）。§7.8 的保守限制列在 `implementation.md` §10 中。

**评估。** 这是整个编译器中最难懂的代码，而这一切都源于“在首次到达该函数的调用内部只检查一次函数体”。BCE 和代码生成面临同样的递归问题，它们各自用约 30 行的基于摘要的不动点就解决了（`ComputeEffects`、`CollectSpecs`）。

**改进方案。** 即第 3 项：将生命周期分析放到类型检查之后运行，按强连通分量逐个处理，用乐观摘要反复迭代环内的函数体直到稳定。预测、收回、线程化类别继承者、环存储、假定缩容以及所有语法回退逻辑都将被移除。§7.8 剩下的只有栈规则（`JoinCycle` 的非固定局部变量检查）和池参数规则。

<a id="410-return-roots-11"></a>
### 4.10 返回根（11）

**追踪内容。** 函数返回值可能具有的每一个根，并在每次调用时进行映射。

**实现方式。** `RetRoot::alts` 为每个不同的根保存一个 `RetAlt`，带有按位与合并的保证。`RetAltVal` 将类别映射到参数的根，而 `CallResult` 用 `MergeVals` 合并映射后的备选项。远距离返回（long-distance returns）目前受限于全局变量（TODO 0d）。推断结果遵循 `let` 规则。

**改进方案。** 有了 4.1 之后，结果直接就是映射后的集合，无需再合并，`RetAlt` 和 `Prov` 备选项将合二为一。远距离返回随后可以使用真正的规则（“以目标帧或其上层帧为根”）。

<a id="411-flow-state-definite-assignment-and-narrowing-12"></a>
### 4.11 流状态：确定赋值与收窄（12）

**实现方式。** 作用域中的每一个变量都会在每个分支处被拍下快照，快照通过 `MergeFlow` 进行合并。循环会清除循环体所重绑定的名称上的收窄状态。这些名称是通过一个按名称追踪调用的语法扫描（`CollectAssignedNames`）以及被调摘要（`reboundoptionals`）找出来的。当该扫描漏掉了通过调用发生的重绑定时，`FinishLoopNarrowing` 就会报错。

**评估。** 这套机制是可行的。但基于名称的循环规则以及“如果猜错了就报错”的模式，与 `PrebindLoopRefs` 和 `refidentityused` 一样，都是对循环只做单遍扫描的同一种症状。每个分支的快照开销都是 O(作用域内变量数)，这正是深层调用路径耗费数 GB 内存的原因。

**改进方案。** 仅对当前语言结构触及的变量做快照（通过对分支进行预扫描，或使用写时复制），并用两轮检查（第 6 项）处理循环，以替代名称扫描。

<a id="412-copy-semantics-and-reference-transparency-13"></a>
### 4.12 拷贝语义与引用透明性（13）

**实现方式。** `CheckValue` 会在目标位置不保留引用时将引用退化，将左值改写为 `&x`（`AutoRef`、`synth`），并通过 `inreturn`、`argpath`、`storagebranches`、`implicitcopy`、`branchcopy`、`inferred` 来决定隐式拷贝。调用会检查参数两遍（第一遍不带参数类型，第二遍带上参数类型），而 `BindBranchesByRef` 会在引用参数处重新检查语言结构。

**评估。** 最近的几次修复都在参数的两阶段检查以及将语言结构作为参数处理上（4c05a0f、fe901f5、3259cd7）。为单条规则使用六个上下文标志位是一种坏味道。

**改进方案。** 在进入第二阶段之前，在一个函数中根据参数类型和参数的形态（左值、引用、左值构成的结构）对每个参数一次性决定“按值还是按引用”，并将该决定记录在 AST 节点上；那些标志位将成为该函数的局部变量。

<a id="413-writability-14-relative-references-15-literal-parameters-17"></a>
### 4.13 可写性（14）、相对引用（15）、字面量参数（17）

这三项分析既稳固又小巧。可写性由类型中的一个标志位外加 `const` 组成，并只有一个强制检查点（`constslot` 下的 `FitsAt`）。相对引用需要根身份并查询 `rootexact`，这正是正确的依赖关系。字面量参数采用了干净的“记录并验证”设计。不需要改动。

<a id="414-held-temporaries-and-liveness-18"></a>
### 4.14 持有临时值与活性（18）

**追踪内容。** 在当前语句更早阶段求值且依然活跃的值（`heldtemps`）、语句中在缩容之后运行的部分（`shrinkrest`），以及某个变量之后是否还会被提及（`UsedAfter`）。

**实现方式。** 持有临时值在约 12 个位置被压入（`CheckArg`、索引和切片的接收者、赋值目标位置、`Binary` 操作数、`==` 视图、渲染、分派参数、字面量字段、返回），并由 `TempScope` 弹出。每次发生缩容时，`UsedAfter` 会用 `MentionsName` 重新扫描块，并按名称追踪嵌套函数。

**评估。** 只要有位置漏掉压入就会产生漏洞（7027e7f、5896dbd、756e1fe），而 `shrinkrest` 之所以存在，是因为 `heldtemps` 看不到当前语句的右侧部分。

**改进方案。** 两者都用各自的遍历器从语法树推导：“在同一条语句中先于节点 N 求值的引用类型子表达式，按求值顺序排列”以及“在外围块中位置 P 处及之后被提及的变量”，对每条语句或每个函数体计算一次（一次逆向遍历即可在 O(n) 时间内给出每个位置的后续使用集合）。这样就不再有容易遗漏的压入点，而且求值顺序也只在一个函数中定义，并与代码生成的 `Snapshot` 共享。

<a id="415-bounds-check-elimination-19"></a>
### 4.15 边界检查消除（19）

BCE 是一个自包含的差分约束分析域，具有代次、带最终所有者的存储位置（places）、失效清除（kills）、求解至不动点的每特化副作用摘要、来自调用点的入口事实、已记录的不变量以及循环视图外提。它的结构稳固，已知缺口都有文档记录，改动频率很低。它消费检查器的来源信息（`ref.root`、`rootexact`、类别根），并自行重新推导别名和副作用。不需要结构性改动；它是第 3 项的参考范本，也是第 4 项的消费者。

<a id="416-optimizer-analyses-20"></a>
### 4.16 优化器分析（20）

优化器的分析包括 `Analyze`（每个变量的写入与取地址，流不敏感）、带使用计数的可达性、带 C 嵌套深度的内联分类、`OptViewed`（当常量折叠将一个值化简为存储位置时，重新物化检查器原本以临时值为根的那个值），以及用于基础情形内联和尾递归消除的纯度检查。它们都很稳固。`OptViewed` 在语法上重新推导检查器已经做过的决策（`TempCopy`）；若在节点上记录“该值是一个临时拷贝”，优化器就能直接读取它。

<a id="417-codegen-analyses-21"></a>
### 4.17 代码生成分析（21）

`CollectSpecs` 通过调用图上的不动点计算自由变量、触及的全局变量以及是否需要 `k_sp`（这是编译器第三次计算调用图）。栈顶缓存按函数和栈类别做决策（`PlanTopClasses`、读取 `RootArg::exact`/`concrete` 的 `RefTopsOk`），规划区域，然后改写已生成的 C 文本：`ExpandTopMarkers` 从生成的文本中解析 `goto` 和标签来安放刷新操作（flushes），而 `HoistAggregateDecls` 作为文本移动声明以绕过一个 MSVC bug。NRVO 是对返回语句进行的语法扫描。

**改进方案。** 将代码生成输出到一个带有显式循环和刷新节点的极简语句列表中，并从该列表而非文本进行展开（中等工作量，隔离良好）。在 `RefTopsOk` 和 `SyncReach` 中消费共享的存储摘要（第 4 项）。

<a id="5-cross-cutting-findings-and-recommendations"></a>
## 5. 跨模块发现与建议

按单位投入预期减少的未来改动量排序。工作量是所涉及代码行数的大致规模；“删除”列出将被移除的内容。

<a id="1-alternative-sets-instead-of-one-root-plus-bits-medium-effort-highest-payoff-per-line"></a>
### 1. 用备选集替代单一根加标志位（中等工作量，每行代码的收益最高）

用由 `{root, exact, fromcontainer, slotread}` 构成的小型备选集替换 `Prov::root/rootexact/rootfrom/intogs/cyclelocal/hidesclass/slotread`，并通过集合并集进行合并。消费者遍历备选集：存活期更长意味着每一个备选项存活期都更长；污点意味着存在某个备选项是 grow-shrink 根；可环存储意味着每一个备选项都可环存储；类别隐藏意味着存在某个备选项是类别根。`holderroot` 变为内容集合；`RetAlt` 与 `CycleRoots::RootSet` 成为同一类型。该改动会机械地涉及约 1,500 行代码（`MergeVals`、`Hides`、`InnerRoot`、`GrowShrinkTaint`、`RetAltVal`、`CallResult`、`CheckRefRebindRoot`、`RootArg` 及各类扫描）。删除：五个标志位、`Hides`、`InnerRoot` 的平局决胜规则、约 20 处的 `intogs`/`hidesclass` 传播逻辑，以及“合并值隐藏了 X”这一整类 bug。风险：低；测试夹具套件锁定了每一条规则。应最先做这一项；后续所有项目都以它为前提。

<a id="2-derive-statement-liveness-and-later-use-from-the-tree-small-effort"></a>
### 2. 从语法树推导语句活性与后续使用（小工作量）

用一个遍历“语句中先于 N 求值的操作数”的遍历器替换 `heldtemps` 的压入、`HoldValue`/`HoldLocation`/`HoldSequence`、`shrinkrest`/`RestScope`、`render` 和 `invalue`；用每个函数体预计算一张后续使用表替换 `UsedAfter`/`MentionsName`/`blockpos` 重扫描和 `loopassigned`。这会用约 150 行代码替换约 300 行代码，不再有容易遗漏的压入点，并只在一处规定求值顺序（代码生成的 `Snapshot` 应读取同一个函数）。风险：低。

<a id="3-split-lifetime-analysis-from-type-checking-fixpoint-per-cycle-large-effort-largest-deletion"></a>
### 3. 将生命周期分析从类型检查中拆分出来；按环求不动点（大工作量，删除最多）

类型检查保留其单遍流程，并按类型、绑定、字面量参数、函数值、可写性和池创建特化。生命周期分析（清单中的 2–11）随后在已完成的特化图上按被调优先顺序运行，以乐观摘要反复迭代每个递归环的函数体直到摘要稳定，正如 `BCE::ComputeEffects` 和 `CodeGen::CollectSpecs` 已经做的那样。删除：`typecheck_cycles.h`（预测与 `ReturnConflict`）、`RetRoot::pred/used*/predlost/usedthreads`、`cycleroot` 哨兵、线程化类别继承者/`relied`/`Unthread`、`cyclestores`、`assumedshrinks`/`SettleAssumedShrinks`/`KeepShrinkChecks`、`cyclesites`/`ResolveCycleSites`、`SyntacticShrinks`/`SyntacticGrows`/`ScanReceivers`、`InProgressRebinds`、`NamedOutside` 的 pending 分支，以及每个 `ApplyCallee*` 的 `inprogress` 分支。这大约有 1,500 行代码，同时 `implementation.md` §10 中列出的 §7.8 保守限制也将被规范的真正规则取代。代价：环内的函数体会被分析多次（受限于格高度，实际中为两到三轮），且分析需要自己遍历检查后的函数体（`Check` 方法仍保留用于类型检查）。风险：中等；它改变的是报错的时机，而不是哪些程序合法，因此测试夹具依然有效。前提：第 1 项。

<a id="4-one-storage-and-effect-model-for-checker-bce-and-codegen-medium-effort"></a>
### 4. 为检查器、BCE 和代码生成提供统一的存储与副作用模型（中等工作量）

为变量、类别根、临时值、全局变量和静态数据定义区域（regions）。为每个特化计算一份摘要，记录它到达、写入、调整大小（包括平衡性）、存入（包括存入的值）、增长以及返回哪些区域。摘要还记录重绑定的可选值、自由变量和被写入的整数。

随后三组辅助函数将查询这份摘要：检查器的 `NoteRootEvent`/`ShrinkTargets`/`RootCandidates`/`MayAliasRoots`/`NamedOutside`，BCE 的 `UltOf`/`Reach`/`AffectedByWrite`/`Effects`，以及代码生成的 `CollectSpecs`/`RefTopsOk`/`SyncReach`。这将移除三次调用图遍历和四套独立的别名分析。它与第 3 项完美契合，因为第 3 项会赋予检查器自己的摘要不动点。

<a id="5-obligations-instead-of-key-facts-medium-effort-after-3"></a>
### 5. 用义务替代键中的事实位（中等工作量，在第 3 项之后）

让特化键只保留真正改变函数体语义的内容（类型、绑定、字面量参数、函数值、可写性、池、`needs`）。让分析记录关于类别根的义务（`A outlives B`、`A is not B`、`A holds no grow-shrink array`、`A's contents are exactly one array`），由每个调用点用其具体根来清偿，从而泛化 `liveshrinks`。从 `RootArg` 中删除 `depthkey`/`EnvReach`、`growshrink`/`byteview`/`viewslot`/`intogs`/`heldexact`，并删除 `SettleParamRootExactness`、`growconflicts`/`ResolveGrowConflicts`，以及它们导致的多余特化分裂（今天一个函数如果一次用全局变量调用、一次用局部变量调用，就会生成两份函数体）。

<a id="6-loops-two-iteration-check-instead-of-name-scans-small-effort-independent"></a>
### 6. 循环：两轮检查替代名称扫描（小工作量，独立）

将循环体检查两遍，第二遍带上第一遍发现的绑定、存储和收窄状态（一次拓宽步骤），并在第二遍报告错误。删除 `PrebindLoopRefs`/`ResolvePrebind`、`refprebound`、`refidentityused`、`loopassigned`/`AssignedInEnclosingLoop`、`pendingshrinks`/`ResolvePendingShrinks`、`CollectAssignedNames` 的调用追踪，以及 `FinishLoopNarrowing` 的“猜测错误”报错。代价：循环体会被检查两遍（嵌套循环为 $2^{\text{depth}}$；可以通过在每一轮外层循环中仅检查内层循环一次，或通过记忆化，将每个循环限制在两轮内）。这一项可在第 3 项之前或之后进行。

<a id="7-smaller-items"></a>
### 7. 较小改进项

* 为每个参数一次性决定按值还是按引用（4.12）并记录在节点上；删去 `argpath`/`storagebranches`/`implicitcopy`/`branchcopy`。
* 在节点上记录“临时拷贝”标记，以便 `OptViewed` 直接读取（4.16）。
* 从语句列表而非文本中发出代码生成的栈顶缓存刷新（4.17）。
* 按语言结构而非按整个作用域来对流状态拍快照（4.11）。

<a id="6-sequencing-and-validation"></a>
## 6. 排序与验证

顺序：1 → 2 → 6 → (3 + 4 并行) → 5 → 7。第 1、2、6 项各自都是一周左右规模的改动，单独做就很有价值。第 3 + 4 项属于结构性重写，应仅在第 1 项落地后再开始，因为重写后的摘要是由集合构成的。

每一步的验证手段都是现有测试套件：`test/errors_tc`（647 个带预期报错信息的拒绝用例）、`test/lifetimes` 和 `test/expected`（输出固定的正向程序）、示例程序以及标准库，需在两种优化级别下并分别通过 TinyCC 进行验证。如果某项改动在另一行报错或报错措辞不同，需要更新预期结果。如果某项改动接受了旧编译器拒绝的程序，则需复核该测试夹具（`errors_tc` 中有许多拒绝用例本身就是保守限制，尤其是第 3 项会放开其中一部分）。每次落地前保留一份改动前编译器的临时构建产物，并对所有测试夹具和示例程序比对判定结果差异（`docs/testing.md`、`klang-branch-validation-snapshots`）。

<a id="7-status"></a>
## 7. 进展状态

* **1. 备选集** 已于 2026-09-25 落地：`ast.h` 中的 `Roots`/`RootAlt` 替换了 `root`/`rootexact`/`rootfrom`/`intogs`/`cyclelocal`/`hidesclass`/`slotread`，`Val::contents` 替换了 `holderroot`/`holderexact`，`VarDef::contents` 替换了 `contentroot`/`contentexact`/`contentset`。回读在读取点一次性枚举其候选作为精确备选项（§9.5），环预测器也单独持有返回值的每个备选项。`RootArg::intogs` 变为 `gsvia`；`InnerRoot`、`CanonRoot`（`rootalias` 从未被设置过）、`Hides` 和 `NoteContentRoot` 均已移除。在循环内部，当一个具有多个根的 `var` 被循环重绑定到一个语法扫描无法证明它原本就拥有的根时，它会被视作受其各个根约束（`loopretargets`）。这是针对以前被折叠的不精确根所提供保证的单遍替代方案；第 6 项也将其一并移除了。
* **6. 循环检查至不动点** 已于 2026-09-25 落地。`CheckLoopPasses`（`typecheck_flow.h`）会从循环入口与上一轮回边的并集出发重新检查循环体，直到没有任何回传给循环头的事实发生变化（`LoopPass`，位于 `BindProv`/`CheckRefRebindRoot`/`RecordStore` 的 `NoteFact`，以及对流状态的比较）。除最后一轮外，每一轮都是发现轮（discovery pass）。在发现轮中，在绑定之前被读取的变量没有根（`RefProvOf`，以及在向调用传入此类变量时使用 `RootArg::unknown`），缩容扫描会将其放行。已删除：`PrebindLoopRefs`/`ResolvePrebind`/`refprebound`、`RefExactOf`/`refidentityused`、`PushLoopAssigned`/`loopassigned`/`AssignedInEnclosingLoop`/`CollectAssignedBases`、`loopretargets`、`pendingshrinks`/`ResolvePendingShrinks`、`CollectAssignedNames`/`KillNarrowingsAssignedIn`、`NarrowedOptionals`/`CollectCheckedRebinds`/`FinishLoopNarrowing`、`SlotReadMayRetarget`、`EndLoop`。三个测试夹具现在会在使用点而非重绑定点或循环之后报错（`rebind_root_identity`、`optional_loop_fnval_rebind`、`optional_while_condition_value`）。没有外部变量重绑定或存储的循环只需一轮；测试套件的检查耗时保持不变。
* **3. 按轮次检查环** 已于 2026-09-25 落地，且未采用原提案中将生命周期分析从类型检查拆分的方案。原定通过拆分获得的不动点，改为通过再次检查环内函数体来实现。`CheckSpecBody` 会为环头重复运行 `CheckSpecBodyOnce`，直到没有任何成员的记录发生变化，而环成员会在调用到达它们时被重新检查（`FnSpec::stale`）。每一轮在回边处读取上一轮的记录（`FnSpec::prev`、`RecordOf`）。第一轮不读取任何记录，因此回边的结果尚未指向任何位置（`Roots::unknown`，与第 6 项发现轮中的底值相同，现在也作为持有者内容所携带的一个标志位），每条规则都会放行它。每一轮都会重复类型检查，但没有可测量的性能开销。已删除：`typecheck_cycles.h`（669 行：基于语法的返回根预测器、`ReturnConflict`）、`RetRoot::pred/used*`、`cycleroot` 哨兵、`cyclestores`（用设置了 `incycle` 的重检查替代了重放）、`ThreadedClass::relied`/`RelyOnThread`（重检查能直接发现类别被破坏）、`assumedshrinks`/`SettleAssumedShrinks`/`KeepShrinkChecks`/`SB_ASSUMED`/`livecapture`、`cyclesites`/`ResolveCycleSites`/`CycleOpen`、`SyntacticShrinks`/`SyntacticGrows`/`ScanReceivers`、`InProgressRebinds`、`LiveShrink::guessed`，以及每个 `ApplyCallee*` 的 `inprogress` 分支：共删除 719 行，新增 433 行。预测器的冲突报错（"use one source"、"back edges were not given"）已经消失。环的结果是其所有返回值的并集，并且像任何调用的结果一样在每个使用点进行检查。因此，有 13 个测试夹具现在会在使用点报错（存活期不足的存储、指向 grow-shrink 数组的视图），而另外两个原本因预测器自身不精确而被拒绝的程序现在被接受并成为正向测试夹具（`cycle_holder_unknown_store`、`cycle_threaded_return`）。环在两到三轮内收敛（`K_ROUNDS=1` 可追踪它们）。
* **4. 统一存储与副作用模型** 在第 3 项之后被重新评估。BCE 和代码生成是在优化后（内联和折叠之后）的树上做摘要，而在优化器运行之前生成的检查器记录无法替代那些摘要。区域词汇表（变量、类别根、临时值、静态数据）已经通过 `VarDef` 实现了共享。这一项保持原状未动。
* **5. 用义务替代键中的事实位** 在动手前进行了实测（2026-09-25，在所有示例和最大的测试上使用了临时的 `K_KEYSTATS` 计数）。没有任何函数的两个特化仅在深度键上存在差异。各个事实位（`growshrink`、`byteview`、`viewslot`、`heldexact`）在每个程序中只多分裂出 1 到 4 个特化（物理示例中 847 个里占 12 个），类别分组又多分裂出几个。该重构方案将把函数体中所有依赖类别的检查都移到调用点（存活期、grow-shrink 污点、精确性、缩容扫描的字节视图与视图槽豁免），连同报错和对应的测试夹具一起迁移，而换来的只是函数体数量减少 2%，因此最终没有实施。`RootArg::unknown`（第 3 项）是此后唯一新增的键标志位。
* **7. 较小改进项**（2026-09-25）：流快照改为仅覆盖保存与恢复之间的代码能够按名称引用的变量（`NamedFrames`、`SaveFlow`、`CheckSpecBodyOnce` 对外层收窄状态的恢复），而不是调用路径上的每一个变量。在一条 3,000 层嵌套调用链上实测，检查速度提升了五分之一，峰值内存没有变化，说明 §4.11 中提到的“数 GB”内存来自每个活跃帧的函数体和记录，而不是快照。按值/按引用决策（4.12）、供 `OptViewed` 读取的临时拷贝标注（4.16）以及代码生成的标记展开（4.17）均保持原状。那六个标志位中的前三个各自都曾是近期修复的对象，且已有测试夹具固定了行为。`OptViewed` 的语法测试（`r != n && NamesStorage(r)`）总是能与检查器的 `TempCopy` 位置匹配，因为折叠只会改写原本是语言结构或调用的节点，而其值必然是临时拷贝。文本展开则是孤立且正确的。
* **2. 从语法树推导语句活性** 已于 2026-09-25 落地，形式为 `typecheck.h`/`typecheck_exprs.h` 中的 `nodepath`（`PathEntry`、`NodeScope`）、`nodevals` 和 `ForOperands`。`HeldOperands` 替换了 `heldtemps`、`TempScope`、`HoldValue`/`HoldLocation`/`HoldSequence`、接收者与渲染压入操作以及 `render` 标志位（现为 `renderarg`/`renderwhere`）。`LaterOperands` 替换了 `shrinkrest`/`RestScope`，并为 `UsedAfter` 提供语句剩余部分。调用的两个阶段在一个地方统一处理：处于 `discovering` 重载阶段的调用不持有任何东西，而正在应用被调摘要的调用已经消费了它自己的操作数。`UsedAfter` 的块与循环重扫描（`blockpos`、`MentionsName`）暂留给第 3 项，该项的活性分析将把它们覆盖吸收。§5.1 的语法位置规则（`Call::standalone`、`invalue`）随后被替换为持有操作数、语言结构头部通往的部分（`AfterHead`），以及针对构造中值的缩容日志记录。
