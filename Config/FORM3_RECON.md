# 形态 3 只读取证（A + B + C + D）

> 本文件是**只读取证结论**，不含决策。开工前需用户确认末尾「待确认取舍点」。
> 取证日期：2026-10-03。SDK 根：`F:/Workbuddy/SMLDev/SMLStarterProject/Source/FactoryGame/Public`

---

## 0. 现有资产盘点（形态 3 的起点）

WirelessTerminal 现有 22 个源文件，形态 1（建筑版）+ 形态 2（便携版）均已交付。可直接复用的：

| 组件 | 复用度 | 说明 |
|---|---|---|
| `UWirelessTerminalPanelWidget` | 高 | 三栏 UI 全在 C++ 里 build（无 UMG 资产），新增区块只需在 `RebuildWidget()` 里插控件 |
| `UWirelessTerminalRCO` | 高 | 4 个 Server RPC + 4 个 Client RPC + 4 个本地委托，形态 3 所有新功能照此模式加 |
| `UWirelessTerminalChatWidget` | 高 | B 的对话逻辑不用重写，只需换一个「被谁唤起」的入口 |
| `UWirelessTerminalRegistry` | 中 | 新增建筑/配方/里程碑照抄现有注册路径 |
| `WirelessTerminalTypes.h` | 中 | `EWT_MachineCategory` 加枚举项即可在左栏多一类 |

**关键前提（形态 2 已定）**：本 mod **刻意零 `.uasset`**，所有 UI 在 C++ 里 build。
这条前提直接决定 B 的实现路线（见 §2）。

---

## 1. A —— 终端远程功能（跨基地切电 / 远程开关机器）

### 1.1 找到的落点（全部 public，可直接调）

**`AFGBuildableCircuitSwitch : AFGBuildableCircuitBridge`**（`Buildables/FGBuildableCircuitSwitch.h`）
这是一个「把两个电网桥接/断开」的建筑，即游戏里的**电源开关**：

```cpp
UFUNCTION( BlueprintCallable, Category = "FactoryGame|Circuits|Switch" )
void SetSwitchOn( bool isSwitchOn );                    // ★ 开合开关

UFUNCTION( BlueprintPure, ... )
bool IsSwitchOn() const { return mIsSwitchOn; }          // 当前状态

UFUNCTION( BlueprintPure, ... )
bool IsSwitchConnected() const { return Super::IsBridgeConnected(); }
// 是否两端都接上了电网（未连接 = 面板上要提示"未接线"）

virtual bool IsBridgeActive() const override { return mIsSwitchOn; }

UPROPERTY( BlueprintAssignable )
FOnCircuitSwitchPropertyChanged mOnIsSwitchOnChanged;    // 开关状态变化事件
```

- `mIsSwitchOn` 是 `UPROPERTY( SaveGame, ReplicatedUsing = OnRep_IsSwitchOn )`，**复制字段**，
  且 `SetSwitchOn()` 内部会走 `OnRep_IsSwitchOn` ⇒ **必须服务端调用**才有意义。
- 派生类 **`AFGBuildablePriorityPowerSwitch : AFGBuildableCircuitSwitch`**
  （`Buildables/FGBuildablePriorityPowerSwitch.h`）是本体的「优先级电源开关」：
  ```cpp
  UFUNCTION( BlueprintPure )   int32 GetPriority() const { return mPriority; }
  UFUNCTION( BlueprintCallable ) void SetPriority( int32 priority );   // ★ 顺带可做「远程调优先级」
  class AFGPriorityPowerSwitchInfo* GetInfo() const;
  ```
  `mPriority` 注释：**数字越大越先被限电，≤0 表示永不被自动关**。
  ⇒ A 可以做「远程开合 + 远程调优先级」两件事，成本几乎相同。

### 1.2 电网读数（面板顶部电力条已有，但可补）

