#pragma once

#include "CoreMinimal.h"
#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGBuildablePowerStorage.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGBuildablePipeReservoir.h"
#include "Buildables/FGCentralStorageContainer.h"
#include "FGPowerConnectionComponent.h"
#include "Remote_FactoryUpgrades.generated.h"

class UWorld;

/**
 * ★2026-11-xx：研究推进后让已放置的升级建筑按当前研究等级重新套用容量（只增不减、幂等）。
 * 解决「先放后研究 → 研究后空提升」：电池/加速倍率改写即生效；云端库存 Resize 扩容。
 * 由 Remote_FactoryGameWorldModule 的 1s 研究推进定时器（与 WT_AdvanceResearchBlocks 同一脉冲）调用。
 */
void WT_RefreshResearchedUpgrades(UWorld* World);

class UFGPipeConnectionComponent;
class UFGFactoryConnectionComponent;   // ★0.2.42 固态两站的传送带接口
class UFGPowerConnectionComponent;     // ★0.2.42 超级蓄电池的电力接口
class USkeletalMeshComponent;          // ★0.2.48 电池建模：原版 SCS 的 SK 内部电池体
class UMaterialInstanceDynamic;        // ★0.2.48 J1 兜底发光：SK 电池体 MID 缓存
class UStaticMesh;                     // 引擎类型前置声明（头文件通用卫生）
class UStaticMeshComponent;            // ★2026-11-02 发光面片组件指针（拆除时精确销毁，不靠按名查找）

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
 * ★0.2.48 数值（用户定案）：**初始储电量取原版大型蓄电池的 16 倍** ——
 *   基础值 `mPowerStoreCapacity = 1600` MWh（原版蓝图 Build_PowerStorageMk1 里是 100；0.2.48 由 400 提到 1600）。
 *   ★充电速率（mPowerInputCapacity）2026-11-07 定案改为「按里程碑档位固定上限」：
 *   Lv1~4=200 / Lv5~8=500 / Lv9~12=1000 / Lv13~16=2500 / Lv17~20=5000 MW（原版蓝图 100 MW；
 *   不再随等级 ×1.25 暴涨，避免 Lv20≈34.7GW 任何电网都供不上；且按小厂现实余电上限定档，始终可达、不 runaway）。具体见 WT_PowerStorageChargeCapMW。
 *   ★原版头文件明文 `mPowerInputCapacity` 是**输入**功率上限且「there is no cap on output power」
 *   ⇒ 放电速率本来就无上限，"放电"体现在**容量**上（可放出总电量更多），无放电速率字段可改。
 *   ★接线逻辑经三路证据复核**与原版一致、未改动**（详见 cpp 里 0.2.42/0.2.48 注释）。
 *   储电量实际部署值 = 1600 × WT_TrackScale(等级)；充电上限 = WT_PowerStorageChargeCapMW(等级)；均在 BeginPlay 计算。
 */
UCLASS()
class REMOTE_FACTORY_API AWTUpgradePowerStorage : public AFGBuildablePowerStorage
{
    GENERATED_BODY()
public:
    AWTUpgradePowerStorage();

    /** Scale capacity / charge rate by the player's researched PowerStorage level. */
    virtual void BeginPlay() override;

    /** Re-apply battery capacity from the current researched level (called by the research-advance pulse; idempotent). */
    void ApplyResearchedCapacity();

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
 * ★1.0.7p 一次「成功进缓冲」的搬运样本。
 *
 * 为什么带 Item：面板列表每行要显示**该物品**的下载速度（用户 2026-10-27 定案的行布局
 * 【图标】【文本】【数量】【下载速度】【选择/更换】），只有总量样本是算不出每行读数的。
 *
 * 为什么整块做成 USTRUCT + UPROPERTY 数组：`Item` 是裸 UClass 语义的 TSubclassOf，
 * 不反射就是 GC 看不见的引用（与 mOutputFilter 同一条 CRASH GUARD，见下方 data 段注释）。
 * 样本数 ≤ 窗口/间隔 ≈ 3~10 个，反射扫描开销可忽略。
 */
USTRUCT()
struct FWT_PullSample
{
    GENERATED_BODY()

