#pragma once

#include "CoreMinimal.h"
#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGBuildablePowerStorage.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGBuildablePipeReservoir.h"
#include "Buildables/FGCentralStorageContainer.h"
#include "FGPowerConnectionComponent.h"
#include "Remote_FactoryUpgrades.generated.h"

class UFGPipeConnectionComponent;
class UFGFactoryConnectionComponent;   // ★0.2.42 固态两站的传送带接口
class UFGPowerConnectionComponent;     // ★0.2.42 超级蓄电池的电力接口
class USkeletalMeshComponent;          // ★0.2.48 电池建模：原版 SCS 的 SK 内部电池体
class UMaterialInstanceDynamic;        // ★0.2.48 J1 兜底发光：SK 电池体 MID 缓存
class UStaticMesh;                     // 引擎类型前置声明（头文件通用卫生）

/**
 * ★0.2.48 D：电线杆式电力连接组件。
 * 默认 UFGCircuitConnectionComponent::mMaxNumConnectionLinks = 1（桩构造里写死），即一个口只能接 1 根线。
 * 用户要「一个接点并 4 根线」（像电线杆），故子类在构造里把上限提到 4。
 * 该字段为 protected 且无公开 setter，只能靠子类构造设定。
 */
UCLASS()
class REMOTE_FACTORY_API UWT_PowerConnectionComponent : public UFGPowerConnectionComponent
{
    GENERATED_BODY()
public:
    UWT_PowerConnectionComponent();
};

/**
 * Form 3 "C": the terminal's upgrade buildings.
 *
 * Three independent placeables, each solving one problem the base terminal has:
 *
 *   * 储能单元  AWTUpgradePowerStorage -- a battery bank the terminal can route power into
 *   * 大容量仓库 AWTUpgradeStorage     -- 位面存储站：大格子仓库 + 原版位面存储上传链路
 *   * 产能加速器 AWTUpgradeAccelerator -- a factory-wide production boost emitter
 *
 * ## Why these are engine subclasses and not brand-new buildings
 *
 * Each one inherits from the vanilla building that already does the job (battery / container /
 * factory) rather than reimplementing its behaviour on top of AFGBuildable. That buys the
 * inventory component, the power-info component, the circuit connection points, the save/load
 * serialisation and the replication -- all wired up by the parent constructor -- for free. What is
 * left for this mod to do is exactly the interesting part: change the NUMBERS, and make the change
 * stick in a way that survives a save.
 *
 * ## ★ The reflection problem, stated once
 *
 * The numbers we want to change are private in the shipped SDK headers:
 *
 *   * AFGBuildablePowerStorage::mPowerStoreCapacity  (private, header line ~144)
 *   * AFGBuildablePowerStorage::mPowerInputCapacity  (private, ~156)
 *   * AFGBuildableFactory::mPowerConsumption         (private, ~661)
 *
 * "private" in the SDK header is a COMPILE-TIME restriction on this module; it is not a runtime
 * restriction on the object. The properties are real, they are registered with UClass, and
 * FProperty can read and write them. So the constructors below write them through reflection, which
 * is the same technique the rest of this mod already uses for mIsUseable / mInteractWidgetSoftClass
 * / mStackSize.
 *
 * The key detail is WHEN. These are EditDefaultsOnly, so the right place is the CONSTRUCTOR -- the
 * CDO is built once, the values are captured into every instance the game later spawns, and a
 * loaded save keeps whatever it serialised. Writing them at BeginPlay would work for new buildings
 * but silently skip the CDO, which is what the build-gun preview and the descriptor tooltip read.
 *
 * ## Why the derived classes barely add members
 *
 * A subclass with no new fields has no new save data, so these three buildings are interchangeable
 * with their parents as far as the save system is concerned. That makes them cheap to keep working
 * across game updates: the parent does the serialising, and this file only has to keep the
 * constructor overrides honest.
 */