**`UFGPowerCircuit::GetStats( FPowerCircuitStats& ) const`**（`FGPowerCircuit.h:171`）
`FPowerCircuitStats` 字段（全部 `UPROPERTY(NotReplicated)` ⇒ **只有服务端读得到**）：
`PowerProductionCapacity` / `PowerProduced` / `PowerConsumed` / `MaximumPowerConsumption` /
`BatteryPowerInput` / `BoostProduced`。

**`AFGCircuitSubsystem : AFGSubsystem`**（`FGCircuitSubsystem.h`）
```cpp
static AFGCircuitSubsystem* Get( UWorld* world );
UFUNCTION( BlueprintPure ) static AFGCircuitSubsystem* GetCircuitSubsystem( UObject* worldContext );
FORCEINLINE UFGCircuit* FindCircuit( int32 circuitID ) const { return mCircuits.FindRef( circuitID ); }
template< class T > FORCEINLINE T* FindCircuit( int32 circuitID ) const;
void ConnectComponents( UFGCircuitConnectionComponent*, UFGCircuitConnectionComponent* );
void DisconnectComponents( UFGCircuitConnectionComponent*, UFGCircuitConnectionComponent* );
void RemoveComponent( UFGCircuitConnectionComponent* );
```
**`AFGBuildableCircuitBridge`** 提供电路号入口：
```cpp
UFUNCTION( BlueprintPure ) int32 GetCircuitID0() const;   // -1 = 未连接
UFUNCTION( BlueprintPure ) int32 GetCircuitID1() const;
UFGCircuitConnectionComponent* GetConnection0();
UFGCircuitConnectionComponent* GetConnection1();
bool IsBridgeConnected();
```
⇒ **A 的完整能力边界**：既能开关桥接器（切电），也能按 `GetCircuitID0/1()` 反查两侧电路做读数。

### 1.3 网络边界（照现有 RCO 模式）

- 目标寻址：**沿用现有的 `AFGBuildable*` NetGUID 方案**（`Server_RequestOpenUI(AFGBuildable*)` 已经在用），
  绝不能传世界坐标（堆叠歧义）或数组索引（专用服务器客户端没有那个数组）。
- 新增 RPC 形态：`Server_SetSwitchOn(AFGBuildable*, bool)` + `Server_SetSwitchPriority(AFGBuildable*, int32)`
  + 对应 `Client_NotifySwitchResult(AFGBuildable*, bool)`。
- 门禁：`WT_HasAuthority`（= `GetOwnerPlayerController()->HasAuthority()`）。
- 左栏归类：`EWT_MachineCategory` 新增 `PowerSwitch` 项，`WT_Classify()` 里判 `AFGBuildableCircuitSwitch`。

### 1.4 A 的待确认取舍点

- **A1** 范围只做「开合 + 调优先级」，还是要连**电网读数明细**（每路电路的发电/耗电/储能）？
- **A2** 开关列在左栏独立一类，还是并进现有「发电机」类？
- **A3** 是否需要「未接线的开关给出提示」？（`IsSwitchConnected()` 已具备，做与不做代价接近，但 UI 文案要定）

---

## 2. B —— AI 对话全局快捷键（不打开终端即可唤起）

### 2.1 ★ 核心约束：本 mod 零 `.uasset`

形态 2 的路线图已经写死这条：*"the clean route is the `mDefaultEquipmentActions` bitmask plus an
Enhanced Input mapping context, and both want authored `UInputAction` assets this mod deliberately does
not ship"*。所以 B 有三条候选路：

### 2.2 候选路线（按可行性排序）

**路线 1：复用游戏已有的 `UInputAction` 资产（推荐）**
SDK 里已有 164 个 `IA_*` 资产，且存在完整的「GameplayTag → UInputAction」绑定表：