    FWT_PullSample() = default;
    FWT_PullSample(float InTime, int32 InAmount, UClass* InItem)
        : Time(InTime), Amount(InAmount), Item(InItem) {}

    /** 游戏时间（GetTimeSeconds），样本数组按它升序。 */
    UPROPERTY()
    float Time = 0.f;

    /** 这一批真正进缓冲的件数。 */
    UPROPERTY()
    int32 Amount = 0;

    /** 这批货的物品类（按类统计每行读数的键）。 */
    UPROPERTY()
    TSubclassOf<UFGItemDescriptor> Item;
};

/**
 * ★1.0.8BI 由样本窗口算「件/分钟」（**上传侧**与下载侧共用同一套口径）。
 *
 * 算法与 `AWTUpgradeCloudDownloader::WT_RatePerMinute` 逐字一致，抽成自由函数是因为
 * 上传站（固体 `AWTUpgradeStorage`）也需要同一读数 ——
 * 两个类各复制一份公式，任何一个改动不同步就会让"同一物品上传 60/分、下载 60/分"
 * 这两个本该对称的数字对不上，玩家会当成 bug。
 *
 * 口径（★精确版，别改成「窗口总量 ÷ 窗口长度」，会系统性高估 3 倍）：
 *   丢掉窗口内**最老样本的量**，只算它之后的量 ÷ 它到最新样本的时间跨度。
 *   搬运是离散的：3 个样本的真实含义是"最老那组代表它之前"，不属于这段区间。
 *   样本 < 2 个 ⇒ 返回 0（那一批是"从无到有"攒的，不知道代表多长时间，瞎猜只会虚高）。
 */
inline float WT_SampleRatePerMinute(const TArray<FWT_PullSample>& Samples,
                                    float Now, UClass* OnlyItem, float Window)
{
    TArray<int32, TInlineAllocator<8>> InWindow;
    for (int32 i = 0; i < Samples.Num(); ++i)
    {
        if (Now - Samples[i].Time > Window)
        {
            continue;
        }
        if (OnlyItem && Samples[i].Item.Get() != OnlyItem)
        {
            continue;
        }
        InWindow.Add(i);
    }
    if (InWindow.Num() < 2)
    {
        return 0.f;
    }

    int32 Sum = 0;
    for (int32 k = 1; k < InWindow.Num(); ++k)
    {
        Sum += Samples[InWindow[k]].Amount;
    }
    const float Span = Samples[InWindow.Last()].Time - Samples[InWindow[0]].Time;
    if (Sum <= 0 || Span <= 0.f)
    {
        return 0.f;
    }
    return static_cast<float>(Sum) * 60.f / Span;
}

/**
 * ★1.0.8BI 把样本窗口按类摊成「每类 件/分钟」（面板行内读数用）。
 * 只返回窗口内真的出现过样本的类；其余类不填（面板显示「--」），免得逐行调 O(样本)。
 */
inline void WT_CollectItemRates(const TArray<FWT_PullSample>& Samples, float Now, float Window,
                                TMap<UClass*, float>& OutRates)
{
    OutRates.Reset();
    TSet<UClass*> Seen;
    for (const FWT_PullSample& Sample : Samples)
    {
        UClass* Cls = Sample.Item.Get();
        if (!Cls || Now - Sample.Time > Window)
        {
            continue;
        }
        bool bAlready = false;
        Seen.Add(Cls, &bAlready);
        if (bAlready)
        {
            continue;
        }
        const float Rate = WT_SampleRatePerMinute(Samples, Now, Cls, Window);
        if (Rate > 0.f)
        {
            OutRates.Add(Cls, Rate);
        }
    }
}


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

    /** Re-apply storage grid + resize the live inventory from the current researched level (idempotent). */
    void ApplyResearchedCapacity();