/**
 * 储能单元 -- a power storage building with a much bigger store and a faster charge rate.
 *
 * The terminal's top readout sums every AFGBuildablePowerStorage in the world, so putting one of
 * these down is immediately visible as a larger "stored" number: the upgrade is measurable from the
 * panel that motivated it.
 *
 * ★0.2.32 外观（用户定案）：用**原版大型蓄电池模型** `SM_PowerStorage_01`，等比放大到 **2x2 地基**
 *   （1600 x 1600 uu = 16m x 16m）。原版本体实测只有 6m x 6m x 12m（清空盒 600 x 600 x 1200 uu，
 *   模型在 `MainMesh` 上缩放 1:1），所以等比放大 1600/600 ≈ 2.667 倍，高度随之约 32m。
 *   缩放倍率由模型真实包围盒算出（见 cpp 的 WT_ScaleForFootprint），清空盒按缩放后的顶面写。
 *
 * ★0.2.48 数值（用户定案）：**初始储电量与充放速率都取原版大型蓄电池的 4 倍** ——
 *   基础值 `mPowerStoreCapacity = 400` MWh、`mPowerInputCapacity = 400` MW
 *   （原版蓝图 Build_PowerStorageMk1 里是 100 / 100）。
 *   ★原版头文件明文 `mPowerInputCapacity` 是**输入**功率上限且「there is no cap on output power」
 *   ⇒ 放电速率本来就无上限，"放电 4 倍"体现在**容量**上（可放出总电量 4 倍），无放电速率字段可改。
 *   ★接线逻辑经三路证据复核**与原版一致、未改动**（详见 cpp 里 0.2.42/0.2.48 注释）。
 *   实际部署值 = 基础值 × 玩家研究等级倍率，在 `BeginPlay()` 计算。
 */
UCLASS()
class REMOTE_FACTORY_API AWTUpgradePowerStorage : public AFGBuildablePowerStorage
{
    GENERATED_BODY()
public:
    AWTUpgradePowerStorage();

    /** Scale capacity / charge rate by the player's researched PowerStorage level. */
    virtual void BeginPlay() override;

    /** Tick: push charge/status custom primitive data so the battery's glow window animates. */
    virtual void Tick(float DeltaSeconds) override;

    /** ★0.2.48 J1 修复：建完特效后基类会把 WTMainMesh 这个 proxy 重新 SetInstanced(true) 实例化，
     *  清掉我们在 BeginPlay/Tick 写的每实例自定义数据(13/14 发光)。Super 之后重写以保窗亮。 */
    virtual void OnBuildEffectFinished() override;

    /** Human readable label, shared by the descriptor, the scan row and the milestone. */
    static FText GetDisplayName();

private:
    /**
     * ★0.2.48 D 修复（用户：原版电池有 2 个接线柱，这个电池做像电线杆的一个接点并 4 根线）。
     *
     * 引擎源码 AFGBuildablePowerStorage::AFGBuildablePowerStorage() 只建 mBatteryInfo 与
     * MainMesh（UFGColoredInstanceMeshProxy），**不含任何连接组件** —— 原版的
     * FGPowerConnectionComponent 是蓝图 Build_PowerStorageMk1 里摆的。C++ CDO 一个都没有 ⇒
     * 玩家拿电线点不到接口。故在构造函数自建，并接到基类自带的 mPowerInfo
     * （AFGBuildableFactory ctor 建，命名 "powerInfo"）。
     *
     * 用户最终定案（2026-10-09）：4 口要落在**同一个坐标**上，像电线杆那样一个接点并多根线，
     * 不要分散到前/后/左/右四面。原版仅 1 个电力连接组件（前脸 Y-、近顶部，原版模型空间坐标
     * (-0.037,-321.483,1149.996)）。
     *
     * ★0.2.48 D(4线，2026-10-09)：用**单个** UWT_PowerConnectionComponent（子类把
     *   mMaxNumConnectionLinks 从默认 1 提到 4）落在该点。理由：电线工具的「就近取口」会一直选到
     *   同一个最近口 —— 若 4 个独立口同坐标、每口 mMax=1，实际只能并 1 根线；单口 mMax=4 才
     *   稳定并 4 根（最贴合「像电线杆」）。坐标 = MeshLoc + PortPos*MeshScale（PortPos 单点）。
     */
    UPROPERTY()
    TArray<TObjectPtr<class UWT_PowerConnectionComponent>> mPowerConnections;