```cpp
// FGInputSettings.h
UFUNCTION( BlueprintCallable )
TSoftObjectPtr<UInputAction> GetInputActionForTag( const FGameplayTag& Tag ) const;
// 底层数据：UPROPERTY(config) TArray<FInputActionTagBinding> mInputActionTagBindings;
//           （config = Input，即 DefaultInput.ini 可配）

// FGEnhancedInputComponent.h
template< class UserClass, typename FuncType >
void UFGEnhancedInputComponent::BindActionByTag( const FGameplayTag& InputTag,
                                                 ETriggerEvent TriggerEvent,
                                                 UserClass* Object, FuncType Func );
```
⇒ **理论上可以：挑一个语义合适的已有 tag（如 `Chat` / `ToggleCodex` / `ToggleQuickSearch`），
在自己新增的 Mapping Context 里把它绑到目标键，然后 `BindActionByTag` 到自己的回调。**
这条**不需要自制 `.uasset`**，但需要确认：
- (a) 该 tag 对应的 action 是否允许多上下文复绑；
- (b) 是否可用 `UFGInputMappingContext`（`Input/FGInputMappingContext.h`）在运行时 **NewObject 造一个空 context** 再塞 mapping —— 还是必须落一个 `.uasset`。
- ⚠️ 这里的 (b) 是**未取证项**：`UInputMappingContext` 的 `Mappings` 数组能否在运行时 Add 出来，需要看 `InputMappingContext.h` 的 API 可见性。

**路线 2：自制一个最小 `.uasset`（打破零资产前提）**
- 本 mod 的 `.uplugin` 已 `"CanContainContent": true`；WirelessPower 就有完整 `Content/` 目录。
- 做法：造 `IMC_WirelessTerminal`（mapping context）+ `IA_WT_OpenChat`（input action）两个资产，
  随 mod 打包。⇒ **可靠、官方推荐**，但**违反「零 .uasset」这条现有设计约束**。

**路线 3：绕开 Enhanced Input，用 Slate 层输入预处理**
- 游戏自己有 `UFGPlayerInputPreProcessor : IInputProcessor`（`FGPlayerInput.h`），在 Slate 之前拦按键。
- 但它是**引擎类**，mod 侧只能「新建自己的 `IInputProcessor` 并 `RegisterInputPreProcessor`」——
  这需要直接依赖 Slate 模块，且要自己管注册/注销生命周期。
- ⇒ 最"脏"但**完全不需要资产**。适合「只要一个热键」的极简需求。

### 2.3 ★★重大发现：游戏**原生就是为 mod 扩展输入而设计的**

`Input/FGChildInputMappingContext.h` 的头注释逐字写道：

> Child mapping contexts are input mapping contexts that are automatically bound when their parent
> input context is bound. They can be used for **extending existing mapping contexts in a modular
> fashion, e.g. from game feature plugins or mods**.
> Child mapping contexts, unlike their parent contexts, are **primary data assets** and are
> **discovered automatically through Asset Manager**.

```cpp
UCLASS()
class FACTORYGAME_API UFGChildInputMappingContext : public UFGInputMappingContext
{
	/** The parent context for the given mapping context.
	 *  The child contexts will automatically get bound or unbound when their parent context state changes.
	 *  Main use case of this are extending existing input contexts on the game subsystems for e.g. mods */
	UPROPERTY( EditAnywhere, Category = "Mapping" )
	TSoftObjectPtr<UFGInputMappingContext> mParentContext;
};
```

⇒ **这是官方给 mod 留的正式扩展点**，比路线 1/3 都干净：
- 造一个 `UFGChildInputMappingContext` 资产，把 `mParentContext` 指向 `MC_PlayerActions`（或更合适的父上下文），
  被 Asset Manager 自动发现并随父上下文一起绑定 ⇒ **不需要手动管绑定生命周期**。
- 但**它仍是 primary data asset** ⇒ **B 无论如何都需要至少一个 `.uasset`**，
  「零 .uasset」这条前提**在 B 上必然被打破**（除非走路线 3 的 Slate 预处理）。

### 2.4 其他可复用的输入机制