    /** ★2026-11-02：显式销毁 BeginPlay 运行时加的发光面片（WTCloudGlowPlane），防止拆除后残留。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** ★2026-11-02：BeginDestroy 兜底——若拆解路径未触发 EndPlay，此处必然触发，确保光膜组件被销毁。 */
    virtual void BeginDestroy() override;
    /** ★2026-11-02：拆除开始即销毁光膜——拆除特效（PlayDismantleEffect/UFGMaterialEffect_Build）会把建筑网格
     *   套上拆除材质，运行时发光面片不受管理会露出原始外星纹理方块；在 Dismantle_Implementation 先销毁即可避免。 */
    virtual void Dismantle_Implementation() override;

    /**
     * ★1.0.6 修复「塞进去的东西凭空消失」（用户实测反馈：传送带送不进、背包存入直接没了）。
     *
     * ## 旧实现为什么错（0.2.36b ~ 1.0.5）
     * 旧版**刻意拦断原版上传链路**（Tick/Factory_Tick 都不调 Super），每 0.5s 把本地缓冲
     * 整格抽干进自己的账本 mLedger，而账本只有在 `WT_PoolAmount(类) == 0`（原版云端这一类
     * 一件都没有）时才由回退 ticker 注入原版池。玩家打开的是**原版**位面存储站面板，它只画
     * 本地缓冲 + 原版池，看不见账本 ⇒ 只要原版云里早就存过铁板（几乎必然），之后往上传站塞的
     * 每一块铁板都进账本、永不入池、面板永远空白 = 「凭空消失」。传送带那头看到的是货到站
     * 就没了、站内也不显示 ⇒ 「输入不进去」。
     *
     * ## 现在的做法
     * ① `Super::Tick` / `Super::Factory_Tick` **恢复调用** —— 原版「缓冲 → 原版云」的计时、
     *    配额检查、网络通知、面板进度条全部归原版管；
     * ② 再叠一层自己的 `PumpBufferToCentralStorage()`：**只调原版公开 API**
     *    （`AFGCentralStorageSubsystem::CanUploadInventoryItemToCentralStorage` +
     *    `UploadItemFromInventoryToCentralStorage`），不依赖 Super 是否被桩吃掉，也让上传
     *    速度不卡在原版每秒一件上。
     *    ★1.0.7 线程纪律：它**只在 `Tick()`（游戏线程）** 里跑；`Factory_Tick` 是原版
     *    `TickFactoryActors` 的 ParallelFor 生产线程，在那里调原版上传入口会触发
     *    `check(IsInGameThread())` 断言崩游戏（2026-10-27 实测崩溃，详见函数注释）。
     * ③ 货始终真实存在于**本地缓冲 或 原版云池**这两处玩家看得见的地方，账本不再承接新货。
     *
     * `mLedger` 字段保留：① 老存档里已经黑洞掉的货要靠回退 ticker 迁进原版池；
     * ② 流体站/下载站仍在用同一套账本 helper。
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

    // ---- ★1.0.8BI 本机上传速率（面板行内「正在上传 ↑60/分」的读数来源）------------------
    //
    // 用户 2026-10-28 场景：「同一个物品，一边上传，一边下载」—— 上传站与下载站共用同一个云端池，
    // 池里的数字是两边相抵后的净值，**看不出各自的流量**。所以上传站必须能报"我这台在往上传多少"。
    //
    // ★在此之前本类**完全没有**速率接口（只有下载站有）：上传侧的速率没有任何采样点，
    //   面板只能写「上传站 · 只读」。现在在 PumpBufferToCentralStorage 里按类记样本。
    // 口径与下载站完全一致（`WT_SampleRatePerMinute`，5 秒窗口，件/分钟），只统计
    // **真的推进了原版池**的件数（`GetNumItemsFromCentralStorage` 的前后差），
    // 不是"尝试上传多少"：配额满/被原版拒收时上传速率为 0，这正是玩家要看的事实。
    float GetUploadRatePerMinute() const;

    /** 每一类物品各自的上传速度（件/分钟）；只填窗口内出现过样本的类。 */
    void GetItemRatesPerMinute(TMap<UClass*, float>& OutRates) const;

private:
    /**
     * ★1.0.8BI 记一次「本拍真的上传了多少件某一类」。
     *
     * 调用点在 PumpBufferToCentralStorage 的按类累加处（一拍一次，不是每件一次 ——
     * 每件一次会在窗口里塞进几十个样本，让"跨度"塌到毫秒级、读数抖动）。
     * 与下载站的 RecordPull 同款：先淘汰窗口外的头部样本，再追加。
     */
    void RecordUploadSample(UClass* Item, int32 Amount);