    /**
     * ★0.2.48 电池建模（用户实测「顶部往下看无材质」，改材质抄写两轮未愈）。
     *
     * 离线解析 Build_PowerStorageMk1.uasset（_cs_ports 解码链）实锤：
     *   - 蓝图对 MainMesh **零 OverrideMaterials** —— 抄到的槽全是 SM_PowerStorage_01
     *     资产自带材质，其中混着 "Fbx Default Material" 导入占位槽；
     *   - 原版真正盖在顶上的电池体是 SCS 的 SK_PowerStorage_01 **骨骼网格组件**
     *     （parent=MainMesh、相对变换单位；充放电动画与 Opening/Closing 音效都挂它）。
     * ⇒ 只摆 SM 外框 = 顶部镂空/占位材质。修法与原版同构：SK 挂在 WTMainMesh 下
     *   （继承等比放大），材质用 SK 资产自带（原版组件同样零 override）。
     */
    UPROPERTY()
    TObjectPtr<class USkeletalMeshComponent> WTInnerBatteryMesh = nullptr;

    /**
     * ★0.2.48 J1 兜底发光：SK 电池体各槽位建的 MID 缓存，每帧驱动其发光标量参数
     * （Materialize/FinalizeGlow/DayEmis/...），不依赖 SM 窗每实例数据是否生效。
     * 未暴露该参数的材质静默无副作用。
     */
    UPROPERTY()
    TArray<TObjectPtr<class UMaterialInstanceDynamic>> mGlowMIDs;

    /**
     * ★0.2.48 充电动画（v3）：可见主网格必须是 **UFGColoredInstanceMeshProxy**（实例化网格）。
     * 原因：发光窗材质读「每实例自定义数据」(Per-Instance Custom Data) 索引 11+mStatus/mChargePrimitiveID
     * = 13/14；普通 UStaticMeshComponent 没有 instance buffer，SetCustomPrimitiveDataFloat 写的是组件级
     * CPD，材质读到 0 ⇒ 不发光（v2 即栽在此）。proxy 把每实例数据喂给材质，故 glow 生效。
     * 发射通道：WT_UpdateBatteryGlow 用 FResolvedFactoryCustomizationData::ApplyToPerInstanceCustomData
     * 填基础色(0-10) + 写 13/14 发光，整体 SetUserDefinedData 上传。SK 电池体仍走组件级 CPD 兜底。
     * 绝不碰继承来的 mMeshMesh（触碰会在 cook 阶段崩）。
     */
    UPROPERTY()
    TObjectPtr<class UFGColoredInstanceMeshProxy> WTMainMesh = nullptr;


    /**
     * ★0.2.48 充电动画（v2）：把充电状态(11 + mStatusPrimitiveID) 与 电量百分比(11 + mChargePrimitiveID)
     * 这两路自定义图元数据写到可见组件 WTInnerBatteryMesh(SK) + WTMainMesh(SM)（及 proxy 副通道），
     * 还原原版充电动画。用 SetCustomPrimitiveDataFloat 直接驱动，每次 Tick 重写 ⇒ 重载后也重新点亮。
     */
    void WT_UpdateBatteryGlow();
};

/**
 * 云端储存上传站 -- 位面存储站（★0.2.32：基类改为原版「位面存储站上传器」；★0.2.36 改名）
 *
 * ★ 2026-10-06 用户定案：把原版的「容量」建筑做进模组。原版那一件就是
 *   `AFGCentralStorageContainer`（`Build_CentralStorage_C`，官方中文名「位面存储站上传器」），
 *   它本身又派生自 `AFGBuildableStorage`，所以：
 *
 *     * 库存格子（mInventorySizeX / mInventorySizeY）仍然是 PUBLIC，本类照旧直接赋值，
 *       不需要反射 —— 这是三件里唯一一件不用反射的；
 *     * 基类另外白拿了原版的上传链路：BeginPlay 把自己注册进 `AFGCentralStorageSubsystem`，
 *       `Factory_Tick` 把容器里的东西逐个上传进位面存储站。
 *
 *   ★0.2.36b 用户定案（「先看原版云端是否修建+内容，若都无才用我们的」）：上面那条原版上传
 *   链路**已被我们主动拦死** —— 本类的 `Tick()` 与 `Factory_Tick()` 都**不调 Super**，
 *   改为把本地缓冲搬进本站自己的账本 `mLedger`（SaveGame 持久化）。只有「原版云端里没有
 *   这类物品」时，回退 ticker 才把账本货经原版 API 注入池子（见 `StartCloudFallbackTicker`）。
 *   ⇒ 建造取料天然「原版优先」，两种记账永不互相污染。
 *
 * ★0.2.36（用户定案）：
 *   * 中文名「大容量仓库」→「云端储存上传站」；
 *   * 交互面板（可配置 UI）从原版 Build_CentralStorage_C 的 CDO 抄 mInteractWidgetSoftClass，
 *     打开建筑 = 原版位面存储站上传器的完整面板；
 *   * 建造配方 = 原版 Recipe_CentralStorage 的四样材料同顺序 + 1 个强化能量碎片。
 *
 * 网格：仍然自建 `WTMainMesh`，绝不碰任何继承来的网格子对象（原因见 cpp 的 WT_CreateMainMesh）。
 *       `AFGCentralStorageContainer` 自身不建任何网格代理，所以这里是安全的。
 *
 * ★0.2.32 外观（用户定案）：用**原版位面存储站上传器模型** `SM_CentralStorage_01`，保持原版 1:1
 *   尺寸（清空盒实测 500 x 900 x 423 uu = 5m x 9m x 4.23m）。
 *
 * ★0.2.36b 材质（用户定案「白模修复」）：**不再**套自建淡蓝材质 —— 原版工厂建筑的材质并不在
 *   静态网格资产上，而是蓝图在网格组件上做的 override；裸用 `SM_*` 只会得到白模。改为
 *   `WT_CopyVanillaMeshMaterials()` 加载原版蓝图 CDO、逐槽 `GetMaterial/SetMaterial` 抄过来。
 */
