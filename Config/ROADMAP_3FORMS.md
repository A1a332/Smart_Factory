# 无线终端 · 三形态路线图

> 用户决策（2026-10-03）：**「3个都要，逐步调整」** —— 建筑版、手持版、以及后续扩展全要，
> 分阶段做完。本文是执行依据，每完成一项就勾掉。

---

## 形态总览

| # | 形态 | 打开方式 | 本质 | 状态 |
|---|---|---|---|---|
| 1 | **固定终端**（现在这个） | 建个建筑，走过去按 E | `AFGBuildable` + 交互组件 | ✅ 已做完，编译 + 部署完成 |
| 2 | **手持终端**（便携终端） | 工作台做出来，放进手臂槽，掏出即开面板 | `AFGEquipment` 派生 | ✅ 已做完，编译 + 部署完成 |
| 3 | **扩展套件**（A+B+C+D） | 见下 | 开关远程控制 / 全局快捷键 / 三个升级建筑 / 多人共享 | ✅ 已做完，编译 + 部署完成（v0.2.0），**等用户实测** |

命名区分：形态 1 = **无线终端**；形态 2 = **便携终端**（或「手持终端」）。

---

## 形态 3 实施记录

用户决策（2026-10-03）：**「做ABD，C留下一个存储仓库」→「那就做完整的C」→「一起做了」**
⇒ C 恢复完整三件套；四项**全部按 FORM3_RECON.md 里的倾向方案**一次性实施，不再逐条确认。

| 项 | 选定的方案 | 关键落点 |
|---|---|---|
| **A** 远程开关 | 开合 + 调优先级，独立 `PowerSwitch` 分类，未接线给提示 | `AFGBuildableCircuitSwitch::SetSwitchOn` / `AFGBuildablePriorityPowerSwitch::SetPriority`（均 public，但状态是 Replicated ⇒ 必走服务端） |
| **B** 全局快捷键 | **零资产路线**：Slate `IInputProcessor`，`Ctrl+T` | `FSlateApplication::RegisterInputPreProcessor` —— 见下「★B 的重大修正」 |
| **C** 升级建筑 | 甲案：三个独立建筑，同一里程碑 | 储能单元 / 大容量仓库 / 产能加速器 |
| **D** 多人共享 | 弱共享（状态一致）+ 主控仲裁 | 服务端 roster（`WirelessTerminalShared`），随扫描回复下发 |

### ★ B 的重大修正：不必引入 `.uasset`

FORM3_RECON §2 的结论是「B 无论如何都需要至少一个 `.uasset`」，依据是所有 Enhanced Input
路线（含官方给 mod 留的 `UFGChildInputMappingContext`）都要求 **primary data asset**。

**这个结论被推翻了。** 实际实现走的是：

```
FSlateApplication::Get().RegisterInputPreProcessor(processor)   // IInputProcessor
```

理由与代价都记在 `WirelessTerminalHotkey.h` 的头注释里，要点：

- `IInputProcessor` 是**引擎层**接口，`FSlateApplication` 的注册入口与资产无关 ⇒ 零 `.uasset`。
- 游戏自己的 `UFGPlayerInputPreProcessor` 就是这个模式，所以这不是 hack 而是既有扩展点。
- 代价（必须手工处理的三件事）：
  1. **生命周期**：没有自动反注册 ⇒ 由 `UGameInstanceSubsystem::Deinitialize` 负责。
  2. **文本输入**：预处理器在 Slate 之前拿到按键 ⇒ `ShouldIgnoreInput()` 里查
     `GetUserFocusedWidget(0)->SupportsKeyboardFocus()`，否则打字会触发快捷键。
  3. **手柄**：KeyEvent 表达不了手柄 ⇒ B 明确不支持手柄；手柄入口仍是形态 2「掏出即开」。

绑定 `Ctrl+T`（弦键）而不是单键：`T` 本体是手电，加 Ctrl 可绕开全部未修饰键的占用问题。
`UFGInputTriggerChordBinding` 那条路因此也不再需要。

### ★ A 的实现要点

- 新 RPC：`Server_SetSwitchOn(AFGBuildable*, bool)` / `Server_SetSwitchPriority(AFGBuildable*, int32)`
  + `Client_NotifySwitchResult(AFGBuildable*, bool, FString)`。