- `UFGInputTriggerChordBinding`（`Input/Triggers/FGInputTriggerChordBinding.h`）
  —— 运行时注入「弦键」（Ctrl+C / Ctrl+V 这类组合键）的触发器，注释明写 *"for the player when binding
  keys as chords"*。⇒ **B 若要 `Ctrl+某键` 形式，这是现成组件。**
- `UFGInputMappingContext`（`Input/FGInputMappingContext.h`）带 `mMenuPriority`，用于按键重绑定菜单排序。

### 2.5 B 的待确认取舍点（★最关键）

- **B1（★必答）** 走哪条路线？三选一：
  - **路线 1+**：用 `UFGChildInputMappingContext` 扩展游戏已有上下文（**官方 mod 扩展点，最推荐**），
    资产数 = 1~2 个（1 个 child context + 1 个 input action）。
  - **路线 2**：自造独立 mapping context（不被自动发现，需手动 `BindMappingContext`）。
  - **路线 3**：Slate 层 `IInputProcessor` 硬拦按键（**唯一真正零资产**，但最脏、要自己管生命周期）。
  **⇒ 需要你拍板：接受为 B 引入 `.uasset`（1~2 个）吗？**
  接受则走路线 1+，代价是打包流程加一个内容打包步骤（mod 已 `CanContainContent: true`，WirelessPower 已有先例）。
- **B2** 绑定哪个键？建议避开游戏已占用的（E 使用、Q/E 建造枪切换、Tab 背包、M 地图、T 手电等）。
- **B3** 要不要做成**弦键**（如 `Ctrl+T`，可用现成的 `UFGInputTriggerChordBinding`）以彻底避开冲突？
- **B4** 手柄要不要一起支持？（路线 1+/2 天然支持；路线 3 要自己映射手柄键）
- **B5** 唤起形态：**非模态浮层**（现有 `ChatWidget` 就是这么挂的，`AddToViewport(20)`）还是**暂停游戏的模态框**？

---

## 3. C —— 终端升级模块（已恢复完整版：储能 / 扩容 / 提速）

> 用户已确认 C **不再收缩为「一个存储仓库」**，按原设计做完整三件套。

### 3.1 储能模块 —— 落点最清晰

**`AFGBuildablePowerStorage : AFGBuildableFactory`**（`Buildables/FGBuildablePowerStorage.h`）
即游戏里的**电池**，公开只读接口齐全：

```cpp
UFUNCTION( BlueprintPure ) float GetPowerStore() const;            // 当前储量 MWh
UFUNCTION( BlueprintPure ) float GetPowerStoreCapacity() const;    // 上限 MWh
UFUNCTION( BlueprintPure ) float GetPowerStorePercent() const;     // 0..1
UFUNCTION( BlueprintPure ) float GetPowerInput() const;            // 当前输入 MW
UFUNCTION( BlueprintPure ) float GetPowerOutput() const;           // 当前输出 MW
UFUNCTION( BlueprintPure ) float GetTimeUntilFull() const;         // 多少秒充满
UFUNCTION( BlueprintPure ) float GetTimeUntilEmpty() const;        // 多少秒放完
UFUNCTION( BlueprintPure ) EBatteryStatus GetBatteryStatus() const;
UFUNCTION( BlueprintPure ) uint8 GetIndicatorLevelMax() const;
```
**容量/速率的可写字段**（private，`EditDefaultsOnly`）：
`mPowerStoreCapacity`(容量) / `mPowerInputCapacity`(输入上限) / `mIndicatorLevelMax`(指示灯)。
⇒ 这两个 private 字段**要在 mod 侧改**，需走反射写（`FindFProperty` + `SetPropertyValue`），
或干脆自己派生一个 `AWTBuildablePowerStorage : AFGBuildablePowerStorage` 在 ctor 里写默认值。
- ⚠️ **未取证**：`mPowerStoreCapacity` 是 `EditDefaultsOnly`，ctor 里能否直接写（需确认它是 private 还是 protected—**private ⇒ 派生类也不能直接写，必须反射**）。本轮取证只看到它在 `private:` 段（文件 `Buildables/FGBuildablePowerStorage.h`，`private:` 前是 `UFGColoredInstanceMeshProxy* mMeshMesh;`）。
  ⇒ **结论：储能模块若想"加大容量"，只能做新建筑类 + 反射写，或另辟蹊径。**