    /** ★1.0.8BI 上传速率窗口（秒），与下载站 WT_RATE_WINDOW 保持一致。 */
    static constexpr float WT_RATE_WINDOW = 5.f;

    /**
     * ★1.0.8BI 上传样本环形窗口（最多约 3~10 个）。
     *
     * UPROPERTY 的理由同下载站 `mPullSamples`：元素里的 Item 是 TSubclassOf，
     * 不反射就是 GC 看不见的引用。**不入存档**（纯运行期读数）。
     */
    UPROPERTY()
    TArray<FWT_PullSample> mUploadSamples;

    /**
     * ★1.0.6 把本地缓冲推进原版云池（**只在 `Tick()` 里调**，内部节流 + 游戏线程守卫）。
     *
     * 旧名 `DrainInventoryToLedger` —— 那时是「整格抽干进自己的账本」，正是消失 bug 的来源。
     * 现在语义反了：**原地推进原版池**，货从缓冲直接进玩家看得见的云端，不经过账本。
     * 走原版公开 API，因此配额上限、物品过滤、通知/复制全部由原版负责。
     *
     * ★★★1.0.7 起**禁止**在 `Factory_Tick()` 里调用：原版 `AFGBuildableSubsystem::TickFactoryActors`
     *   用 `ParallelFor` 并行回调 `Factory_Tick`（生产线程），而原版上传入口会同步广播
     *   `mOnCentralStorageItemAmountUpdated` ⇒ 购物车 `UpdateShoppingList()` ⇒
     *   `FTimerManager::FindTimer()` ⇒ `check(IsInGameThread())` 直接崩游戏。
     *   2026-10-27 04:50 的崩溃（UECC-Windows-9EA759F0…）就是这条路径，callstack 见 .cpp。
     */
    void PumpBufferToCentralStorage();

    /**
     * ★0.2.42 传送带入口（用户反馈「云端固体接不了传送带」的同一根因）。
     *
     * 引擎源码 `AFGBuildableStorage::AFGBuildableStorage()` 只建 `StorageInventory`，
     * **不含任何连接组件** —— 原版位面存储站上传器的连接组件同样是蓝图里摆的。
     * 所以 C++ 子类必须自建，传送带才有点可接。本类只摆**输入**（上传站只进不出）。
     */
    UPROPERTY()
    TObjectPtr<class UFGFactoryConnectionComponent> mConveyorIn = nullptr;

    /** ★2026-11-02：BeginPlay 运行时经 WT_CloneVanillaSCSComponent 创建的发光面片组件。
     *   直接持有指针，拆除时精确 DestroyComponent（不依赖按名查找，避免 NewObject 重名后缀 /
     *   GetFName 匹配失效导致拆除后残留「外星纹理方块 + 光膜」）。 */
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> mGlowPlane = nullptr;

    /** 本站累计上传、尚未注入原版池的库存（存档持久化）。 */
    UPROPERTY(SaveGame)
    TArray<FItemAmount> mLedger;