- 三层门禁，顺序不可调换：**authority → 类型（Cast）→ 权限（D 的主控）→ 已接线**。
- 未接线（`IsSwitchConnected()==false`）**不静默成功**：开关翻过去电网也不会合并，
  所以回执直接报失败 + 原因，UI 也提前把按钮置灰。
- 扫描期就把状态塞进 `FWT_MachineEntry`（`bSwitchOn` / `bSwitchConnected` / `SwitchPriority`），
  避免每次点选再多跑一趟 RPC。
- 点击后立即重扫（不等 5 秒轮询），否则按钮手感像坏的。

### ★ C 的实现要点

三个建筑都是**继承本体类**而不是从 `AFGBuildable` 重写：父类 ctor 会建好库存组件、
电力组件、接线点、存档序列化与复制，mod 只需改数字。

| 建筑 | 改什么 | 怎么改 |
|---|---|---|
| 储能单元 `AWTUpgradePowerStorage` | `mPowerStoreCapacity` 1000 MWh、`mPowerInputCapacity` 100 | **反射**（两个都是 private） |
| 大容量仓库 `AWTUpgradeStorage` | `mInventorySizeX/Y = 8 x 12`（96 格） | **直接赋值**（这两个是 public） |
| 产能加速器 `AWTUpgradeAccelerator` | `SetPendingProductionBoost(2.0)`，上限 2.5 | public virtual，BeginPlay 里调 |

- **反射写必须在构造函数**：这些是 `EditDefaultsOnly`，CDO 只构造一次并被复制进每个实例；
  写在 BeginPlay 会漏掉 CDO，而建造枪预览与描述符提示读的正是 CDO。
- 加速器**不做范围光环**：本体的 boost 是逐建筑属性（能量碎片也是插哪台管哪台）。
  半成品的范围效果是一次没人要求的平衡改动，所以明确不做。
- 三个各自有描述符（**必须三个类**：建造枪按描述符**类**列入，同类的多个描述符在库存/
  配方层面是同一个物品）+ 配方 + 全息图，里程碑**同一个**（技术等级 4）。

### ★ D 的实现要点

- 弱共享：**共享的是工厂状态，不是 UI**。每个人开自己的面板，开关翻转靠本体已有的
  `mOnIsSwitchOnChanged` + Replicated 字段天然同步 —— 这部分是白嫖的。
- 需要额外判定的只有「**谁能写**」：取物会进某个人的背包，必须有唯一归属 ⇒ 主控制。
- roster 放在**服务端命名空间**（`WirelessTerminalShared`）而不是新复制 Actor：
  mod 无法给 `AFGGameState` 注入 `UPROPERTY`，而 roster 只需要被服务端读，
  答案随每次扫描回复下发 ⇒ **零复制、零 spawn、零清理**。
- 持有 `TWeakObjectPtr` ⇒ 玩家断开后下次读取自然消失，不需要断开钩子。
- 会话按**终端**隔离（`GetInteractBuildable()`），大基地里两台终端各自有各自的主控。

---

## 形态 2：手持终端 —— 抄作业对象已确定

### 关键发现（已从游戏文档核实）

游戏文档 `CommunityResources/Docs/en-US.json` 里扒出的**真实类清单**（这是权威依据，别猜）：

```
装备类（Actor）                          装备描述符（物品）
─────────────────────────────────────────────────────────────
FGObjectScanner   :: Equip_ObjectScanner_C   ← BP_EquipmentDescriptorObjectScanner_C
FGChainsaw        :: Equip_Chainsaw_C        ← Desc_Chainsaw_C
FGJetPack         :: Equip_JetPack_C         ← BP_EquipmentDescriptorJetPack_C
FGHoverPack       :: Equip_HoverPack_C       ← BP_EquipmentDescriptorHoverPack_C
FGZipline         :: Equip_Zipline_C         ← BP_EqDescZipLine_C
FGParachute       :: Equip_Parachute_C       ← Desc_Parachute_C
FGPortableMiner…  :: Equip_PortableMiner…_C  ← BP_ItemDescriptorPortableMiner_C
```

**★`AFGObjectScanner` 是最佳模板** —— 它本来就是「手持道具 + 打开一个信息界面」，
和我们的手持终端用途几乎一样。

### 装备三件套（缺一不可）