### 3.2 扩容模块 —— 落点：仓库尺寸

**`AFGBuildableStorage : public AFGBuildableFactory`**（Abstract，`Buildables/FGBuildableStorage.h`）
```cpp
UFUNCTION( BlueprintPure ) FORCEINLINE UFGInventoryComponent* GetStorageInventory() { return mStorageInventory; }
public:  float mStackingHeight;  int32 mInventorySizeX;  int32 mInventorySizeY;   // ★ public 可写
private: UPROPERTY(SaveGame) TObjectPtr<UFGInventoryComponent> mStorageInventory;
```
⇒ **`mInventorySizeX/Y` 是 public**，扩容模块可以干净地改尺寸（改完需 `Inventory->Resize()`）。
**`UFGInventoryComponent` API**（`FGInventoryComponent.h`）：
`AddStack`/`AddStacks`/`AddStackToIndex`/`GetStackFromIndex`/`GetInventoryStacks(TArray&, bool getEmptyStacks)`/
`GetNumItems(TSubclassOf)`/`GetSizeLinear()`/`Empty()`/`Resize(int32)`/`IsValidIndex`/
`HasEnoughSpaceForStack(s)`/`IsItemAllowed`/`FindEmptyIndex`/`GetFirstIndexWithItem`。

### 3.3 提速模块 —— 落点：产能 boost

**`AFGBuildableFactory`**（`Buildables/FGBuildableFactory.h`）
```cpp
UFUNCTION( BlueprintCallable ) FORCEINLINE float GetCurrentProductionBoost() const;
UFUNCTION( BlueprintCallable ) FORCEINLINE float GetPendingProductionBoost() const;
UFUNCTION( BlueprintCallable ) virtual void SetPendingProductionBoost( float newPendingProductionBoost );  // ★
```
⇒ 本体的提速靠 power shard（`mProductionBoostShardDescriptor`），**`SetPendingProductionBoost` 是 public virtual**
⇒ 提速模块可以「以编程方式施加一个 boost 倍率」，不需要插碎片。

⚠️ **关键限制**：`mPowerConsumption` / `mCurrentProductionBoost` 都是 **`private:`** 段
（`FGBuildableFactory.h` 的 `private:` 在 656 行附近）。
⇒ 任何"改工厂本体属性"的做法都得走反射或派生类。

### 3.4 「倒空库存进玩家背包」的现成帮手

```cpp
// Buildables/FGBuildableFactory.h:346  ★ public static
static void MoveOrDropInventory( UFGInventoryComponent* inventory, AFGCharacterPlayer* player );

// Buildables/FGBuildableManufacturer.h:132
virtual bool MoveOrDropOutputInventory( AFGCharacterPlayer* player );
UFGInventoryComponent* GetInputInventory();   // :157
UFGInventoryComponent* GetOutputInventory();  // :161

// Buildables/FGBuildableResourceExtractorBase.h:117
UFUNCTION( BlueprintPure ) virtual UFGInventoryComponent* GetOutputInventory() const;
```
⇒ 储能/扩容/提速三件套若做成**建筑**，「装不下的就掉地上」这条通用逻辑已经现成。

### 3.5 C 的待确认取舍点（★需要你定义）

C 恢复完整后，**它到底是什么形态的东西**必须定清楚，三种互不相同的做法：