UCLASS()
class REMOTE_FACTORY_API AWTUpgradeStorage : public AFGCentralStorageContainer
{
    GENERATED_BODY()
public:
    AWTUpgradeStorage();

    /** Scale the inventory grid by the player's researched Storage level. */
    virtual void BeginPlay() override;

    /**
     * ★0.2.36b 建造取料优先级（用户定案）：本站**不走原版上传链路**（Tick/Factory_Tick
     * 都不调 Super，原版两条路上传的哪条都拦死），改为把本地缓冲记进自己的账本 mLedger
     * —— 只有「原版云端里没有这类物品」时，才由回退 ticker 把账本货注入原版池
     * （见 StartCloudFallbackTicker）。建造取料因此天然「原版优先」。
     */
    virtual void Tick(float dt) override;
    virtual void Factory_Tick(float dt) override;

    /**
     * ★0.2.36c 拆除期修复（用户定案「别静默丢料」）：本站的本地缓冲每 0.5s 就被 Tick 抽干进
     * `mLedger`，所以原版 `AFGBuildable::GetDismantleRefund_Implementation ->
     * GetDismantleInventoryReturns()` 看到的是一个**空库存** ⇒ 直接拆掉＝玩家塞进去的货全蒸发。
     * 这里在 Super 之后把账本逐类并进退款列表（原版只退 RF_SOLID，账本也全是固体，口径一致）。
     */
    virtual void GetDismantleRefund_Implementation(TArray<FInventoryStack>& out_refund,
                                                   bool noBuildCostEnabled) const override;

    /** Size the inventory grid from the current Storage research level (called from ctor + BeginPlay). */
    void ApplyStorageGrid();

    /** Human readable label, shared by the descriptor, the scan row and the milestone. */
    static FText GetDisplayName();

    // ---- 本站账本（我们自己的云） -------------------------------------------------
    /** 账本快照（只读）。 */
    const TArray<FItemAmount>& GetLedger() const { return mLedger; }

    /** 记入账本，返回记账后总量。 */
    int32 AddToLedger(TSubclassOf<UFGItemDescriptor> ItemClass, int32 Amount);

    /** 从账本扣减，返回实际扣到的数量（不足时给多少扣多少）。 */
    int32 TakeFromLedger(TSubclassOf<UFGItemDescriptor> ItemClass, int32 Want);

    /** 账本里该类还剩多少。 */
    int32 GetLedgerAmount(TSubclassOf<UFGItemDescriptor> ItemClass) const;

private:
    /** 把本地缓冲整个搬进账本（Tick 与 Factory_Tick 共用；原版上传路径全部拦死）。 */
    void DrainInventoryToLedger();

    /**
     * ★0.2.42 传送带入口（用户反馈「云端固体接不了传送带」的同一根因）。
     *
     * 引擎源码 `AFGBuildableStorage::AFGBuildableStorage()` 只建 `StorageInventory`，
     * **不含任何连接组件** —— 原版位面存储站上传器的连接组件同样是蓝图里摆的。
     * 所以 C++ 子类必须自建，传送带才有点可接。本类只摆**输入**（上传站只进不出）。
     */
    UPROPERTY()
    TObjectPtr<class UFGFactoryConnectionComponent> mConveyorIn = nullptr;