```
① 装备类        class AWirelessTerminalEquipment : public AFGEquipment
② 装备描述符    class UWirelessTerminalEquipDescriptor : public UFGEquipmentDescriptor
③ 配方          class UWirelessTerminalEquipRecipe : public UFGRecipe
                  mProducedIn = 工作台 BP_WorkshopComponent（不是建造枪！）
```

**活样板**（照着抄就行）：
```
配方:  /Game/FactoryGame/Recipes/Equipment/Recipe_ObjectScanner.Recipe_ObjectScanner_C
   mProducedIn = "/Game/FactoryGame/Buildable/-Shared/WorkBench/BP_WorkshopComponent.BP_WorkshopComponent_C"
   mProduct    = (ItemClass=BP_EquipmentDescriptorObjectScanner_C, Amount=1)
   mIngredients = 强化铁板 x4 + ...
   mManufactoringDuration = 40.0
装备类: Equip_ObjectScanner_C （在 FGObjectScanner 名下）
描述符: BP_EquipmentDescriptorObjectScanner_C （在 FGEquipmentDescriptor 名下）
```

### 装备类要写的字段（从 Equip_ObjectScanner_C 扒出来的）

| 字段 | 样例值 | 说明 |
|---|---|---|
| `mEquipmentSlot` | `ES_ARMS` | 装备槽，手臂 |
| `mAttachSocket` | `hand_rSocket` | 右手骨骼挂点 |
| `mArmAnimation` | `AE_Generic1Hand` | 手臂姿态动画 |
| `mEquipMontage` / `mUnEquipMontage` | 动画/音效 | 掏出/收起时的动作，可为空 |
| `mNeedsDefaultEquipmentMappingContext` | `True` | 需要输入映射上下文 |
| `mCostToUse` | 空 | 使用消耗 |

### 打开方式（关键难点）

`AFGEquipment` 需要**输入绑定**来决定「按什么键打开」。两条路：

- **A. 用 `mDefaultEquipmentActions`**（bitset，样板里是 `0`）—— 复杂，涉及 Enhanced Input
- **B. 覆盖 `AFGEquipment::OnEquip()/OnUnEquip()` 钩子**，掏出即打开面板
- **C. 用 SML 的按键绑定**（如果 SDK 提供）

**倾向 B**：掏出终端 = 打开面板，收起 = 关闭面板。这样**不需要额外按键**，
玩家掏出来就直接看到界面，体验最直白，也最容易实现。

### 联机边界（★必须重划）

装备是**客户端本地**的东西，而现在的扫描/取物/传送都是**服务端 RPC**（`UWirelessTerminalRCO`）。
所以：

- 装备类自己**只负责开界面**，不碰任何游戏状态
- 面板打开后，里面的操作**照旧走 RCO**（服务端执行）
- ⇒ 这样边界最干净：**装备 = 一个新入口，业务逻辑原封不动复用**

### 复用清单（能省一大半工作）

| 组件 | 复用? |
|---|---|
| `UWirelessTerminalPanelWidget` 面板 UI | ✅ 原样复用（`CreateWidget` + `AddToViewport`） |
| `UWirelessTerminalChatWidget` AI 对话 | ✅ 原样复用 |
| `UWirelessTerminalRCO` 所有 RPC | ✅ 原样复用 |
| 扫描/配置/里程碑 | ⚠️ 里程碑要新增一个（解锁便携终端） |
| 装备类 + 描述符 + 配方 | ❌ 新写（三件套） |

---

## 执行顺序

1. ~~**【当前】等编译 → 部署 → 用户实测形态 1**（建筑版 + AI 对话按钮）~~ ✅
2. ~~写形态 2 三件套（装备类 / 描述符 / 配方），先**不出包**，编译验证能过~~ ✅
3. ~~加一个里程碑解锁便携终端 + 更新说明书~~ ✅
4. ~~编译 → 部署 → 实测形态 2~~ ✅ 编译部署完成，**等用户实测**
5. ~~形态 3：A + B + C（完整三件套）+ D~~ ✅ **已实现**
6. ~~编译通过 → 部署 → 实测形态 3~~ ✅ 编译 + 部署 + 出包完成（v0.2.0），**等用户实测**

### ★ 形态 3 编译/打包踩坑记录（这三条别忘）