- **C-甲：三个独立建筑**（小电池 / 大仓库 / 加速器），各自是新 `AFGBuildable` 子类 + 描述符 + 配方 + 里程碑。
  ⇒ 工作量最大，但最"像游戏本体"，可自由摆放、可联机、可存档，也能直接被终端面板扫到。
- **C-乙：终端的一个「升级模块」物品**，放在建筑版终端（`AWirelessTerminalBuildable`）上生效
  （装上去让终端「带电池 / 带仓库 / 带加速光环」）。
  ⇒ 需要给终端建筑加 `UFGInventoryComponent` 存放模块 + 在面板里加"模块槽"区块。
- **C-丙：只做「一个带储能 + 大仓容 + 提速的老式终端升级版」**（把三者合进终端建筑自身）。
  ⇒ 工作量最小，但"升级"的意味最弱。

- **C1（★必答）** 选甲 / 乙 / 丙？
- **C2** 若选甲：三个建筑是**新里程碑解锁**，还是**同一里程碑一次给全**？
- **C3** 提速模块的倍率上限？本体 power shard 走的是 `mProductionBoostShardDescriptor`，
  我们要不要设一个上限避免把产线跑到崩？

---

## 4. D —— 多人同步增强（队友共享同一面板）

### 4.1 现有的联机边界

- `UWirelessTerminalRCO` 的 4 个 Server RPC 都是**单播**：谁请求，`Client_*` 就回给谁。
  面板数据由 `Client_ReceiveMachineList` 单播给请求者 ⇒ **D 需要额外机制**。
- `UFGRemoteCallObject` **没有 `HasAuthority()`**，靠 `GetOwnerPlayerController()->HasAuthority()`。
- `UFGRemoteCallObject` 是 UObject 非 UActorComponent，须至少一个 `UPROPERTY(Replicated)` 字段
  才参与 net driver（现有 `int32 DummyReplicatedField` 就是为此）。
- ★ 跨线传对象**只能**走 `AFGBuildable*` / `AActor*`（NetGUID 复制）。

### 4.2 D 的待确认取舍点

- **D1** 「共享」的粒度：
  - **弱共享**：队友各自打开自己的面板，但**看到的开关状态实时一致**（靠 `mOnIsSwitchOnChanged` 复制）。
    ⇒ 代价极低，本质是"状态同步"而非"共享 UI"。
  - **强共享**：房主/任意玩家打开面板，**所有队友屏幕上出现同一个面板**，轮流操作（"共享控制台"）。
    ⇒ 要新建一个复制型 Actor（或 GameState 上的 `UPROPERTY(Replicated)` 状态 + `Client` 广播），
      并且要处理"谁在点按钮"的仲裁。
- **D2** 若强共享：是**终端建筑**作为共享宿主（谁点终端谁为主控），还是**队伍维度**（任意玩家都能唤起一个共享面板）？
- **D3** 权限：队友能不能**操作**（切电/取物），还是只能**看**？
  取物/传送类操作涉及"物品进谁背包"，强共享下必须明确归属。

---

## 5. 待确认取舍点汇总（请逐条拍板）

| 编号 | 问题 | 我的倾向 |
|---|---|---|
| A1 | A 只做开合+优先级，还是连电路读数明细 | 做开合+优先级，读数沿用现有顶部电力条 |
| A2 | 开关独立一类 or 并进发电机类 | 独立 `PowerSwitch` 类，语义更清楚 |
| A3 | 未接线开关是否提示 | 做，成本接近零 |
| **B1** | **B 走路线 1+（child context，官方 mod 扩展点）/ 2（自造 context）/ 3（Slate 拦截）？是否接受引入 1~2 个 .uasset？** | **路线 1+**；接受引入资产 |
| B2 | 绑哪个键 | 避开游戏占用键 |
| B3 | 是否用弦键（Ctrl+X） | 倾向用，彻底避冲突 |
| B4 | 手柄要不要一起 | 要（路线 1+ 免费获得） |
| B5 | 非模态浮层 or 模态框 | 非模态（复用现有 ChatWidget 行为） |
| **C1** | **C 选甲（三个独立建筑）/ 乙（终端模块）/ 丙（合成进终端）** | **甲**（最像本体，且可直接被终端面板扫到） |
| C2 | 三个建筑是否同一里程碑 | 同一里程碑，靠材料成本区分 |
| C3 | 提速倍率上限 | 设上限（具体值待定） |
| **D1** | **D 弱共享（状态一致）or 强共享（同一面板）** | 弱共享优先（代价极低且已有一半基础设施） |
| D2 | 强共享宿主 | 终端建筑为主控宿主 |
| D3 | 队友权限 | 看为主，操作限房主 |