    /** 本站累计上传、尚未注入原版池的库存（存档持久化）。 */
    UPROPERTY(SaveGame)
    TArray<FItemAmount> mLedger;

    /**
     * ★0.2.36e 上一次把缓冲搬进账本的世界时间（秒）。纯 POD，不需要 UPROPERTY（非 UObject 引用，
     * GC 无关），也不该存档。
     *
     * 为什么必须节流：本类的 drain 有**两条**触发路径 ——
     *   ① 自己的 PrimaryActorTick（TickInterval = 0.5s）；
     *   ② 工厂 tick 子系统的 `Factory_Tick`：基类链上 `AFGBuildableStorage : AFGBuildableFactory`，
     *      而 AFGBuildableFactory 的 ctor 把 `mFactoryTickFunction.bStartWithTickEnabled = true`、
     *      `TickInterval = 0.0`，**AFGBuildableStorage 的 ctor 并不关掉它**；`FFactoryTickFunction`
     *      的目标是 AFGBuildableSubsystem（批处理），最终回调各建筑的虚函数 `Factory_Tick`。
     *      （实证：`AFGBuildablePipeReservoir::Factory_Tick(float){}` 是 Shipping 下真实存在的覆写，
     *       不是 editor-only —— 说明引擎确实会调它。）
     * ⇒ 不节流的话，同一份库存（阶段 5 时 10x10 = 100 格）一个周期会被扫两遍，甚至每帧一遍。
     *   所以把节流放进 drain 函数**内部**：无论哪条路径先到、另一条会不会到，扫描频率恒为 0.5s。
     *   —— 这样也避免「删掉 Factory_Tick 的 drain」那种改法带来的回归风险（万一 Tick 没跑，
     *      上传会静默失效；而保留双路径 + 内部节流则任一条活下来都能工作）。
     */
    double mLastLedgerDrainTime = -1.0;
};

/**
 * 云端存储下载站（★0.2.36 新增，用户定案）
 *
 * 与上传站配套的另一半：从云端池（AFGCentralStorageSubsystem 的聚合库存）往本地搬货。
 *
 * ## 设计
 *   * 基类 = 原版 `AFGBuildableStorage`（普通仓库），**故意不**派生
 *     `AFGCentralStorageContainer` —— 否则 BeginPlay 会把自己注册成"上传器"，
 *     一台机器既上传又下载会互相打架。
 *   * ★0.2.36b 取货优先级（用户定案「先看原版云端有没有，没有才用我们的」）：
 *     ① 原版云端池：`IsCentralStorageBuilt()` 为真时，用原版现成的
 *        `UFGInventoryLibrary::MoveItemFromCentralStorage()`（内部＝池扣数+目标库存加货，幂等不溢出）；
 *     ② 我们自己的云：原版池**没修建**、或池里这一类为 0 时，才遍历各上传站的账本
 *        （`AWTUpgradeStorage::GetLedger()`）取货；没放下的立刻退回账本。
 *   * 每个 Tick 周期（约 2 秒）最多搬**一种**物品的一组（stack）—— 慢慢搬，
 *     不跟传送带/玩家抢库存锁，也不会一帧把云端搬空。
 *   * 交互面板**用云端面板**（★0.2.36b 用户定案「云端用云端的」）：从原版位面存储站
 *     `Build_CentralStorage_C` 的 CDO 抄 `mInteractWidgetSoftClass`，打开就是原版云端界面。
 *     **不用**工业级集装箱 `Build_StorageContainerMk2_C` 的面板。
 *   * 本地缓冲格网跟随 Storage 研究阶段放大（与上传站同一套 ApplyStorageGrid）。
 *
 * ★ v1 注意：下载不耗电（仓库基类本身不进电力逻辑）。
 * ★0.2.42：传送带接口**已开放** —— 用户在 0.2.41 实测反馈「云端固体接不了传送带」，
 *   根因是原版连接组件摆在蓝图里、C++ CDO 没有；现在构造函数自建输出口并绑定库存
 *   （下载站只出不出，上传站对称只建输入口）。
 */