1. **13 个编译错误一次全暴露**（首轮真编译时）：根因是 `bIsEnabled` 是我凭空造的成员
   （`IInputProcessor` 根本没它）、`bIsSessionOwner` 挂错结构体（在 reply 上不在 entry 上）、
   `WT_CountItems` 前向引用、`UFGRecipe::mIngredients/mProducedIn` 是 protected 而我在
   **自由函数**里写（C2248）。
   ⇒ 修法：helper 收 `TArray&` 引用而不是对象指针，**零 AT 配置**就让派生类成员函数去命名受保护成员。
2. **cook 期 Fatal：`Default subobject ... MainMesh already exists`**。
   `AFGBuildablePowerStorage` 的 ctor 已经有 `CreateDefaultSubobject<...>(TEXT("MainMesh"))`。
   ⇒ "MainMesh" 的匹配是 `GetMainMeshes()` 的**子串** Contains，所以自建网格用
   **`WTMainMesh`**（包含但不等于）。
3. **cook 期 `EXCEPTION_ACCESS_VIOLATION`：`UFGColoredInstanceMeshProxy::ShouldCreateRenderState()` 空指针**。
   该函数第一行是 `GetWorld()->WorldType`，而 **CDO 构造期 `GetWorld()`==nullptr**；我又在子类
   ctor 里对该 proxy 调 `SetStaticMesh` ⇒ 崩。**曾试图复用父类 mesh（`WT_FindInheritedMainMesh`）
   就是踩这个坑**。⇒ 铁律：子类 ctor 里**绝不碰继承来的 mesh 组件**，一律自建。

### 形态 3 新增文件

```
Source/WirelessTerminal/WirelessTerminalUpgrades.h/.cpp            三个升级建筑
Source/WirelessTerminal/WirelessTerminalUpgradeHolograms.h/.cpp    三个放置全息图
Source/WirelessTerminal/WirelessTerminalUpgradeRegistry.h/.cpp     描述符/配方/里程碑
Source/WirelessTerminal/WirelessTerminalSharedSession.h/.cpp       D 的服务端共享会话
Source/WirelessTerminal/WirelessTerminalHotkey.h/.cpp              B 的 Ctrl+T 输入预处理器
```

改动文件：`WirelessTerminalTypes.h`（新增枚举项 + 遥测字段 + 共享会话字段）、
`WirelessTerminalRCO.h/.cpp`（3 个新 RPC + 扫描期遥测）、
`WirelessTerminalPanelWidget.h/.cpp`（开关操作区 + 共享横幅 + 电力读数补储能）、
`WirelessTerminalGameWorldModule.cpp`（注册升级套件 + 里程碑 + roster 复位）、
`Config/AccessTransformers.ini`（给第二个里程碑开 friend）。

---

## ✅ 形态 2 实施记录（2026-10-03 14:4x）

新增文件：
```
Source/WirelessTerminal/WirelessTerminalEquipment.h    (133 行)
Source/WirelessTerminal/WirelessTerminalEquipment.cpp  (261 行)
```

### 从 SDK 头核实的真事实（不是猜的）

读 `SMLStarterProject/Source/FactoryGame/Public/Equipment/FGEquipment.h`：

| 事实 | 位置 | 影响 |
|---|---|---|
| `Equip(AFGCharacterPlayer*)` / `UnEquip()` 是 **public virtual** | 224 / 227 行 | **可以安全覆盖**，比 Enhanced Input 路线简单得多 |
| `WasEquipped()` / `WasUnEquipped()` 是 `BlueprintNativeEvent` | 465 / 468 行 | 备选钩子 |
| `mEquipmentSlot` / `mArmAnimation` / `mAttachSocket` / `mNeedsDefaultEquipmentMappingContext` / `mCostToUse` 在 **protected** | 546+ 行 | 子类可以直接写（我们从 `AFGEquipment` 派生，不是外部类） |
| `mDefaultEquipmentActions` 也在 protected | 588 行 | 同上 |
| `IsLocalInstigator()` 是 public | 271 行 | 用它挡掉「专用服务器/远端复制实例不该开 UI」 |
| `UFGEquipmentDescriptor` 只有一个字段 `mEquipmentClass`（public） | Resources/FGEquipmentDescriptor.h | 描述符极简 |
| `UFGItemDescriptor::mStackSize` 在 **protected**（353/386 行段） | Resources/FGItemDescriptor.h:393 | 用反射写 `SS_ONE` |