    /**
     * ★1.0.6 上一次把缓冲推进原版云池的世界时间（秒）。纯 POD，不需要 UPROPERTY（非 UObject
     * 引用，GC 无关），也不该存档。
     *
     * 为什么必须节流：本函数有**两条**触发路径 ——
     *   ① 自己的 PrimaryActorTick（TickInterval = 0.5s）；
     *   ② 工厂 tick 子系统的 `Factory_Tick`：基类链上 `AFGBuildableStorage : AFGBuildableFactory`，
     *      而 AFGBuildableFactory 的 ctor 把 `mFactoryTickFunction.bStartWithTickEnabled = true`、
     *      `TickInterval = 0.0`，**AFGBuildableStorage 的 ctor 并不关掉它**；`FFactoryTickFunction`
     *      的目标是 AFGBuildableSubsystem（批处理），最终回调各建筑的虚函数 `Factory_Tick`。
     * ⇒ 不节流的话，同一份库存（阶段 5 时 10x10 = 100 格）一个周期会被扫两遍，甚至每帧一遍。
     *   节流放在函数**内部**：无论哪条路径先到、另一条会不会到，推进频率恒定。
     */
    double mLastBufferPumpTime = -1.0;

    /**
     * ★1.0.6 每轮 pump 最多向原版云提交多少次 `UploadItemFromInventoryToCentralStorage`。
     *
     * 原版这个入口按**格**提交、但内部一次只消化一件（见 WT_UploadStackToPool 的实测注释），
     * 所以「1 次调用 = 1 件」是保守估计。每轮 32 次 × 每秒 2 轮 = 64 件/秒：既远快于原版
     * 每秒一件的手感，又不会在一帧里把 100 格搬空导致卡顿。
     */
    static constexpr int32 WT_MAX_UPLOADS_PER_PUMP = 32;
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

    /** Re-apply storage grid + resize the live inventory from the current researched level (idempotent). */
    void ApplyResearchedCapacity();

    /** ★2026-11-02：显式销毁 BeginPlay 运行时加的发光面片（WTCloudGlowPlane），防止拆除后残留。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** ★2026-11-02：BeginDestroy 兜底——若拆解路径未触发 EndPlay，此处必然触发，确保光膜组件被销毁。 */
    virtual void BeginDestroy() override;
    /** ★2026-11-02：拆除开始即销毁光膜（同上传站），避免拆除特效期间露出原始外星纹理方块。 */
    virtual void Dismantle_Implementation() override;
    virtual void Tick(float dt) override;

    /** Size the inventory grid from the current Storage research level (ctor + BeginPlay). */
    void ApplyStorageGrid();

    static FText GetDisplayName();

    // ---- ★1.0.7n 面板「下载速度」----------------------------------------------------
    //
    // 用户诉求（2026-10-27）：「加上一个下载速度」。
    // 口径 = **最近 WT_RATE_WINDOW 秒实际搬进本站缓冲的件数**，折算成「件/分钟」
    // （与传送带 60/min、120/min 的读法一致，玩家可以直接和下游带速对比）。
    //   * 只统计「真的进了缓冲」的量（Move/Grab/AddStack 的有效返回值），不是「尝试搬多少」；
    //   * 缓冲满了搬不动 ⇒ 样本为 0 ⇒ 速度自然掉到 0，这正是玩家需要看到的事实；
    //   * 世界时间取 GetTimeSeconds()（Tick 与面板同进程，不受暂停/编辑器影响）。
    // 面板侧每 0.5s 读一次即可（见 UWTItemSelectPanelWidget::RefreshSpeedText）。
    float GetDownloadRatePerMinute() const;

    /**
     * ★1.0.7p 每一类物品各自的下载速度（件/分钟），供面板行内「下载速度」列使用。
     *
     * 只返回**窗口内出现过样本**的类（其余类填 0 即可，面板显示为「--」）——
     * 这样面板不必对上百行逐一调用 O(样本) 的算法，一次拿全表。
     * 口径与 GetDownloadRatePerMinute 完全一致，只是把样本先按类筛一遍。
     */
    void GetItemRatesPerMinute(TMap<UClass*, float>& OutRates) const;