UCLASS()
class REMOTE_FACTORY_API AWTUpgradeCloudDownloader : public AFGBuildableStorage
{
    GENERATED_BODY()
public:
    AWTUpgradeCloudDownloader();

    virtual void BeginPlay() override;
    virtual void Tick(float dt) override;

    /** Size the inventory grid from the current Storage research level (ctor + BeginPlay). */
    void ApplyStorageGrid();

    static FText GetDisplayName();

private:
    /** Pull one stack of one item type from the cloud pool into our inventory. */
    void TryPullFromCloud();

    /** Seconds between pulls. Slow on purpose: one stack of one type per cycle. */
    float mPullInterval = 2.0f;
    float mPullCooldown = 0.0f;

    /**
     * ★0.2.42 传送带接口（用户反馈「云端固体接不了传送带」）。
     *
     * 引擎源码 `AFGBuildableStorage::AFGBuildableStorage()` 只建 `StorageInventory`
     * （`mCachedInputConnections` 只是一个运行期缓存数组），**不含任何 UFGFactoryConnectionComponent**
     * —— 原版 Mk1/Mk2 集装箱的进出带接口同样是蓝图摆的。所以我们的 C++ 子类必须自建一个
     * **输出口**，并把库存绑上去（`SetInventory(GetStorageInventory())`），传送带才有点可接。
     * ★0.2.48：下载站=纯下载（只出不出），只留这一个输出口；之前叠的输入口与输出口同坐标
     * （均 (0,390,100)）导致「一个口同时显输入+输出箭头」且连接全息吸错口，已删除。
     * （上传站 `AWTUpgradeStorage` 对称地只留输入口。）
     */
    UPROPERTY()
    TObjectPtr<class UFGFactoryConnectionComponent> mConveyorOut = nullptr;
};

/**
 * 云端液态上传站（★0.2.36 新增，用户定案）：管道流体的云端入口。
 *
 * ## 数据通路（全部走原版 API，不自算流体）
 *
 *   管网 ──Factory_PullPipeInput──> 本站 ──记入**我们自己的液态账本**──> 云端（我们的账）
 *
 *   * 管网把流体送进本站的管道接口（连接组件的 mFluidDescriptor 由管网下发）；
 *   * Tick 周期调 `UFGPipeConnectionComponent::Factory_PullPipeInput` 把流体按原版
 *     m³↔件换算率拉成一「库存栈」（与原版精炼厂拉输入同一入口）；
 *   * ★0.2.36b（用户定案「云端固液分开」）：把件数按**物品类**记入本站自己的
 *     `mFluidLedger`（SaveGame 持久化），**不直接进原版池** —— 于是原版云端始终是原版的，
 *     我们的货独立记账，两者永不互相污染。
 *   * `StartCloudFallbackTicker()`（5s）只在原版池对某类流体为 0 时，才把账本货经原版
 *     `UploadItemFromInventoryToCentralStorage` 注入池子 —— 这就是用户定案的
 *     「先看原版云端有没有，没有才用我们的」。
 *
 * ## 面板（★0.2.36b 用户定案「云端用云端的」）
 *
 * 用原版**云端面板**（位面存储站 `Build_CentralStorage_C` 的 mInteractWidgetSoftClass），
 * 不用工业集装箱面板、也不摆选型面板（选型是下载站的事）。
 *
 * ## 为什么不直接搓 mFluidBox
 *
 * FFluidBox 没有"流体种类"字段（种类在连接组件上），而且原版的压强/层流模拟自己做必然失真。
 * Factory_Pull/PushPipeOutput 是原版机器真正的进出货入口，时间债限速、压差结算全部原版。
 *
 * 管道接口：原版缓存罐的接口是蓝图里摆的，C++ CDO 没有 —— 构造函数自建
 * UFGPipeConnectionComponent（PCT_CONSUMER，我们从管网取货）并 override GetPipeConnections()。
 * ★必须建满两个：原版 `AFGBuildablePipeReservoir::BeginPlay` 会索引第 2 个接口。
 */
UCLASS()
class REMOTE_FACTORY_API AWTUpgradeCloudFluidUploader : public AFGBuildablePipeReservoir
{
    GENERATED_BODY()
public:
    AWTUpgradeCloudFluidUploader();

    virtual void BeginPlay() override;
    virtual void Tick(float dt) override;