---

## 6. 未取证项与已解决项

### 已解决
- **~~`UInputMappingContext::Mappings` 运行时可否 Add~~** → **不必查了**：
  `UFGChildInputMappingContext` 是官方的 mod 扩展路径，直接用资产做，不需要运行时造 context。
- **~~`AFGBuildablePowerStorage::mPowerStoreCapacity` 是否 private~~** → **已确认是 `private:`**
  （`Buildables/FGBuildablePowerStorage.h`：`private:` 在 126 行，`mPowerStoreCapacity` 在 144 行，
  `mPowerInputCapacity` 在 156 行）。
  ⇒ **C 的储能扩容只能靠"派生新建筑类 + 反射写"或"派生类里 ctor 后反射赋值"**，
  不能指望直接继承改。同理 `AFGBuildableFactory::mPowerConsumption`（`private:` 段）也一样。
  **→ 实施时按此执行：`AWTUpgradePowerStorage` 的 ctor 里反射写这两个字段，成立。**

### ★★ 已被实施推翻的结论（重要更正）

- **原结论 §2.2 / §2.3 / §5-B1**：「B 无论如何都需要至少一个 `.uasset`」。
  **❌ 错。** 实际实施走的是 **Slate 层 `IInputProcessor`**（原 §2.2 的「路线 3」），
  并且**不需要**自己 `AddBlueprintInputPreprocessor` 那套 UObject 包装 —— 直接用
  `FSlateApplication::Get().RegisterInputPreProcessor(TSharedPtr<IInputProcessor>)`。
  ⇒ **本 mod 保持零 `.uasset`**，原「接受引入 1~2 个资产」的取舍点**作废**，
  打包流程也不必为 B 增加内容打包步骤。
  依据：`FSlateApplication` 的注册入口与资产无关；`IInputProcessor` 是引擎层接口
  （`Framework/Application/IInputProcessor.h`），游戏自己的 `UFGPlayerInputPreProcessor`
  就是这个模式。详见 `WirelessTerminalHotkey.h` 头注释。

- **原结论 §2.2 路线 1 的「复用已有 `UInputAction` 资产」**：仍然可行，但**未被采用** ——
  它需要自造 mapping context 才能把 tag 绑到新键，反而比 Slate 路线更绕。

### 仍需补
1. ~~本 mod 打包是否会因新增 `Content/` 而改变现有 Alpakit 流程~~ → **作废**：B 零资产，
   仍无 `Content/`。
2. 哪几个父上下文适合挂 child context → **作废**：不再走 child context 路线。

### 新增的、实施期才发现的可用接口

- **`FSlateApplication::RegisterInputPreProcessor`**（`Framework/Application/SlateApplication.h`）
  —— 引擎层按键拦截，**资产无关**，是 B 的实际落点。
- **`FSlateApplication::GetUserFocusedWidget(0)->SupportsKeyboardFocus()`**
  —— 预处理器在 Slate 之前拿到按键，**必须**用它挡掉「正在打字」的情况，
  否则在聊天框里敲 `t` 会触发快捷键。
- **`AFGBuildableStorage::mInventorySizeX / mInventorySizeY` 确认 public**
  —— 大容量仓库无需反射，直接赋值（比预想的更干净）。