    // ---- ★1.0.7 面板选型：输出白名单 ------------------------------------------------
    //
    // 用户定案「云端下载站的 UI 重构，采用新面板，全物品中选择需要输出的物品」。
    // 语义（★1.0.7x 改口，用户 2026-10-27「列表并未选择任何材料，但仍在下载」）：
    //   * `mOutputFilter` 为空  = **一件都不搬**（显式选择制）。玩家没点过【选择】就停着不动，
    //     面板标题会写「输出：未选择（不会下载）」把这件事说清楚；
    //   * `mOutputFilter` 非空  = 白名单，只搬勾选的种类，其余一动不动。
    //   旧口径「空 = 搬全部」已作废：它让机器在玩家什么都没选时自己搬货，玩家判定为 bug。
    // 面板（UWTItemSelectPanelWidget）只是这份数据的视图，自己不藏状态。
    void SetOutputFilter(const TArray<TSubclassOf<UFGItemDescriptor>>& InFilter);

    /** 面板点一行：在 / 不在白名单里就切换（加或删）。 */
    void ToggleOutputItem(TSubclassOf<UFGItemDescriptor> ItemClass);

    /** 该物品是否允许被本站拉取（白名单为空 ⇒ 一件都不允许）。 */
    bool IsOutputAllowed(TSubclassOf<UFGItemDescriptor> ItemClass) const;

    const TArray<TSubclassOf<UFGItemDescriptor>>& GetOutputFilter() const { return mOutputFilter; }

    // ---- ★1.0.7o 下载速度 = 原版云端上传速度 --------------------------------------
    //
    // 用户定案（2026-10-27）：「把原版的上传速度搬过来，改成下载速度」。
    // 即下载站不再用「每 2 秒一批、每批最多一组」的固定节拍，而是**沿用原版上传口径的限速**：
    //   原版每上传 1 件要 `GetCentralStorageTimeToUpload()` 秒（该值已包含 MAM 的
    //   Central Storage Upload Speed 解锁倍率，见 UFGCentralStorageSettings 的注释）
    //   ⇒ 本文下载站也按「每 T 秒 1 件」搬，上传/下载速度对称。
    //
    // 为什么读子系统而不是读设置：`UFGCentralStorageSettings::mDefaultTimeToUpload` 是**基础值**，
    // 玩家解锁加速后真实生效的是子系统那个已经乘过倍率的版本。
    /** 原版「上传 1 件需要多少秒」（含解锁倍率）。取不到时回落到设置默认值，再不行 1 秒。 */
    float GetVanillaUploadSecondsPerItem() const;

    /** 下载速度上限（件/分钟）= 60 ÷ 原版上传秒数。面板显示用。 */
    float GetDownloadRateLimitPerMinute() const;

private:
    /**
     * 从云端取货，**最多 MaxItems 件**（由原版上传速度累积出的额度决定）。
     * @return 真正搬进缓冲的件数（0 = 这一拍没搬到，额度不扣、也别囤）。
     */
    int32 TryPullFromCloud(int32 MaxItems);

    /**
     * ★1.0.7n 记录一次**成功**搬运（Amount = 真正进缓冲的件数）到滑动窗口。
     *
     * 只在 Move/Grab/AddStack 的返回值 > 0 时调用 —— 「取到货但放不进去」不算下载速度，
     * 那属于缓冲堵塞（面板上的速度掉到 0 就是这个含义）。
     * 里面顺便淘汰过期样本，所以数组长度恒 ≤ 窗口/间隔 ≈ 3 个，无内存增长。
     */
    void RecordDownloadSample(int32 Amount, UClass* Item);

    /**
     * ★1.0.7p 本地算法：从样本数组算「件/分钟」。
     *
     * @param Samples   按时间升序的样本
     * @param Now       当前游戏时间
     * @param OnlyItem  非空 = 只看这一类的样本（面板行内读数）；空 = 全部（整体读数）
     *
     * 口径（1.0.7n 定案，勿改）：**丢掉窗口内最老的那批量**，只算它之后的量 ÷
     * 「最老样本 → 最新样本」的时间跨度。用「窗口总量 ÷ 窗口长度」会系统性高估
     * （离散搬运、5 秒窗口常落 3 个样本 ⇒ 高 20%）。
     */
    static float WT_RatePerMinute(const TArray<FWT_PullSample>& Samples, float Now, UClass* OnlyItem);