    /** 基类返回的是蓝图摆的缓存数组（C++ CDO 为空），这里返回自建的接口。 */
    virtual TArray<class UFGPipeConnectionComponent*> GetPipeConnections() override;

    static FText GetDisplayName();

    // ---- 本站账本（我们自己的液态云） ---------------------------------------------
    const TArray<FItemAmount>& GetFluidLedger() const { return mFluidLedger; }
    int32 AddToLedger(TSubclassOf<UFGItemDescriptor> FluidClass, int32 Amount);
    int32 TakeFromLedger(TSubclassOf<UFGItemDescriptor> FluidClass, int32 Want);
    int32 GetLedgerAmount(TSubclassOf<UFGItemDescriptor> FluidClass) const;

private:
    /** One pull cycle: pipeline -> stack -> our own ledger. */
    void TryUploadToCloud(float dt);

    /** 本站累计送进云、尚未被下载站取走的流体（存档持久化）。 */
    UPROPERTY(SaveGame)
    TArray<FItemAmount> mFluidLedger;

    /**
     * ★0.2.36b 管道接口 ×2（用户定案：入口/出口统一摆 ±X 两侧同一组坐标）。
     * ★原版 AFGBuildablePipeReservoir::BeginPlay 会索引第 2 个接口
     *   （2026-10-07 实测崩溃：1 个接口 ⇒ "index 1 into array of size 1"），
     *   原版缓存罐本来就是双口，这里必须给满两个。
     */
    UPROPERTY()
    TObjectPtr<UFGPipeConnectionComponent> mPipeInA = nullptr;

    UPROPERTY()
    TObjectPtr<UFGPipeConnectionComponent> mPipeInB = nullptr;

    /** Seconds between pulls. */
    float mUploadInterval = 2.0f;
    float mUploadCooldown = 0.0f;
};

/**
 * 云端液态下载站（★0.2.36 新增，用户定案）：云端流体流回管网的出口。
 *
 * 数据通路与上传站镜像：
 *
 *   云端池 ──TryRemoveItemsFromCentralStorage(公开API)──> 本站 ──Factory_PushPipeOutput──> 管网
 *   我们的液态账本 ──（原版池该类为 0 时兜底）────────────────┘
 *
 *   * 选型（方案B，用户定案）：按 E 打开原生选型面板（UWTFluidSelectPanelWidget，纯 C++ 构建，
 *     参考 Remote_FactoryPanelWidget 先例）——面板列出云端池里现有的流体，点选即锁定；
 *   * Tick 周期**先取原版云端池**（每周期最多 50 件，原版公开 API，天然幂等），
 *     原版池没有该类流体时，才从我们各液态上传站的账本兜底取（★0.2.36b 用户定案
 *     「先看原版云端有没有，没有才用我们的」）；
 *   * 再推回管网：走原版生产端入口 Factory_PushPipeOutput。
 *
 * ★防丢料两道闸：
 *   1) 推管网前先查 IsConnected()，没接管子不扣货；
 *   2) 推送返回实际入网量，没推出去的差额立刻退回我们的液态账本（管网满/类型冲突时）。
 *   没选流体 / 没接管子 / 云里没货，三个条件任一不满足就静默空转。
 *
 * ★管道接口 ×2：同上传站，必须给满两个（原版缓存罐 BeginPlay 索引第 2 个接口），
 *   这里类型是 PCT_PRODUCER（我们往管网送货）。
 */
UCLASS()
class REMOTE_FACTORY_API AWTUpgradeCloudFluidDownloader : public AFGBuildablePipeReservoir
{
    GENERATED_BODY()
public:
    AWTUpgradeCloudFluidDownloader();

    virtual void BeginPlay() override;
    virtual void Tick(float dt) override;

    virtual TArray<class UFGPipeConnectionComponent*> GetPipeConnections() override;

    static FText GetDisplayName();

    /** Panel selection write-back (single-player listen server: direct write is safe). */
    void SetSelectedFluid(TSubclassOf<UFGItemDescriptor> FluidClass);
    TSubclassOf<UFGItemDescriptor> GetSelectedFluid() const { return mSelectedFluidClass; }

private:
    /** One download cycle: cloud pool -> stack -> pipeline. */
    void TryDownloadFromCloud(float dt);

    /** 玩家在面板上选定的流体（存档持久化）。 */
    UPROPERTY(SaveGame)
    TSubclassOf<UFGItemDescriptor> mSelectedFluidClass = nullptr;