### 两个踩过的坑

1. **`FGEquipmentDescriptor.h` 在 `Resources/` 下，不在 `Equipment/` 下**
   （`FGEquipment.h` 才在 `Equipment/`）。写错导致 `fatal error C1083`。
2. **构造期不能直接写 protected 成员**时用反射；但**从基类派生的类可以直接写 protected**，
   别过度使用反射。

### 设计要点

- **打开方式 = 覆盖 `Equip()/UnEquip()`**：先 `Super::Equip(Character)`（让引擎完成附着/
  动画/映射上下文），再 `OpenPanel()`。避免 `mDefaultEquipmentActions` + Enhanced Input
  （那需要 authored `UInputAction` 资产，本 mod 刻意不带任何 .uasset）。
- **`IsLocalInstigator()` 守卫**：只有本地玩家的控制器才有 viewport，服务端调用必须直接 return。
- **`EndPlay()` 兜底**：玩家死亡 / 关卡卸载 / 面板还开着时物品被丢弃 —— 不兜底会留个幽灵 UI。
- **`CreateWidget<UWirelessTerminalPanelWidget>(PC, ...)` + `AddToViewport(10)` + `SetInputMode()`**：
  和固定终端完全一样的创建路径，`SetInputMode()` 是 `UFGInteractWidget` 的 BlueprintNativeEvent，
  不调的话 AI 对话框收不到键盘输入。
- **幂等**：`OpenPanel()` 开头 `if (PanelWidget) return;` —— Equip 可能跑两次（热键栏重选、存档重载）。
- **联机边界**：装备类**只开关 UI**，不读不写任何游戏状态，业务全走原来那套 `UWirelessTerminalRCO`。

### 注册接线

- `WirelessTerminalEquipment.h` 导出 `WirelessTerminal::RegisterHandheld()`。
- `WirelessTerminalRegistry.cpp`：`RegisterBuildables()` 末尾调 `RegisterHandheld()`。
- `AddUnlockRecipes()` 里 `AddUnique(UWirelessTerminalEquipRecipe::StaticClass())` ——
  **一个里程碑同时解锁两个配方**（固定 + 便携），一次购买两样都能造。
- 配方 `mProducedIn` = `/Game/FactoryGame/Buildable/-Shared/WorkBench/BP_WorkshopComponent.BP_WorkshopComponent_C`。
- 成本：铁棒 x20 + 螺丝 x30 + 铜板 x10（40 秒），比固定终端略便宜。
  （v0.2.2 改版：终端整体下移到科技层级 1，材料随之改为 铁棒/螺丝/铜板。
   v0.2.1 曾改为「铜板 x15 + 转子 x5 + 模块化框架 x1」；
   更早是「钢筋混凝土梁 x4 + 电缆 x12 + 电路板 x2」—— 层级 4/5 的材料放在
   层级 2 的解锁里，买得起也造不出来。细则见 WirelessTerminalRegistry.cpp 顶部注释。）

### 验证

- 编译 `EXITCODE=0`，`[24/24]`，`Result: Succeeded`，0 error。
- DLL 从 391168 → **418304** 字节；UTF-16LE 搜到 5 个新标识：
  `AWirelessTerminalEquipment` / `UWirelessTerminalEquipDescriptor` /
  `UWirelessTerminalEquipRecipe` / `便携终端` / `BP_WorkshopComponent`。
- 部署 28/28；zip = 工程 = game = local 三处 md5 全一致（Steam `40feedf1f5b0`）。
- 发布 zip 9 / 7 条目，pdb=0，AI ini 已注入。

---

## 风险与对策

| 风险 | 对策 |
|---|---|
| 装备类继承链写错（是 `AFGEquipment` 不是 `AFGBuildable`） | 已核实基类名，样板 = `Equip_ObjectScanner_C` |
| 输入绑定搞不定 | 优先走「掏出即打开」的钩子路线，绕开 Enhanced Input |
| 装备做进建造枪菜单（错） | ★装备走**工作台**配方（`BP_WorkshopComponent`），不是建造枪 |
| 联机不同步 | 装备只开界面，业务全走服务端 RCO |
| 装备模型没有 | 先借用 `/Engine/BasicShapes/Cube` 或扫描器的网格占位 |