    /** ★1.0.7n 下载速度滑动窗口长度（秒）。速率 = 窗口内件数 × 60 ÷ 窗口长度。 */
    static constexpr float WT_RATE_WINDOW = 5.f;

    /**
     * ★1.0.7n/p 滑动窗口样本：(游戏时间秒, 件数, 物品类)，按时间升序。
     * 运行期状态、不进存档；UPROPERTY 只为让 GC 看见样本里的 UClass 引用。
     */
    UPROPERTY()
    TArray<FWT_PullSample> mPullSamples;

    /**
     * ★1.0.7o 取货额度（件）：每 Tick 按 `dt / 原版上传秒数` 累积，够 1 件才搬。
     *
     * 取代 1.0.7n 及以前的 `mPullInterval=2s` 固定节拍（那个是「每 2 秒搬一组」，
     * 稳态实测约 1200 件/分，和原版上传的 60 件/分完全不对称）。
     * 上限 = 两拍的额度（≥1 件），见 Tick 里 CreditCap 的说明：堵塞期间不囤积、解堵不爆量。
     */
    float mPullCredit = 0.f;

    /** ★1.0.7o 限速口径日志限流（变了才打，避免每帧刷屏）。 */
    float mLastRateLimitLogTime = -1000.f;

    /**
     * ★1.0.7j 诊断限流：白名单非空却整轮没搬成时，每 10 秒最多打一条 WT_WARNF。
     *
     * 为什么需要：`[downloader] pulled ...` 那两条是 WT_LOGF，被 WT_LOG_VERBOSE=false 整句编译剔除，
     * Shipping 日志里根本没有 —— 「面板选了但货不动」时完全无法从日志判断卡在哪一步。
     * 这里不改搬运逻辑，只补一条可查的现状快照（白名单首个类 / 缓冲占用 / 原版云端是否已建）。
     */
    float mLastIdleDiagTime = -1000.f;

    /**
     * ★1.0.7m 「搬货成功」日志限流（10 秒一条）。
     *
     * 旧版成功日志是 WT_LOGF，被 `WT_LOG_VERBOSE=false` 整句编译剔除 ⇒ Shipping 里
     * 「面板选了、货也搬了」和「一件都没搬」在日志上长得一模一样。改成 WT_WARNF 后必须限流：
     * 持续下载时每 2 秒就会成功一次，不限流会把日志刷满。
     */
    float mLastPullLogTime = -1000.f;

    /**
     * ★1.0.7 输出白名单（面板选择，SaveGame 持久化）。
     *
     * ★必须 UPROPERTY：TSubclassOf 持有 UClass*，不反射就是 GC 看不见的强引用（与
     *   Remote_FactoryPanelWidget 同一条 CRASH GUARD）。
     * ★必须 SaveGame：玩家选完重进存档还得记得，否则每局都要重选一遍。
     * 存入前按类名排序（见 SetOutputFilter），保证同一份选择写出来的存档字节稳定。
     */
    UPROPERTY(SaveGame)
    TArray<TSubclassOf<UFGItemDescriptor>> mOutputFilter;

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

    /** ★2026-11-02：BeginPlay 运行时经 WT_CloneVanillaSCSComponent 创建的发光面片组件。
     *   直接持有指针，拆除时精确 DestroyComponent（不依赖按名查找，避免残留）。固体下载站同上传站。 */
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> mGlowPlane = nullptr;
};