    /** 选定的流体注回管网用的双接口（与上传站同一组坐标，用户定案「入口/出口同一位置」）。 */
    UPROPERTY()
    TObjectPtr<UFGPipeConnectionComponent> mPipeOutA = nullptr;

    UPROPERTY()
    TObjectPtr<UFGPipeConnectionComponent> mPipeOutB = nullptr;

    /** Seconds between downloads. */
    float mDownloadInterval = 2.0f;
    float mDownloadCooldown = 0.0f;
};

/**
 * 产能加速器 -- ★ 0.2.32 起【退役】（用户定案：产能不做建筑）。
 *
 * 产能轨改为「纯物品线」：原版能量碎片二次加工成新版能量碎片，插进**原版机器自带的能量碎片插槽**
 * （`AFGBuildableFactory::mInventoryPotential`），不再需要任何自研建筑。
 * 本类与 `UWTUpgradeAcceleratorRecipe` / `UWTUpgradeAcceleratorDescriptor` 一并**保留编译与注册**，
 * 理由与 v0.2.3 退役固定式终端相同：旧存档里已经放置的实例必须还能加载、保存、正常显示。
 * 它不再出现在任何解锁路径里（T1 里程碑不再发它，MAM 研究节点也将不再发它），所以新玩家拿不到。
 *
 * 下面这段是退役前的设计说明，保留备查。
 *
 * 产能加速器 -- a production-boost emitter.
 *
 * ## What this building does and does not do
 *
 * It is a factory building that, once placed and powered, holds a standing production boost. The
 * interesting question is what the boost APPLIES to, and the honest answer for a first cut is
 * "itself, plus anything explicitly pointed at it" -- because AFGBuildableFactory's boost is a
 * per-building property, not a radius effect. Vanilla power shards work the same way: a shard
 * boosts the machine it is inserted into.
 *
 * So this building is the SHARD FREE alternative for one machine: build it next to a manufacturer,
 * wire it to the same circuit, and the terminal offers a boost control for it. Any notion of an
 * area effect is deliberately out of scope rather than half-implemented -- a boost that silently
 * leaked to neighbours would be a balance change nobody asked for, and hard to explain in the UI.
 *
 * The boost is applied via AFGBuildableFactory::SetPendingProductionBoost, which is public virtual
 * and is the same entry point a power shard uses.
 */
UCLASS()
class REMOTE_FACTORY_API AWTUpgradeAccelerator : public AFGBuildableFactory
{
    GENERATED_BODY()
public:
    AWTUpgradeAccelerator();

    /** Human readable label, shared by the descriptor, the scan row and the milestone. */
    static FText GetDisplayName();

    /** The standing boost this building wants applied. Clamped; see the cpp for the ceiling. */
    UPROPERTY(EditDefaultsOnly, Category = "Remote_Factory")
    float mStandingBoost = 2.0f;

protected:
    virtual void BeginPlay() override;
};

namespace Remote_FactoryUpgrades
{
    // ---- classification predicates -----------------------------------------------------------
    // Used by the RCO scan to bucket these buildings. They take a UClass* rather than an instance
    // so the scan can classify without a cast, and so a subclass of a subclass still matches.

    bool IsUpgradePowerStorage(const UClass* Class);
    bool IsUpgradeStorage(const UClass* Class);
    bool IsUpgradeCloudDownloader(const UClass* Class);
    bool IsUpgradeCloudFluidUploader(const UClass* Class);
    bool IsUpgradeCloudFluidDownloader(const UClass* Class);
    bool IsUpgradeAccelerator(const UClass* Class);

    /** True for any of ours, so the panel can mark the row as one of ours. */
    bool IsUpgradeBuilding(const UClass* Class);

    /** The boost ceiling, exposed so the panel's slider and the class clamp cannot drift apart. */
    float MaxProductionBoost();

    /**
     * ★0.2.36b 建造取料回退 ticker（用户定案）：
     * 每 5s 检查一次 —— 原版云端池里某类物品为 0（没修建或没存货）时，把**我们账本**里的该类
     * 货以 100/次 的节奏注入原版池，让建造能取用我们的云。原版池有货时一律不动，
     * 于是「建造先吃原版云端、原版没有才吃我们的」自然成立。
     */
    void StartCloudFallbackTicker(class UWorld* World);
}