/**
 * 云端液态上传站（★1.0.6 恢复：本版把「云端液体两台站」重新加回；方孔处接原版缓冲罐的管道接口）。
 *
 * ## 数据通路（全部走原版 API，不自算流体）
 *
 *   管网 ──Factory_PullPipeInput──> 本站 ──记入**我们自己的液态账本**──> 云端（我们的账）
 *
 *   * 管网把流体送进本站的管道接口（连接组件的 mFluidDescriptor 由管网下发）；
 *   * Tick 周期调 `UFGPipeConnectionComponent::Factory_PullPipeInput` 把流体按原版
 *     m³↔件换算率拉成一「库存栈」（与原版精炼厂拉输入同一入口）；
 *   * 把件数按**物品类**记入本站自己的 `mFluidLedger`（SaveGame 持久化），**不直接进原版池** ——
 *     于是原版云端始终是原版的，我们的货独立记账（用户定案「云端固液分开」）。
 *   * 下载站取货时先查原版池，原版池没有才动我们的液态账本（见下载站实现）。
 *
 * ## 为什么继承自 AFGBuildablePipeReservoir
 * 原版缓冲罐自带流体机制（FFluidBox / 压强结算 / 连接组件数组），我们白拿它即可，
 * 不需要自己搓流体模拟。唯一要补的是**自建两个管道接口**——原版缓冲罐的接口是蓝图里
 * 摆的，C++ CDO 没有；且 `AFGBuildablePipeReservoir::BeginPlay` 会索引第 2 个接口
 * （1 个接口 ⇒ "index 1 into array of size 1" 崩溃），所以必须建满两个。
 *
 * ## 外观（与固体一致）
 *   主体保持**云端建模** `SM_CentralStorage_01`（同固体两站，Yaw180 朝向修正），
 *   圆口视觉件 `WT_AddRoundPortVisual` 叠在管道锚点外侧；管道接口摆原版仓库口坐标
 *   `WT_VANILLA_PORT_LOC`(0,390,100)/Yaw90，与固体两站同挂点、**方向一致**
 *   （上传站=从本地管网吸入进云端、下载站=从云端排进本地管网，均朝外 +Y）。
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
     * 管道接口 ×2（原版缓存罐 BeginPlay 索引第 2 个接口，1 个必崩；两个口必须都存在）。
     * 类型 PCT_ANY（取货走 Factory_PullPipeInput，与类型无关），余量 1.5m。
     * 固定摆原版仓库口坐标 (0,390,100)/Yaw90（与固体两站一致）。
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
 * 云端液态下载站（★1.0.6 恢复：与上传站配套的流体出口）。
 *
 * 数据通路与上传站镜像：
 *
 *   云端池 ──TryRemoveItemsFromCentralStorage──> 本站 ──Factory_PushPipeOutput──> 管网
 *   我们的液态账本 ──（原版池该类为 0 时兜底）────────────────┘
 *
 *   * 选型：按 E 打开原生选型面板（UWTFluidSelectPanelWidget，纯 C++ 构建），列出云端里现有的流体，点选即锁定；
 *   * Tick 周期**先取原版云端池**（每周期最多 50 件，原版公开 API，天然幂等），
 *     原版池没有该类流体时，才从我们各液态上传站的账本兜底取；
 *   * 再推回管网：走原版生产端入口 Factory_PushPipeOutput。
 *
 * ★防丢料两道闸：
 *   1) 推管网前先查 IsConnected()，没接管子不扣货；
 *   2) 推送返回实际入网量，没推出去的差额立刻退回我们的液态账本（管网满/类型冲突时）。
 *   没选流体 / 没接管子 / 云里没货，三个条件任一不满足就静默空转。
 *
 * ★管道接口 ×2：同上传站，必须给满两个（原版缓存罐 BeginPlay 索引第 2 个接口），
 *   这里类型是 PCT_ANY（我们往管网供货）。
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

    /** 选定的流体注回管网用的双接口（与上传站同一组坐标，方向一致）。 */
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

    /** Re-apply the production boost from the current researched level (called by the research-advance pulse; idempotent). */
    void ApplyResearchedCapacity();

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
    bool IsUpgradeAccelerator(const UClass* Class);
    bool IsUpgradeCloudFluidUploader(const UClass* Class);
    bool IsUpgradeCloudFluidDownloader(const UClass* Class);

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
