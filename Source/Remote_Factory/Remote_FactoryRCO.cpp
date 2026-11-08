#include "Remote_FactoryRCO.h"

#include "Remote_FactoryLoc.h"
#include "Remote_FactoryCollectibles.h"
#include "Remote_FactoryLog.h"
#include "Remote_FactoryCrashTrace.h"
#include "Remote_FactorySmartWarehouse.h"                // ★2026-11-03 右侧面板取出：TransferFromContainersToInventory
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGBuildableResourceExtractor.h"
#include "Buildables/FGBuildableResourceExtractorBase.h"
#include "Buildables/FGBuildableGenerator.h"
#include "Buildables/FGBuildableResourceSink.h"
#include "Buildables/FGBuildableResourceSinkShop.h"   // ★0.2.146 AWESOME 商店
#include "Buildables/FGBuildableMAM.h"                // ★0.2.146 MAM 稳定原生类（取代纯 widget 兜底）
#include "Buildables/FGBuildableSpaceElevator.h"
#include "Buildables/FGBuildableAutomatedWorkBench.h"
#include "Buildables/FGBuildableHubTerminal.h"
#include "Buildables/FGBuildableTradingPost.h"             // ★0.2.153 枢纽相关：AFGBuildableTradingPost + UFGTradingPostIntegratedBuildable
#include "FGBuildableSubsystem.h"                          // ★0.2.153 AFGBuildableSubsystem::Get / 模板 GetTypedBuildable
#include "FGItemPickup.h"                                  // AFGItemPickup（收集品扫描 / 传送落点判定）
#include "FGDropPod.h"                                     // AFGDropPod（空投舱扫描）
#include "Buildables/FGBuildableCircuitSwitch.h"           // AFGBuildableCircuitSwitch（远程开关）
#include "Buildables/FGBuildablePowerPole.h"               // ★1.0.9 电线杆 AFGBuildablePowerPole（GetPowerCircuit/EPowerPoleType）
#include "FGPowerConnectionComponent.h"                    // ★1.0.9 UFGPowerConnectionComponent::IsConnected（inline，FGCircuitConnectionComponent.h:118）
#include "Buildables/FGBuildablePriorityPowerSwitch.h"      // AFGBuildablePriorityPowerSwitch（优先级开关）
#include "Remote_FactorySharedSession.h"                   // Remote_FactoryShared::IsSessionOwner / SetViewer
#include "Components/CapsuleComponent.h"                   // ★0.2.153 重建：UCapsuleComponent（枢纽/落点胶囊体）
#include "Components/ChildActorComponent.h"               // ★1.0.28 UChildActorComponent::GetChildActor（矿点载体第三种拓扑：CACE 不走 attach 链）
#include "FGPowerInfoComponent.h"                          // ★0.2.153b 电力总览：建筑上的电力信息组件
#include "FGFactoryConnectionComponent.h"                  // ★1.0.11 物流未连接判定（GetConnection/IsConnected 均为 inline，FGFactoryConnectionComponent.h:96/111）
#include "FGPipeConnectionComponent.h"                     // ★1.0.11 管道接口未连接判定（GetConnection inline 读 mConnectedComponent，FGPipeConnectionComponent.h:69）
#include "FGPowerCircuit.h"                                // ★0.2.153b UFGPowerCircuit::GetStats / FPowerCircuitStats
#include "Buildables/FGBuildablePowerStorage.h"            // ★0.2.153b 电池储能 GetPowerStore/GetPowerStoreCapacity
#include "Buildables/FGBuildableResourceSinkShop.h"        // ★0.2.153b AWESOME 商店分类（此前落 Other 被丢）
#include "Resources/FGItemDescriptor.h"                    // ★0.2.153m UFGItemDescriptor::GetBigIcon（列表行图标）
#include "Internationalization/Internationalization.h"      // ★0.2.153t FInternationalization::Get().GetCurrentCulture()（按游戏语言决定要不要用中文表）
#include "Internationalization/Culture.h"                   // ★0.2.153t FCulture::GetTwoLetterISOLanguageName()（Internationalization.h 只前向声明 FCulture，不解引用）
#include "FGVehicle.h"                                      // ★0.2.153m AFGVehicle（运输工具扫描：轮式载具/火车/无人机）
#include "Engine/World.h"                                  // ★本轮：TActorIterator<AFGVehicle>（枚举载具 footprint 避让飞行器）
#include "GameFramework/Volume.h"                          // ★本轮：AVolume（落点障碍检测跳过音频/后处理/触发体积）
#include "FGRecipe.h"                                       // ★0.2.153p UFGRecipe::GetProducts（生产机器产出物图标）
#include "Resources/FGResourceNode.h"                       // ★0.2.153p AFGResourceNode（AFGBuildableResourceExtractorBase::GetResourceNode 的返回类型，需完整定义才能调 GetResourceClass）
#include "Resources/FGResourceNodeBase.h"                   // ★1.0.27 AFGResourceNodeBase 完整定义（资源节点载体判定 Cast/GetResourceNodeType；FGResourceNode.h 只间接涉及）
#include "FGPortableMiner.h"                                 // ★2026-11-02 便携式采矿机：AFGPortableMiner : public AActor（非 AFGBuildable），单独遍历入【采矿设备】
#include "WorldPartition/WorldPartitionSubsystem.h"            // ★2026-11-02 远距传送卡顿缓解：UWorldPartitionSubsystem::IsStreamingCompleted / OnUpdateStreamingState / RegisterStreamingSourceProvider
#include "Components/WorldPartitionStreamingSourceComponent.h" // ★2026-11-02 注入临时流式源（实现 IWorldPartitionStreamingSourceProvider）
#include "WorldPartition/WorldPartitionRuntimeCell.h"           // ★2026-11-02 EWorldPartitionRuntimeCellState::Loaded（IsStreamingCompleted 入参枚举；subsystem/组件头未传递包含，必须显式 include）
#include "Components/SceneComponent.h"                        // ★2026-11-02 临时流式源载体的根组件
#include "GameFramework/PlayerController.h"                   // ★2026-11-02 GetFirstPlayerController / GetPawn（预流式距离门槛）
#include "HAL/PlatformProcess.h"                              // ★2026-11-02 FPlatformProcess::Sleep（预流式轮询）
#include "FGGameState.h"                                     // ★1.0.28 AFGGameState::GetNodePuritySettings（全局纯度兜底：运行期放置节点未走世界初始化随机化）
#include "Remote_FactoryResourceNodes.h"                    // ★1.0.27 FWTResourceNodes::LoadSpawns/NodeKey（实时矿点条目与静态库去重）
#include "Resources/FGExtractableResourceInterface.h"         // ★0.2.153z11d 矿种权威接口 GetExtractableResource()（替代 deprecated 的 GetResourceNode）
#include "ModLoading/ModLoadingLibrary.h"                   // ★0.2.153p UModLoadingLibrary::GetLoadedModInfo（MOD建筑友好名）
#include "Buildables/FGBuildablePipeReservoir.h"            // ★0.2.153m AFGBuildablePipeReservoir（液体容器）
#include "Buildables/FGBuildablePortalBase.h"               // ★0.2.153m AFGBuildablePortalBase（外星传送门/传送门卫星）
#include "Buildables/FGBuildablePortal.h"                   // ★0.2.153r AFGBuildablePortal::GetFuelInventory（传送门消耗物图标）
#include "Buildables/FGBuildableGeneratorFuel.h"            // ★0.2.153r AFGBuildableGeneratorFuel::GetCurrentFuelClass / GetFuelInventory（发电机消耗物图标）
#include "Buildables/FGBuildableRailroadStation.h"          // ★0.2.153z 车站：AFGBuildableRailroadStation（铁路车站，GetStationIdentifier）
#include "Buildables/FGBuildableDockingStation.h"           // ★0.2.153z 车站：AFGBuildableDockingStation（车辆停靠站，GetIsInLoadMode/GetInventory）
#include "FGTrainStationIdentifier.h"                       // ★0.2.153z 铁路车站真实站名 AFGTrainStationIdentifier::GetStationName
#include "WheeledVehicles/FGWheeledVehicle.h"               // ★0.2.153r AFGWheeledVehicle::GetStorageInventory（运输工具货物图标）
#include "EngineUtils.h"                                  // ★0.2.153 重建：TActorIterator 模板完整定义
#include "NavigationSystem.h"                             // ★0.2.153c: UNavigationSystemV1::ProjectPointToNavigation（枢纽落点 navmesh 兜底）
#include "Net/UnrealNetwork.h"                             // DOREPLIFETIME（GetLifetimeReplicatedProps）
#include "FGPlayerController.h"                            // AFGPlayerController::GetRemoteCallObjectOfClass
#include "FGCharacterPlayer.h"                            // AFGCharacterPlayer::GetInventory
#include "Buildables/FGBuildableFactory.h"                 // AFGBuildableFactory::MoveOrDropInventory / GetOutputInventory
#include "FGInventoryComponent.h"                          // UFGInventoryComponent
#include "Misc/FileHelper.h"                               // ★0.2.153e 落点表持久化
#include "Misc/Paths.h"                                    // ★0.2.153e FPaths::ProjectSavedDir
#include "Serialization/JsonReader.h"                      // ★0.2.153e TJsonReaderFactory
#include "Serialization/JsonSerializer.h"                  // ★0.2.153e FJsonSerializer
#include "Dom/JsonObject.h"                                // ★0.2.153e FJsonObject
#include "Dom/JsonValue.h"                                 // ★0.2.153e FJsonValue
#include "TimerManager.h"                                  // ★0.2.153e 传后地面校正定时器
#include "Engine/OverlapResult.h"                          // ★0.2.153n FOverlapResult（落点被挡时打印挡路组件名）
#include "FGCrate.h"                                        // ★1.0.8 死亡箱子 AFGCrate + EFGCrateType（容器类目新条目）
// ★1.0.7i 「里程碑阶段」筛选的数据源：建筑 -> 建造描述符 -> 配方 -> 蓝图阶段
#include "Resources/FGBuildingDescriptor.h"                 // UFGBuildingDescriptor（mBuildableClass 反射读取）
#include "FGSchematic.h"                                    // UFGSchematic::GetTechTier / GetUnlocks
#include "Unlocks/FGUnlockRecipe.h"                         // UFGUnlockRecipe::GetRecipesToUnlock
#include "UObject/UObjectIterator.h"                        // TObjectIterator（三张阶段表一次性建表）

// ----------------------------------------------------------------------------
// ★0.2.153 重建：WT_HasAuthority
//
// 原定义所在的 include 在上轮编辑事故（_rf_edit_0153.py 误删 RCO.cpp 顶部若干
// #include + 函数头）中一并丢失，全工程检索（含 F:/ E:/ 全盘）无任何残留定义。
// 此处按原语义重建：RCO 的 Server RPC 实现必须运行在服务端权威侧
// （"server owns the character transform"），用「世界存在 AuthGameMode」判定 ——
// 监听 / 专用服务器均返回 true，纯客户端返回 false。等价原
// AFGPlayerController::HasAuthority() 的判权威意图，且不依赖已丢失的玩家控制器句柄。
// ----------------------------------------------------------------------------
static inline bool WT_HasAuthority(const UObject* Obj)
{
    if (!Obj)
    {
        return false;
    }
    UWorld* World = Obj->GetWorld();
    return World && (World->GetAuthGameMode() != nullptr);
}

// ----------------------------------------------------------------------------
// ★0.2.153 重建：下列 8 个成员函数体被上轮编辑事故（_rf_edit_0153.py）整段误删，
//   头文件声明仍在、全工程无残留（F:/ D:/ E:/ 全盘 / git / 回收站均无备份）。按声明 + SDK 现有
//   API 重建：Get / GetLifetimeReplicatedProps / 三个 Client 广播 / Server_RequestOpenUI 为确定性
//   重建；Server_ScanMachines / Server_PullOutput 按 SDK API 重建，目标"能编过 + 不崩溃 + 进游戏可验"。
//   注：全图电力统计 / 收集品 / 共享会话字段暂置默认，待对原始源码或实测再补。
// ----------------------------------------------------------------------------
URemote_FactoryRCO* URemote_FactoryRCO::Get(AFGPlayerController* PC)
{
    if (!PC)
    {
        return nullptr;
    }
    return PC->GetRemoteCallObjectOfClass<URemote_FactoryRCO>();
}

void URemote_FactoryRCO::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(URemote_FactoryRCO, DummyReplicatedField);
}

// ----------------------------------------------------------------------------
// ★0.2.153p MOD 建筑识别（用户实测：「MOD建筑，我的列表中读取不到」）。
// 根因：0.2.153 重建扫描时把 0.2.146 的 Modded 分支漏掉了 ⇒ 第三方 MOD 建筑（父类是裸
// AFGBuildable / AFGBuildableFactory，不命中 Manufacturer/Storage/Generator… 任何一条 Cast）
// 一律落进 Other，而 Other 不在面板任何分组里（= 直接看不见）。
// 判据取**包路径**而不是类名黑名单：原版 C++ 类在 /Script/FactoryGame，原版蓝图/资产在
// /Game/FactoryGame；SML 挂载的 MOD 内容一律在 /<ModReference>/... 下，故「两段前缀之外」即 MOD。
// ----------------------------------------------------------------------------
static bool WT_IsVanillaClass(const UClass* Cls)
{
    if (!Cls || !Cls->GetOutermost())
    {
        return true;   // 判不出来就当原版，绝不把原版建筑误塞进 MOD 组
    }
    const FString Pkg = Cls->GetOutermost()->GetName();
    return Pkg.StartsWith(TEXT("/Script/FactoryGame"))
        || Pkg.StartsWith(TEXT("/Game/FactoryGame"))
        || Pkg.StartsWith(TEXT("/Engine/"));
}

/** ★0.2.153t 当前 UI 语言是否为中文（zh*）。英文游戏下不强塞中文，交给原生/SML 名字。 */
static bool WT_IsChineseUI()
{
    // FInternationalization::GetCurrentCulture() → FCultureRef（Internationalization.h:63），
    // GetTwoLetterISOLanguageName()（Culture.h:79）：zh-Hans / zh-Hant / zh-CN 都是 "zh"。
    return FInternationalization::Get().GetCurrentCulture()->GetTwoLetterISOLanguageName() == TEXT("zh");
}

/**
 * ★0.2.153t 建筑显示名走**原生翻译**（用户：「你就不能走原生游戏翻译吗」）。
 *   ① AFGBuildable::mDisplayName —— FText，引擎按当前语言求值（zh-Hans 下就是中文）；
 *   ② 建造它所用描述符的名字 UFGItemDescriptor::GetItemName()（FGItemDescriptor.h:191，同为 FText）
 *      —— 这就是**建造菜单/HUD 里显示的那一行**，与游戏原生显示完全同源。很多 MOD 建筑压根不填
 *      mDisplayName，旧代码此时退化成 C++ 类名（日志实锤过 "WirelessPowerRelay" 这种）；
 *   ③ 类名（最后兜底）。
 * ※ 若某 MOD 连描述符名也是英文，那是**该 MOD 没提供 locres**（引擎日志里能看到
 *   "LocRes .../Mods/<mod>/Content/Localization/.../xx.locres could not be opened"），
 *   游戏自己的建造菜单里同样是英文——这种我们补不了，也不该硬编一个名字。
 */
static FText WT_BuildableDisplayName(const AFGBuildable* B)
{
    if (!B)
    {
        return FText::GetEmpty();
    }
    const FText Own = B->mDisplayName;
    if (!Own.IsEmpty())
    {
        return Own;
    }
    if (const TSubclassOf<UFGItemDescriptor> Desc = B->GetBuiltWithDescriptor())
    {
        const FText ItemName = UFGItemDescriptor::GetItemName(Desc);
        if (!ItemName.IsEmpty())
        {
            return ItemName;
        }
    }
    return FText::FromString(B->GetClass()->GetName());
}

/**
 * ★1.0.7i 里程碑阶段（建筑）—— 面板「里程碑阶段」筛选维度的唯一数据源。
 *
 * 链路（一手，逐环都有头文件依据）：
 *   可建造类 ──mBuildableClass──> 建造描述符(Desc_*_C) ──产物──> 配方(Recipe_*_C)
 *            ──UFGUnlockRecipe──> 蓝图(Schematic_*_C) ──GetTechTier──> 阶段号
 *
 * 为什么不用 AFGBuildable::GetBuiltWithDescriptor()：它是游戏侧非 inline 实现，
 * 在 SML 下返回值不可靠。这里改成「反射读描述符 CDO 的 mBuildableClass」—— 该字段在
 * FGBuildingDescriptor.h:74 是 protected，但 UPROPERTY 已反射，FindFProperty 就读得到，
 * 与 access specifier 无关。其余静态访问器（UFGRecipe::GetProducts / UFGSchematic::GetUnlocks /
 * GetTechTier）与物品面板的目录构建用的是同一套调用，那条链在本机实测可用。
 *
 * 三张表只建一次：蓝图/配方都是静态数据，运行期不会变；单次成本 = 三遍类集合遍历，
 * 之后每次扫描只是三次 TMap 查表。查不到（MOD 建筑 / 无配方的道具）返回 99 = 未归类。
 */
static UClass* WT_ReadDescriptorBuildableClass(UClass* DescClass)
{
    if (!DescClass)
    {
        return nullptr;
    }
    UObject* CDO = DescClass->GetDefaultObject();
    if (!CDO)
    {
        return nullptr;
    }
    static const FName PropName(TEXT("mBuildableClass"));
    FProperty* Prop = FindFProperty<FProperty>(DescClass, PropName);
    FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop);
    if (!ObjProp)
    {
        return nullptr;
    }
    return Cast<UClass>(ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(CDO)));
}

static int32 WT_BuildingMilestoneTier(UClass* BuildClass)
{
    if (!BuildClass)
    {
        return 99;
    }

    static bool bBuilt = false;
    static TMap<UClass*, int32> TierByRecipe;    // 配方类 -> 阶段
    static TMap<UClass*, int32> TierByDesc;      // 建造描述符类 -> 阶段
    static TMap<UClass*, UClass*> DescByBuild;   // 可建造类 -> 建造描述符类
    static TMap<UClass*, int32> Memo;            // 可建造类 -> 结果（扫描每 15s 一次，避免重复解析）

    if (!bBuilt)
    {
        bBuilt = true;
        const double T0 = FPlatformTime::Seconds();

        // ① 配方 -> 阶段：同一配方被多个蓝图发放时取最小阶段（最早能造出来的那一档）。
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* S = *It;
            if (!S || !S->IsChildOf(UFGSchematic::StaticClass()))
            {
                continue;
            }
            if (S->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
            {
                continue;
            }
            const int32 Tier = UFGSchematic::GetTechTier(S);
            for (UFGUnlock* U : UFGSchematic::GetUnlocks(S))
            {
                UFGUnlockRecipe* RecipeUnlock = Cast<UFGUnlockRecipe>(U);
                if (!RecipeUnlock)
                {
                    continue;
                }
                for (const TSubclassOf<UFGRecipe>& R : RecipeUnlock->GetRecipesToUnlock())
                {
                    if (UClass* RC = R.Get())
                    {
                        int32& Slot = TierByRecipe.FindOrAdd(RC, 99);
                        Slot = FMath::Min(Slot, Tier);
                    }
                }
            }
        }

        // ② 建造描述符 -> 阶段：配方的产物里凡是建造描述符的都记一份。
        int32 RecipeWithTier = 0;
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* R = *It;
            if (!R || !R->IsChildOf(UFGRecipe::StaticClass()))
            {
                continue;
            }
            if (R->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
            {
                continue;
            }
            const int32* TierPtr = TierByRecipe.Find(R);
            if (!TierPtr)
            {
                continue;
            }
            ++RecipeWithTier;
            for (const FItemAmount& Product : UFGRecipe::GetProducts(R))
            {
                UClass* ItemCls = Product.ItemClass.Get();
                if (!ItemCls || !ItemCls->IsChildOf(UFGBuildingDescriptor::StaticClass()))
                {
                    continue;
                }
                int32& Slot = TierByDesc.FindOrAdd(ItemCls, 99);
                Slot = FMath::Min(Slot, *TierPtr);
            }
        }

        // ③ 可建造类 -> 建造描述符（反查表）。
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* D = *It;
            if (!D || !D->IsChildOf(UFGBuildingDescriptor::StaticClass()))
            {
                continue;
            }
            if (D->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
            {
                continue;
            }
            if (UClass* B = WT_ReadDescriptorBuildableClass(D))
            {
                DescByBuild.Add(B, D);
            }
        }

        WT_WARNF(TEXT("[milestone] tier table built in %.1f ms: recipes=%d withTier=%d desc=%d buildable=%d"),
                 (FPlatformTime::Seconds() - T0) * 1000.0,
                 TierByRecipe.Num(), RecipeWithTier, TierByDesc.Num(), DescByBuild.Num());
    }

    if (const int32* Cached = Memo.Find(BuildClass))
    {
        return *Cached;
    }
    int32 Tier = 99;
    if (UClass* const* Desc = DescByBuild.Find(BuildClass))
    {
        if (const int32* T = TierByDesc.Find(*Desc))
        {
            Tier = *T;
        }
    }
    Memo.Add(BuildClass, Tier);
    return Tier;
}

/** ★0.2.153t 载具版：同一条原生取值链（AFGVehicle::GetBuiltWithDescriptor，FGVehicle.h:242）。 */
static FText WT_VehicleDisplayName(const AFGVehicle* V)
{
    if (!V)
    {
        return FText::GetEmpty();
    }
    const FText Own = V->mDisplayName;
    if (!Own.IsEmpty())
    {
        return Own;
    }
    if (const TSubclassOf<UFGItemDescriptor> Desc = V->GetBuiltWithDescriptor())
    {
        const FText ItemName = UFGItemDescriptor::GetItemName(Desc);
        if (!ItemName.IsEmpty())
        {
            return ItemName;
        }
    }
    return FText::FromString(V->GetClass()->GetName());
}

// ----------------------------------------------------------------------------
// ★1.0.28l 资源关键词表（MOD 矿点汉化 + 包装建筑↔矿点配对共用）
// PEP 类「可放置资源节点」MOD 没有 zh-Hans locres ⇒ 原生 GetItemName 只能给英文
// （实测 'Limestone Node (Impure)' / 'Raw Biomass'），而同一颗节点在层②遍历里又能拿到
// 游戏自带的中文资源名 ⇒ ① 按关键词把英文名映射成原生中文资源名；② 用同一批关键词判定
// 「包装建筑」与「真实矿点 actor」是否同一颗节点（避免同位置出两条）。
// 关键词一律小写、单资源词根（Build_IronOre_* 与 Desc_OreIron_C 都能命中 "iron"）。
struct FWT_ResKeyword
{
    const TCHAR* Key;
    const TCHAR* Zh;
};
static const FWT_ResKeyword GWT_ResKeywords[] = {
    { TEXT("limestone"), TEXT("石灰石") },
    { TEXT("iron"),      TEXT("铁矿石") },
    { TEXT("copper"),    TEXT("铜矿石") },
    { TEXT("caterium"),  TEXT("锍") },
    { TEXT("coal"),      TEXT("煤") },
    { TEXT("sulfur"),    TEXT("硫磺") },
    { TEXT("quartz"),    TEXT("石英") },
    { TEXT("bauxite"),   TEXT("铝土矿") },
    { TEXT("uranium"),   TEXT("铀") },
    { TEXT("mycelia"),   TEXT("菌丝体") },
    { TEXT("nitrogen"),  TEXT("氮气") },
    { TEXT("biomass"),   TEXT("生物质") },
    { TEXT("oil"),       TEXT("原油") },
    { TEXT("water"),     TEXT("水") },
    { TEXT("sam"),       TEXT("SAM") },
};

/** ★1.0.28l 两个名字是否指向同一资源（PEP 包装建筑 ↔ 真实矿点 actor 配对判据之一）。 */
static bool WT_SameResourceKeyword(const FString& A, const FString& B)
{
    const FString LA = A.ToLower();
    const FString LB = B.ToLower();
    for (const FWT_ResKeyword& K : GWT_ResKeywords)
    {
        if (LA.Contains(K.Key) && LB.Contains(K.Key))
        {
            return true;
        }
    }
    return false;
}

/** ★1.0.28l 英文矿点名 → 原生中文资源名。仅在中文 UI + 名字里没有中文时尝试；
 *  认不出的英文名返回 false（原样保留，绝不臆造）。
 *  OutPurityIdx：从 "(Impure)/(Normal)/(Pure)" 后缀解析出的纯度档（0 贫瘠 / 1 普通 / 2 纯）。 */
static bool WT_LocalizeResourceName(const FString& In, FString& OutZh, int32& OutPurityIdx)
{
    OutPurityIdx = -1;
    if (In.IsEmpty() || !WT_IsChineseUI())
    {
        return false;
    }
    for (const TCHAR C : In)
    {
        if (C >= 0x4E00 && C <= 0x9FFF)
        {
            return false;   // 含中文 ⇒ 原生已翻译，不要动
        }
    }
    const FString Low = In.ToLower();
    if (Low.Contains(TEXT("(impure)")))       { OutPurityIdx = 0; }
    else if (Low.Contains(TEXT("(normal)")))  { OutPurityIdx = 1; }
    else if (Low.Contains(TEXT("(pure)")))    { OutPurityIdx = 2; }
    for (const FWT_ResKeyword& K : GWT_ResKeywords)
    {
        if (Low.Contains(K.Key))
        {
            OutZh = K.Zh;
            return true;
        }
    }
    return false;
}

// ----------------------------------------------------------------------------
// ★0.2.153r MOD 名中文映射（用户要求：MOD 建筑列表显示中文）
// SML 只给 FModInfo::FriendlyName（多数 MOD 是英文）。取值优先级：
//   ① RF_mod_names.json（Saved/Remote_Factory/，用户可编辑，键 = mod reference）→ 最优先，自己改的算数；
//   ② 内置中文表（只收录能准确意译的 mod，绝不臆造）；
//   ③ SML FriendlyName；
//   ④ mod reference 原文。
// 文件**首次运行时自动生成模板**：把本机已加载的全部 mod 写进去（值默认 FriendlyName），
// 用户把它改成中文即可生效——未收录的 mod 不会凭空编名字。
// ----------------------------------------------------------------------------
static FString WT_ModNameFilePath()
{
    // 与落点表同目录（Saved/Remote_Factory 子目录刻意不随 mod 改名）
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Remote_Factory"), TEXT("RF_mod_names.json"));
}

static TMap<FString, FString> WT_LoadModNameOverrides()
{
    TMap<FString, FString> Out;
    FString Raw;
    if (!FFileHelper::LoadFileToString(Raw, *WT_ModNameFilePath()))
    {
        return Out; // 文件不存在：空表（随后由 WT_EnsureModNameTemplate 生成模板）
    }
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        WT_WARNF(TEXT("[mod-name] file corrupt, starting empty: %s"), *WT_ModNameFilePath());
        return Out;
    }
    for (const auto& Pair : Root->Values)
    {
        FString V;
        if (Pair.Value.IsValid() && Pair.Value->TryGetString(V) && !V.IsEmpty())
        {
            Out.Add(Pair.Key, V);
        }
    }
    return Out;
}

// 进程内读一次就够（改文件需重开游戏生效，避免每个 MOD 建筑都去读盘）。
static const TMap<FString, FString>& WT_CachedModNameOverrides()
{
    static TMap<FString, FString> Cache;
    static bool bLoaded = false;
    if (!bLoaded)
    {
        Cache = WT_LoadModNameOverrides();
        bLoaded = true;
    }
    return Cache;
}

static const TMap<FString, FString>& WT_BuiltinModNames()
{
    // 键 = mod reference **或** C++ 模块名（两者常同名，但不必）。只放能准确意译的；
// 拿不准的一律不收，留给 json / FriendlyName。
    static TMap<FString, FString> M = {
        // ★自家：UE 模块名刻意保留 Remote_Factory（插件已改名 Smart_Factory），两个键都收。
        {TEXT("Remote_Factory"),            TEXT("智能工厂")},
        {TEXT("Smart_Factory"),             TEXT("智能工厂")},
        {TEXT("SmartFactoryLib"),           TEXT("智能工厂库")},
        {TEXT("FasterManualCraftingRedux"), TEXT("更快手工制作（重制版）")},
        {TEXT("RecipeCopier"),              TEXT("配方复制器")},
        {TEXT("ThirdPersonView"),           TEXT("第三人称视角")},
        // ★0.2.153u WirelessPower 的 C++ 模块名与 mod reference 不同名（包路径首段= WirelessPower，
        // SML FModInfo.Name = WirelessPower_Remake），两个键都收，分组标题才命中中文。
        {TEXT("WirelessPower_Remake"),      TEXT("无线电力（重制版）")},
        {TEXT("WirelessPower"),             TEXT("无线电力（重制版）")},
        {TEXT("Additional_300_Inventory_Slots"), TEXT("额外 300 格背包")},
        {TEXT("MarcioCommonLibs"),          TEXT("Marcio 公共库")},
        {TEXT("MinoDabsCommonLib"),         TEXT("MinoDabs 公共库")},
    };
    return M;
}

/**
 * 首次运行把本机已加载 mod 全写进 json 当翻译模板（★0.2.153s 值优先用**内置中文表**，
 * 没有中文才退回 FriendlyName）——否则模板里的英文会因为「json 最优先」把内置中文挡住
 * （本局实锤：模板写了 "RecipeCopier":"Settings Copier"，中文表名永远轮不到）。
 * 文件**已存在**时只做「升级」：仅当该项仍是默认值（= FriendlyName/Name，说明用户没改过）
 * 且内置表有中文，才替换为中文；用户手改过的值一律保留。
 */
static void WT_EnsureModNameTemplate(UWorld* World)
{
    // 一次会话只试一次（否则每个 MOD 建筑都读一遍盘）
    static bool bChecked = false;
    if (bChecked)
    {
        return;
    }
    bChecked = true;
    UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    UModLoadingLibrary* Lib = GI ? GI->GetSubsystem<UModLoadingLibrary>() : nullptr;
    if (!Lib)
    {
        return;
    }
    const TMap<FString, FString>& Builtin = WT_BuiltinModNames();

    FString Raw;
    const bool bHaveFile = FFileHelper::LoadFileToString(Raw, *WT_ModNameFilePath()) && !Raw.IsEmpty();
    TSharedPtr<FJsonObject> Root;
    if (bHaveFile)
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            return; // 坏文件：绝不动，免得毁掉用户内容
        }
    }
    else
    {
        Root = MakeShared<FJsonObject>();
    }

    bool bChanged = false;
    for (const FModInfo& Info : Lib->GetLoadedMods())
    {
        if (Info.Name.IsEmpty())
        {
            continue;
        }
        const FString DefaultName = Info.FriendlyName.IsEmpty() ? Info.Name : Info.FriendlyName;
        FString Cur;
        const bool bHas = Root->TryGetStringField(Info.Name, Cur);
        // ★0.2.153t 模板同样跟随语言：中文游戏下才把默认英文替换成内置中文
        const FString* Zh = WT_IsChineseUI() ? Builtin.Find(Info.Name) : nullptr;
        if (Zh)
        {
            // 内置中文优先：模板/未改过的项直接写中文
            if (!bHas || Cur.IsEmpty() || Cur == DefaultName)
            {
                Root->SetStringField(Info.Name, *Zh);
                bChanged = true;
            }
        }
        else if (!bHas)
        {
            // 没中文译名：只补缺失项（值用 FriendlyName，用户可自己改）
            Root->SetStringField(Info.Name, DefaultName);
            bChanged = true;
        }
    }
    if (!bChanged)
    {
        return; // 无需写盘（用户已自行改过 或 模板已是最新）
    }
    FString OutString;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
    if (FJsonSerializer::Serialize(Root.ToSharedRef(), Writer)
        && FFileHelper::SaveStringToFile(OutString, *WT_ModNameFilePath(),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        WT_WARNF(TEXT("[mod-name] template %s: %s"),
                 bHaveFile ? TEXT("upgraded") : TEXT("written"), *WT_ModNameFilePath());
    }
}

/**
 * ★0.2.153s 包路径 → **候选** mod reference 列表（按把握排序）。
 * 教训（本局日志实锤 `[scan-cat] modded ... mod='Script'`）：**包路径首段不是 mod reference**！
 *   - C++ 类的包 = "/Script/<模块名>"          （如 /Script/WirelessPower_Remake → 首段 "Script" ❌）
 *   - BP 类的包   = "/Game/<ModRef>/..."        （SML 把内容挂到 /Game 下）
 *   - 内容 mod    = "/<ModRef>/..."             （SML 默认挂载根，首段即 mod reference ✓）
 * ⇒ 首段是引擎固定根（Script/Game/Engine）时取**下一节**，并把首段留作兜底候选。
 */
static void WT_ModRefCandidates(const UClass* Cls, TArray<FString>& Out)
{
    Out.Reset();
    if (!Cls || !Cls->GetOutermost())
    {
        return;
    }
    FString Pkg = Cls->GetOutermost()->GetName();
    if (Pkg.StartsWith(TEXT("/")))
    {
        Pkg = Pkg.Mid(1);
    }
    TArray<FString> Parts;
    Pkg.ParseIntoArray(Parts, TEXT("/"), /*bCullEmpty=*/true);
    if (Parts.Num() == 0)
    {
        return;
    }
    if (Parts[0] == TEXT("Script") || Parts[0] == TEXT("Game") || Parts[0] == TEXT("Engine"))
    {
        if (Parts.Num() > 1)
        {
            Out.Add(Parts[1]);      // 真正的模块名 / mod reference
        }
        Out.Add(Parts[0]);          // 兜底候选（基本查不到，保留只为可解释）
    }
    else
    {
        Out.Add(Parts[0]);          // SML 挂载根 = mod reference
    }
}

/** 单个候选取名：① 用户 json → ② 内置中文表 → ③ SML FriendlyName；都不中返回 false。 */
static bool WT_TryResolveModNameOne(const FString& Ref, UWorld* World, FString& OutName)
{
    if (Ref.IsEmpty())
    {
        return false;
    }
    if (const FString* Zh = WT_CachedModNameOverrides().Find(Ref))
    {
        OutName = *Zh;
        return true;
    }
    // ★0.2.153t 内置中文表只在**中文游戏**下启用：英文环境交给原生/SML 名字，不强塞中文。
    if (WT_IsChineseUI())
    {
        if (const FString* Zh2 = WT_BuiltinModNames().Find(Ref))
        {
            OutName = *Zh2;
            return true;
        }
    }
    if (World)
    {
        if (UGameInstance* GI = World->GetGameInstance())
        {
            if (UModLoadingLibrary* ModLib = GI->GetSubsystem<UModLoadingLibrary>())
            {
                FModInfo Info;
                if (ModLib->GetLoadedModInfo(Ref, Info) && !Info.FriendlyName.IsEmpty())
                {
                    OutName = Info.FriendlyName;
                    return true;
                }
                // ★0.2.153u 兜底：C++ 模块名与 mod reference 不同名时（如 WirelessPower vs
                // WirelessPower_Remake），按「互为包含」匹配已加载 mod。限较短串长度 >= 5，
                // 防止 "SML" 这类超短名被任意候选误吞。取第一个命中的。
                for (const FModInfo& M : ModLib->GetLoadedMods())
                {
                    if (M.Name.Len() < 5 || Ref.Len() < 5)
                    {
                        continue;
                    }
                    if ((Ref.Contains(M.Name) || M.Name.Contains(Ref)) && !M.FriendlyName.IsEmpty())
                    {
                        OutName = M.FriendlyName;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/**
 * MOD 友好名（分组标题用）。逐个试候选 mod reference，第一个取到名字的即返回；
 * 全不中就退回首个候选（模块名），**绝不返回 "Script"/"Game" 这种引擎根名**。
 */
static FString WT_ResolveModName(UWorld* World, const UClass* Cls)
{
    TArray<FString> Cands;
    WT_ModRefCandidates(Cls, Cands);
    if (Cands.Num() == 0)
    {
        return FString();
    }
    WT_EnsureModNameTemplate(World);   // 首次运行生成翻译模板（内部只做一次）
    for (const FString& Ref : Cands)
    {
        FString Name;
        if (WT_TryResolveModNameOne(Ref, World, Name))
        {
            return Name;
        }
    }
    return Cands[0];
}

// ----------------------------------------------------------------------------
// ★0.2.153r 图标辅助（用户要求：从【容器】开始往下的类目都要有图标——产出物用产出物图标，
// 消耗物用消耗物图标）。统一走 UFGItemDescriptor::GetBigIcon（与建造菜单同一张图，不是自绘素材）。
// ----------------------------------------------------------------------------
static UTexture2D* WT_ItemIcon(const TSubclassOf<UFGItemDescriptor>& Desc)
{
    return Desc ? UFGItemDescriptor::GetBigIcon(Desc) : nullptr;
}

/**
 * 取库存里第一个非空格子的物品图标：容器存货 / 发电机燃料 / 传送门电池 / 商店货架 /
 * 太空电梯交付物 / 回收站票券 都走这一条（避免为每种建筑写一遍遍历）。
 * UFGInventoryComponent::GetInventoryStacks 是 const 成员（FGInventoryComponent.h:451），
 * FInventoryStack::HasItems() / FInventoryItem::GetItemClass()（:103 / :45）同为 const。
 */
static UTexture2D* WT_FirstStackIcon(const UFGInventoryComponent* Inv)
{
    if (!Inv)
    {
        return nullptr;
    }
    TArray<FInventoryStack> Stacks;
    Inv->GetInventoryStacks(Stacks, /*getEmptyStacks=*/false);
    for (const FInventoryStack& S : Stacks)
    {
        if (S.HasItems())
        {
            if (UTexture2D* T = WT_ItemIcon(S.Item.GetItemClass()))
            {
                return T;
            }
        }
    }
    return nullptr;
}

/**
 * ★0.2.153z 机器运行状态文本（用户要求：生产 / 采矿设备显示运行 / 待机 / 阻塞）。
 * 直接读引擎 EProductionStatus（AFGBuildableFactory::GetCachedProductionState，FGBuildable.h:534，已一手核实）。
 * 该枚举值：IS_NONE / IS_PRODUCING / IS_PRODUCING_WITH_CRYSTAL / IS_STANDBY / IS_ERROR。
 */
static FText WT_ProdStateText(const AFGBuildableFactory* F)
{
    if (!F)
    {
        return FText::GetEmpty();
    }
    switch (F->GetCachedProductionState())
    {
        case EProductionStatus::IS_PRODUCING:
        case EProductionStatus::IS_PRODUCING_WITH_CRYSTAL:
            return FText::FromString(TEXT("运行中"));
        case EProductionStatus::IS_STANDBY:
            return FText::FromString(TEXT("待机"));
        case EProductionStatus::IS_ERROR:
            return FText::FromString(TEXT("阻塞"));
        default:
            return FText::FromString(TEXT("空闲"));
    }
}

/**
 * ★1.0.9 把一条统计通道降采样到最多 MaxPoints 个点，**每段取最大值**。
 *
 * 为什么取最大而不是平均：用户要看的就是峰值，曲线上的尖峰周围全是低值，
 * 取平均会把尖峰抹平（300 点压到 120 点，一个孤立的尖峰会被摊到相邻点上）。
 * 取最大值保证「这条线画出来的最高点 == 这段时间真实的最高点」。
 *
 * 为什么降采样：FPowerCircuitStats 的环形缓冲有 300+ 点，五条线原样复制是每条电网
 * 6KB 以上；面板一屏要画的宽度也就 300px 左右，120 点已经比像素还密。
 */
static void WT_DownsamplePoints(const TArray<float>& Raw, int32 MaxPoints, TArray<float>& Out)
{
    Out.Reset();
    const int32 N = Raw.Num();
    if (N <= 0)
    {
        return;
    }
    if (MaxPoints <= 0 || N <= MaxPoints)
    {
        Out = Raw;
        return;
    }

    const int32 Step = FMath::DivideAndRoundUp(N, MaxPoints);
    Out.Reserve(FMath::DivideAndRoundUp(N, Step));
    for (int32 i = 0; i < N; i += Step)
    {
        const int32 End = FMath::Min(i + Step, N);
        float M = Raw[i];
        for (int32 k = i + 1; k < End; ++k)
        {
            M = FMath::Max(M, Raw[k]);
        }
        Out.Add(M);
    }
}

/**
 * ★1.0.8 生产状态码（供 UI 侧彩色指示灯取色，随条目一并复制到客户端）。
 *
 * 与 WT_ProdStateText 共用同一份 EProductionStatus 判定，只是返回整数码 —— 两个函数必须
 * 保持同源：面板侧的 WT_ProdStateColor / WT_ProdStateText 也按这同一套编码 switch，
 * 所以「灯色」与「状态文字」在结构上不可能对不上（改一处必须改四处，编译期就能看出漏改）。
 *
 * 编码与 FWT_MachineEntry::ProdState 注释一致：0=空闲 1=运行 2=待机 3=阻塞。
 */
static uint8 WT_ProdStateCode(const AFGBuildableFactory* F)
{
    if (!F)
    {
        return 0;
    }
    switch (F->GetCachedProductionState())
    {
        case EProductionStatus::IS_PRODUCING:
        case EProductionStatus::IS_PRODUCING_WITH_CRYSTAL:
            return 1;
        case EProductionStatus::IS_STANDBY:
            return 2;
        case EProductionStatus::IS_ERROR:
            return 3;
        default:
            return 0;
    }
}

/**
 * ★0.2.153z 库存首格文本（用户要求：容器 / 运输工具 / 车站显示存放 / 运输物品）。
 * 返回 "物品名 ×数量"，空库存返回 "空"。复用 WT_FirstStackIcon 同款 GetInventoryStacks 遍历
 * （FGInventoryComponent.h:451 / FInventoryStack::NumItems:116 / FInventoryStack::HasItems:104，已一手核实）。
 */
static FText WT_FirstStackText(const UFGInventoryComponent* Inv)
{
    if (!Inv)
    {
        return FText::GetEmpty();
    }
    TArray<FInventoryStack> Stacks;
    Inv->GetInventoryStacks(Stacks, /*getEmptyStacks=*/false);
    for (const FInventoryStack& S : Stacks)
    {
        if (S.HasItems())
        {
            const TSubclassOf<UFGItemDescriptor> Cls = S.Item.GetItemClass();
            if (Cls)
            {
                return FText::Format(FText::FromString(TEXT("{0} ×{1}")),
                                     UFGItemDescriptor::GetItemName(Cls),
                                     FText::AsNumber(S.NumItems));
            }
        }
    }
    return FText::FromString(TEXT("空"));
}

/**
 * ★1.0.10 「已存在物品」筛选：把单个物品描述符记进标签表。
 * 按 UClass 名去重（不是按显示名 —— 显示名会随语言变，且 mod 物品可能重名）。
 * 单行的物品数很少（容器格子 / 配方产物 / 燃料），线性查重足够，不值得上 TSet。
 */
static void WT_AddItemTag(TSubclassOf<UFGItemDescriptor> Cls, TArray<FWT_ItemTag>& Out)
{
    if (!Cls)
    {
        return;
    }
    const FString Key = Cls.Get()->GetName();
    for (const FWT_ItemTag& Existing : Out)
    {
        if (Existing.Key == Key)
        {
            return;
        }
    }
    FWT_ItemTag Tag;
    Tag.Key   = Key;
    Tag.Label = UFGItemDescriptor::GetItemName(Cls).ToString();
    Out.Add(MoveTemp(Tag));
}

/**
 * ★1.0.10 整个库存里的物品标签。
 * ★刻意收「每一格」而不是只收首格：玩家开这个下拉的动机是「哪个箱子有铁板」，
 * 只记首格会让第二格才装铁板的箱子筛不出来。
 */
static void WT_CollectInvItemTags(const UFGInventoryComponent* Inv, TArray<FWT_ItemTag>& Out)
{
    if (!Inv)
    {
        return;
    }
    TArray<FInventoryStack> Stacks;
    Inv->GetInventoryStacks(Stacks, /*getEmptyStacks=*/false);
    for (const FInventoryStack& S : Stacks)
    {
        if (S.HasItems())
        {
            WT_AddItemTag(S.Item.GetItemClass(), Out);
        }
    }
}

/**
 * ★1.0.10 库存总件数（「数量」筛选维度的数据源）。
 * 与 WT_CollectInvItemTags 同一个遍历口径（GetInventoryStacks 只取非空堆）。
 */
static int32 WT_InvTotalItems(const UFGInventoryComponent* Inv)
{
    if (!Inv)
    {
        return 0;
    }
    TArray<FInventoryStack> Stacks;
    Inv->GetInventoryStacks(Stacks, /*getEmptyStacks=*/false);
    int32 Total = 0;
    for (const FInventoryStack& S : Stacks)
    {
        if (S.HasItems())
        {
            Total += S.NumItems;
        }
    }
    return Total;
}

/**
 * ★1.0.11 容器填充率（「填充率」筛选维度 / 已满·已空问题位的数据源）。
 *
 * 口径 = **已占用格数 / 总格数 × 100**，不是按件数。理由见 FWT_MachineEntry::FillPercent
 * 注释：不同物品堆叠上限不同，按件数比会得出「装电缆的箱子比装铁板的满」这种反直觉结论。
 *
 * 必须用 getEmptyStacks=true 取全部格：取非空堆时总格数就是已用格数，算出来恒 100%。
 */
static float WT_InvFillPercent(const UFGInventoryComponent* Inv)
{
    if (!Inv)
    {
        return -1.f;
    }
    TArray<FInventoryStack> Stacks;
    Inv->GetInventoryStacks(Stacks, /*getEmptyStacks=*/true);
    if (Stacks.Num() <= 0)
    {
        return 0.f;
    }
    int32 Used = 0;
    for (const FInventoryStack& S : Stacks)
    {
        if (S.HasItems())
        {
            ++Used;
        }
    }
    return 100.f * static_cast<float>(Used) / static_cast<float>(Stacks.Num());
}

/**
 * ★1.0.11 物流连接判定：这栋建筑有没有「接口」，其中是否有任意一个真的接上了。
 *
 * 语义刻意分两层，因为「没接口」和「有接口但没接」是两回事：
 *   - 枢纽终端 / MAM 这类建筑本来就没有传送带口 ⇒ bHasPorts=false ⇒ **不算**未连接物流
 *     （否则整个枢纽类目都会被标成「孤岛」，问题徽标就失去意义了）；
 *   - 有口却一个都没接 ⇒ 返回 false 且 bHasPorts=true ⇒ 记 NoLogistics 问题位。
 *
 * 只用 GetConnection() 这种 inline 读 UPROPERTY 的访问器（FGFactoryConnectionComponent.h:96 /
 * FGPipeConnectionComponent.h:69），不碰需要走 .cpp 的 IsConnected() —— 后者在 SML 环境
 * 属于「能链上就用、链不上就废」的高风险符号，而这里读的是同一份 mConnectedComponent。
 */
static bool WT_LogisticsConnected(const AActor* A, bool& bOutHasPorts)
{
    bOutHasPorts = false;
    if (!A)
    {
        return true;
    }

    TArray<UFGFactoryConnectionComponent*> Conns;
    A->GetComponents<UFGFactoryConnectionComponent>(Conns);
    for (const UFGFactoryConnectionComponent* C : Conns)
    {
        if (!C)
        {
            continue;
        }
        bOutHasPorts = true;
        if (C->GetConnection() != nullptr)
        {
            return true;
        }
    }

    TArray<UFGPipeConnectionComponent*> Pipes;
    A->GetComponents<UFGPipeConnectionComponent>(Pipes);
    for (const UFGPipeConnectionComponent* P : Pipes)
    {
        if (!P)
        {
            continue;
        }
        bOutHasPorts = true;
        if (P->GetConnection() != nullptr)
        {
            return true;
        }
    }
    return false;
}

/**
 * ★1.0.11 一次性填好「问题 / 效率 / 填充率 / 耗电」四个新字段（用户方案稿的筛选数据源）。
 *
 * 集中在一个函数里而不是散进各 if-else 类目分支：这些量对**所有**建筑都成立，
 * 散着写必然出现「某个类目漏填 ⇒ 那个类目筛问题永远 0 条」的静默错误。
 * 成本：每栋建筑一次组件内遍历 + 两次 GetInventoryStacks（只对容器），
 * 与已有的图标/物品标签填充同量级，相对 15s 一次的全图扫描可以忽略。
 */
static void WT_FillQualityFields(AFGBuildable* B, uint8 Cat, FWT_MachineEntry& E)
{
    if (!B)
    {
        return;
    }

    // ---- 生产状态派生的两个问题位 + 效率 ----
    if (const AFGBuildableFactory* F = Cast<AFGBuildableFactory>(B))
    {
        // ★效率只在「真的有产出」时才可信。原版 GetProductivity 是滑动窗口统计，
        // 从未跑过的机器会给出 0 —— 那属于「没开机」而不是「效率低」，用 -1 区分开，
        // 免得玩家筛「效率 <50%」时被一堆从未启动的机器淹掉。
        const uint8 PS = WT_ProdStateCode(F);
        E.Efficiency01 = (PS == 1) ? FMath::Clamp(F->GetProductivity(), 0.f, 1.f) : -1.f;
        if (PS == 3)
        {
            E.IssueFlags |= WT_IssueBit(EWT_Issue::OutputBlocked);
        }
        else if (PS == 2)
        {
            E.IssueFlags |= WT_IssueBit(EWT_Issue::InputStarved);
        }
        // ★超频判据用「待定电位」而不是「当前电位」：玩家刚推完滑条、还没跑完一个周期时，
        // 当前电位仍是旧值，用当前值会漏掉刚超频的那批机器。
        if (F->GetPendingPotential() > 1.001f)
        {
            E.IssueFlags |= WT_IssueBit(EWT_Issue::Overclocked);
        }
    }

    // ---- 电力 ----
    if (const UFGPowerInfoComponent* PIC = B->FindComponentByClass<UFGPowerInfoComponent>())
    {
        E.PowerMW = PIC->GetActualConsumption();
        if (!PIC->IsConnected())
        {
            E.IssueFlags |= WT_IssueBit(EWT_Issue::NoPower);
        }
    }

    // ---- 物流 ----
    {
        bool bHasPorts = false;
        const bool bConnected = WT_LogisticsConnected(B, bHasPorts);
        if (bHasPorts && !bConnected)
        {
            E.IssueFlags |= WT_IssueBit(EWT_Issue::NoLogistics);
        }
    }

    // ---- 燃料 ----
    if (const AFGBuildableGeneratorFuel* G = Cast<AFGBuildableGeneratorFuel>(B))
    {
        if (!G->GetCurrentFuelClass())
        {
            E.IssueFlags |= WT_IssueBit(EWT_Issue::FuelStarved);
        }
    }

    // ---- 容器填充率（已满 / 已空）----
    if (Cat == static_cast<uint8>(EWT_MachineCategory::Container))
    {
        const UFGInventoryComponent* Inv = nullptr;
        if (AFGBuildableStorage* S = Cast<AFGBuildableStorage>(B))
        {
            Inv = S->GetStorageInventory();
        }
        E.FillPercent = WT_InvFillPercent(Inv);
        if (E.FillPercent >= 99.f)
        {
            E.IssueFlags |= WT_IssueBit(EWT_Issue::Full);
        }
        else if (E.FillPercent >= 0.f && E.FillPercent <= 0.01f)
        {
            E.IssueFlags |= WT_IssueBit(EWT_Issue::Empty);
        }
    }
}

void URemote_FactoryRCO::Server_ScanMachines_Implementation(FVector Origin, float MaxRangeMeters)
{
    FWT_MachineListReply Reply;
    Reply.CategoryCounts.SetNumZeroed(static_cast<int32>(EWT_MachineCategory::Count));

    // ★1.0.27 修复「skyblock 类地图放置的资源节点进 MOD 建筑栏」（用户 2026-10-25 实测）：
    //   根因：可放置资源节点 = AFGBuildable 载体 + 挂接的 AFGResourceNodeBase 子 actor。
    //   SDK 一手核实 AFGResourceNodeBase : AFGStaticReplicatedActor（FGResourceNodeBase.h:93），
    //   与 AFGBuildable 单继承不相交 ⇒ Cast<AFGResourceNodeBase>(B) 对纯 buildable 恒失败，
    //   只能走「挂接子 actor」判据。旧代码把载体当裸 buildable 归进 MOD 建筑桶。
    //   修法：① 载体建筑转成实时资源节点条目（Target=null 坐标传送，与静态库条目同构），
    //         且永远不进建筑列表；② 另扫世界中不挂载体的独立 mod 矿点 actor 补齐。
    //   去重口径与静态库一致（FWTResourceNodes::NodeKey = 资源名+坐标米）——库里已有的全图
    //   条目质量更高（带里程碑/纯度审计），实时只补玩家新放置的。
    TSet<FString> LibNodeKeys;
    {
        TArray<FWT_ResourceNodeSpawn> LibNodes;
        if (FWTResourceNodes::LoadSpawns(LibNodes, nullptr))
        {
            LibNodeKeys.Reserve(LibNodes.Num());
            for (const FWT_ResourceNodeSpawn& S : LibNodes)
            {
                LibNodeKeys.Add(FWTResourceNodes::NodeKey(S));
            }
        }
    }

    // ★1.0.28l bKeyOnly：已被 PEP 包装建筑配对的矿点只做存在性记账（LiveNodeKeys），
    // 不再下发条目 —— 否则同一颗节点在列表里出两条（用户实测 3 节点 6 行）。
    auto AppendLiveNode = [&](AFGResourceNodeBase* N, bool bKeyOnly)
    {
        if (!N)
        {
            return;
        }
        const TSubclassOf<UFGItemDescriptor> RCls = N->GetResourceClass();
        const FString ResName = RCls ? UFGItemDescriptor::GetItemName(RCls).ToString() : FString(TEXT("资源节点"));
        const FVector Loc = N->GetActorLocation();
        FWT_ResourceNodeSpawn KeySpawn;
        KeySpawn.ResourceName = ResName;
        KeySpawn.Location = Loc;
        // ★1.0.28 存在性记账：在任何过滤（库去重/范围）之前记录「世界中确实有这颗矿点」，
        // 供客户端把静态库中附近已消失的条目判为拆除（见 FWT_MachineListReply::LiveNodeKeys）。
        Reply.LiveNodeKeys.Add(FWTResourceNodes::NodeKey(KeySpawn));
        if (LibNodeKeys.Contains(FWTResourceNodes::NodeKey(KeySpawn)))
        {
            return;   // 静态库已有（全图 dump）⇒ 不重复下发
        }
        if (bKeyOnly)
        {
            return;   // 已被包装建筑条目代表（1.0.28l），只记账
        }
        const float DistM = (Loc - Origin).Size() / 100.f;
        if (MaxRangeMeters > 0.f && DistM > MaxRangeMeters)
        {
            return;   // 遵守扫描范围
        }
        FWT_MachineEntry E;
        E.Target = nullptr;                                  // 非建筑 ⇒ 坐标传送（Server_TeleportToCoord）
        // ★1.0.28l mod 资源描述符没有 zh-Hans locres ⇒ GetItemName 仍是英文（实测 'Raw Biomass'），
        // 中文 UI 下按资源关键词映射成原生中文名；注意 NodeKey 仍用原始 ResName，不影响去重口径。
        {
            FString ZhName;
            int32 PIdx = -1;
            E.DisplayName = FText::FromString(
                WT_LocalizeResourceName(ResName, ZhName, PIdx) ? ZhName : ResName);
        }
        E.Location = Loc;
        E.DistanceMeters = DistM;
        E.BaseRadius = 200.f;                                // 矿点小，落点半径 2m（与静态库条目同口径）
        E.bHasInteractUI = false;                            // 矿点无交互面板 ⇒ 只给[传送]
        const bool bModNode = !WT_IsVanillaClass(N->GetClass());
        E.Category = static_cast<uint8>(bModNode ? EWT_MachineCategory::ResourceNode_Mod
                                                 : EWT_MachineCategory::ResourceNode_Vanilla);
        // 纯度：仅 AFGResourceNode 支系有枚举（井主/Base 支系没有，留空走面板默认）。
        // ★1.0.28 skyblock 纯度兜底：节点自身 GetResourcePurity() 只读逐节点 mPurity/mPurityOverride，
        // 不查 GameMode 全局 mNodePuritySettings（ForceNodePuritySettings 那套，即「全纯」类 MOD 的载体）。
        // 运行期放置的节点没走世界初始化随机化派发 ⇒ mPurityOverride 多为 RP_MAX ⇒ 回退基础 mPurity（原纯度），
        // 导致 skyblock 放置矿点只显示原纯度。这里额外读全局确定性强制档（AllPure/AllNormal/AllImpure）
        // 覆盖显示纯度，与原版世界初始化行为对齐；NPS_NoChange / AllRandom / Increase / Decrease 不覆盖（保留逐节点值）。
        EResourcePurity EffPurity = EResourcePurity::RP_MAX;   // 哨兵：未取到节点纯度（非 AFGResourceNode 保持原行为）
        if (const AFGResourceNode* Node = Cast<AFGResourceNode>(N))
        {
            EffPurity = Node->GetResourcePurity();
            if (UWorld* W = GetWorld())
            {
                if (const AFGGameState* GS = Cast<AFGGameState>(W->GetGameState()))
                {
                    switch (GS->GetNodePuritySettings())
                    {
                    case ENodePuritySettings::NPS_AllPure:   EffPurity = EResourcePurity::RP_Pure;   break;
                    case ENodePuritySettings::NPS_AllNormal: EffPurity = EResourcePurity::RP_Normal; break;
                    case ENodePuritySettings::NPS_AllImpure: EffPurity = EResourcePurity::RP_Inpure; break;
                    default: break;   // NoChange / AllRandom / Increase / Decrease 不覆盖
                    }
                }
            }
        }
        if (EffPurity != EResourcePurity::RP_MAX)
        {
            switch (EffPurity)
            {
            case EResourcePurity::RP_Inpure: E.StatusLine = FText::FromString(TEXT("贫瘠")); E.PurityFilter = 0; break;
            case EResourcePurity::RP_Normal: E.StatusLine = FText::FromString(TEXT("普通")); E.PurityFilter = 1; break;
            case EResourcePurity::RP_Pure:   E.StatusLine = FText::FromString(TEXT("纯"));   E.PurityFilter = 2; break;
            default: break;
            }
        }
        // ★1.0.28k 里程碑序位/阶段头：此前实时条目缺省 MilestoneTier=99 全沉底、无阶段分组头，
        // 与静态库条目（带 tier）混排时面板阶段排列错乱（用户实测）。与 WT_ScanResourceNodes
        // 同口径查表（描述符短名 → 资源显示名兜底），手动放置的原版资源节点归入正确阶段组；
        // mod 独有资源查不中保持 99 沉底（无阶段头，排在最后，属预期）。
        {
            int32 Tier = 99;
            FString TierLabel;
            FWTResourceNodes::LookupMilestone(RCls ? RCls->GetName() : FString(),
                                              ResName, N->GetResourceNodeType(), Tier, TierLabel);
            E.MilestoneTier = Tier;
            E.MilestoneLabel = TierLabel;
        }
        // 图标 best-effort（mod 资源描述符未加载则解析失败留空，与静态库条目同行为）
        if (RCls)
        {
            if (UTexture2D* IconTex = UFGItemDescriptor::GetBigIcon(RCls))
            {
                E.ProductIcon = TSoftObjectPtr<UTexture2D>(IconTex);
            }
        }
        Reply.Entries.Add(E);
        if (Reply.CategoryCounts.IsValidIndex(E.Category))
        {
            Reply.CategoryCounts[E.Category]++;
        }
    };

    if (UWorld* World = GetWorld())
    {
        // ★1.0.28 载体识别重写（用户实测 1.0.27 仍漏：「同时存在 MOD 建筑与 MOD 节点」）。
        // 旧判据 GetAttachedActors 只覆盖 RootComponent 的 attach 树——一手核实
        // AActor::ForEachAttachedActors（Actor.cpp:2815）BFS 只从 RootComponent 出发，
        // 矿点若挂在载体**非 root 组件**（mod 常用的专用挂点网格体）、或载体反而挂在矿点
        // 底下、或矿点坐在 ChildActorComponent 里，三种形态全部漏 ⇒ 载体落进 MOD 建筑桶。
        // 新判据与拓扑方向无关，建筑循环之前一次性建表：
        //   ① 每个世界矿点沿 GetAttachParentActor 向上找直接父 actor（Actor.cpp:2795：
        //      root 组件 attach 父的 owner，与父侧用哪个组件挂接无关）；
        //   ② 每个矿点的 GetAttachedActors 向下找直接子 actor（载体反挂矿点底下的形态）。
        // ③（载体自身 ChildActorComponent 里坐着矿点）在建筑循环内逐建筑查——GetComponents
        //      模板（Actor.h:4005）+ GetChildActor（ChildActorComponent.h:208）。
        // 载体永远不进建筑列表；矿点由层② TActorIterator 统一下发（挂接 actor 同样在
        // 遍历范围内，不在此重复添加，故 1.0.27 的 HandledNodes 防重表已无意义，移除）。
        TSet<const AFGBuildable*> CarrierSet;
        // ★1.0.28l 同一次遍历顺手收「全图节点位置表」与「已被包装建筑配对表」：
        //   前者给下文 PEP 节点去重配对用，后者让层②跳过已配对的矿点（同一颗节点只出一条）。
        TArray<AFGResourceNodeBase*> WorldNodes;
        TSet<AFGResourceNodeBase*> PairedNodes;
        for (TActorIterator<AFGResourceNodeBase> NodeIt(World); NodeIt; ++NodeIt)
        {
            AFGResourceNodeBase* N = *NodeIt;
            if (!IsValid(N))
            {
                continue;
            }
            WorldNodes.Add(N);
            if (AActor* Parent = N->GetAttachParentActor())
            {
                if (AFGBuildable* PB = Cast<AFGBuildable>(Parent))
                {
                    CarrierSet.Add(PB);
                }
            }
            TArray<AActor*> Kids;
            N->GetAttachedActors(Kids);
            for (AActor* K : Kids)
            {
                if (AFGBuildable* KB = Cast<AFGBuildable>(K))
                {
                    CarrierSet.Add(KB);
                }
            }
        }

        if (AFGBuildableSubsystem* BS = AFGBuildableSubsystem::Get(World))
        {
            const TArray<AFGBuildable*>& All = BS->GetAllBuildablesRef();
            for (AFGBuildable* B : All)
            {
                if (!B)
                {
                    continue;
                }

                // ★1.0.28 载体建筑判定（三种拓扑，与挂接方向/类别无关）：
                //   a. CarrierSet（循环前建表）：矿点挂在载体上，或载体反挂在矿点底下；
                //   b. 自身 ChildActorComponent 里坐着矿点（CACE 不保证走 attach 链）。
                // 载体永远不进建筑列表；矿点由层② TActorIterator 统一下发。
                // （1.0.27 旧判据 GetAttachedActors 只认 root attach 树，且带 mod 类别
                //  约束，实测漏判 ⇒ 用户报「同时存在 MOD 建筑与 MOD 节点」，已重写。）
                // ★1.0.28m 节点型 MOD 建筑（PEP 可放置矿点）识别**提前**到载体剔除之前：
                // 这类建筑本身就是矿点（AFGBuildable 派生，非 AFGResourceNodeBase），即使身上
                // 挂/坐着矿点 actor（attach 或 ChildActor 形态）也绝不能当"载体"剔除 —— 否则
                // 用户放的该类节点整条消失。实测症状：列表只剩 _Impure_/_Normal_ 变体，
                // 一个 _Pure_ 都没有（PEP pak 里 Pure 资产齐全，说明是被本规则误伤）。
                const FString NodePkg0 = B->GetClass()->GetOutermost()->GetName();
                const bool bNodeLike = NodePkg0.Contains(TEXT("BuildableResourceNodes")) ||
                                       NodePkg0.Contains(TEXT("NodeBuildables"));
                bool bCarrier = CarrierSet.Contains(B);
                if (!bCarrier)
                {
                    TArray<UChildActorComponent*> CACs;
                    B->GetComponents(CACs);
                    for (UChildActorComponent* CAC : CACs)
                    {
                        if (CAC && CAC->GetChildActor() && Cast<AFGResourceNodeBase>(CAC->GetChildActor()))
                        {
                            bCarrier = true;
                            break;
                        }
                    }
                }
                // ★1.0.28n 移除 1.0.28m 的 [diag-pep] 探针（诊断已完成，避免 15s 扫描刷屏日志）。
                if (bCarrier && !bNodeLike)
                {
                    continue;   // 载体剔除；矿点条目由层②负责，不在此重复下发
                }

                // ★1.0.28b 节点型 MOD 建筑（PEP「可放置矿点」把矿点做成 AFGBuildable 派生类，
                // 一手实锤 FactoryGame.log [scan-cat]：cls='SKBK_Build_Limestone_Impure_C'
                // pkg='/PEP_Mod/BuildableResourceNodes/NodeBuildables/Limestone/Impure/…'）：
                // 不是 AFGResourceNodeBase ⇒ 层② TActorIterator 扫不到 ⇒ 旧逻辑落【MOD建筑】桶
                // （用户实测 1.0.28「MOD建筑依旧存在MOD资源节点」）。按包路径特征识别为矿点，
                // 转成与层②同构的 MOD 矿点条目，不再进建筑列表。
                {
                    const FString& NodePkg = NodePkg0;
                    if (bNodeLike)
                    {
                        FWT_MachineEntry NE;
                        NE.Target = B;                          // 真实建筑 ⇒ 走机器传送链路（比坐标传送更稳）
                        NE.DisplayName = WT_BuildableDisplayName(B);
                        NE.Location = B->GetActorLocation();
                        NE.DistanceMeters = (NE.Location - Origin).Size() / 100.f;
                        if (MaxRangeMeters > 0.f && NE.DistanceMeters > MaxRangeMeters)
                        {
                            continue;
                        }
                        const FBox NB = B->GetComponentsBoundingBox(/*bNonColliding=*/true);
                        NE.BaseRadius = (NB.IsValid != 0)
                            ? FMath::Max(NB.GetExtent().X, NB.GetExtent().Y) : 0.f;
                        NE.bHasInteractUI = false;              // 矿点无交互面板 ⇒ 只给[传送]（与层②同口径）
                        NE.Category = static_cast<uint8>(EWT_MachineCategory::ResourceNode_Mod);
                        NE.ModName = WT_ResolveModName(World, B->GetClass());
                        // ★1.0.28l ①去重 + ②汉化（用户实测：3 颗 PEP 节点在列表里出 6 行，
                        //   且名字是英文 'Limestone Node (Impure)' —— PEP 没提供 zh-Hans locres）。
                        //   根因：PEP「可放置节点」= 包装建筑（AFGBuildable 派生，走本分支）+ 同一处的
                        //   真实矿点 actor（AFGResourceNodeBase，层②会再下发一条）⇒ 同一颗节点两条。
                        //   做法：按「位置 ≤500cm 且资源关键词相同」配对；配上的矿点记入 PairedNodes，
                        //   层②只记账不下发；名字/纯度改用矿点的原生中文资源名 + 纯度枚举。
                        AFGResourceNodeBase* Partner = nullptr;
                        {
                            const FString CarrierCls = B->GetClass()->GetName();
                            float BestD2 = 500.f * 500.f;
                            for (AFGResourceNodeBase* Cand : WorldNodes)
                            {
                                if (!IsValid(Cand) || PairedNodes.Contains(Cand))
                                {
                                    continue;
                                }
                                const TSubclassOf<UFGItemDescriptor> CandRes = Cand->GetResourceClass();
                                if (!WT_SameResourceKeyword(CarrierCls,
                                                            CandRes ? CandRes->GetName() : FString()))
                                {
                                    continue;
                                }
                                const float D2 = FVector::DistSquared(Cand->GetActorLocation(), NE.Location);
                                if (D2 <= BestD2)
                                {
                                    BestD2 = D2;
                                    Partner = Cand;
                                }
                            }
                            // ② 关键词认不出（mod 资源类名与包装类名用词不同，如 'SKBK_Build_SAM_*'
                            //    ↔ 'Desc_MineralSam_C'）时退化为纯位置配对：300cm 内「只有一颗」
                            //    未配对矿点才算，避免与旁边真实矿脉误配。
                            if (!Partner)
                            {
                                AFGResourceNodeBase* Only = nullptr;
                                int32 NearCount = 0;
                                const float Tight2 = 300.f * 300.f;
                                for (AFGResourceNodeBase* Cand : WorldNodes)
                                {
                                    if (!IsValid(Cand) || PairedNodes.Contains(Cand))
                                    {
                                        continue;
                                    }
                                    if (FVector::DistSquared(Cand->GetActorLocation(), NE.Location) > Tight2)
                                    {
                                        continue;
                                    }
                                    Only = Cand;
                                    ++NearCount;
                                }
                                if (NearCount == 1)
                                {
                                    Partner = Only;
                                }
                            }
                        }
                        if (Partner)
                        {
                            PairedNodes.Add(Partner);
                            if (const TSubclassOf<UFGItemDescriptor> PR = Partner->GetResourceClass())
                            {
                                const FText PName = UFGItemDescriptor::GetItemName(PR);
                                if (!PName.IsEmpty())
                                {
                                    NE.DisplayName = PName;   // 与建造菜单/物品栏同源的原生中文名
                                }
                            }
                            if (const AFGResourceNode* PRN = Cast<AFGResourceNode>(Partner))
                            {
                                switch (PRN->GetResourcePurity())
                                {
                                case EResourcePurity::RP_Inpure: NE.StatusLine = FText::FromString(TEXT("贫瘠")); NE.PurityFilter = 0; break;
                                case EResourcePurity::RP_Normal: NE.StatusLine = FText::FromString(TEXT("普通")); NE.PurityFilter = 1; break;
                                case EResourcePurity::RP_Pure:   NE.StatusLine = FText::FromString(TEXT("纯"));   NE.PurityFilter = 2; break;
                                default: break;
                                }
                            }
                            WT_WARNF(TEXT("[scan-node] paired carrier '%s' <-> node '%s' res='%s'"),
                                     *B->GetName(), *Partner->GetName(), *NE.DisplayName.ToString());
                        }
                        // 没配上（或矿点不在流送范围）时兜底汉化：英文名按资源关键词映射原生中文，
                        // 纯度取 "(Impure)/(Normal)/(Pure)" 后缀；认不出的名字原样保留（绝不臆造）。
                        if (NE.PurityFilter < 0)
                        {
                            FString ZhName;
                            int32 PIdx = -1;
                            if (WT_LocalizeResourceName(NE.DisplayName.ToString(), ZhName, PIdx))
                            {
                                NE.DisplayName = FText::FromString(ZhName);
                                NE.PurityFilter = PIdx;
                                switch (PIdx)
                                {
                                case 0: NE.StatusLine = FText::FromString(TEXT("贫瘠")); break;
                                case 1: NE.StatusLine = FText::FromString(TEXT("普通")); break;
                                case 2: NE.StatusLine = FText::FromString(TEXT("纯"));   break;
                                default: break;
                                }
                            }
                        }
                        // 纯度：PEP 显示名自带后缀 (Impure)/(Normal)/(Pure)；无后缀留空走面板默认。
                        {
                            const FString Low = NE.DisplayName.ToString().ToLower();
                            if      (Low.Contains(TEXT("(impure)"))) { NE.StatusLine = FText::FromString(TEXT("贫瘠")); NE.PurityFilter = 0; }
                            else if (Low.Contains(TEXT("(normal)"))) { NE.StatusLine = FText::FromString(TEXT("普通")); NE.PurityFilter = 1; }
                            else if (Low.Contains(TEXT("(pure)")))   { NE.StatusLine = FText::FromString(TEXT("纯"));   NE.PurityFilter = 2; }
                        }
                        // 图标 best-effort：建造描述符大图（与建筑行同源）
                        if (const TSubclassOf<UFGItemDescriptor> ND = B->GetBuiltWithDescriptor())
                        {
                            if (UTexture2D* NIcon = UFGItemDescriptor::GetBigIcon(ND))
                            {
                                NE.Icon = TSoftObjectPtr<UTexture2D>(NIcon);
                            }
                        }
                        WT_WARNF(TEXT("[scan-node] buildable-node '%s' cls='%s' pkg='%s' mod='%s'"),
                                 *B->GetName(), *B->GetClass()->GetName(), *NodePkg, *NE.ModName);
                        if (NE.Category < static_cast<uint8>(EWT_MachineCategory::Count))
                        {
                            Reply.CategoryCounts[NE.Category]++;
                        }
                        Reply.Entries.Add(NE);
                        continue;
                    }
                }

                const FVector Loc = B->GetActorLocation();
                const float DistM = (Loc - Origin).Size() / 100.f;
                if (MaxRangeMeters > 0.f && DistM > MaxRangeMeters)
                {
                    continue;
                }

                FWT_MachineEntry E;
                E.Target = B;
                // ★0.2.153b 修复：用建筑自带本地化名（此前误填 C++ 类名 → 面板显示 "Build_MinerMk1_C"）。
                // ★0.2.153t 再补一层：mDisplayName 为空时取**建造描述符的原生名**（与建造菜单同源），
                // 不再直接退化成类名 —— MOD 建筑多半不填 mDisplayName。
                E.DisplayName = WT_BuildableDisplayName(B);
                E.DistanceMeters = DistM;
                // ★0.2.153e 坐标直传：扫描时把坐标与水平半宽随回复复制到客户端，远端 Target 为 null
                // 时 UI 退化为按坐标传（Server_TeleportToCoord），不受网络 relevancy 限制。
                E.Location = Loc;
                {
                    const FBox EB = B->GetComponentsBoundingBox(/*bNonColliding=*/true);
                    E.BaseRadius = (EB.IsValid != 0)
                        ? FMath::Max(EB.GetExtent().X, EB.GetExtent().Y) : 0.f;
                }
                // 注：AFGBuildable::GetInteractWidgetClass() 为 protected，模块外不可直接调；
                // 重建版保守置 true —— 客户端按 Target 尝试开界面，无交互界面时为空操作，不致命。
                E.bHasInteractUI = true;

                uint8 Cat = static_cast<uint8>(EWT_MachineCategory::Other);
                // ★0.2.153l 制作台/装备工坊必须**先于** Manufacturer 判定：二者是纯 BP 建筑，一旦
                // 父类是 AFGBuildableManufacturer 就会被 Production 抢走 ⇒ 0.2.146 加的这两个类目
                // 一直读不到任何条目。判据取两条独立证据的「或」，互不依赖：
                // ① BP 包名（GetOutermost 的 FName，零字符串分配，9 万+ 传送带也走得起）；
                // ② 类名前缀（防 cooked 包名与源码期不一致）。命中时打诊断日志，下一轮日志即可核实。
                {
                    const UClass* Cls = B->GetClass();
                    const FName BPPkg = Cls->GetOutermost()->GetFName();
                    const FString ClsName = Cls->GetName();
                    const bool bBench = (BPPkg == FName(TEXT("Build_WorkBench"))) || ClsName.StartsWith(TEXT("Build_WorkBench"));
                    const bool bShop  = (BPPkg == FName(TEXT("Build_Workshop")))  || ClsName.StartsWith(TEXT("Build_Workshop"));
                    // ★0.2.153r 用户定案：制作台 / 装备工坊并入【枢纽】类目
                    // （与 MAM / 商店 / 回收站 / 太空电梯同组；仍必须前置判定，否则会被 Producer 抢走）
                    if (bBench)     { Cat = static_cast<uint8>(EWT_MachineCategory::Hub); }
                    else if (bShop) { Cat = static_cast<uint8>(EWT_MachineCategory::Hub); }
                    if (bBench || bShop)
                    {
                        WT_WARNF(TEXT("[scan-cat] '%s' pkg='%s' cls='%s' cat=%d"),
                                 *B->GetName(), *BPPkg.ToString(), *ClsName, static_cast<int32>(Cat));
                    }
                }
                if (Cat == static_cast<uint8>(EWT_MachineCategory::Other))
                {
                    // ★0.2.153m 容器（PipeReservoir+Storage）/ 外星传送门：接在 Manufacturer 之后
                    // （父类均为 AFGBuildableFactory，与 Manufacturer 互不命中，保留既有优先级不动）。
                    // ★本轮 液体/固体容器已合并回「容器」(Container=1)。
                    if      (Cast<AFGBuildableManufacturer>(B))          Cat = static_cast<uint8>(EWT_MachineCategory::Producer);
                    else if (Cast<AFGBuildablePipeReservoir>(B))         Cat = static_cast<uint8>(EWT_MachineCategory::Container);
                    else if (Cast<AFGBuildableStorage>(B))               Cat = static_cast<uint8>(EWT_MachineCategory::Container);
                    else if (Cast<AFGBuildablePortalBase>(B))            Cat = static_cast<uint8>(EWT_MachineCategory::Portal);
                    else if (Cast<AFGBuildableResourceExtractorBase>(B)) Cat = static_cast<uint8>(EWT_MachineCategory::Miner);
                    else if (Cast<AFGBuildableGenerator>(B))             Cat = static_cast<uint8>(EWT_MachineCategory::Generator);
                    // ★0.2.153z 车站（铁路车站 / 车辆停靠站）并入【车站】类目（用户要求「车站类显示装卸货状态+物品」）。
                    // 二者均为 AFGBuildable 派生，置于 Hub 判定之前不影响既有优先级。
                    else if (Cast<AFGBuildableRailroadStation>(B))       Cat = static_cast<uint8>(EWT_MachineCategory::Station);
                    else if (Cast<AFGBuildableDockingStation>(B))        Cat = static_cast<uint8>(EWT_MachineCategory::Station);
                    else if (Cast<AFGBuildableCircuitSwitch>(B))         Cat = static_cast<uint8>(EWT_MachineCategory::PowerSwitch);
                    // ★1.0.9 电线杆 / 电缆塔：AFGBuildablePowerPole 父类是裸 AFGBuildable
                    // （FGBuildablePowerPole.h:22），上面所有 Cast 都不命中 ⇒ 此前落 Other 被丢弃。
                    // 归到【电网】只是为了下面「不进 Entries」有个明确判据 —— 杆本身不单独成行，
                    // 它只在电网聚合里当定位与计数把手（见下方 Reply.Grids 构建）。
                    else if (Cast<AFGBuildablePowerPole>(B))             Cat = static_cast<uint8>(EWT_MachineCategory::PowerGrid);
                    // ★0.2.153r 用户定案：回收站 / 枢纽终端 / 太空电梯 / 分子分析机 / AWESOME 商店
                    // 全部并入【枢纽】类目（连同上面的制作台、装备工坊）。枚举值保留不动，只是不再落入
                    // 各自的桶——面板的 BuildingCats 已同步移除这几项，避免显示空类目。
                    else if (Cast<AFGBuildableResourceSink>(B))          Cat = static_cast<uint8>(EWT_MachineCategory::Hub);
                    else if (Cast<AFGBuildableHubTerminal>(B))           Cat = static_cast<uint8>(EWT_MachineCategory::Hub);
                    else if (Cast<AFGBuildableSpaceElevator>(B))         Cat = static_cast<uint8>(EWT_MachineCategory::Hub);
                    // ★0.2.153l 自动工作台类目按用户要求下架：AFGBuildableAutomatedWorkBench 派生自
                    // AFGBuildableManufacturer（SDK 实锤），上面 Producer 一行已收编它，无需单独桶。
                    else if (Cast<AFGBuildableMAM>(B))                   Cat = static_cast<uint8>(EWT_MachineCategory::Hub);
                    // ★z11v3c 修编译：AFGDropPod 不是 AFGBuildable 子类(实锤 is_base_of<AFGBuildable,AFGDropPod>=false)，
                    // 且 DropPod 是普通 Actor(收集品，在 2535 行 EWT_CollectibleKind::DropPod 独立扫描中处理)，绝不
                    // 进入本 AFGBuildable 建筑循环 ⇒ 原 Cast<AFGDropPod>(B) 既非法(C4996 当错误)又是死分支，删除。
                    // EWT_MachineCategory::DropPod(值10) 枚举值保留：面板 case(155 行)仍引用其显示名，但本建筑循环
                    // 不再赋值(DropPod 非建筑，本来也进不来)，该 case 仅作预留、不会命中。
                    else if (Cast<AFGBuildableResourceSinkShop>(B))      Cat = static_cast<uint8>(EWT_MachineCategory::Hub);
                }
                // ★0.2.153p MOD 建筑（上面原版类目一条都没命中 ⇒ 父类是裸 AFGBuildable / 自定义基类）：
                // 包路径不在 /Script/FactoryGame 与 /Game/FactoryGame 之下即 MOD 内容 ⇒ 进 MOD建筑桶
                // （否则落 Other，而 Other 不在面板任何分组里 = 用户看不见，即本轮报的「读不到」）。
                if (Cat == static_cast<uint8>(EWT_MachineCategory::Other)
                    && !WT_IsVanillaClass(B->GetClass()))
                {
                    Cat = static_cast<uint8>(EWT_MachineCategory::Modded);
                    E.ModName = WT_ResolveModName(World, B->GetClass());
                    // ★0.2.153s 打完整包路径：下次再出现怪分组名，一眼看出该取哪一段。
                    // ★0.2.153t 再打最终显示名：验证「原生翻译」取到的是什么（英文=该 MOD 没 locres）。
                    WT_WARNF(TEXT("[scan-cat] modded '%s' cls='%s' pkg='%s' mod='%s' name='%s'"),
                             *B->GetName(), *B->GetClass()->GetName(),
                             *B->GetClass()->GetOutermost()->GetName(), *E.ModName,
                             *E.DisplayName.ToString());
                }
                E.Category = Cat;

                // ★1.0.7i 里程碑阶段：面板新增「里程碑阶段」筛选维度（用户诉求）。
                // 资源节点的阶段由矿种表给出（上面独立分支，别覆盖）；电网没有「一个建筑」这个概念，
                // 留 99。其余类目按「解锁该建筑的蓝图阶段」填，查不到（MOD 建筑/无配方道具）也是 99。
                if (Cat != static_cast<uint8>(EWT_MachineCategory::ResourceNode_Vanilla)
                    && Cat != static_cast<uint8>(EWT_MachineCategory::ResourceNode_Mod)
                    && Cat != static_cast<uint8>(EWT_MachineCategory::PowerGrid))
                {
                    E.MilestoneTier = WT_BuildingMilestoneTier(B->GetClass());
                }

                // ★1.0.11 问题 / 效率 / 填充率 / 耗电：所有类目统一在这里填，
                // 不散进下面各 if-else 类目分支（散着写必有类目漏填，表现为「该类目筛问题恒 0 条」）。
                WT_FillQualityFields(B, Cat, E);

                // ★本轮 枢纽子类型：Hub 类目下按具体 Cast 区分 6 种建筑 + 回收站，
                // 供面板顶部图标栏按类型固定展示（即使 Target 因 relevancy 为 null 也能识别）。
                if (Cat == static_cast<uint8>(EWT_MachineCategory::Hub))
                {
                    const UClass* HubCls = B->GetClass();
                    const FName HubPkg = HubCls->GetOutermost()->GetFName();
                    const FString HubClsName = HubCls->GetName();
                    const bool bBench = (HubPkg == FName(TEXT("Build_WorkBench"))) || HubClsName.StartsWith(TEXT("Build_WorkBench"));
                    const bool bShop  = (HubPkg == FName(TEXT("Build_Workshop")))  || HubClsName.StartsWith(TEXT("Build_Workshop"));
                    if      (Cast<AFGBuildableHubTerminal>(B))      E.HubKind = static_cast<uint8>(EWT_HubKind::HubTerminal);
                    else if (Cast<AFGBuildableSpaceElevator>(B))    E.HubKind = static_cast<uint8>(EWT_HubKind::SpaceElevator);
                    else if (Cast<AFGBuildableMAM>(B))              E.HubKind = static_cast<uint8>(EWT_HubKind::MAM);
                    else if (Cast<AFGBuildableResourceSinkShop>(B)) E.HubKind = static_cast<uint8>(EWT_HubKind::AwesomeShop);
                    else if (Cast<AFGBuildableResourceSink>(B))     E.HubKind = static_cast<uint8>(EWT_HubKind::ResourceSink);
                    else if (bBench)                                E.HubKind = static_cast<uint8>(EWT_HubKind::HubWorkbench);
                    else if (bShop)                                 E.HubKind = static_cast<uint8>(EWT_HubKind::EquipmentWorkshop);
                    else                                            E.HubKind = static_cast<uint8>(EWT_HubKind::Unknown);
                }

                // ★1.0.28j 建筑图标：对【非 Hub】建筑条目先尝试建造描述符大图标（与建造菜单同源）。
                // 此前仅 Producer/Miner/Hub 三类填，导致 Container/Generator/Other 等建筑子目在列表里是灰块
                // （用户实测"建筑中部分子类目没有贴图"）。Hub 类目跳过——其图标走下方专属兜底链
                // （★1.0.28g HubTerminal 必须保持 E.Icon 为空才能触发 Hub_256 候选链，绝不能抢先占住）。
                if (Cat != static_cast<uint8>(EWT_MachineCategory::Hub))
                {
                    if (const TSubclassOf<UFGItemDescriptor> Desc = B->GetBuiltWithDescriptor())
                    {
                        if (UTexture2D* IconTex = UFGItemDescriptor::GetBigIcon(Desc))
                        {
                            E.Icon = TSoftObjectPtr<UTexture2D>(IconTex);
                        }
                    }
                }
                // ★0.2.153m 列表行图标（用户要求：生产机器 / 采矿设备显示对应物品图标）。
                // 图标取自建造该建筑所用描述符（AFGBuildable::GetBuiltWithDescriptor →
                // UFGItemDescriptor::GetBigIcon），与建造菜单里显示的是同一张图，不是自己画的素材。
                // 只给这两个类目填：其余类目保持空路径，避免给每条回复加上万条路径字符串。
                    if (Cat == static_cast<uint8>(EWT_MachineCategory::Producer) ||
                        Cat == static_cast<uint8>(EWT_MachineCategory::Miner) ||
                        Cat == static_cast<uint8>(EWT_MachineCategory::Hub))
                {
                    if (const TSubclassOf<UFGItemDescriptor> Desc = B->GetBuiltWithDescriptor())
                    {
                        if (UTexture2D* IconTex = UFGItemDescriptor::GetBigIcon(Desc))
                        {
                            E.Icon = TSoftObjectPtr<UTexture2D>(IconTex);
                        }
                    }
                    // ★1.0.28c 枢纽图标兜底（实测截图：枢纽终端/制作台/太空电梯/回收站灰块）：
                    // 这些建筑非玩家建造流生成（开局自带/特殊解锁）⇒ GetBuiltWithDescriptor 为 null。
                    // 兜底 = 按类加载原版 descriptor（资产存在性已经游戏 .utoc 目录索引实锤
                    // Desc_HubTerminal/Desc_SpaceElevator/Desc_WorkBench/Desc_ResourceSink；
                    // 目录经游戏真实 DefaultEngine.ini（Build_HubTerminal 等 Build_ 资产路径）交叉验证），
                    // 再走同一 GetBigIcon 读其配置好的图标。加载失败⇒保持空（面板回落占位块），零风险。
                    if (E.Icon.IsNull() && Cat == static_cast<uint8>(EWT_MachineCategory::Hub))
                    {
                        const TCHAR* FallbackDesc = nullptr;
                        switch (E.HubKind)
                        {
                        case static_cast<uint8>(EWT_HubKind::HubTerminal):   FallbackDesc = TEXT("/Game/FactoryGame/Buildable/Factory/HubTerminal/Desc_HubTerminal.Desc_HubTerminal_C"); break;
                        case static_cast<uint8>(EWT_HubKind::HubWorkbench):  FallbackDesc = TEXT("/Game/FactoryGame/Buildable/Factory/WorkBench/Desc_WorkBench.Desc_WorkBench_C"); break;
                        case static_cast<uint8>(EWT_HubKind::SpaceElevator): FallbackDesc = TEXT("/Game/FactoryGame/Buildable/Factory/SpaceElevator/Desc_SpaceElevator.Desc_SpaceElevator_C"); break;
                        case static_cast<uint8>(EWT_HubKind::ResourceSink):  FallbackDesc = TEXT("/Game/FactoryGame/Buildable/Factory/ResourceSink/Desc_ResourceSink.Desc_ResourceSink_C"); break;
                        default: break;
                        }
                        // 注：不写成 `if (A && UClass* x = expr)` —— 声明不能混进布尔表达式（MSVC 报错）。
                        UClass* DescCls = FallbackDesc
                            ? StaticLoadClass(UFGItemDescriptor::StaticClass(), nullptr, FallbackDesc)
                            : nullptr;
                        if (DescCls)
                        {
                            if (UTexture2D* FallbackTex = UFGItemDescriptor::GetBigIcon(DescCls))
                            {
                                E.Icon = TSoftObjectPtr<UTexture2D>(FallbackTex);
                            }
                            // ★1.0.28g HubTerminal 跳过 GetSmallIcon 兜底：Desc_HubTerminal.GetSmallIcon
                            // 返回通用占位/菱形徽章，会抢先在下方 if(E.Icon.IsNull()) 守卫之前占住 E.Icon，
                            // 导致 975 行 Hub_256 候选链永不执行、面板也优先用此占位图——这正是
                            // 1.0.28f 部署后仍显示错误图标的根因。HubTerminal 故意留空，交给 975 行 Hub_256 链。
                            else if (E.HubKind != static_cast<uint8>(EWT_HubKind::HubTerminal))
                            {
                                if (UTexture2D* SmallTex = UFGItemDescriptor::GetSmallIcon(DescCls))
                                {
                                    E.Icon = TSoftObjectPtr<UTexture2D>(SmallTex);
                                }
                            }
                        }

                        // ★1.0.28e 候选链换权威实锤路径：2026-10-26 自写 utoc 目录索引解析器
                        // （FIoDirectoryIndexResource 序列化契约按引擎源码 IoDirectoryIndex.h/.cpp 逐字段核对）
                        // 从 FactoryGame-Windows.utoc 完整走树实锤：SchematicIcon_Hub_1..5 / HubUpgrade
                        // 全部在 .../SchematicIcons/OldIcons/ 子目录（旧路径少一层 OldIcons ⇒
                        // LoadPackage SkipPackage，日志 [scan-hub] ALL candidates missed 实锤）。
                        // ★1.0.28f 实测截图仍是白菱形：SchematicIcon_Hub_* 本身就是「菱形里程碑徽章」
                        // 贴图，加载成功≠用户要的 HUB 建筑外观。同一次 utoc 走树另实锤
                        // TradingPost/UI/Hub_256、Hub_512（HUB 建筑图标，带 .ubulk 真贴图），
                        // 置于候选链最前；SchematicIcon 链保留作兜底。
                        if (E.Icon.IsNull() && E.HubKind == static_cast<uint8>(EWT_HubKind::HubTerminal))
                        {
                            static const TCHAR* HubTexCandidates[] = {
                                TEXT("/Game/FactoryGame/Buildable/Factory/TradingPost/UI/Hub_256.Hub_256"),
                                TEXT("/Game/FactoryGame/Buildable/Factory/TradingPost/UI/Hub_512.Hub_512"),
                                TEXT("/Game/FactoryGame/Buildable/Factory/TradingPost/UI/SchematicIcons/OldIcons/SchematicIcon_Hub_1.SchematicIcon_Hub_1"),
                                TEXT("/Game/FactoryGame/Buildable/Factory/TradingPost/UI/SchematicIcons/OldIcons/SchematicIcon_Hub_2.SchematicIcon_Hub_2"),
                                TEXT("/Game/FactoryGame/Buildable/Factory/TradingPost/UI/SchematicIcons/OldIcons/SchematicIcon_HubUpgrade.SchematicIcon_HubUpgrade"),
                                TEXT("/Game/FactoryGame/Buildable/Factory/TradingPost/UI/SchematicIcons/SchematicIcon_Hub_1.SchematicIcon_Hub_1"),   // 旧路径兜底（utoc 无此包，防引擎改版）
                                TEXT("/Game/FactoryGame/Buildable/Factory/TradingPost/UI/SchematicIcons/Icon_Hub_1.Icon_Hub_1"),
                                TEXT("/Game/FactoryGame/Buildable/Factory/TradingPost/UI/SchematicIcons/Icon_Hub.Icon_Hub"),
                            };
                            for (const TCHAR* TexPath : HubTexCandidates)
                            {
                                if (UTexture2D* HubTex = LoadObject<UTexture2D>(nullptr, TexPath))
                                {
                                    E.Icon = TSoftObjectPtr<UTexture2D>(HubTex);
                                    WT_WARNF(TEXT("[scan-hub] HubTerminal icon fallback hit: %s"), TexPath);
                                    break;
                                }
                            }
                            if (E.Icon.IsNull())
                            {
                                WT_WARNF(TEXT("[scan-hub] HubTerminal icon fallback: ALL candidates missed"));
                            }
                        }
                    }
                    // ★0.2.153p 产出物图标（同一支里顺手取，避免再来一次 Cast 链）：
                    //  生产机器 = 当前配方 UFGRecipe::GetProducts 的第 0 项（FGRecipe.h:63 静态函数，
                    //  与 GetCurrentRecipe(FGBuildableManufacturer.h:165) 配套）；
                    //  采矿设备 = 所连资源节点的资源类（AFGResourceNodeBase::GetResourceClass，
                    //  FGResourceNodeBase.h:144，经 GetResourceNode() 取到节点）。
                    // ★0.2.153z 生产设备 + 采矿设备：产出物图标 + 状态行（用户要求「显示机器状态」）。
                    // 采矿设备显示「所连资源节点的矿种 + 纯度」；★0.2.153z11d：矿种/纯度改从权威接口
                    // GetExtractableResource() 取（deprecated 的 GetResourceNode() 在自定义/新版矿点常为
                    // null ⇒ 读不到），接口背后对象即 AFGResourceNode，Cast 回节点即可取纯度。
                    TSubclassOf<UFGItemDescriptor> OutDesc = nullptr;
                    FText RName = FText::GetEmpty();
                    FText RPurity = FText::GetEmpty();
                    bool bHasRes = false;
                    if (const AFGBuildableManufacturer* M = Cast<AFGBuildableManufacturer>(B))
                    {
                        const TSubclassOf<UFGRecipe> Recipe = M->GetCurrentRecipe();
                        if (Recipe)
                        {
                            const TArray<FItemAmount> Prods = UFGRecipe::GetProducts(Recipe);
                            if (Prods.Num() > 0)
                            {
                                OutDesc = Prods[0].ItemClass;
                            }
                        }
                    }
                    else if (const AFGBuildableResourceExtractorBase* Ex = Cast<AFGBuildableResourceExtractorBase>(B))
                    {
                        TSubclassOf<UFGItemDescriptor> Cls = nullptr;
                        if (TScriptInterface<IFGExtractableResourceInterface> Res = Ex->GetExtractableResource())
                        {
                            Cls = Res->GetResourceClass();
                            if (AFGResourceNode* RN = Cast<AFGResourceNode>(Res.GetObject()))
                            {
                                RPurity = RN->GetResourcePurityText();
                            }
                        }
                        if (!Cls)
                        {
                            if (const AFGResourceNode* Node = Ex->GetResourceNode())
                            {
                                Cls = Node->GetResourceClass();
                                RPurity = Node->GetResourcePurityText();
                            }
                        }
                        OutDesc = Cls;
                        if (Cls) { RName = UFGItemDescriptor::GetItemName(Cls); bHasRes = true; }
                    }
                    if (OutDesc)
                    {
                        if (UTexture2D* OutTex = UFGItemDescriptor::GetBigIcon(OutDesc))
                        {
                            E.ProductIcon = TSoftObjectPtr<UTexture2D>(OutTex);
                        }
                    }

                    // ★本轮 搜索用产出物名称（物品/材料名）
                    E.ProductName = bHasRes ? RName : (OutDesc ? UFGItemDescriptor::GetItemName(OutDesc) : FText::GetEmpty());

                    // ★1.0.10 「已存在物品」筛选：采矿设备 = 所采矿种；生产设备 = 当前配方**全部**产物
                    // （多产物配方只记主产物的话，玩家按副产物筛会漏掉这些机器）。
                    WT_AddItemTag(OutDesc, E.ItemTags);
                    if (const AFGBuildableManufacturer* ProdMach = Cast<AFGBuildableManufacturer>(B))
                    {
                        if (const TSubclassOf<UFGRecipe> CurRecipe = ProdMach->GetCurrentRecipe())
                        {
                            for (const FItemAmount& ProdAmt : UFGRecipe::GetProducts(CurRecipe))
                            {
                                WT_AddItemTag(ProdAmt.ItemClass, E.ItemTags);
                            }
                        }
                    }

                    // ★1.0.8 状态码（供 UI 行内彩色指示灯取色）。与下面两处 WT_ProdStateText
                    // 共用同一份 EProductionStatus 判定 ⇒ 灯色与文字一一对应。
                    // 只给【生产设备】与【采矿设备】填（用户口径），其余类目保持 255=不画灯。
                    E.ProdState = WT_ProdStateCode(Cast<AFGBuildableFactory>(B));

                    // ★0.2.153z 生产 / 采矿设备状态行：生产机器=产物名+状态；采矿设备=矿种(+纯度)+状态。
                    if (const AFGBuildableManufacturer* M = Cast<AFGBuildableManufacturer>(B))
                    {
                        const FText ProdName = OutDesc ? UFGItemDescriptor::GetItemName(OutDesc) : FText::GetEmpty();
                        E.StatusDetail = ProdName;   // ★1.0.8 状态灯前的「产物名」段
                        E.StatusLine = FText::Format(FText::FromString(TEXT("{0} · {1}")),
                                                    ProdName, WT_ProdStateText(Cast<AFGBuildableFactory>(B)));
                    }
                    else if (const AFGBuildableResourceExtractorBase* Ex = Cast<AFGBuildableResourceExtractorBase>(B))
                    {
                        FString RS = bHasRes
                            ? (RPurity.IsEmpty()
                                ? RName.ToString()
                                : FString::Printf(TEXT("%s · %s"), *RName.ToString(), *RPurity.ToString()))
                            : FString(TEXT("未连接矿点"));
                        E.StatusDetail = FText::FromString(RS);   // ★1.0.8 状态灯前的「矿种·纯度」段
                        E.StatusLine = FText::FromString(FString::Printf(
                            TEXT("%s · %s"), *RS, *WT_ProdStateText(Cast<AFGBuildableFactory>(B)).ToString()));
                    }
                }
                else
                {
                    // ★0.2.153r 其余类目的产出物 / 消耗物图标（用户要求「从【容器】开始往下的类目
                    // 都要加个图标」）。语义按建筑实际角色取，取不到就留空（面板自动不显示该图）：
                    UTexture2D* ProdTex = nullptr;
                    if (Cat == static_cast<uint8>(EWT_MachineCategory::Container))
                    {
                        // ★本轮 容器 = 液体储罐（PipeReservoir）+ 固体容器（Storage）合并为「容器」一类。
                        if (const AFGBuildablePipeReservoir* R = Cast<AFGBuildablePipeReservoir>(B))
                        {
                            // 液体容器 = 罐内液体（GetFluidDescriptor，FGBuildablePipeReservoir.h:87）
                            ProdTex = WT_ItemIcon(R->GetFluidDescriptor());
                            E.StatusLine = R->GetFluidDescriptor()
                                ? UFGItemDescriptor::GetItemName(R->GetFluidDescriptor())
                                : FText::FromString(TEXT("空"));
                            // ★1.0.10 物品筛选：罐里的液体
                            WT_AddItemTag(R->GetFluidDescriptor(), E.ItemTags);
                        }
                        else if (AFGBuildableStorage* S = Cast<AFGBuildableStorage>(B))
                        {
                            // 固体容器 = 全箱总量（GetStorageInventory，FGBuildableStorage.h:30）。
                            // ★本轮 用户诉求：存量直接显示「总数」（不再只显示第一格那一组）。
                            // 实时刷新由面板 0.1s tick 客户端直读复制库存组件完成（见 PanelWidget）。
                            ProdTex = WT_FirstStackIcon(S->GetStorageInventory());
                            const int32 Total = WT_InvTotalItems(S->GetStorageInventory());
                            E.StatusLine = FText::Format(FText::FromString(TEXT("共 {0} 件")),
                                                        FText::AsNumber(Total));
                            // ★1.0.10 物品筛选：**整箱每一格**都要记（只按首格筛会让
                            // 「第二格才有铁板」的箱子找不到）
                            WT_CollectInvItemTags(S->GetStorageInventory(), E.ItemTags);
                            E.ContentStackCount = Total;
                        }
                        // ★1.0.10 旧「有存货/空」布尔筛选已下线（被容器类目的「数量」档取代：
                        // 空 / 少量 / 中量 / 大量）。这里只记物品与件数，不再存布尔标记。
                    }
                    else if (Cat == static_cast<uint8>(EWT_MachineCategory::Generator))
                    {
                        // 发电机 = 消耗物（燃料）：优先当前燃料类，回退燃料库存第一格
                        // （FGBuildableGeneratorFuel.h:103 / :54）
                        if (const AFGBuildableGeneratorFuel* G = Cast<AFGBuildableGeneratorFuel>(B))
                        {
                            ProdTex = WT_ItemIcon(G->GetCurrentFuelClass());
                            if (!ProdTex)
                            {
                                ProdTex = WT_FirstStackIcon(G->GetFuelInventory());
                            }
                            // ★1.0.10 物品筛选：当前燃料 + 燃料库存里的品类
                            WT_AddItemTag(G->GetCurrentFuelClass(), E.ItemTags);
                            WT_CollectInvItemTags(G->GetFuelInventory(), E.ItemTags);
                            E.StatusLine = (G->GetCurrentFuelClass())
                                ? FText::Format(FText::FromString(TEXT("燃料 · {0}")),
                                                UFGItemDescriptor::GetItemName(G->GetCurrentFuelClass()))
                                : WT_FirstStackText(G->GetFuelInventory());
                        }
                    }
                    else if (Cat == static_cast<uint8>(EWT_MachineCategory::Portal))
                    {
                        // 外星传送门 = 消耗物（电池/燃料）：GetFuelInventory（FGBuildablePortal.h:56）
                        if (const AFGBuildablePortal* P = Cast<AFGBuildablePortal>(B))
                        {
                            ProdTex = WT_FirstStackIcon(P->GetFuelInventory());
                            E.StatusLine = WT_FirstStackText(P->GetFuelInventory());
                        }
                    }
                    else if (Cat == static_cast<uint8>(EWT_MachineCategory::Hub))
                    {
                        // ★0.2.153r 合并后的枢纽组：制作台 / 装备工坊（Manufacturer 派生）显示配方产物；
                        // 商店显示货架、回收站显示票券（产出）、太空电梯显示交付物（消耗）。
                        if (AFGBuildableResourceSinkShop* SH = Cast<AFGBuildableResourceSinkShop>(B))
                        {
                            ProdTex = WT_FirstStackIcon(SH->GetShopInventory());       // :27
                        }
                        else if (const AFGBuildableResourceSink* K = Cast<AFGBuildableResourceSink>(B))
                        {
                            ProdTex = WT_FirstStackIcon(K->GetCouponInventory());      // :34
                        }
                        else if (AFGBuildableSpaceElevator* SE = Cast<AFGBuildableSpaceElevator>(B))
                        {
                            ProdTex = WT_FirstStackIcon(SE->GetInputInventory());      // :140
                        }
                        else if (const AFGBuildableManufacturer* M = Cast<AFGBuildableManufacturer>(B))
                        {
                            const TSubclassOf<UFGRecipe> Recipe = M->GetCurrentRecipe();
                            if (Recipe)
                            {
                                const TArray<FItemAmount> Prods = UFGRecipe::GetProducts(Recipe);
                                if (Prods.Num() > 0)
                                {
                                    ProdTex = WT_ItemIcon(Prods[0].ItemClass);
                                    E.StatusLine = FText::Format(FText::FromString(TEXT("{0} · {1}")),
                                        UFGItemDescriptor::GetItemName(Prods[0].ItemClass),
                                        WT_ProdStateText(Cast<AFGBuildableFactory>(B)));
                                    E.ProductName = UFGItemDescriptor::GetItemName(Prods[0].ItemClass);
                                }
                            }
                        }
                        // 枢纽终端 / MAM 无输入产出库存 ⇒ 留空
                    }
                    else if (Cat == static_cast<uint8>(EWT_MachineCategory::Station))
                    {
                        // ★0.2.153z 车站：车辆停靠站显示装载/卸载模式 + 库存物品；铁路车站显示真实站名。
                        if (const AFGBuildableDockingStation* DS = Cast<AFGBuildableDockingStation>(B))
                        {
                            ProdTex = WT_FirstStackIcon(DS->GetInventory());
                            // ★1.0.10 物品筛选：站内货物
                            WT_CollectInvItemTags(DS->GetInventory(), E.ItemTags);
                            E.ContentStackCount = WT_InvTotalItems(DS->GetInventory());
                            const bool bLoad = DS->GetIsInLoadMode();
                            E.StatusLine = FText::Format(FText::FromString(TEXT("{0} · {1}")),
                                bLoad ? FText::FromString(TEXT("装载站")) : FText::FromString(TEXT("卸载站")),
                                WT_FirstStackText(DS->GetInventory()));
                        }
                        else if (const AFGBuildableRailroadStation* TS = Cast<AFGBuildableRailroadStation>(B))
                        {
                            E.StatusLine = FText::FromString(TEXT("铁路车站"));
                            if (AFGTrainStationIdentifier* Id = TS->GetStationIdentifier())
                            {
                                // 用真实站名覆盖默认 DisplayName（蓝图站名多为通用 "Train Station"）
                                E.DisplayName = Id->GetStationName();
                            }
                        }
                    }
                    if (ProdTex)
                    {
                        E.ProductIcon = TSoftObjectPtr<UTexture2D>(ProdTex);
                    }
                }

                // ★1.0.9 电线杆不单独成行：它是【电网】类目的聚合材料（见下方 Reply.Grids），
                // 一座基地动辄几十根杆，全塞进列表只会把真正想看的那几条电路挤掉。
                // 放在计数之前，避免 CategoryCounts 里留下「杆数」这种和面板显示口径不符的数字。
                if (Cat == static_cast<uint8>(EWT_MachineCategory::PowerGrid))
                {
                    continue;
                }

                if (Cat < static_cast<uint8>(EWT_MachineCategory::Count))
                {
                    Reply.CategoryCounts[Cat]++;
                }

                if (AFGBuildableCircuitSwitch* SW = Cast<AFGBuildableCircuitSwitch>(B))
                {
                    E.bSwitchOn = SW->IsSwitchOn();
                    E.bSwitchConnected = SW->IsSwitchConnected();
                    if (AFGBuildablePriorityPowerSwitch* P = Cast<AFGBuildablePriorityPowerSwitch>(B))
                    {
                        E.SwitchPriority = P->GetPriority();
                    }
                }

                Reply.Entries.Add(E);
            }

            // ★2026-11-02 便携式采矿机（AFGPortableMiner）纳入【采矿设备】：
            // 一手核实 FGPortableMiner.h —— AFGPortableMiner : public AActor（非 AFGBuildable、非
            // AFGBuildableResourceExtractorBase），主建筑循环(TActorIterator<AFGBuildable>)遍历不到，
            // 1547 行 Cast<AFGBuildableResourceExtractorBase> 也扑空 ⇒ 此前完全不进【采矿设备】。
            // 这里单独遍历，按 Miner 类目计入；资源类取自 mExtractResourceNode->GetResourceClass()。
            {
                int32 PMCount = 0;
                for (TActorIterator<AFGPortableMiner> PMIt(World); PMIt; ++PMIt)
                {
                    AFGPortableMiner* PM = *PMIt;
                    if (!IsValid(PM))
                    {
                        continue;
                    }
                    const FVector Loc = PM->GetActorLocation();
                    const float DistM = (Loc - Origin).Size() / 100.f;
                    if (MaxRangeMeters > 0.f && DistM > MaxRangeMeters)
                    {
                        continue;
                    }

                    FWT_MachineEntry E;
                    E.Target = PM;
                    E.DisplayName = FText::FromString(TEXT("便携式采矿机"));
                    E.DistanceMeters = DistM;
                    E.Location = Loc;
                    {
                        const FBox EB = PM->GetComponentsBoundingBox(/*bNonColliding=*/true);
                        E.BaseRadius = (EB.IsValid != 0)
                            ? FMath::Max(EB.GetExtent().X, EB.GetExtent().Y) : 0.f;
                    }
                    E.bHasInteractUI = true;
                    E.Category = static_cast<uint8>(EWT_MachineCategory::Miner);
                    E.MilestoneTier = 99;
                    E.ProdState = 255;  // 便携采矿机无统一生产状态灯

                    // 资源类（矿种 + 纯度）：挂在 mExtractResourceNode（AFGResourceNode）
                    TSubclassOf<UFGItemDescriptor> Cls = nullptr;
                    FText RPurity = FText::GetEmpty();
                    FText RName = FText::GetEmpty();
                    bool bHasRes = false;
                    if (AFGResourceNode* RN = PM->mExtractResourceNode)
                    {
                        Cls = RN->GetResourceClass();
                        RPurity = RN->GetResourcePurityText();
                    }
                    if (Cls)
                    {
                        RName = UFGItemDescriptor::GetItemName(Cls);
                        bHasRes = true;
                        if (UTexture2D* OutTex = UFGItemDescriptor::GetBigIcon(Cls))
                        {
                            E.ProductIcon = TSoftObjectPtr<UTexture2D>(OutTex);
                        }
                        WT_AddItemTag(Cls, E.ItemTags);
                    }
                    const FString RS = bHasRes
                        ? (RPurity.IsEmpty() ? RName.ToString()
                                             : FString::Printf(TEXT("%s · %s"), *RName.ToString(), *RPurity.ToString()))
                        : FString(TEXT("未连接矿点"));
                    E.StatusDetail = FText::FromString(RS);
                    E.StatusLine = FText::FromString(RS);
                    E.ProductName = bHasRes ? RName : FText::GetEmpty();

                    if (static_cast<uint8>(EWT_MachineCategory::Miner) < static_cast<uint8>(EWT_MachineCategory::Count))
                    {
                        Reply.CategoryCounts[static_cast<uint8>(EWT_MachineCategory::Miner)]++;
                    }
                    Reply.Entries.Add(E);
                    ++PMCount;
                }
                if (PMCount > 0)
                {
                    WT_WARNF(TEXT("[scan] portable miners counted: %d"), PMCount);
                }
            }

            // ★1.0.8 死亡箱子（用户诉求：【容器】类目新增「死亡箱子」条目）。
            //
            // 为什么不并进上面那个 GetAllBuildables 循环：AFGCrate 派生自 AFGInteractActor
            // （FGCrate.h:26 `class AFGCrate : public AFGInteractActor`），**不是 AFGBuildable**
            // ⇒ AFGBuildableSubsystem::GetAllBuildablesRef() 永远不收它，只能单独遍历世界 actor。
            //
            // 只收 CT_DeathCrate（玩家死亡时掉落的箱子）；CT_DismantleCrate（拆除残留）刻意不收 ——
            // 大面积拆基地会在一张图里留下成百个拆解箱，把容器列表彻底淹没。两类都由
            // AFGCrate::GetCrateType() 区分（FGCrate.h:109，inline 读 mCrateType UPROPERTY）。
            // 箱子被取空后原版会自毁（FGCrate.h:23 注释），下一轮扫描自然消失，无需额外清理。
            for (TActorIterator<AFGCrate> CrateIt(World); CrateIt; ++CrateIt)
            {
                AFGCrate* Crate = *CrateIt;
                if (!IsValid(Crate) || Crate->GetCrateType() != EFGCrateType::CT_DeathCrate)
                {
                    continue;
                }

                FWT_MachineEntry CE;
                CE.Target      = Crate;
                CE.Category    = static_cast<uint8>(EWT_MachineCategory::Container);
                CE.DisplayName = WT_LOCTEXT("Crate_DeathCrate", "死亡箱子");
                // ★1.0.8 修复：死亡箱是 IFGUseableInterface，但本面板没有它的交互 UI（无
                // mInteractWidgetSoftClass，WT_ResolveInteractWidgetClass 对箱子返回 nullptr）→
                // 走"打开面板"只会静默无反应。故交互 UI 标 false，让"打开面板"按钮对箱子隐藏，
                // 改由"取料(Take output)"按钮经 Server_PullOutput 的 AFGCrate 支线把物品搬进背包。
                CE.bHasInteractUI = false;
                CE.Location    = Crate->GetActorLocation();
                CE.DistanceMeters = (CE.Location - Origin).Size() / 100.f;
                // 死亡箱没有建筑 footprint（不是 Buildable），落点用保守半径：箱子约 1m 见方，
                // 传送到旁边 60cm 即可，不会嵌进箱体。
                CE.BaseRadius  = 60.f;
                // 箱子没有"产出"，行内显示箱内第一格存货（与容器行的 StatusLine 语义一致）
                CE.StatusLine  = WT_FirstStackText(Crate->GetInventory());
                // ★1.0.10 物品 / 数量筛选：死亡箱里捡回来的东西（每格都记）
                WT_CollectInvItemTags(Crate->GetInventory(), CE.ItemTags);
                CE.ContentStackCount = WT_InvTotalItems(Crate->GetInventory());
                if (UTexture2D* InnerTex = WT_FirstStackIcon(Crate->GetInventory()))
                {
                    CE.ProductIcon = TSoftObjectPtr<UTexture2D>(InnerTex);
                }
                // 箱子图标走原版 representation 贴图（蓝图里按 CT_DeathCrate 选 mDeathCrateIcon，
                // 是 protected 成员取不到；GetActorRepresentationTexture 是接口的 public 虚函数）
                if (UTexture2D* CrateTex = Crate->GetActorRepresentationTexture())
                {
                    CE.Icon = TSoftObjectPtr<UTexture2D>(CrateTex);
                }

                if (CE.Category < static_cast<uint8>(EWT_MachineCategory::Count))
                {
                    Reply.CategoryCounts[CE.Category]++;
                }
                WT_WARNF(TEXT("[scan-crate] death crate '%s' at %s dist=%.0fm items='%s'"),
                         *Crate->GetName(), *CE.Location.ToCompactString(),
                         CE.DistanceMeters, *CE.StatusLine.ToString());
                Reply.Entries.Add(CE);
            }

            // ★1.0.28 第二层：世界中全部资源节点 actor（含挂在载体上的——TActorIterator
            // 不区分挂接状态；含 vanilla 类的——mod 地图可能用原版矿点 BP 放置，静态库
            // 未收录的必须能实时补上）。库已有的由 AppendLiveNode 内 NodeKey 去重跳过
            // （只留存在性记账）；井卫星不收（用户口径：只收主节点）。
            // 1.0.27 此处曾跳过 vanilla 类 + 查 HandledNodes 防重：前者会让「mod 载体 +
            // 原版类矿点」形态下矿点彻底消失（载体已剔除、这里又跳过），后者随层①不再
            // 下发条目已成死代码，一并移除。
            for (TActorIterator<AFGResourceNodeBase> It(World); It; ++It)
            {
                AFGResourceNodeBase* N = *It;
                if (!IsValid(N))
                {
                    continue;
                }
                // ★2026-11-02 修复：散落矿（Deposit 类型节点）不应计入【资源节点】——与静态库构建器
                // （Remote_FactoryResourceNodes.cpp 的 `if (Type==Deposit) continue;`）保持一致。
                const EResourceNodeType NodeType = N->GetResourceNodeType();
                if (NodeType == EResourceNodeType::FrackingSatellite
                    || NodeType == EResourceNodeType::Deposit)
                {
                    continue;
                }
                // 1.0.28l：已被 PEP 包装建筑配对的矿点只记账不下发
                AppendLiveNode(N, PairedNodes.Contains(N));
            }

            // ★0.2.153b 重建：全厂电力总览（误删后未还原 → 面板恒显 发电0/耗电0、电池0/0）。
            // 电路经 UFGPowerInfoComponent::GetPowerCircuit() 去重后累加 GetStats()；电池从储能建筑直加。
            //
            // ★1.0.9 同时逐条落成 FWT_GridEntry（面板【电网】类目的内容来源）：
            // 上面的合计是「一个数」，这里是「每条电路 + 它的曲线」，两者同一份 GetStats ⇒ 永不对不上。
            TMap<int32, FWT_GridEntry> GridMap;
            TSet<const UFGPowerCircuit*> SeenCircuits;
            for (AFGBuildable* B : All)
            {
                if (!B) { continue; }
                if (const AFGBuildablePowerStorage* PS = Cast<AFGBuildablePowerStorage>(B))
                {
                    Reply.BatteryStoredMWh   += PS->GetPowerStore();
                    Reply.BatteryCapacityMWh += PS->GetPowerStoreCapacity();
                }
                if (const UFGPowerInfoComponent* PIC = B->FindComponentByClass<UFGPowerInfoComponent>())
                {
                    if (UFGPowerCircuit* C = PIC->GetPowerCircuit())
                    {
                        bool bAlready = false;
                        SeenCircuits.Add(C, &bAlready);
                        if (!bAlready)
                        {
                            FPowerCircuitStats S;
                            C->GetStats(S);
                            Reply.PowerProducedMW       += S.PowerProduced;
                            Reply.PowerConsumedMW       += S.PowerConsumed;
                            Reply.PowerCapacityMW       += S.PowerProductionCapacity;
                            Reply.PowerMaxConsumptionMW += S.MaximumPowerConsumption;

                            // ---- 逐条电网快照 ----
                            FWT_GridEntry G;
                            G.CircuitId        = C->GetCircuitID();
                            G.ConsumedMW       = S.PowerConsumed;
                            G.PeakConsumedMW   = S.MaximumPowerConsumption;
                            G.ProducedMW       = S.PowerProduced;
                            G.CapacityMW       = S.PowerProductionCapacity;
                            G.BoostMW          = S.BoostProduced;
                            G.bHasPower        = C->HasPower();
                            G.bFuseBlown       = C->IsFuseTriggered();
                            G.HistorySeconds   = S.StatHistoryTime;

                            // 代表位置先取这个建筑；下面电线杆那遍若有更近的杆会覆盖它。
                            G.Location         = B->GetActorLocation();
                            G.DistanceMeters   = (G.Location - Origin).Size() / 100.f;

                            // 五条线，通道顺序与原版图例一致（Kind 见 FWT_GridSeries 注释）。
                            // 用 GetPoints<成员指针> 而不是自己遍历 Items：它和原版蓝图调用的
                            // GetConsumedPoints/GetPeakConsumedPoints 是同一份实现（FGPowerCircuit.h:100），
                            // 包括它那个 (FirstIndex+1+i)%count 的环形偏移 —— 自己重算只会画出一条错位的线。
                            auto AddSeries = [&G](uint8 Kind, const TArray<float>& Raw)
                            {
                                FWT_GridSeries S2;
                                S2.Kind = Kind;
                                WT_DownsamplePoints(Raw, 120, S2.Points);
                                G.Series.Add(MoveTemp(S2));
                            };
                            {
                                TArray<float> Raw;
                                S.GetPoints<&FPowerGraphPoint::Consumed>(Raw);            AddSeries(0, Raw);
                                S.GetPoints<&FPowerGraphPoint::MaximumConsumption>(Raw);  AddSeries(1, Raw);
                                S.GetPoints<&FPowerGraphPoint::Produced>(Raw);            AddSeries(2, Raw);
                                S.GetPoints<&FPowerGraphPoint::ProductionCapacity>(Raw);  AddSeries(3, Raw);
                                S.GetPoints<&FPowerGraphPoint::BoostProduced>(Raw);       AddSeries(4, Raw);
                            }

                            // 先把键取出来再 MoveTemp：`GridMap.Add(G.CircuitId, MoveTemp(G))`
                            // 的实参求值顺序未指定，若 MoveTemp 先跑，G.CircuitId 就来自一个已
                            // 被移走的对象（int32 目前是 POD 所以不会错，但这属于「靠运气」）。
                            const int32 NewCircuitId = G.CircuitId;
                            GridMap.Add(NewCircuitId, MoveTemp(G));
                        }
                    }
                }
            }
            Reply.CircuitCount = SeenCircuits.Num();

            // ---- ★1.0.9 用电线杆补「杆数 / 接线口 / 更近的位置」----
            // 电线杆自带 UFGPowerConnectionComponent 但**不一定**挂 UFGPowerInfoComponent，
            // 所以上面那遍建筑循环统计不到它们（这也是为什么单独跑一遍 actor 迭代）。
            for (TActorIterator<AFGBuildablePowerPole> PoleIt(World); PoleIt; ++PoleIt)
            {
                const AFGBuildablePowerPole* Pole = *PoleIt;
                if (!IsValid(Pole) || !Pole->GetPowerCircuit()) { continue; }

                FWT_GridEntry* G = GridMap.Find(Pole->GetPowerCircuit()->GetCircuitID());
                if (!G) { continue; }   // 极少数情况下杆在、成员建筑已不在 ⇒ 跳过，不为它单开一条

                const TArray<UFGPowerConnectionComponent*>& Conns = Pole->GetPowerConnections();
                ++G->PoleCount;
                G->LinkMax += Conns.Num();
                for (const UFGPowerConnectionComponent* Conn : Conns)
                {
                    G->LinkUsed += (Conn && Conn->IsConnected()) ? 1 : 0;
                }

                const FVector PLoc = Pole->GetActorLocation();
                const float PDistM = (PLoc - Origin).Size() / 100.f;
                if (G->PoleCount == 1 || PDistM < G->DistanceMeters)
                {
                    G->Location = PLoc;
                    G->DistanceMeters = PDistM;
                }
            }

            // 离终端近的电网排前面（与其它类目「最近优先」口径一致）。
            GridMap.GenerateValueArray(Reply.Grids);
            Reply.Grids.Sort([](const FWT_GridEntry& A, const FWT_GridEntry& B)
            {
                return A.DistanceMeters < B.DistanceMeters;
            });

            for (const FWT_GridEntry& G : Reply.Grids)
            {
                WT_WARNF(TEXT("[scan-grid] circuit=%d poles=%d links=%d/%d dist=%.0fm "
                              "cons=%.1f peak=%.1f prod=%.1f cap=%.1f boost=%.1f hist=%.0fs pts=%d"),
                         G.CircuitId, G.PoleCount, G.LinkUsed, G.LinkMax, G.DistanceMeters,
                         G.ConsumedMW, G.PeakConsumedMW, G.ProducedMW, G.CapacityMW, G.BoostMW,
                         G.HistorySeconds,
                         G.Series.Num() > 0 ? G.Series[0].Points.Num() : 0);
            }
        }

        // ★0.2.153m 运输工具扫描（用户要求：运输车/卡车/火车/无人机进列表）。
        // 载具**不是** AFGBuildable（AFGVehicle : AFGDriveablePawn），不在 BuildableSubsystem 的
        // 建筑表里 ⇒ 必须单独扫一遍世界 actor：TActorIterator<AFGVehicle> 按 IsA 过滤，天然覆盖
        // 轮式载具（拖拉机/卡车/探险车）、AFGRailroadVehicle（机车/货车厢）、AFGDroneVehicle（无人机）。
        {
            const uint8 CatVehicle = static_cast<uint8>(EWT_MachineCategory::Vehicle);
            for (TActorIterator<AFGVehicle> It(World); It; ++It)
            {
                AFGVehicle* V = *It;
                if (!V)
                {
                    continue;
                }
                const FVector VLoc = V->GetActorLocation();
                const float VDistM = (VLoc - Origin).Size() / 100.f;
                if (MaxRangeMeters > 0.f && VDistM > MaxRangeMeters)
                {
                    continue;
                }

                FWT_MachineEntry E;
                E.Target = V;
                // AFGVehicle::mDisplayName 是 public UPROPERTY，与建筑一样读本地化名；
                // ★0.2.153t 空则退到**建造描述符的原生名**（AFGVehicle::GetBuiltWithDescriptor，
                // FGVehicle.h:242）——与建造菜单同源，最后才回退类名。
                E.DisplayName = WT_VehicleDisplayName(V);
                E.DistanceMeters = VDistM;
                E.Location = VLoc;
                {
                    const FBox EB = V->GetComponentsBoundingBox(/*bNonColliding=*/true);
                    E.BaseRadius = (EB.IsValid != 0)
                        ? FMath::Max(EB.GetExtent().X, EB.GetExtent().Y) : 0.f;
                }
                // 载具没有可远程打开的交互面板（Server_RequestOpenUI 现收 AActor，但载具无交互面板）⇒ 只给[传送]。
                E.bHasInteractUI = false;
                E.Category = CatVehicle;
                // ★0.2.153r 运输工具图标 = 车上装的第一格货物（轮式载具有货箱：
                // WheeledVehicles/FGWheeledVehicle.h:91 GetStorageInventory）。
                // 机车 / 货车厢 / 无人机未暴露库存 API（SDK 实锤 FGRailroadVehicle.h 无 inventory）
                // ⇒ 取不到就留空，面板自动不显示该图，不臆造。
                if (const AFGWheeledVehicle* WV = Cast<AFGWheeledVehicle>(V))
                {
                    if (UTexture2D* CargoTex = WT_FirstStackIcon(WV->GetStorageInventory()))
                    {
                        E.ProductIcon = TSoftObjectPtr<UTexture2D>(CargoTex);
                    }
                    // ★0.2.153z 运输工具显示运输物品（轮式载具有货箱库存）
                    E.StatusLine = WT_FirstStackText(WV->GetStorageInventory());
                    // ★1.0.10 物品 / 数量筛选：整车货物（每格都记）
                    WT_CollectInvItemTags(WV->GetStorageInventory(), E.ItemTags);
                    E.ContentStackCount = WT_InvTotalItems(WV->GetStorageInventory());
                }

                if (CatVehicle < static_cast<uint8>(EWT_MachineCategory::Count))
                {
                    Reply.CategoryCounts[CatVehicle]++;
                }
                Reply.Entries.Add(E);
            }
        }
    }

    Client_ReceiveMachineList(Reply);
}

void URemote_FactoryRCO::Server_RequestOpenUI_Implementation(AActor* Target)
{
    if (!Target)
    {
        return;
    }
    // 服务端确认建筑存在后，把"打开原生交互界面"指令下发给请求方客户端；
    // 真正的 widget 由客户端按 mInteractObject 指向 Target 生成（同按 E 的交互，只是免去走过去）。
    Client_OpenInteractUI(Target);
}

void URemote_FactoryRCO::Server_PullOutput_Implementation(AActor* Target)
{
    int32 ItemsMoved = 0;
    bool  bSuccess   = false;

    if (Target && WT_HasAuthority(this))
    {
        UWorld* World = GetWorld();
        AFGPlayerController* PC = Cast<AFGPlayerController>(GetOuter());
        if (!PC && World)
        {
            PC = Cast<AFGPlayerController>(World->GetFirstPlayerController());
        }
        AFGCharacterPlayer* Char = (PC ? Cast<AFGCharacterPlayer>(PC->GetCharacter()) : nullptr);
        UFGInventoryComponent* PlayerInv = (Char ? Char->GetInventory() : nullptr);

        if (AFGBuildableManufacturer* M = Cast<AFGBuildableManufacturer>(Target))
        {
            if (UFGInventoryComponent* OutInv = M->GetOutputInventory())
            {
                if (PlayerInv && Char)
                {
                    // 把输出库存整体搬入玩家背包，放不下的落到脚边（与原版取料行为一致）。
                    AFGBuildableFactory::MoveOrDropInventory(OutInv, Char);
                    bSuccess = true;
                }
            }
        }
        // ★1.0.8 死亡箱子：AFGCrate 不是 AFGBuildable，走自己的库存。
        // MoveOrDropInventory 是 AFGBuildableFactory 的 **static** 成员
        // （FGBuildableFactory.h:346），只吃 UFGInventoryComponent* + 角色，
        // 不要求调用者是工厂建筑 ⇒ 死亡箱的 GetInventory()（FGCrate.h:89）可以直接喂进去，
        // 原版「放不下就掉脚边」的行为同样生效。
        else if (AFGCrate* Crate = Cast<AFGCrate>(Target))
        {
            if (UFGInventoryComponent* CrateInv = Crate->GetInventory())
            {
                if (PlayerInv && Char)
                {
                    AFGBuildableFactory::MoveOrDropInventory(CrateInv, Char);
                    bSuccess = true;
                    WT_WARNF(TEXT("[pull] death crate '%s' drained into '%s'"),
                             *Crate->GetName(), *Char->GetName());
                }
            }
        }
    }

    Client_NotifyPullResult(Target, ItemsMoved, bSuccess);
}

// 集装箱面板【取出】的服务端实现。客户端只发请求，一切权威操作都在这里。
//
// 玩家会遇到的情况，逐条对应：
//   · 正常取货        → TransferFromContainersToInventory 从各集装箱扫同物品、搬进背包，返回实际转移量。
//   · 背包满          → 那边"先加后删"，一个都放不进就一个都不删 ⇒ 集装箱数量不变，物品绝不蒸发。
//   · 只放得下一部分  → 只搬放得下的量（日志 take partial），剩下留在集装箱里。
//   · 请求量超存量    → 扫到的量就是上限，多出的部分自然取不到。
//   · 集装箱里已没有  → Got=0，什么都不改。
//   · 非权威端误调    → WT_HasAuthority 拦掉（客户端不该走到这，但防御）。
//
// ★为什么参数是"物品类 + 数量"而不是"集装箱 actor"：面板显示的是**全局聚合量**（不分哪个箱子），
//   所以服务端也按全局扫，玩家点一次就可能从好几个箱子各拿一点。这与面板语义一致。
// ★日志 take-from-container 是排查取货问题的首要证据（want / got / 背包前后数量都要有）。
void URemote_FactoryRCO::Server_TakeFromContainer_Implementation(TSubclassOf<UFGItemDescriptor> Cls, int32 Want, AActor* OnlyContainer)
{
    if (!Cls || Want <= 0)
    {
        return;
    }
    if (!WT_HasAuthority(this))
    {
        return;
    }
    UWorld* World = GetWorld();
    // 先试 RCO 的 Outer（正常就是玩家控制器），取不到再退回世界里的第一个玩家控制器。
    AFGPlayerController* PC = Cast<AFGPlayerController>(GetOuter());
    if (!PC && World)
    {
        PC = Cast<AFGPlayerController>(World->GetFirstPlayerController());
    }
    AFGCharacterPlayer* Char = (PC ? Cast<AFGCharacterPlayer>(PC->GetCharacter()) : nullptr);
    UFGInventoryComponent* PlayerInv = (Char ? Char->GetInventory() : nullptr);
    if (!PlayerInv)
    {
        return;   // 玩家没有角色/背包（刚出生/正在切换），直接放弃本次请求
    }

    const int32 Got = Remote_FactorySmartWarehouse::TransferFromContainersToInventory(World, Cls, Want, PlayerInv, OnlyContainer);
    WT_WARNF(TEXT("[warehouse-panel] take-from-container '%s' want=%d got=%d scope=%s | inv=%s size=%d char=%s nowHas=%d"),
             (Cls ? *Cls->GetName() : TEXT("?")), Want, Got,
             (OnlyContainer ? *OnlyContainer->GetName() : TEXT("ALL")),
             *PlayerInv->GetName(), PlayerInv->GetSizeLinear(),
             Char ? *Char->GetName() : TEXT("null"),
             Cls ? PlayerInv->GetNumItems(Cls) : -1);
}

// ★2026-11-05 集装箱面板【存入】的服务端实现（单种物品）。
// 与取出完全镜像，只是方向相反：取出是「集装箱 → 背包」，这里是「背包 → 集装箱」。
//
// 玩家会遇到的情况，逐条对应：
//   · 正常          → 依次往各集装箱里塞，塞满一个换下一个，直到凑够 Want 或箱子都满。
//   · 箱子全满      → Stored=0，背包一个不减（先加后删，物理上不可能吞物品）。
//   · 只塞得下一部分 → 只搬塞得下的，剩下留在背包（日志里 want/have/stored 三个数能对出来）。
//   · 非建造材料    → IsStorableMaterial 直接拦掉（装备/消耗品/弹药不存）。
//   · 背包里没有    → Have=0 ⇒ Stored=0，什么都不改。
//   · 非权威端误调  → WT_HasAuthority 拦掉（防御）。
//
// ★为什么不在这里再夹一次 Want：TransferFromInventoryToContainers 内部已经用
//   `Min(Want, SourceInv->GetNumItems(Cls))` 夹过，且每个箱子还按 WT_MaxAddableTo 二分算容量。
//   多夹一层只会让日志里两个数对不上，排查变难。
void URemote_FactoryRCO::Server_StoreToContainer_Implementation(TSubclassOf<UFGItemDescriptor> Cls, int32 Want, AActor* OnlyContainer)
{
    if (!Cls || Want <= 0)
    {
        return;
    }
    if (!WT_HasAuthority(this))
    {
        return;
    }
    if (!Remote_FactorySmartWarehouse::IsStorableMaterial(Cls))
    {
        WT_WARNF(TEXT("[warehouse-panel] store ignored (not a storable material): '%s'"), *Cls->GetName());
        return;
    }

    UWorld* World = GetWorld();
    AFGPlayerController* PC = Cast<AFGPlayerController>(GetOuter());
    if (!PC && World)
    {
        PC = Cast<AFGPlayerController>(World->GetFirstPlayerController());
    }
    AFGCharacterPlayer* Char = (PC ? Cast<AFGCharacterPlayer>(PC->GetCharacter()) : nullptr);
    UFGInventoryComponent* PlayerInv = (Char ? Char->GetInventory() : nullptr);
    if (!PlayerInv)
    {
        return;
    }

    const int32 Stored = Remote_FactorySmartWarehouse::TransferFromInventoryToContainers(World, Cls, Want, PlayerInv, OnlyContainer);
    WT_WARNF(TEXT("[warehouse-panel] store-to-container '%s' want=%d stored=%d scope=%s | inv=%s size=%d char=%s nowHas=%d"),
             (Cls ? *Cls->GetName() : TEXT("?")), Want, Stored,
             (OnlyContainer ? *OnlyContainer->GetName() : TEXT("ALL")),
             *PlayerInv->GetName(), PlayerInv->GetSizeLinear(),
             Char ? *Char->GetName() : TEXT("null"),
             Cls ? PlayerInv->GetNumItems(Cls) : -1);
}

// ★2026-11-05 集装箱面板【全部存入】的服务端实现。
// ★关键：必须**先拍快照再转移**。绝不能在「遍历背包格子」的循环里直接存 ——
//   RemoveFromIndex 会让后面的格子前移/清空，边遍历边删必然漏物品（经典 off-by-index 事故）。
//   故这里先把可存物品聚合成 (物品类 → 总数)，再按快照逐个调转移函数；
//   转移函数内部自己重新扫背包当前格子，与快照无关，顺序也就无关了。
void URemote_FactoryRCO::Server_StoreAllToContainers_Implementation()
{
    if (!WT_HasAuthority(this))
    {
        return;
    }
    UWorld* World = GetWorld();
    AFGPlayerController* PC = Cast<AFGPlayerController>(GetOuter());
    if (!PC && World)
    {
        PC = Cast<AFGPlayerController>(World->GetFirstPlayerController());
    }
    AFGCharacterPlayer* Char = (PC ? Cast<AFGCharacterPlayer>(PC->GetCharacter()) : nullptr);
    UFGInventoryComponent* PlayerInv = (Char ? Char->GetInventory() : nullptr);
    if (!PlayerInv)
    {
        return;
    }

    // ① 快照：同一物品可能散在多格，这里按类累加。
    TMap<TSubclassOf<UFGItemDescriptor>, int32> Snapshot;
    const int32 Size = PlayerInv->GetSizeLinear();
    for (int32 i = 0; i < Size; ++i)
    {
        FInventoryStack S;
        if (!PlayerInv->GetStackFromIndex(i, S) || !S.HasItems())
        {
            continue;
        }
        const TSubclassOf<UFGItemDescriptor> C = S.Item.GetItemClass();
        if (!C || !Remote_FactorySmartWarehouse::IsStorableMaterial(C))
        {
            continue;
        }
        Snapshot.FindOrAdd(C) += S.NumItems;
    }

    if (Snapshot.Num() == 0)
    {
        WT_WARNF(TEXT("[warehouse-panel] store-all: nothing storable in inventory (%d slots scanned)."), Size);
        return;
    }

    // ② 按快照逐个转移。Stored 是所有物品的合计，Kinds 是真正存进去过的物品种类数。
    int32 TotalStored = 0;
    int32 Kinds = 0;
    for (const TPair<TSubclassOf<UFGItemDescriptor>, int32>& KV : Snapshot)
    {
        const int32 One = Remote_FactorySmartWarehouse::TransferFromInventoryToContainers(World, KV.Key, KV.Value, PlayerInv);
        TotalStored += One;
        if (One > 0)
        {
            ++Kinds;
        }
    }

    WT_WARNF(TEXT("[warehouse-panel] store-all done: %d items across %d/%d kinds | inv=%s char=%s"),
             TotalStored, Kinds, Snapshot.Num(),
             *PlayerInv->GetName(), Char ? *Char->GetName() : TEXT("null"));
}

void URemote_FactoryRCO::Client_ReceiveMachineList_Implementation(const FWT_MachineListReply& Reply)
{
    OnMachineListReceived.Broadcast(Reply);
}

void URemote_FactoryRCO::Client_OpenInteractUI_Implementation(AActor* Target)
{
    OnInteractUIRequested.Broadcast(Target);
}

void URemote_FactoryRCO::Client_NotifyPullResult_Implementation(AActor* Target, int32 ItemsMoved, bool bSuccess)
{
    OnPullResult.Broadcast(Target, ItemsMoved, bSuccess);
}

// ★2026-11-02 智能仓储贡献量：客户端缓存（服务端经 Client_ReceiveSWContribution 下发）。
static FWT_SWContributionMap GClientSWContribution;

void URemote_FactoryRCO::Client_ReceiveSWContribution_Implementation(const TArray<FWT_SWContributionEntry>& Contribution)
{
    GClientSWContribution.Reset();
    for (const FWT_SWContributionEntry& E : Contribution)
    {
        if (E.ItemClass)
        {
            GClientSWContribution.Add(E.ItemClass, E.Amount);
        }
    }
    OnSWContributionReceived.Broadcast(GClientSWContribution);
}

const FWT_SWContributionMap& URemote_FactoryRCO::GetClientSWContribution()
{
    return GClientSWContribution;
}

// ★2026-11-04 智能仓储开关的联机同步（头文件里写了动机，这里写实现要点）。
//   · 服务端权威写入：SetSmartWarehouseEnabled 改的是进程级静态量，在服务端改才是"真"状态
//     （存档序列化发生在服务端，聚合表的刷新 ticker 也在服务端跑）。
//   · 写完立刻 RefreshContainerAvailability：否则服务端聚合表还是旧值，
//     玩家开完开关马上取货会得到空结果（看起来像"开了没用"）。
//   · 回下发权威值：客户端以服务端值为准，避免两边各行其是。
//   · 单机/Host：Server RPC 在权威端就地执行，同样会走 Client_SetSmartWarehouse 回填，逻辑统一。
void URemote_FactoryRCO::Server_SetSmartWarehouse_Implementation(bool bEnable)
{
    if (!WT_HasAuthority(this))
    {
        return;
    }
    Remote_FactorySmartWarehouse::SetSmartWarehouseEnabled(bEnable);
    Remote_FactorySmartWarehouse::RefreshContainerAvailability(GetWorld());
    WT_WARNF(TEXT("[smart-warehouse] server set to %d (authoritative), broadcasting to client."),
             bEnable ? 1 : 0);
    Client_SetSmartWarehouse(bEnable);
}

// 客户端收到权威值：写本地静态量（让面板/成本格着色立刻跟随），并刷新可用量缓存。
// ★这里是"客户端唯一允许改开关状态的地方"——玩家点按钮只发请求，不回写本地，否则又会出现两边不一致。
void URemote_FactoryRCO::Client_SetSmartWarehouse_Implementation(bool bEnable)
{
    Remote_FactorySmartWarehouse::SetSmartWarehouseEnabled(bEnable);
    Remote_FactorySmartWarehouse::RefreshContainerAvailability(GetWorld());
    WT_WARNF(TEXT("[smart-warehouse] client received authoritative state: %d."), bEnable ? 1 : 0);
}

// ----------------------------------------------------------------------------
// ★0.2.153 重建：落点搜索辅助函数。
//
// 原定义随上轮编辑事故（_rf_edit_0153.py 误删顶部 include + 函数头）一并丢失，
// 全盘检索（F:/ D:/ SMLStarterProject）无残留。按调用点注释的 0.2.135 落点语义重建：
//   WT_FindCollectibleLandingSpot —— 收集品(外星 slug/球/果实 / 空投舱 / 文件坐标)落点：
//       先试「物品坐标 2m(200cm) 外」环搜 → 失败退「通用环搜」加宽 → 仍失败且为实体
//       收集品(bHasActor)时「洞穴兜底：直接站在物品上方」；坐标模式(bHasActor=false)
//       搜不到则交由调用方取消传送（不硬落虚空）。
//   WT_FindSafeLandingSpot —— 机器/建筑通用落点(0.2.88；0.2.153e 第八轮大肥鱼定案改候选序)：
//       再 8 向环搜，最后加宽兜底；搜不到交由调用方取消（0.2.135 不再硬落）。
// 所有候选点均做「上方200下探-400找地面 + ECC_Pawn 胶囊占位检测」，落点 = 地面 + 胶囊半高 + 2。
// ----------------------------------------------------------------------------
namespace
{
    // 单个候选 XY：上方 200 下探 -400 找地面；命中则做胶囊占位检测，通过返回安全落点。
    static bool WT_TryGroundAt(UWorld* World, const FCollisionQueryParams& Q,
                               const FVector& Xy, float CapRadius, float CapHalfH, FVector& OutDest)
    {
        FHitResult Hit;
        const FVector Start = Xy + FVector(0.f, 0.f, 200.f);
        const FVector End   = Xy - FVector(0.f, 0.f, 400.f);
        if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Q))
        {
            return false;
        }
        const FVector Dest = Hit.ImpactPoint + FVector(0.f, 0.f, CapHalfH + 2.f);
        const FCollisionShape Shape = FCollisionShape::MakeCapsule(CapRadius, FMath::Max(CapHalfH - 2.f, 10.f));
        if (World->OverlapBlockingTestByChannel(Dest, FQuat::Identity, ECC_Pawn, Shape, Q))
        {
            return false;
        }
        OutDest = Dest;
        return true;
    }

    // 以 Center 为中心、半径 Radius 的环上 N 向搜索，返回首个安全落点。
    static bool WT_TryRing(UWorld* World, const FCollisionQueryParams& Q,
                           const FVector& Center, float Radius, float CapRadius, float CapHalfH,
                           FVector& OutDest)
    {
        static const int32 N = 12;
        for (int32 i = 0; i < N; ++i)
        {
            const FVector Dir = FRotator(0.f, (360.f / static_cast<float>(N)) * static_cast<float>(i), 0.f).Vector();
            if (WT_TryGroundAt(World, Q, Center + Dir * Radius, CapRadius, CapHalfH, OutDest))
            {
                return true;
            }
        }
        return false;
    }
}

static bool WT_FindCollectibleLandingSpot(UWorld* World, AFGCharacterPlayer* Character,
                                          FVector Center, float ItemTopZ, bool bHasActor, FVector& OutDest)
{
    if (!World || !Character)
    {
        return false;
    }
    const UCapsuleComponent* Cap = Character->GetCapsuleComponent();
    const float CapRadius = Cap ? Cap->GetScaledCapsuleRadius() : 60.f;
    const float CapHalfH  = Cap ? Cap->GetScaledCapsuleHalfHeight() : 90.f;

    FCollisionQueryParams Q(SCENE_QUERY_STAT(WT_CollectibleLanding), /*bTraceComplex=*/false);
    Q.AddIgnoredActor(Character);

    // 1) 专用落点：物品坐标 2m(200cm) 外环搜
    if (WT_TryRing(World, Q, Center, 200.f, CapRadius, CapHalfH, OutDest))
    {
        return true;
    }
    // 2) 通用环搜兜底：加宽到 400cm（2m 贴墙/夹缝时外扩再试）
    if (WT_TryRing(World, Q, Center, 400.f, CapRadius, CapHalfH, OutDest))
    {
        return true;
    }
    // 3) 洞穴兜底：实体收集品直接站在物品上方（bHasActor=false 坐标模式无实体可踩 → 交给调用方取消）
    if (bHasActor)
    {
        const FVector Dest = FVector(Center.X, Center.Y, ItemTopZ + CapHalfH + 2.f);
        const FCollisionShape Shape = FCollisionShape::MakeCapsule(CapRadius, FMath::Max(CapHalfH - 2.f, 10.f));
        if (!World->OverlapBlockingTestByChannel(Dest, FQuat::Identity, ECC_Pawn, Shape, Q))
        {
            OutDest = Dest;
            return true;
        }
    }
    return false;
}

static bool WT_FindSafeLandingSpot(UWorld* World, AFGCharacterPlayer* Character,
                                   FVector Center, float BaseRadius, FVector ToPlayer,
                                   const FVector& MachineFwd, FVector& OutDest)
{
    if (!World || !Character)
    {
        return false;
    }
    const UCapsuleComponent* Cap = Character->GetCapsuleComponent();
    const float CapRadius = Cap ? Cap->GetScaledCapsuleRadius() : 60.f;
    const float CapHalfH  = Cap ? Cap->GetScaledCapsuleHalfHeight() : 90.f;

    FCollisionQueryParams Q(SCENE_QUERY_STAT(WT_SafeLanding), /*bTraceComplex=*/false);
    Q.AddIgnoredActor(Character);

    const float RingR = BaseRadius + 100.f;
    // ★0.2.153e 第八轮（大肥鱼定案）：机器 forward 就是出料面，ToPlayer/世界 8 向都可能在出口处。
    // 候选序以机器朝向为基准：先 -forward（出料面对侧）、再 ±90（两翼）、最后 +forward（出料面），
    // 再补 4 条 45° 斜向。每个候选 WT_TryGroundAt 内部做地面 trace + overlap（撞地基/机器自动跳下一个）；
    // navmesh 不参与（地基上不一定有 navmesh），Z 一律 trace 地面顶面兜底——都是现状已实现的行为。
    {
        FVector Fwd = MachineFwd;
        Fwd.Z = 0.f;
        if (Fwd.SizeSquared() < KINDA_SMALL_NUMBER)
        {
            Fwd = FVector(1.f, 0.f, 0.f); // forward 全竖直（理论上不存在）时的无害兜底
        }
        Fwd.Normalize();
        const FVector Side = FVector(Fwd.Y, -Fwd.X, 0.f); // Fwd 左转 90°（水平）
        const FVector DiagA = (Fwd + Side).GetSafeNormal();
        const FVector DiagB = (Fwd - Side).GetSafeNormal();
        const FVector FwdDirs[7] = {
            -Fwd,          // 出料面对侧：最优先
             Side, -Side,  // 两翼
             Fwd,          // 出料面：最末
             DiagA, DiagB  // 斜向补位
        };
        for (const FVector& Dir : FwdDirs)
        {
            if (WT_TryGroundAt(World, Q, Center + Dir * RingR, CapRadius, CapHalfH, OutDest))
            {
                return true;
            }
        }
    }
    // 8 向环搜（世界坐标兜底）
    static const int32 N = 8;
    for (int32 i = 0; i < N; ++i)
    {
        const FVector Dir = FRotator(0.f, (360.f / static_cast<float>(N)) * static_cast<float>(i), 0.f).Vector();
        if (WT_TryGroundAt(World, Q, Center + Dir * RingR, CapRadius, CapHalfH, OutDest))
        {
            return true;
        }
    }
    // 加宽兜底：半径 +200cm
    const float RingR2 = RingR + 200.f;
    for (int32 i = 0; i < N; ++i)
    {
        const FVector Dir = FRotator(0.f, (360.f / static_cast<float>(N)) * static_cast<float>(i), 0.f).Vector();
        if (WT_TryGroundAt(World, Q, Center + Dir * RingR2, CapRadius, CapHalfH, OutDest))
        {
            return true;
        }
    }
    return false;
}

// ---- ★0.2.153e 远程传送落点表（大肥鱼第七轮定案；★0.2.153h 键改稳定身份）----
// 远距离传送时目的地周围地形/碰撞尚未流送，trace/navmesh 都拿不到真地面，传送前根本算不出落点。
// 改为表驱动：近距离成功落地过一次就把落点存盘；远距离直接查表传到存档点——落地后目的地区块
// 才开始流送，那时地面 Z 才算得出来，所以传完等 0.6s 再补一次地面校正（绝不在传之前算）。
// ★0.2.153h 键 = Hub|对象路径名（大肥鱼定案「按 FGuid 别拿坐标当键」；SDK 实锤 FActorSaveHeader
// 无实例 FGuid，FG 存档里等价稳定 ID = 对象路径名，跨存档持久）。拆了重建 = 新名字 = 新键，
// 旧表项成无害孤儿（永不命中、不动别人记录）；旧版坐标键条目一并留作孤儿，不做迁移不清表。
static FString WT_HubSpotFilePath()
{
    // 沿用收集品持久化位置（Saved/Remote_Factory 子目录刻意不随 mod 改名，见 RF_collected.json 约定）
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Remote_Factory"), TEXT("RF_teleport_spots.json"));
}

static FString WT_HubSpotKey(const AActor* Target)
{
    // ★0.2.153h 坐标键的坑实锤：拿量化坐标当键，拆了重建/缓存污染后旧落点会被一直端出来
    // （「每次落同一个错点」根因）。改按稳定身份（对象路径名，存档跨会话持久）。
    return FString::Printf(TEXT("Hub|%s"), *Target->GetPathName());
}

static TMap<FString, FVector> WT_LoadHubSpots()
{
    TMap<FString, FVector> Out;
    FString Raw;
    if (!FFileHelper::LoadFileToString(Raw, *WT_HubSpotFilePath()))
    {
        return Out; // 首次使用/文件不存在：空表
    }
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        WT_WARNF(TEXT("[teleport-hub] spot file corrupt, starting empty: %s"), *WT_HubSpotFilePath());
        return Out;
    }
    for (const auto& Pair : Root->Values)
    {
        const TSharedPtr<FJsonObject> Obj = Pair.Value->AsObject();
        if (!Obj.IsValid() || !Obj->HasField(TEXT("x")) || !Obj->HasField(TEXT("y")) || !Obj->HasField(TEXT("z")))
        {
            continue;
        }
        Out.Add(Pair.Key, FVector(
            Obj->GetNumberField(TEXT("x")),
            Obj->GetNumberField(TEXT("y")),
            Obj->GetNumberField(TEXT("z"))));
    }
    return Out;
}

static void WT_SaveHubSpot(const FString& Key, const FVector& Dest)
{
    TMap<FString, FVector> Spots = WT_LoadHubSpots();
    Spots.Add(Key, Dest);
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    for (const auto& Pair : Spots)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetNumberField(TEXT("x"), Pair.Value.X);
        Obj->SetNumberField(TEXT("y"), Pair.Value.Y);
        Obj->SetNumberField(TEXT("z"), Pair.Value.Z);
        Root->SetObjectField(Pair.Key, Obj);
    }
    FString OutString;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
    if (FJsonSerializer::Serialize(Root.ToSharedRef(), Writer)
        && FFileHelper::SaveStringToFile(OutString, *WT_HubSpotFilePath(),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        WT_WARNF(TEXT("[teleport-hub] spot-saved key='%s' dest=%s"), *Key, *Dest.ToString());
    }
    else
    {
        WT_WARNF(TEXT("[teleport-hub] spot-save FAILED path=%s"), *WT_HubSpotFilePath());
    }
}

// ★0.2.153f 传送 ground-fix 公共助手（原 Server_TeleportToCoord 内联 lambda 提取共用；
// ★0.2.153h 上移到 Server_TeleportTo 之前供 hub 分支复用，并扩展：可忽略指定 actor + 空键不回写表）：
// 0.6s 后把角色校正到落点正下真地面（远距落地时目的地区块才刚开始流送）；命中且 Key 非空即写回
// 落点表（首次到那再补写真落点，大肥鱼第十轮定案）。校正本身是几厘米级竖直贴地，保持 sweep=true
// 正确（sweep 只在长距瞬移时才是误判源）。
static void WT_ScheduleTeleportGroundFix(UWorld* World, AFGCharacterPlayer* Character,
                                         const FString& Key, AActor* IgnoreActor = nullptr)
{
    if (!World || !Character)
    {
        return;
    }
    TWeakObjectPtr<AFGCharacterPlayer> CharW(Character);
    TWeakObjectPtr<UWorld> WorldW(World);
    TWeakObjectPtr<AActor> IgnoreW(IgnoreActor);
    FTimerHandle FixHandle;
    World->GetTimerManager().SetTimer(FixHandle, FTimerDelegate::CreateWeakLambda(Character,
        [CharW, WorldW, Key, IgnoreW]()
    {
        AFGCharacterPlayer* FixChar = CharW.Get();
        UWorld* FixWorld = WorldW.Get();
        if (!FixChar || !FixWorld)
        {
            return;
        }
        FCollisionQueryParams FixQ(SCENE_QUERY_STAT(WT_CoordFix), /*bTraceComplex=*/false);
        FixQ.AddIgnoredActor(FixChar);
        if (AActor* FixIgnore = IgnoreW.Get())
        {
            // ★0.2.153h hub 用：忽略目标建筑本体（终端就贴着落点），别把人校正到终端顶上
            FixQ.AddIgnoredActor(FixIgnore);
        }
        FHitResult FixHit;
        const FVector CurLoc = FixChar->GetActorLocation();
        const UCapsuleComponent* FixCap = FixChar->GetCapsuleComponent();
        const float FixHalfH = FixCap ? FixCap->GetScaledCapsuleHalfHeight() : 90.f;
        if (FixWorld->LineTraceSingleByChannel(FixHit,
                CurLoc + FVector(0.f, 0.f, 200.f),
                CurLoc - FVector(0.f, 0.f, 600.f), ECC_Pawn, FixQ))
        {
            // ★0.2.153n 贴地判据修正（bug①「卡在枢纽背面的墙里」真凶之一）：
            // 旧逻辑「命中第一个面就贴」会把人**钉到枢纽结构顶上**——本局实测：人被兜底砸到终端
            // 正上方（枢纽结构内部）后，射线起点 -2353.9 本身就在实体里，命中 -2354（≈起点），
            // 于是「贴地」贴了个寂寞，人继续卡在墙里。
            // 真地面只可能在**脚下**（≤ 胶囊底 + 容差）；命中面高于脚底 = 头顶的结构/天花板 ⇒ 不贴，
            // 让玩家靠重力自然落到外面（兜底位置本身已由宽松阶段尽量选在结构之外）。
            const float FixBottom = CurLoc.Z - FixHalfH;
            if (FixHit.ImpactPoint.Z > FixBottom + 30.f)
            {
                WT_WARNF(TEXT("[teleport-fix] ground-fix-above-skip hitZ=%.0f bottomZ=%.0f key='%s'"),
                         FixHit.ImpactPoint.Z, FixBottom, *Key);
                return;
            }
            const FVector Fixed(FixHit.ImpactPoint.X, FixHit.ImpactPoint.Y,
                                FixHit.ImpactPoint.Z + FixHalfH + 2.f);
            // ★0.2.153z11v2：入表前嵌入校验——若落点仍嵌在阻挡几何里（玩家卡墙/卡结构/卡枢纽），
            //   绝不写表，从根源杜绝「毒化落点缓存」：旧版曾把卡墙点写进 RF_teleport_spots.json，
            //   玩家重进后反复落到墙里。测试胶囊抬高到地面之上、只探【身体核心】是否嵌墙，避免
            //   站在地面上时胶囊触地造成的误判(false positive)。
            const FCollisionShape FixShape = FCollisionShape::MakeCapsule(
                FMath::Max((FixCap ? FixCap->GetScaledCapsuleRadius() : 60.f) * 0.4f, 20.f),
                FMath::Max(FixHalfH - 2.f, 10.f));
            const FVector EmbedLoc(Fixed.X, Fixed.Y, Fixed.Z + FixHalfH + 5.f);
            const bool bEmbedded = FixWorld->OverlapBlockingTestByChannel(
                EmbedLoc, FQuat::Identity, ECC_Pawn, FixShape, FixQ);
            if (bEmbedded)
            {
                WT_WARNF(TEXT("[teleport-fix] spot-skip-embedded key='%s' at=%s (不写毒化缓存)"),
                         *Key, *Fixed.ToString());
            }
            else if (!Key.IsEmpty())
            {
                WT_SaveHubSpot(Key, Fixed); // 空键=未经验证，只贴地不入表
            }
            FixChar->SetActorLocation(Fixed, /*bSweep=*/true, nullptr, ETeleportType::TeleportPhysics);
            WT_WARNF(TEXT("[teleport-fix] ground-fix Z=%.0f key='%s'"),
                     FixHit.ImpactPoint.Z, *Key);
        }
        else
        {
            WT_WARNF(TEXT("[teleport-fix] ground-fix-skip (terrain not streamed yet) key='%s'"), *Key);
        }
    }), 0.6f, /*bLoop=*/false);
}

// ★0.2.153o 落地面朝目标（用户定案：「传送到终端**面前**」）：落点算法只能保证脚下有地、身上不卡，
// 但镜头朝向仍沿用传送前的旧朝向 ⇒ 人站在终端旁边却对着墙/荒野，体感就是「没传送到终端面前」。
// 落地后把控制器与角色 yaw 对准目标（只转水平朝向，不改 pitch/roll，避免仰天/低头）。
// SetControlRotation 为 AController 的 public 虚函数（Controller.h:163），蓝图可调、服务端调安全。
static void WT_FaceTarget(AFGCharacterPlayer* Character, const FVector& TargetLoc, float ExtraYawDeg = 0.f)
{
    if (!Character)
    {
        return;
    }
    AController* Ctrl = Character->GetController();
    if (!Ctrl)
    {
        return;
    }
    FVector Dir = TargetLoc - Character->GetActorLocation();
    Dir.Z = 0.f;
    if (Dir.SizeSquared() < KINDA_SMALL_NUMBER)
    {
        return;
    }
    FRotator Look = Dir.Rotation();             // 水平朝向（Z 已清零 ⇒ pitch=0）
    if (ExtraYawDeg != 0.f)
    {
        // ★0.2.153x：在「面向目标」基础上叠加额外偏航（用户实测落点后需补转才能正对面板/入口）。
        // UE Yaw 正方向为逆时针（左转），故「左转 N°」= +N、「右转 N°」= -N。
        Look.Yaw = FMath::UnwindDegrees(Look.Yaw + ExtraYawDeg);
    }
    Ctrl->SetControlRotation(Look);
    Character->SetActorRotation(FRotator(0.f, Look.Yaw, 0.f));
}

// ★本轮：稳健障碍检测 —— 用 ObjectType 查询【所有碰撞体类型】(WorldStatic/WorldDynamic/Pawn/
// PhysicsBody/Vehicle/Destructible)，规避「部分载具/飞行器对 ECC_Pawn 通道为 Ignore/Overlap 导致
// ECC_Pawn overlap 漏检、落点被接受在载具内」(用户实锤"卡进飞行器")。只要某 actor 有碰撞体且与
// 玩家胶囊重叠（不论它对 Pawn 的响应是 Block 还是 Overlap），都判为阻挡。忽略玩家/目标/枢纽本体
// 及其挂接（平台/栏杆/楼梯=可站立，不算障碍）。返回首个阻挡者名字（空=未挡）。
static FString WT_BlockingActorAt(UWorld* World, const FVector& Center,
                                  const FCollisionShape& Shape,
                                  const TArray<AActor*>& Ignore)
{
    if (!World) { return FString(); }
    TArray<FOverlapResult> Hits;
    FCollisionObjectQueryParams ObjQ;
    ObjQ.AddObjectTypesToQuery(ECC_WorldStatic);
    ObjQ.AddObjectTypesToQuery(ECC_WorldDynamic);
    ObjQ.AddObjectTypesToQuery(ECC_Pawn);
    ObjQ.AddObjectTypesToQuery(ECC_PhysicsBody);
    ObjQ.AddObjectTypesToQuery(ECC_Vehicle);
    ObjQ.AddObjectTypesToQuery(ECC_Destructible);
    FCollisionQueryParams QP(SCENE_QUERY_STAT(WT_ObjOverlap), /*bTraceComplex=*/false);
    for (AActor* A : Ignore) { QP.AddIgnoredActor(A); }
    if (!World->OverlapMultiByObjectType(Hits, Center, FQuat::Identity, ObjQ, Shape, QP))
    {
        return FString();
    }
    for (const FOverlapResult& H : Hits)
    {
        AActor* A = H.GetActor();
        if (A && !Ignore.Contains(A))
        {
            // ★本轮：音频/后处理/触发体积(AVolume)无实体碰撞，不应作为落点障碍
            // （AkSpatialAudioVolume 实锤误杀落点）；载具/飞行器由调用方从忽略集剔除后在此被抓到。
            if (Cast<AVolume>(A)) { continue; }
            return A->GetName();
        }
    }
    return FString();
}

// ★本轮：判断「枢纽/建筑挂接中的可移动障碍」（飞行器/载具/门等）—— 这些不进忽略集，
// 要被落点障碍检测抓到（用户实锤"卡飞行器"：枢纽自带飞行器是 hub 挂接，旧忽略集把它跳过了）。
// 静止结构（平台/栏杆/楼梯=可站立）仍进忽略集。
static bool WT_IsMovableObstacle(AActor* A)
{
    if (!A) { return false; }
    if (A->IsA<AFGVehicle>()) { return true; }
    const USceneComponent* RC = A->GetRootComponent();
    if (RC && RC->Mobility != EComponentMobility::Static) { return true; }
    return false;
}

// ★本轮：收集「落点需避让的载具/飞行器包围盒」。
// 关键实锤：枢纽自带飞行器【没有碰撞体】（玩家能直接穿过它 = 用户实锤"可以动" ⇒ 非卡死），
//   OverlapMultiByObjectType 查不到它，只能按【包围盒 XY】避让。收录三类：
//   ① 目标/枢纽的【可移动挂接】(飞行器/载具/门) —— 枢纽自带飞行器即此类（枢纽挂接+可移动）；
//   ② 目标/枢纽挂接中【完全没有碰撞体】的 actor —— 静止平台/栏杆有碰撞体(可站立)被排除，
//      无碰撞体的挂接(=玩家能穿过的飞行装饰/载具)必被收；这能抓到「Static 移动性 + 无碰撞 + 通用名」
//      的 mod 飞行器（前两轮 AFGVehicle 枚举 / 关键词都没抓到的正是它）；
//   ③ 目标 Center 半径 Radius 内、IsA<AFGVehicle>() 或名字命中载具关键词 的 actor —— 覆盖非挂接的 mod 飞行载具。
// 静止【有碰撞】结构（平台/栏杆/楼梯=可站立）不入此表，落点仍可在平台上。
static bool WT_NameIsVehicle(AActor* A)
{
    if (!A) { return false; }
    const FString N = A->GetName();
    return N.Contains(TEXT("Vehicle")) || N.Contains(TEXT("Fly"))
        || N.Contains(TEXT("Air")) || N.Contains(TEXT("Drone"))
        || N.Contains(TEXT("Hover")) || N.Contains(TEXT("Ship"))
        || N.Contains(TEXT("Plane")) || N.Contains(TEXT("Copter"));
}
// 某 actor 是否【没有任何碰撞体】（完全无碰撞 ⇒ 玩家能穿过；区别于可站立的实体平台）
static bool WT_HasAnyCollision(AActor* A)
{
    if (!A) { return false; }
    TArray<UPrimitiveComponent*> Prims;
    A->GetComponents<UPrimitiveComponent>(Prims);
    for (UPrimitiveComponent* P : Prims)
    {
        if (P && P->GetCollisionEnabled() != ECollisionEnabled::NoCollision) { return true; }
    }
    return false;
}
static TArray<FBox> WT_CollectVehicleBoxes(UWorld* W, AActor* Target, AActor* Hub,
                                           const FVector& Center, float Radius)
{
    TArray<FBox> Out;
    auto AddAttached = [&](AActor* Root)
    {
        if (!Root) { return; }
        TArray<AActor*> Attached;
        Root->GetAttachedActors(Attached, /*bResetArray=*/true, /*bRecursivelyIncludeAttachedActors=*/true);
        for (AActor* A : Attached)
        {
            if (!A) { continue; }
            // ② 无碰撞挂接 或 ① 可移动 或 名字命中 ⇒ 收（平台有碰撞且非移动非命中 ⇒ 排除）
            if (!WT_HasAnyCollision(A) || WT_IsMovableObstacle(A) || WT_NameIsVehicle(A))
            {
                const FBox B = A->GetComponentsBoundingBox();
                if (B.IsValid != 0) { Out.Add(B); }
            }
        }
    };
    AddAttached(Target);
    AddAttached(Hub);
    if (W)
    {
        for (TActorIterator<AActor> It(W); It; ++It)
        {
            AActor* A = *It;
            if (!A) { continue; }
            const FBox B = A->GetComponentsBoundingBox();
            if (B.IsValid == 0) { continue; }
            if ((B.GetCenter() - Center).Size() > Radius) { continue; }
            if (A->IsA<AFGVehicle>() || WT_NameIsVehicle(A)) { Out.Add(B); }
        }
    }
    return Out;
}

// 候选水平 XY 是否落在任一包围盒（XY 扩张 PadR 防擦边）内 —— 落点避让载具用
static bool WT_IsInBoxXY(const FVector& At, const TArray<FBox>& Boxes, float PadR)
{
    for (const FBox& B : Boxes)
    {
        const FVector C = B.GetCenter();
        const FVector E = B.GetExtent();
        if (FMath::Abs(At.X - C.X) <= (E.X + PadR) && FMath::Abs(At.Y - C.Y) <= (E.Y + PadR))
        {
            return true;
        }
    }
    return false;
}

// ★0.2.153z11o：取 actor 的【本地空间】AABB（与世界旋转无关）。
// 旧法 `GetComponentsBoundingBox()` 取的是【世界轴对齐盒】(AABB)，再反变换 8 个角回本地 →
// 对旋转后的 actor 会高估本地盒尺寸、甚至把"最短水平轴"判反（旋转 corruption），导致枢纽一旋转
// 落点就乱/抖动。正确做法：逐 primitive 取【本地 bounds】(GetLocalBounds) 经其相对 actor 的
// transform 累加，得到与旋转无关的本地盒（LocalCenter/LExt 正确）。下游 TransformPosition/
// TransformVector 不变，仅把"世界 AABB 反变换"这一步换掉。
static FBox WT_GetActorLocalBox(AActor* A)
{
    FBox LB(EForceInit::ForceInitToZero);
    if (!A) return LB;
    TArray<UPrimitiveComponent*> Prims;
    A->GetComponents<UPrimitiveComponent>(Prims);
    const FTransform ActorInv = A->GetTransform().Inverse();
    for (UPrimitiveComponent* P : Prims)
    {
        if (!P) continue;
        // 组件本地空间几何盒（不含组件相对 actor 的 transform）。
        // ★本 CSS 引擎 GetLocalBounds() 为【无参、返回 FBoxSphereBounds】(SceneComponent.h:237)，
        //   与标准 UE 双参 (FVector&,FVector&) 签名不同——已回一手头文件核实。用 GetBox() 取 FBox。
        const FBoxSphereBounds B = P->GetLocalBounds();
        FBox PB = B.GetBox();
        // 组件本地盒 → 经组件→世界 → 再经 actor→世界逆 = 组件在 actor 本地空间的盒（旋转无关）
        const FTransform Rel = P->GetComponentToWorld() * ActorInv;
        PB = PB.TransformBy(Rel);
        LB += PB;
    }
    return LB;
}


// ★0.2.153q 统一落点搜索器（用户定案：「所有建筑传送逻辑也按枢纽传送走」）
// 0.2.153g~0.2.153o2 为枢纽终端迭代出来的那一套，原先只有枢纽在用；普通建筑/机器/载具仍走
// 0.2.88 的 WT_FindSafeLandingSpot 环搜（搜不到就砸建筑上方 300 —— 与「卡在枢纽背面的墙里」
// 同源的老毛病：建筑 pivot 常在结构内部或底座之下，砸正上方 = 把人塞进结构里）。
// 现在抽成公共函数，两条路径共用同一套：
//   ① 起跳距现场推导 = 目标包围盒沿主方向的水平半径 + 胶囊半径 + 10cm（不留死数字）；
//   ② 高度基准 = 锚点正下方自然地面 Z（pivot 埋地时拿 actor Z 当基准必错）；
//   ③ 8 方向 × 14 圈候选，按代价排序：近场(≤3m)优先 → 正面优先 → 宽松阶段排在最后；
//   ④ 落差哨兵（地面比候选低 >350cm = 缝/坑）、高度哨兵（高于自然地面 +450 严格 / +250 宽松）；
//   ⑤ 严格阶段用真实碰撞体 overlap 查「会不会卡进结构」，宽松阶段退化为「头顶净空」射线；
//   ⑥ 严格阶段全灭先查落点表（远距离直传），命中即返回 bFromCache（调用方补一次 ground-fix）；
//   ⑦ 命中即移动并写表 ⇒ 调用方拿到 FWT_LandSpot 后只做朝向/通知，不再二次移动。
struct FWT_LandSpot
{
    FVector Dest;
    bool    bFound;
    bool    bFromCache;   // true: 点来自存档落点表（函数内已移动），调用方需补一次 ground-fix
};

static FWT_LandSpot WT_SearchLandingSpot(UWorld* World, AFGCharacterPlayer* Character,
                                         const AActor* Target, const FVector& LandBase,
                                         const TCHAR* DirSrc,
                                         const FCollisionQueryParams& GroundTrace,
                                         const FCollisionQueryParams& LandOverlapQ,
                                         const FString& SpotKey, const TCHAR* LogTag,
                                         float StandoffOverride = 0.f,
                                         float MaxStandoff = 0.f,
                                         const FVector* AnchorOverride = nullptr,
                                         const TArray<AActor*>* ExtraIgnore = nullptr)
{
    FWT_LandSpot Out;
    Out.Dest = FVector::ZeroVector;
    Out.bFound = false;
    Out.bFromCache = false;
    if (!World || !Character || !Target)
    {
        return Out;
    }

    // ★0.2.153x：AnchorOverride 由调用方显式给定（电梯=塔基座 pivot），否则回退 actor 位置。
    const FVector Anchor = (AnchorOverride != nullptr) ? *AnchorOverride : Target->GetActorLocation();
    const UCapsuleComponent* Cap = Character->GetCapsuleComponent();
    const float CapH = Cap ? Cap->GetScaledCapsuleHalfHeight() : 90.f;
    const float CapR = Cap ? Cap->GetScaledCapsuleRadius() : 60.f;
    const FCollisionShape CapShape = FCollisionShape::MakeCapsule(CapR, FMath::Max(CapH - 2.f, 10.f));
    // ★本轮：稳健障碍忽略集（玩家 + 目标本体 + 目标挂接 + 调用方追加的枢纽本体）。
    // 目标挂接(建筑子部件)/枢纽本体及其挂接(平台/栏杆/楼梯=可站立)都不算障碍。
    TArray<AActor*> FallbackIgnore;
    FallbackIgnore.Add(Character);
    FallbackIgnore.Add(const_cast<AActor*>(Target));
    {
        TArray<AActor*> TA;
        Target->GetAttachedActors(TA, /*bResetArray=*/true, /*bRecursivelyIncludeAttachedActors=*/true);
        for (AActor* A : TA)
        {
            if (A && WT_IsMovableObstacle(A)) { continue; }
            FallbackIgnore.Add(A);
        }
    }
    if (ExtraIgnore)
    {
        for (AActor* A : *ExtraIgnore) { FallbackIgnore.Add(A); }
    }
    // ★本轮：收集落点需避让的载具/飞行器包围盒（无碰撞体，必须按 XY 包围盒避开；详见 WT_CollectVehicleBoxes）
    const TArray<FBox> FallbackVehicleBoxes = WT_CollectVehicleBoxes(
        World, const_cast<AActor*>(Target), nullptr, Target->GetActorLocation(), 1500.f);

    // ① 起跳距现场推导（★0.2.153i：取代 0.2.153c 的「hub bbox 半宽 +100」——那个死数字正是
    // 「落在终端 10 米外」的根因；★0.2.153q 推广到所有建筑：建筑多大多宽就从多远起跳）。
    // ★0.2.153x：StandoffOverride>0 时直接采用调用方给定固定起跳距（电梯塔 bbox 巨大，用 bbox
    // 推导会把起跳距推到几十米外 ⇒ 落点全灭；用户定案固定 1000cm/10m），跳过 bbox 推导。
    float StandoffBase = 150.f; // 目标无有效包围盒时的兜底（≈旧「交互范围内」语义）
    if (StandoffOverride > 0.f)
    {
        StandoffBase = StandoffOverride;
    }
    else
    {
        const FBox TBox = Target->GetComponentsBoundingBox();
        if (TBox.IsValid != 0)
        {
            const FVector TExt = TBox.GetExtent();
            const float Radius = FMath::Abs(TExt.X * LandBase.X) + FMath::Abs(TExt.Y * LandBase.Y);
            StandoffBase = Radius + CapR + 10.f;
        }
    }

    // ② 高度基准（第五轮定案）：锚点正下方的自然地面 Z。
    float GroundRefZ = Anchor.Z;
    {
        FHitResult GHit;
        if (World->LineTraceSingleByChannel(GHit,
                FVector(Anchor.X, Anchor.Y, Anchor.Z + 300.f),
                FVector(Anchor.X, Anchor.Y, Anchor.Z - 600.f), ECC_Pawn, GroundTrace))
        {
            GroundRefZ = GHit.ImpactPoint.Z;
        }
        else
        {
            UNavigationSystemV1* Nav0 = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
            FNavLocation GNav;
            if ((Nav0 != nullptr)
                && Nav0->ProjectPointToNavigation(FVector(Anchor.X, Anchor.Y, Anchor.Z + 88.f),
                                                  GNav, FVector(200.f, 200.f, 400.f)))
            {
                GroundRefZ = GNav.Location.Z;
            }
        }
    }
    WT_WARNF(TEXT("[%s] anchor=%s groundRefZ=%.0f dirSrc=%s standoff=%.0f"),
             LogTag, *Anchor.ToString(), GroundRefZ, DirSrc, StandoffBase);

    // ③ 方向扇 8 条（45° 间隔，自 LandBase 起）
    FVector LandDirs[8];
    for (int32 k = 0; k < 8; ++k)
    {
        LandDirs[k] = LandBase.RotateAngleAxis(45.f * k, FVector::UpVector);
    }
    // ★0.2.153o 前向优先重排：0°, ±45°, ±90°, ±135°, 180°（原 0..7 顺序会让两侧/背面抢先命中）
    static const int32 DirOrder[8] = {0, 1, 7, 2, 6, 3, 5, 4};

    // ④ 候选代价排序
    struct FHubCandidate { int32 Ring; int32 DirIdx; int32 Cost; bool bBack; };
    const float NearMax = 300.f;   // ★0.2.153o2 用户定案：近场半径 3 米
    TArray<FHubCandidate> Plan;
    Plan.Reserve(8 * 14);
    for (int32 Ring = 0; Ring < 14; ++Ring)
    {
        const bool bNear = (StandoffBase + 100.f * Ring) <= NearMax;
        for (int32 Oi = 0; Oi < 8; ++Oi)
        {
            FHubCandidate C;
            C.Ring   = Ring;
            C.DirIdx = DirOrder[Oi];
            C.bBack  = (Oi >= 5);                       // DirOrder[5..7] = ±135° / 180°
            C.Cost   = (bNear ? 0 : 100)                // 近场优先
                     + Ring + (C.bBack ? 2 : 0) + (Ring >= 10 ? 20 : 0);
            Plan.Add(C);
        }
    }
    Plan.Sort([](const FHubCandidate& A, const FHubCandidate& B)
    {
        if (A.Cost != B.Cost) { return A.Cost < B.Cost; }
        if (A.bBack != B.bBack) { return !A.bBack; }    // 同代价先正面
        return A.Ring < B.Ring;
    });

    bool bCacheTried = false;
    for (int32 Ci = 0; (Ci < Plan.Num()) && !Out.bFound; ++Ci)
    {
        const bool bRelaxed = (Plan[Ci].Ring >= 10);
        // ⑤ 严格阶段全灭时先试落点表（此前验证过的落点），再进宽松阶段——
        // 免得「明明存档里有好点、却把人扔到 10 米外」。
        if (!bCacheTried && bRelaxed && !SpotKey.IsEmpty())
        {
            bCacheTried = true;
            const TMap<FString, FVector> Spots = WT_LoadHubSpots();
            if (const FVector* Cached = Spots.Find(SpotKey))
            {
                // ★0.2.153f sweep=false：sweep 查路径不查终点，会把存档好的落点也挡掉。
                if (Character->SetActorLocation(*Cached, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics))
                {
                    Out.Dest = *Cached;
                    Out.bFound = true;
                    Out.bFromCache = true;
                    WT_WARNF(TEXT("[%s] cache-landed dest=%s"), LogTag, *Cached->ToString());
                    break;
                }
                WT_WARNF(TEXT("[%s] cache-move-failed at %s"), LogTag, *Cached->ToString());
            }
            else
            {
                WT_WARNF(TEXT("[%s] cache-miss key='%s' (进入宽松落点阶段)"), LogTag, *SpotKey);
            }
        }
        const int32 Di = Plan[Ci].DirIdx;
        const float Standoff = StandoffBase + 100.f * Plan[Ci].Ring;
        // ★0.2.153z 枢纽 3m 内定标：超出 MaxStandoff 的候选直接跳过（不往外推到远场），
        // 让落点约束在目标前方 ≤3m（MaxStandoff=0 表示不限制，普通建筑/机器不受影响）。
        if (MaxStandoff > 0.f && Standoff > MaxStandoff)
        {
            continue;
        }
        // ★0.2.153x5：候选点 Z 抬到 GroundRefZ（锚点正下方自然地面 / 平台顶面），
        // 否则电梯这类「actor 原点埋在平台下方」的目标，探地射线起点低于平台顶面，
        // 永远穿透平台打到下方地形（floor='Landscape'）——落点永不上平台。
        const FVector Candidate(
            Anchor.X + LandDirs[Di].X * Standoff,
            Anchor.Y + LandDirs[Di].Y * Standoff,
            GroundRefZ);

        // ★本轮：候选 XY 落在载具/飞行器包围盒内（无碰撞体，玩家能穿过）⇒ 跳，逼向空侧
        if (WT_IsInBoxXY(Candidate, FallbackVehicleBoxes, CapR))
        {
            WT_WARNF(TEXT("[%s] vehicle-box dir=%d standoff=%.0f at %s"),
                     LogTag, Di, Standoff, *Candidate.ToString());
            continue;
        }

        // 下探 1200 找地面（★0.2.153n：-400 打不到悬空底座下的缝底，看不出那里是个缝）
        FHitResult FloorHit;
        FVector FloorPoint = FVector::ZeroVector;
        const TCHAR* FloorSrc = TEXT("trace");
        if (World->LineTraceSingleByChannel(FloorHit,
                Candidate + FVector(0.f, 0.f, 200.f),
                Candidate - FVector(0.f, 0.f, 1200.f), ECC_Pawn, GroundTrace))
        {
            FloorPoint = FloorHit.ImpactPoint;
        }
        else
        {
            // navmesh 投影兜底（★0.2.153c）：竖直 trace 落空时按 extent(200,200,400) 投影，
            // 再空则沿该方向外推 100 投影一次，还空则放弃该候选。
            UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
            FNavLocation NavLoc;
            const FVector ProjBase = Candidate + FVector(0.f, 0.f, 88.f);
            const bool bNav1 = (NavSys != nullptr)
                && NavSys->ProjectPointToNavigation(ProjBase, NavLoc, FVector(200.f, 200.f, 400.f));
            if (!bNav1)
            {
                const FVector PushBase = ProjBase + LandDirs[Di] * 100.f;
                const bool bNav2 = (NavSys != nullptr)
                    && NavSys->ProjectPointToNavigation(PushBase, NavLoc, FVector(200.f, 200.f, 400.f));
                if (!bNav2)
                {
                    WT_WARNF(TEXT("[%s] no-floor+no-navmesh dir=%d standoff=%.0f at %s"),
                             LogTag, Di, Standoff, *Candidate.ToString());
                    continue;
                }
            }
            FloorPoint = NavLoc.Location;
            FloorSrc = TEXT("navmesh");
        }
        // 落差哨兵（bug②「传送过去掉进空隙里」）：地面比候选点低 >350cm = 打到缝底/坑底
        if (FloorPoint.Z < Candidate.Z - 350.f)
        {
            WT_WARNF(TEXT("[%s] gap-reject dir=%d standoff=%.0f floorZ=%.0f candZ=%.0f at %s"),
                     LogTag, Di, Standoff, FloorPoint.Z, Candidate.Z, *Candidate.ToString());
            continue;
        }
        // 落点离地余量 +20cm（★0.2.153n：+2 会被起伏地形擦边误杀，是「48 候选全灭」元凶）
        const FVector Dest = FloorPoint + FVector(0.f, 0.f, CapH + 20.f);
        // 高度哨兵：严格 +450（枢纽抬高平台 ≈+3m 可用，有 overlap 兜底）/ 宽松 +250（防上房顶）
        const float MaxAboveGround = bRelaxed ? 250.f : 450.f;
        if (Dest.Z > GroundRefZ + MaxAboveGround)
        {
            WT_WARNF(TEXT("[%s] height-reject dir=%d destZ=%.0f groundRefZ=%.0f cap=%.0f at %s"),
                     LogTag, Di, Dest.Z, GroundRefZ, MaxAboveGround, *Dest.ToString());
            continue;
        }
        // ★本轮：实体障碍检测（墙/机器/载具/飞行器）—— 用 ObjectType 全查，规避 ECC_Pawn 漏检
        // （部分载具/飞行器对 Pawn 通道为 Ignore/Overlap，落点会被接受在载具内）。覆盖严格/宽松两阶段。
        const FString Blocker = WT_BlockingActorAt(World, Dest, CapShape, FallbackIgnore);
        if (!Blocker.IsEmpty())
        {
            WT_WARNF(TEXT("[%s] obstacle-reject dir=%d standoff=%.0f at %s blocker='%s'"),
                     LogTag, Di, Standoff, *Dest.ToString(), *Blocker);
            continue;
        }
        if (bRelaxed)
        {
            // ★0.2.153n 宽松阶段的替代保险「头顶净空」：不放 overlap（怕地形擦边误杀），
            // 但从落点头顶向上打 400 ⇒ 命中 = 落点在某结构内部/挑檐下/缝隙里，弃用。
            FHitResult RoofHit;
            if (World->LineTraceSingleByChannel(RoofHit, Dest, Dest + FVector(0.f, 0.f, 400.f),
                                                ECC_Pawn, LandOverlapQ))
            {
                WT_WARNF(TEXT("[%s] relaxed-roof-reject dir=%d standoff=%.0f at %s roof='%s'"),
                         LogTag, Di, Standoff, *Dest.ToString(),
                         RoofHit.GetActor() ? *RoofHit.GetActor()->GetName() : TEXT("none"));
                continue;
            }
        }
        // ★0.2.153f sweep=false：sweep 查「当前位置→落点」整条路径，远距必被沿途地形误挡。
        if (!Character->SetActorLocation(Dest, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics))
        {
            WT_WARNF(TEXT("[%s] move-reject dir=%d standoff=%.0f at %s"),
                     LogTag, Di, Standoff, *Dest.ToString());
            continue;
        }
        WT_WARNF(TEXT("[%s] landed dir=%d standoff=%.0f relaxed=%d dirSrc=%s floor='%s'(%s) floorZ=%.0f groundRefZ=%.0f dest=%s"),
                 LogTag, Di, Standoff, bRelaxed ? 1 : 0, DirSrc,
                 FloorHit.GetActor() ? *FloorHit.GetActor()->GetName() : TEXT("none"),
                 FloorSrc, FloorPoint.Z, GroundRefZ, *Dest.ToString());
        if (!SpotKey.IsEmpty())
        {
            WT_SaveHubSpot(SpotKey, Dest);   // ★0.2.153e 落点入表，供远距离查表直传
        }
        Out.Dest = Dest;
        Out.bFound = true;
        break;
    }
    return Out;
}

void URemote_FactoryRCO::Server_TeleportTo_Implementation(AActor* Target)
{
    // 传送会写角色 Transform（可能带着载具/梯子/传送带上的附着状态），是本 mod 里最"物理"的一步。
    WT_ZONE_COARSE(TEXT("RCO.TeleportTo"));

    // AUTHORITY GATE: the server owns the character transform, so this must run here for the move
    // to stick (and for a host to see it).
    if (!WT_HasAuthority(this))
    {
        WT_WARNF(TEXT("Server_TeleportTo called without authority — ignored."));
        return;
    }

    AFGCharacterPlayer* Character = GetOwnerPlayerCharacter();
    if (!Character)
    {
        Client_NotifyTeleport(Target, false, WTTeleportFail::TargetGone);
        return;
    }

    // The actor reference arrives by NetGUID and resolves to null if it is no longer relevant to
    // this client (demolished, collected, or outside its network relevancy set). Bail cleanly.
    if (!IsValid(Target))
    {
        WT_WARNF(TEXT("Server_TeleportTo: target did not resolve on the server."));
        Client_NotifyTeleport(Target, false, WTTeleportFail::TargetGone);
        return;
    }

    UWorld* World = GetWorld();
    const FVector Center = Target->GetActorLocation();
    const FBox Box = Target->GetComponentsBoundingBox(/*bNonColliding=*/true);
    const bool bHaveBox = (Box.IsValid != 0);   // IsValid 是 uint8 位域，直转 bool 报 C4800

    FVector ToPlayer = Character->GetActorLocation() - Center;
    ToPlayer.Z = 0.f;
    if (ToPlayer.SizeSquared() < KINDA_SMALL_NUMBER)
    {
        ToPlayer = FVector(0.f, 1.f, 0.f); // arbitrary fallback direction
    }
    ToPlayer.Normalize();

    // ★0.2.135 外星收集品（slug / 球 / 果实）走专用落点：坐标点 2m 外；洞穴内强制站在物品上方。
    // 机器/建筑仍走 0.2.88 的通用环搜（它们体积大，本来就该落在建筑外沿）。
    const bool bIsPickup = (Cast<AFGItemPickup>(Target) != nullptr);

    FVector Destination;
    // ★0.2.153f：本函数内所有 SetActorLocation 一律 sweep=false（sweep 查「当前位置→落点」路径
    // 而非终点，是 hub encroach-reject / 兜底砸落 bMoved=false 拒传的误判源）；落点合法性由
    // 上游 trace/overlap/高度哨兵保证，砸落由 ground-fix / 重力着陆兜底。
    // ★0.2.153 传送枢纽(终端/集成制作台等枢纽子建筑)：按用户建议简化为"落到终端交互范围内正前方"。
    // 不再做包围盒粗判 / 8 方向环搜 / 屋顶检测。终端屏幕法线正对玩家，
    // 故“终端朝向” = 玩家所在方向 = 函数上文已算好的 ToPlayer(已归一化、压平)。
    // 落点 = 终端位置 + ToPlayer × 150(≈1.5m，交互范围内)；下探找地面(忽略终端/枢纽组件/玩家自身)，
    // 带 sweep 的 SetActorLocation 落位并验证不嵌物体；主方向失败仅做同距(≤1.5m)小幅旋转兜底，绝不扩距离、不爬房顶。
    // ★0.2.153v 太空电梯专用落点（用户 2026-10-23 实测：落点随机、距终端约 100 单位、
    // 未落入塔底交互范围）。SDK 一手实锤：AFGBuildableSpaceElevator
    // （FGBuildableSpaceElevator.h:51）无任何终端位置接口，交互靠玩家准星 trace 塔身
    // （FGBuildable.h:734 只有 GetInteractWidgetClass）⇒ 落点只要靠近塔身即可交互。
    // 通用算法（bld 分支）对它失效的根因：
    //   ① 塔 bbox 水平半径大 ⇒ WT_SearchLandingSpot 按 bbox 推导 StandoffBase，
    //      Ring0 就在塔基外沿，近场候选全被 overlap/gap 哨兵 reject；
    //   ② 宽松阶段（Ring≥10）跳过 overlap 只做头顶净空 ⇒ 落点跑到 10m 开外且可能
    //      塞进塔基（与枢纽 bug② 同源），表现即「落点随机、不在交互范围」。
    // 专用分支：方向 = 自身 forward（authored 正面），走枢纽终端同款 8 向扇搜 +
    // overlap 保留塔体碰撞防嵌墙；起跳距见下方 0.2.153x 定案（1000cm/10m），全灭则拒传。
    AFGBuildableSpaceElevator* Elevator = Cast<AFGBuildableSpaceElevator>(Target);
    if (Elevator)
    {
        // ★0.2.153x12 用户实测 x11「本地系 TransformPosition」：旋转建筑落点仍全错。
        // 根因（dump 一手 + 现象闭环）：电梯旋转被烘焙进子组件 relRot（Input5 relRot y=90），
        // actor 根变换(GetActorTransform) 停在放置朝向、不随玩家旋转更新 ⇒ TransformPosition(本地+Y)
        // 永远落在旧朝向一侧 ⇒ 一转建筑坐标全错。
        // 修复 = 改读建筑「真实入口组件」的世界变换（引擎合成，含子组件 relRot，旋转真跟随）：
        //   EleCenter = Target->GetActorLocation()（绕自身旋转，中心不变）
        //   EleOutward= Entrance->GetForwardVector()（入口组件世界前向，真朝向；兜 GetActorForwardVector）
        //   EleRight  = EleOutward × Up（水平右向，正=玩家右手边）
        //   WorldLand = EleCenter + EleDir*(Standoff+200*Push) + EleRight*RightOff
        //             （EleDir = Input5前向绕塔偏航后的落点方向；x16 定标：朝外+90 → 正对大门，7.5m → 控制台前2m）
        // 落点定好后仍走原版引擎 TeleportTo 防嵌 → 失败回退 SetActorLocation（x10/x11 精神保留）。
        // 零手算魔法世界角，彻底满足「别写偏移、跟随建筑」铁律。
        FCollisionQueryParams EleGround(SCENE_QUERY_STAT(WT_EleLanding), /*bTraceComplex=*/false);
        EleGround.AddIgnoredActor(Character);
        {
            TArray<AActor*> Attached;
            Target->GetAttachedActors(Attached, /*bResetArray=*/true,
                                      /*bRecursivelyIncludeAttachedActors=*/true);
            for (AActor* A : Attached)
            {
                if (A)
                {
                    EleGround.AddIgnoredActor(A);
                }
            }
        }
        // 防嵌查询：只忽略玩家（电梯本体不忽略=平台是合法地面且防嵌平台，x4 纪律）。
        FCollisionQueryParams EleOverlap(SCENE_QUERY_STAT(WT_EleLandOverlap), /*bTraceComplex=*/false);
        EleOverlap.AddIgnoredActor(Character);
        {
            TArray<AActor*> Attached2;
            Target->GetAttachedActors(Attached2, /*bResetArray=*/true,
                                      /*bRecursivelyIncludeAttachedActors=*/true);
            for (AActor* A : Attached2)
            {
                if (A)
                {
                    EleOverlap.AddIgnoredActor(A);
                }
            }
        }

        // —— 真实入口组件（旋转跟随锚）：中部货物输入 Input5，dump 实锤本地(1500,2000,100) 朝外 relRot y=90 ——
        USceneComponent* EleEntrance = nullptr;
        for (UActorComponent* Comp : Target->GetComponents())
        {
            if (Comp && Comp->GetName().Contains(TEXT("Input5")))
            {
                if (USceneComponent* SC = Cast<USceneComponent>(Comp)) { EleEntrance = SC; break; }
            }
        }
        if (!EleEntrance)  // 兜底：任意 +Y 侧 Input（前脸货物输入 Input4~6）
        {
            for (UActorComponent* Comp : Target->GetComponents())
            {
                if (Comp && Comp->GetName().Contains(TEXT("Input")) && Cast<USceneComponent>(Comp))
                {
                    USceneComponent* SC = Cast<USceneComponent>(Comp);
                    if (SC->GetRelativeLocation().Y > 0.f) { EleEntrance = SC; break; }
                }
            }
        }
        const FVector EleCenter = Target->GetActorLocation();
        const FVector EleOutward = (EleEntrance ? EleEntrance->GetForwardVector() : Target->GetActorForwardVector()).GetSafeNormal();
        const FVector EleRight = FVector::CrossProduct(EleOutward, FVector::UpVector).GetSafeNormal(); // 水平右向
        // —— 落点参数（旋转无关；调参只改这几行）——
        const float EleYawOff   = 90.f;    // 落点方向相对「Input5前向」的偏航（度，绕世界Up）。x16 实锤：Input5 是传送带口、
                                           // 其前向=大门口朝外方向的左侧90°（用户观察：落点在门口偏左那侧）⇒ 朝外=前向+90，正对大门/控制台
        const float EleStandout = 2100.f;  // 沿落点方向外推 21m：x19 的 22m 用户实测仍偏外 → 收 1m 到 2100（x20）。被挡则继续往外推兜底
        const float EleRightOff = 0.f;     // 沿真实右向手偏（右偏填正值；旋转安全，取自组件世界朝向）
        // 旋转落点方向（绕世界Up）：用 Outward/Right 线性组合，避坑 CSS 引擎 RotateAngleAxis 异常；
        // 符号取 -sin 复刻 RotateAngleAxis(+Yaw,Up) 左转语义（绕 +Z 右旋=俯视逆时针=左转）。
        const float EleYawRad = FMath::DegreesToRadians(EleYawOff);
        const FVector EleDir = (EleOutward * FMath::Cos(EleYawRad)
                               - EleRight * FMath::Sin(EleYawRad)).GetSafeNormal();
        const UCapsuleComponent* EleCap = Character->GetCapsuleComponent();
        const float EleCapH = EleCap ? EleCap->GetScaledCapsuleHalfHeight() : 90.f;
        const float EleCapR = EleCap ? EleCap->GetScaledCapsuleRadius() : 60.f;

        auto WT_EleTraceFloorZ = [&](const FVector& At, float& OutZ) -> bool
        {
            FHitResult FH;
            if (World->LineTraceSingleByChannel(FH,
                    FVector(At.X, At.Y, At.Z + 600.f),
                    FVector(At.X, At.Y, At.Z - 1500.f),
                    ECC_Pawn, EleGround))
            {
                OutZ = FH.ImpactPoint.Z;
                return true;
            }
            return false;
        };

        // —— 确定性落点 + 防嵌：嵌塔则沿（绕塔偏航后的）落点方向外推重试（组件世界朝向，旋转安全）——
        bool bPlaced = false;
        FVector EleDest = FVector::ZeroVector;
        int32 UsedPush = 0;
        for (int32 Push = 0; (Push < 6) && !bPlaced; ++Push)
        {
            const FVector WorldLand = EleCenter + EleDir * (EleStandout + 200.f * Push) + EleRight * EleRightOff;
            float LZ = 0.f;
            if (!WT_EleTraceFloorZ(WorldLand, LZ))
            {
                UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
                FNavLocation NavLoc;
                if (NavSys && NavSys->ProjectPointToNavigation(
                        FVector(WorldLand.X, WorldLand.Y, WorldLand.Z + 88.f),
                        NavLoc, FVector(200.f, 200.f, 400.f)))
                {
                    LZ = NavLoc.Location.Z;
                }
                else
                {
                    WT_WARNF(TEXT("[teleport-spaceelev] no-floor at world=%s push=%d - skip"),
                             *WorldLand.ToString(), Push);
                    continue;
                }
            }
            // 落差哨兵：打到缝底/坑底（比落点低 >350）⇒ 换更远点
            if (LZ < WorldLand.Z - 350.f)
            {
                WT_WARNF(TEXT("[teleport-spaceelev] gap at world=%s floorZ=%.0f - skip"),
                         *WorldLand.ToString(), LZ);
                continue;
            }
            const FVector TryDest = FVector(WorldLand.X, WorldLand.Y, LZ + EleCapH + 20.f);
            // x17：检测胶囊半径收窄到 65%（用户要落「控制台前小平台」≈7~9m；7.5/9.5m 两档被全尺寸
            // 胶囊挡出）。真实落位仍先走引擎 TeleportTo（全尺寸胶囊防嵌）+ SetActorLocation 回退，不会硬嵌。
            const FCollisionShape EleTestShape = FCollisionShape::MakeCapsule(
                FMath::Max(EleCapR * 0.65f, 28.f), FMath::Max(EleCapH - 2.f, 10.f));
            if (World->OverlapBlockingTestByChannel(TryDest, FQuat::Identity, ECC_Pawn,
                    EleTestShape, EleOverlap))
            {
                // 诊断：列出阻挡者名字（OverlapMultiByChannel 实锤 World.h:2268；FOverlapResult::GetActor/
                // GetComponent 实锤 OverlapResult.h:29/32；仅在被挡时多查一次，不动判定语义）
                FString EleBlkNames;
                {
                    TArray<FOverlapResult> EleBlk;
                    if (World->OverlapMultiByChannel(EleBlk, TryDest, FQuat::Identity, ECC_Pawn,
                            EleTestShape, EleOverlap))
                    {
                        for (const FOverlapResult& OV : EleBlk)
                        {
                            if (AActor* BA = OV.GetActor())
                                EleBlkNames += BA->GetName() + TEXT(" ");
                            else if (UPrimitiveComponent* BC = OV.GetComponent())
                                EleBlkNames += BC->GetName() + TEXT(" ");
                        }
                    }
                }
                WT_WARNF(TEXT("[teleport-spaceelev] overlap at world=%s push=%d testR=%.0f by=[%s]- push out"),
                         *WorldLand.ToString(), Push, EleTestShape.Capsule.Radius, *EleBlkNames);
                UsedPush = Push;
                continue;
            }
            // 先走原版引擎 TeleportTo 防嵌（不嵌=原位落，嵌了引擎自动推出）；
            // 引擎拒绝（嵌太深推不出，x10 实锤会这样）再回退 SetActorLocation（x9 实测可落）。
            bPlaced = Character->TeleportTo(TryDest, Character->GetActorRotation());
            if (bPlaced)
            {
                WT_WARNF(TEXT("[teleport-spaceelev] engine-teleport accepted dest=%s push=%d"),
                         *TryDest.ToString(), Push);
            }
            else
            {
                bPlaced = Character->SetActorLocation(TryDest, /*bSweep=*/false, nullptr,
                                                     ETeleportType::TeleportPhysics);
                WT_WARNF(TEXT("[teleport-spaceelev] engine refused, fallback SetActorLocation dest=%s ok=%d push=%d"),
                         *TryDest.ToString(), bPlaced ? 1 : 0, Push);
            }
            if (bPlaced)
            {
                EleDest = TryDest;
                UsedPush = Push;
            }
        }
        if (bPlaced)
        {
            WT_WARNF(TEXT("[teleport-spaceelev] direct-land dest=%s (yaw=%.0f standoff=%.0f right=%.0f push=%d entrance=%s)"),
                     *EleDest.ToString(), EleYawOff, EleStandout, EleRightOff, UsedPush,
                     EleEntrance ? *EleEntrance->GetName() : TEXT("none"));
            // 落点在入口正前方，落地后面向塔即可
            WT_FaceTarget(Character, Target->GetActorLocation());
            WT_WARNF(TEXT("[teleport-result] '%s' -> dest=%s (space elevator direct landing, rotation-safe)"),
                     *Target->GetName(), *EleDest.ToString());
            Client_NotifyTeleport(Target, true, WTTeleportFail::None);
            return;
        }
        // 全灭（多为塔基周围区块未流送/结构遮挡）：拒绝传送，玩家走近后再试。
        WT_WARNF(TEXT("[teleport-spaceelev] no safe landing near '%s' at %s - refused"),
                 *Target->GetName(), *Target->GetActorLocation().ToString());
        Client_NotifyTeleport(Target, false, WTTeleportFail::NoSafeLanding);
        return;
    }

    AFGBuildableTradingPost* Hub = Target->GetTypedOuter<AFGBuildableTradingPost>();
    const bool bIsHubRelated = (Cast<AFGBuildableHubTerminal>(Target) != nullptr)
                            || (Hub != nullptr)
                            || Target->Implements<UFGTradingPostIntegratedBuildable>();
    if (bIsHubRelated)
    {
        // ★0.2.153g/h 坐标来源（SDK 一手实锤）：终端在枢纽上的位置是蓝图资源摆的，cpp 无偏移
        // 常量；引擎 spawn 终端就落在 TradingPost::mHubTerminalLocation（protected，外部拿不到）
        // 上 ⇒ 终端 actor 世界 pivot 与 BP 摆放点等价。★0.2.153h 简化（用户拍板「找枢纽终端的
        // 坐标不就好了」）：面板扫描只把 AFGBuildableHubTerminal 收进枢纽类目，Target 就是终端
        // 本尊，0.2.153g 的 TermAnchor/mHubTerminal 间接层是死代码，砍掉；传送时服务端直读
        // Target 活坐标，面板每开一次扫描回包 E.Location 也带最新坐标（客户端 Target 失效时按
        // 坐标直传用的就是它）——坐标每次打开面板都是动态刷新的，不落盘不过夜。
        // Hub 正本（bbox/忽略集用）：GetTradingPost()（SDK 公开函数）> GetTypedOuter > Subsystem 枚举兜底。
        if (!Hub)
        {
            // 注意：GetTradingPost() 是非 const 成员函数（SDK 21 行），Term 不能加 const。
            if (AFGBuildableHubTerminal* Term = Cast<AFGBuildableHubTerminal>(Target))
            {
                Hub = Term->GetTradingPost();
            }
        }
        if (!Hub)
        {
            if (AFGBuildableSubsystem* BS = AFGBuildableSubsystem::Get(World))
            {
                TArray<AFGBuildableTradingPost*> Hubs;
                BS->GetTypedBuildable<AFGBuildableTradingPost>(Hubs);
                if (Hubs.Num() > 0)
                {
                    Hub = Hubs[0];
                }
            }
        }

        // ★0.2.153h 坐标正本 = 终端活坐标（Target 本尊，见上注释）。
        // ★0.2.153z11l：制作台落点重定向到同 Hub 终端位置（用户定案"制作台落点=终端落点位置"）。
        //   制作台自身前的几何是房间/屋顶，任何以制作台为锚的外推都会落房间/上屋顶；终端落点经
        //   用户实测确认正确（Hub 主平台、终端旁），故制作台传送直接复用终端几何与方向。
        const bool bIsHubCraft = (Cast<AFGBuildableHubTerminal>(Target) == nullptr);
        AActor* RefTarget = Target;
        if (bIsHubCraft && Hub)
        {
            AFGBuildableHubTerminal* Term = nullptr;
            for (TActorIterator<AFGBuildableHubTerminal> It(World); It; ++It)
            {
                AFGBuildableHubTerminal* T = *It;
                if (T && T->GetTradingPost() == Hub) { Term = T; break; }
            }
            if (!Term)
            {
                for (TActorIterator<AFGBuildableHubTerminal> It(World); It; ++It)
                {
                    if (*It) { Term = *It; break; }
                }
            }
            if (Term) RefTarget = Term;
        }
        WT_WARNF(TEXT("[teleport-hub] ref-target=%s redirect=%d hub=%s"),
                 *RefTarget->GetName(), (RefTarget != Target) ? 1 : 0,
                 Hub ? *Hub->GetName() : TEXT("none"));
        // ★0.2.153z11v3c：落点方向/地板双修复（z11v3「恢复 HEAD」翻车后的精准修）。
        //   方向=枢纽中心→终端「径向外」(远离枢纽本体/储物盒，落终端正前方空地)；绝不加 ±90° 偏航。
        //   径向基准优先用 Hub Actor 原点(枢纽 pivot，离终端数米，稳定不退化)，退化才用 bbox 中心，
        //   再退化才用终端 forward(不翻转，实测 forward 指向远离枢纽的开阔面)。
        //   地板=GroundTrace 不忽略 Hub + ECC_Pawn 竖直射线(取平台顶)；落差判定改为相对「终端平台顶」
        //   PlatTopZ，候选地面比平台顶低 >150cm 即拒(平台外缝/下方地形/坑底)，绝不落空隙。
        //   保留：制作台→同 Hub 终端重定向(z11l)、载具盒 XY 避让(z11g)、8 向兜底、last-resort 不写表。
        const FVector TLoc = RefTarget->GetActorLocation();
        WT_WARNF(TEXT("[teleport-hub] tloc=%s"), *TLoc.ToString());

        // ★z11v3b 地板射线只忽略 目标/玩家（不忽略枢纽结构）—— 候选压在平台上方时射线能命中枢纽
        // 平台顶面(终端站立面)，不再穿透平台打到下方地形(那正是「卡进地底」根因)。
        FCollisionQueryParams GroundTrace(SCENE_QUERY_STAT(WT_HubLanding), /*bTraceComplex=*/false);
        GroundTrace.AddIgnoredActor(Target);
        GroundTrace.AddIgnoredActor(RefTarget);
        GroundTrace.AddIgnoredActor(Character);

        // ★0.2.153i 落点 embed 检验专用查询：忽略玩家/枢纽本体及其挂接(可站立平台)；
        //   制作台重定向时制作台本体(Target≠RefTarget)也不参与校验（我们只关心落终端旁）。
        //   ★z11i 修正：终端本体(RefTarget)【必须】参与校验——否则落点会压在终端身上(卡在终端本体上)。
        //   （终端 case 下 Target==RefTarget，故 Target 也仅在 制作台重定向 时才忽略。）
        FCollisionQueryParams LandOverlapQ(SCENE_QUERY_STAT(WT_HubLandOverlap), /*bTraceComplex=*/false);
        LandOverlapQ.AddIgnoredActor(Character);
        if (Target && Target != RefTarget) { LandOverlapQ.AddIgnoredActor(Target); }
        if (Hub)
        {
            LandOverlapQ.AddIgnoredActor(Hub);
            TArray<AActor*> HubAttached;
            Hub->GetAttachedActors(HubAttached, /*bResetArray=*/true, /*bRecursivelyIncludeAttachedActors=*/true);
            for (AActor* A : HubAttached)
            {
                if (A) { LandOverlapQ.AddIgnoredActor(A); }
            }
        }

        // ★本轮：收集落点需避让的载具/飞行器包围盒（无碰撞体，必须按 XY 包围盒避开）
        const TArray<FBox> HubVehicleBoxes = WT_CollectVehicleBoxes(
            World, RefTarget, Hub, RefTarget->GetActorLocation(), 1500.f);

        // ★z11v3c 落点方向：枢纽中心→终端「径向外」= 远离枢纽本体(储物盒/建筑都在枢纽侧)，
        // 自然落在终端正前方的空地。★z11v3b 翻车根因：bbox 中心常因终端/储物盒同侧而与终端距离 <200 退化，
        // 回退到 terminal-fwd-flipped，而该翻转方向对实测枢纽恰好指向储物盒一侧 → 落储物盒上。
        // 故改用「Hub Actor 原点」(枢纽 pivot，通常离终端数米) 作径向基准(退化阈值放宽 250)；
        // 仍退化才用 bbox 中心；再退化才用终端 forward(★不翻转——实测 forward 指向远离枢纽的开阔面)。
        const FBox HubBox = Hub ? Hub->GetComponentsBoundingBox() : FBox(EForceInit::ForceInitToZero);
        const bool bHaveHubBox = (Hub != nullptr) && (HubBox.IsValid != 0);
        // ★1.0.20 落点方向重写（用户铁律：不要落终端右侧，要「面向终端」）：
        //   ★z11f 铁律——终端 GetActorForwardVector() 不可靠（指向长轴侧面/端面，正是"落右侧"根因），
        //   改用几何：取终端本地包围盒最短水平轴作「脸法线」（薄面=玩家站立的"前面"），
        //   符号取「远离枢纽中心」那侧（朝开放平台/开阔面，避开背面房间）。玩家落此侧、正对终端。
        const FBox RefBox = RefTarget ? RefTarget->GetComponentsBoundingBox() : FBox(EForceInit::ForceInitToZero);
        FVector FaceN = FVector::ZeroVector;
        if (RefBox.IsValid != 0)
        {
            const FTransform InvT = RefTarget->GetActorTransform().Inverse();
            const FBox LocalBox = RefBox.TransformBy(InvT);
            const FVector LE = LocalBox.GetExtent();
            const FVector LocalAxis = (LE.X <= LE.Y) ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f);
            FaceN = RefTarget->GetActorTransform().TransformVector(LocalAxis);
            FaceN.Z = 0.f;
        }
        if (FaceN.SizeSquared() < KINDA_SMALL_NUMBER)
        {
            FaceN = RefTarget ? RefTarget->GetActorForwardVector() : FVector::ZeroVector;
            FaceN.Z = 0.f;
        }
        if (FaceN.SizeSquared() < KINDA_SMALL_NUMBER) { FaceN = ToPlayer; }
        FaceN.Normalize();
        // 符号：远离枢纽中心（朝开放平台/开阔面，避开背面房间）
        FVector HubCenter = Hub ? Hub->GetActorLocation() : (bHaveHubBox ? HubBox.GetCenter() : TLoc);
        FVector ToHub = HubCenter - TLoc; ToHub.Z = 0.f;
        if (ToHub.SizeSquared() > KINDA_SMALL_NUMBER)
        {
            ToHub.Normalize();
            if (FVector::DotProduct(FaceN, ToHub) > 0.f) { FaceN = -FaceN; }
        }
        const FVector HubFinalDir = FaceN;                 // 面向终端（正前方面，开放侧）
        const TCHAR* DirSrc = TEXT("face-terminal");       // 首选方向标签（供落点 tag）

        const FString HubKey = WT_HubSpotKey(Target);
        const FVector HubAnchor = TLoc;
        const UCapsuleComponent* HubCap = Character->GetCapsuleComponent();
        const float HubCapH = HubCap ? HubCap->GetScaledCapsuleHalfHeight() : 90.f;
        const float HubCapR = HubCap ? HubCap->GetScaledCapsuleRadius() : 60.f;
        const FCollisionShape HubTestShape = FCollisionShape::MakeCapsule(
            FMath::Max(HubCapR * 0.65f, 28.f), FMath::Max(HubCapH - 2.f, 10.f));

        // ★z11v3b 地板取「候选点之下的最高命中」：LineTraceMulti 取全部命中，只取
        // ImpactPoint.Z <= At.Z+200 的最高者(=平台顶)，过滤头顶挑檐/平台底；跳过音量/触发体积。
        auto WT_HubTraceFloorZ = [&](const FVector& At, float& OutZ, float DownRange) -> bool
        {
            TArray<FHitResult> Hits;
            if (World->LineTraceMultiByChannel(Hits,
                    FVector(At.X, At.Y, At.Z + 600.f),
                    FVector(At.X, At.Y, At.Z - DownRange),
                    ECC_Pawn, GroundTrace))
            {
                float Best = -1e9f;
                for (const FHitResult& H : Hits)
                {
                    if (Cast<AVolume>(H.GetActor())) { continue; }
                    const float Z = H.ImpactPoint.Z;
                    if (Z <= At.Z + 200.f && Z > Best) { Best = Z; }
                }
                if (Best > -1e8f) { OutZ = Best; return true; }
            }
            return false;
        };

        // ★0.2.153z11g2（枢纽落点落在储物盒顶修复）：落点地板若比四周(±70cm)地板高出
        // >100cm，说明落在储物盒/家具顶（箱顶比平台高 ~169cm，射线取"候选下方最高命中"会误判
        // 箱顶为地面），拒此候选，逼到平台侧空位。只比"有地板的邻点"，邻点是虚空(无地板)不判凸起。
        auto WT_HubIsBumpTop = [&](const FVector& At, float LZ) -> bool
        {
            static const float Off = 70.f;
            static const float BumpThresh = 100.f;
            const FVector Offs[4] = {
                FVector(Off, 0.f, 0.f), FVector(-Off, 0.f, 0.f),
                FVector(0.f, Off, 0.f), FVector(0.f, -Off, 0.f) };
            for (const FVector& O : Offs)
            {
                float NLZ = 0.f;
                if (WT_HubTraceFloorZ(At + O, NLZ, 1500.f) && (LZ - NLZ) > BumpThresh) { return true; }
            }
            return false;
        };

        // ★z11v3c 平台基准：终端正下方的最高地面(平台顶面)。落点必须落在这同一平台上——
        // 任何比平台顶低 >150cm 的候选 = 平台外的缝/下方地形/坑底 ⇒ 拒(绝不落空隙/卡进地底)。
        float PlatTopZ = TLoc.Z;
        {
            TArray<FHitResult> PH;
            if (World->LineTraceMultiByChannel(PH,
                    FVector(TLoc.X, TLoc.Y, TLoc.Z + 600.f),
                    FVector(TLoc.X, TLoc.Y, TLoc.Z - 1500.f),
                    ECC_Pawn, GroundTrace))
            {
                float Best = -1e9f;
                for (const FHitResult& H : PH)
                {
                    if (Cast<AVolume>(H.GetActor())) { continue; }
                    const float Z = H.ImpactPoint.Z;
                    if (Z <= TLoc.Z + 200.f && Z > Best) { Best = Z; }
                }
                if (Best > -1e8f) { PlatTopZ = Best; }
            }
        }

        // ★z11v3d（PEP/天空岛小平台修复）：终端「正前方」推出去常悬空(no-floor ⇒ 原逻辑直接拒绝传送)。
        //   改：正前方 大→小距离优先(保持主世界落点距离感)，前方全灭再 8 向小半径环搜「终端所在平台」
        //   上的点——落到平台上即接受(仍面对终端)，不再因小平台拒绝。落差哨兵加严上界(防砸头顶结构)。
        struct FHubCand { FVector Dir; float Radius; bool bFront; };
        TArray<FHubCand> HubCands;
        static const float HubFrontR[5] = { 300.f, 240.f, 180.f, 120.f, 60.f };
        for (float R : HubFrontR) { HubCands.Add({ HubFinalDir, R, true }); }
        static const float HubSideR[3] = { 130.f, 90.f, 60.f };
        for (int32 HA = 1; HA < 8; ++HA)  // 0°=正前(已列)，45°起环搜平台延伸侧
        {
            const FVector HD = HubFinalDir.RotateAngleAxis(static_cast<float>(HA) * 45.f, FVector::UpVector);
            for (float R : HubSideR) { HubCands.Add({ HD, R, false }); }
        }
        bool bHubPlaced = false;
        FVector HubDest = FVector::ZeroVector;
        FString HubCandTag = DirSrc;
        for (const FHubCand& C : HubCands)
        {
            if (bHubPlaced) { break; }
            const FVector WorldLand = HubAnchor + C.Dir * C.Radius;
            // ★本轮：无碰撞体载具/飞行器按 XY 包围盒避让（必查，与碰撞/类别无关）
            if (WT_IsInBoxXY(WorldLand, HubVehicleBoxes, HubCapR)) { continue; }
            // ★1.0.20：落点方向不得指向枢纽/房间（背面）—否则落枢纽顶或传进房间
            if (ToHub.SizeSquared() > KINDA_SMALL_NUMBER && FVector::DotProduct(C.Dir, ToHub) > 0.3f) { continue; }
            float LZ = 0.f;
            if (!WT_HubTraceFloorZ(WorldLand, LZ, 1500.f))
            {
                UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
                FNavLocation NavLoc;
                if (NavSys && NavSys->ProjectPointToNavigation(
                        FVector(WorldLand.X, WorldLand.Y, WorldLand.Z + 88.f),
                        NavLoc, FVector(200.f, 200.f, 400.f)))
                {
                    LZ = NavLoc.Location.Z;
                }
                else
                {
                    WT_WARNF(TEXT("[teleport-hub] no-floor at world=%s r=%.0f - skip"),
                             *WorldLand.ToString(), C.Radius);
                    continue;
                }
            }
            // ★z11v3g2 凸起检测：落在储物盒顶则拒，逼到平台侧
            if (WT_HubIsBumpTop(WorldLand, LZ))
            {
                WT_WARNF(TEXT("[teleport-hub] bump-top at world=%s r=%.0f - skip"),
                         *WorldLand.ToString(), C.Radius);
                continue;
            }
            // ★z11v3d 同平台哨兵：候选地面必须 ≈ 终端平台顶(PlatTopZ)，落差上/下界都卡(防落缝/坑底/砸头顶)
            if (LZ < PlatTopZ - 150.f || LZ > PlatTopZ + 200.f)
            {
                WT_WARNF(TEXT("[teleport-hub] gap at world=%s floorZ=%.0f platTop=%.0f - skip"),
                         *WorldLand.ToString(), LZ, PlatTopZ);
                continue;
            }
            const FVector TryDest = FVector(WorldLand.X, WorldLand.Y, LZ + HubCapH + 20.f);
            if (World->OverlapBlockingTestByChannel(TryDest, FQuat::Identity, ECC_Pawn,
                    HubTestShape, LandOverlapQ))
            {
                TArray<FOverlapResult> Hb;
                FString Hbn;
                if (World->OverlapMultiByChannel(Hb, TryDest, FQuat::Identity, ECC_Pawn,
                        HubTestShape, LandOverlapQ))
                {
                    for (const FOverlapResult& OV : Hb)
                    {
                        if (AActor* BA = OV.GetActor()) { Hbn += BA->GetName() + TEXT(" "); }
                        else if (UPrimitiveComponent* BC = OV.GetComponent()) { Hbn += BC->GetName() + TEXT(" "); }
                    }
                }
                WT_WARNF(TEXT("[teleport-hub] overlap at world=%s r=%.0f by=[%s] - try other"),
                         *WorldLand.ToString(), C.Radius, *Hbn);
                continue;
            }
            if (!Character->SetActorLocation(TryDest, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics))
            {
                WT_WARNF(TEXT("[teleport-hub] move-reject at %s r=%.0f"), *TryDest.ToString(), C.Radius);
                continue;
            }
            bHubPlaced = true;
            HubDest = TryDest;
            HubCandTag = C.bFront ? TEXT("front") : TEXT("platform");
        }
        // ★z11i（治"卡在终端本体上"）：之前 footprint-below 落点 = 终端原点(HubAnchor) X/Y，等于直接落在
        //   终端脚下/体内；且落点 overlap 校验忽略终端(RefTarget) ⇒ 压在终端上也不拒。
        //   修正：① 删 footprint-below 中心分支；② 落点 X/Y 必须与终端保持安全距离
        //      ClearR = 终端水平半幅 + 玩家胶囊半径 + 余量，绝不再落终端原点；
        //      （终端本体已重新纳入 LandOverlapQ，任何压在终端上的候选都会被拒，逼到 ClearR 外的空侧）；
        //   ③ 多通道竖直射线(Pawn→WorldStatic→Visibility)兜底抓薄基座/装饰面。
        if (!bHubPlaced)
        {
            // 终端水平半幅：保证落点离开终端本体（不卡在终端上）
            const FBox TermBox = RefTarget ? RefTarget->GetComponentsBoundingBox() : FBox(EForceInit::ForceInitToZero);
            const float TermHalf = (TermBox.IsValid != 0)
                ? FMath::Max(TermBox.GetExtent().X, TermBox.GetExtent().Y)
                : 60.f;
            const float ClearR = TermHalf + HubCapR + 30.f;   // 离开终端本体的最小安全距离

            // 多通道竖直射线：ECC_Pawn 漏检的薄平台/装饰基座面，用 WorldStatic/Visibility 兜底
            auto WT_HubFloorAny = [&](const FVector& At, float& OutZ, float DownRange) -> bool
            {
                static const ECollisionChannel Chans[3] = { ECC_Pawn, ECC_WorldStatic, ECC_Visibility };
                for (ECollisionChannel Ch : Chans)
                {
                    TArray<FHitResult> Hits;
                    if (World->LineTraceMultiByChannel(Hits,
                            FVector(At.X, At.Y, At.Z + 600.f),
                            FVector(At.X, At.Y, At.Z - DownRange), Ch, GroundTrace))
                    {
                        float Best = -1e9f;
                        for (const FHitResult& H : Hits)
                        {
                            if (Cast<AVolume>(H.GetActor())) { continue; }
                            const float Z = H.ImpactPoint.Z;
                            if (Z <= At.Z + 200.f && Z > Best) { Best = Z; }
                        }
                        if (Best > -1e8f) { OutZ = Best; return true; }
                    }
                }
                return false;
            };
            // ★1.0.20：落点方向改为「面向终端」(HubFinalDir=几何脸法线)，以下落点均围绕正前方
            TArray<FVector> FootDirs; FootDirs.Add(HubFinalDir);
            // 安全距离档：刚离开终端 → 稍远，优先近
            static const float FootR[3] = { ClearR, ClearR + 60.f, ClearR + 120.f };
            for (const FVector& FD2 : FootDirs)
            {
                if (bHubPlaced) { break; }
                for (float R : FootR)
                {
                    const FVector WorldLand = FVector(
                        HubAnchor.X + FD2.X * R, HubAnchor.Y + FD2.Y * R, PlatTopZ);
                    if (WT_IsInBoxXY(WorldLand, HubVehicleBoxes, HubCapR)) { continue; }
                    if (ToHub.SizeSquared() > KINDA_SMALL_NUMBER && FVector::DotProduct(FD2, ToHub) > 0.3f) { continue; }
                    float LZ = 0.f;
                    if (!WT_HubFloorAny(WorldLand, LZ, 1500.f)) { continue; }
                    if (WT_HubIsBumpTop(WorldLand, LZ)) { continue; }
                    if (LZ < PlatTopZ - 200.f || LZ > PlatTopZ + 200.f) { continue; }
                    const FVector TryDest = FVector(WorldLand.X, WorldLand.Y, LZ + HubCapH + 20.f);
                    if (World->OverlapBlockingTestByChannel(TryDest, FQuat::Identity, ECC_Pawn,
                            HubTestShape, LandOverlapQ)) { continue; }
                    if (!Character->SetActorLocation(TryDest, /*bSweep=*/false, nullptr,
                            ETeleportType::TeleportPhysics)) { continue; }
                    bHubPlaced = true;
                    HubDest = TryDest;
                    HubCandTag = TEXT("footprint");
                    WT_WARNF(TEXT("[teleport-hub] footprint placed at world=%s r=%.0f dir=%s floorZ=%.0f clearR=%.0f"),
                             *WorldLand.ToString(), R, *FD2.ToString(), LZ, ClearR);
                    break;
                }
            }
            // ★1.0.18 实测回调（用户实测 1.0.17"终端远了1米、制作台近了1米"）：
            //   ① 距离：BaseR = ClearR + 100（约离终端 2.36m；1.0.17 用 ClearR+200 实测远1米，回调）；
            //   ② 抬高：HubLift = 200cm 保留（高度实测已对，不动）；
            //   ③ 诊断输出 baseR / lift，供下一轮微调。
            // ★1.0.20：绝对兜底方向统一为「面向终端」(HubFinalDir=几何脸法线)，与上方一致；
            //   高度保留 HubLift=200（同高度存档实测正确，保留）；不同高度(空隙)走下方 diff-height 独立算法。
            if (!bHubPlaced)
            {
                const float HubLift = 200.f;            // 终端架高补偿（同高度存档实测正确，保留）
                const float BaseR = ClearR + 100.f;     // 离终端约 2.36m
                TArray<FVector> BaseDirs; BaseDirs.Add(HubFinalDir);
                for (const FVector& D : BaseDirs)
                {
                    if (bHubPlaced) { break; }
                    const FVector BaseLand = FVector(
                        HubAnchor.X + D.X * BaseR,
                        HubAnchor.Y + D.Y * BaseR, PlatTopZ);
                    if (WT_IsInBoxXY(BaseLand, HubVehicleBoxes, HubCapR)) { continue; }
                    if (ToHub.SizeSquared() > KINDA_SMALL_NUMBER && FVector::DotProduct(D, ToHub) > 0.3f) { continue; }
                    float LZ = PlatTopZ;
                    if (!WT_HubFloorAny(BaseLand, LZ, 1500.f)) { LZ = PlatTopZ; }
                    if (WT_HubIsBumpTop(BaseLand, LZ)) { continue; }
                    if (LZ < PlatTopZ - 250.f || LZ > PlatTopZ + 250.f) { continue; }
                    const bool bOutTerm = !WT_IsInBoxXY(BaseLand, TArray<FBox>{ TermBox }, HubCapR);
                    const FVector TryDest = FVector(BaseLand.X, BaseLand.Y, LZ + HubCapH + 20.f + HubLift);
                    if (World->OverlapBlockingTestByChannel(TryDest, FQuat::Identity, ECC_Pawn,
                            HubTestShape, LandOverlapQ)) { continue; }
                    if (Character->SetActorLocation(TryDest, /*bSweep=*/false, nullptr,
                            ETeleportType::TeleportPhysics))
                    {
                        bHubPlaced = true;
                        HubDest = TryDest;
                        HubCandTag = TEXT("footprint-base");
                        WT_WARNF(TEXT("[teleport-hub] footprint-base placed at world=%s floorZ=%.0f clearR=%.0f baseR=%.0f lift=%.0f outTerm=%d dir=%s"),
                                 *BaseLand.ToString(), LZ, ClearR, BaseR, HubLift, (int)bOutTerm, *D.ToString());
                    }
                }
            }
        }
        // ★1.0.20 不同高度独立算法（用户铁律：不同高度存档终端立在高台、前方落点悬在平台外空隙上方，
        //   前/侧/footprint/base 分支因落差超阈值全 -skip，若走下方 ground-fallback 会用空隙底地板 ⇒ 坠入下方空隙）。
        //   ★本算法独立于「同高度」逻辑（不改动它）：直接在「终端平台顶 PlatTopZ」找安全 XY 落点——
        //   站立高度 = PlatTopZ + 胶囊半高 + 余量（平台即真实地面，绝不叠 HubLift、绝不探空隙底），
        //   方向用「面向终端」(HubFinalDir，与上面一致)，8 向兜底找平台上的空位；落点即站实于平台顶，不再坠空隙。
        if (!bHubPlaced)
        {
            const FBox TermBoxDH = RefTarget ? RefTarget->GetComponentsBoundingBox() : FBox(EForceInit::ForceInitToZero);
            const float TermHalfDH = (TermBoxDH.IsValid != 0)
                ? FMath::Max(TermBoxDH.GetExtent().X, TermBoxDH.GetExtent().Y) : 60.f;
            const float ClearRDH = TermHalfDH + HubCapR + 30.f;
            // 仅用「远离枢纽/房间」的开放半圆方向（0/±45/±90°），绝不回指枢纽或房间(180°等)
            // ⇒ 不会落枢纽顶、不会传进房间；飞行器避让由 WT_IsInBoxXY(HubVehicleBoxes) 负责。
            TArray<FVector> DHDirs; DHDirs.Add(HubFinalDir);
            for (int32 HA : { 1, 2, -1, -2 })
            {
                DHDirs.Add(HubFinalDir.RotateAngleAxis(static_cast<float>(HA) * 45.f, FVector::UpVector));
            }
            const float DHR[4] = { ClearRDH, ClearRDH + 60.f, ClearRDH + 120.f, ClearRDH + 200.f };
            const float DHStand = HubCapH + 20.f;   // 平台顶即地面，无额外抬高
            for (const FVector& DD : DHDirs)
            {
                if (bHubPlaced) { break; }
                for (float R : DHR)
                {
                    const FVector Land = FVector(
                        HubAnchor.X + DD.X * R, HubAnchor.Y + DD.Y * R, PlatTopZ);
                    if (WT_IsInBoxXY(Land, HubVehicleBoxes, HubCapR)) { continue; }
                    if (ToHub.SizeSquared() > KINDA_SMALL_NUMBER && FVector::DotProduct(DD, ToHub) > 0.3f) { continue; }
                    // 不同高度：直接用平台顶，不探空隙底；仅排凸起/重叠/终端本体
                    if (WT_HubIsBumpTop(Land, PlatTopZ)) { continue; }
                    const bool bOutTerm = !WT_IsInBoxXY(Land, TArray<FBox>{ TermBoxDH }, HubCapR);
                    const FVector TryDest = FVector(Land.X, Land.Y, PlatTopZ + DHStand);
                    if (World->OverlapBlockingTestByChannel(TryDest, FQuat::Identity, ECC_Pawn,
                            HubTestShape, LandOverlapQ)) { continue; }
                    if (Character->SetActorLocation(TryDest, /*bSweep=*/false, nullptr,
                            ETeleportType::TeleportPhysics))
                    {
                        bHubPlaced = true;
                        HubDest = TryDest;
                        HubCandTag = TEXT("diff-height");
                        WT_WARNF(TEXT("[teleport-hub] diff-height placed at world=%s floorZ=%.0f clearR=%.0f baseR=%.0f outTerm=%d dir=%s"),
                                 *Land.ToString(), PlatTopZ, ClearRDH, R, (int)bOutTerm, *DD.ToString());
                        break;
                    }
                }
            }
        }
        // ★z11v3f（落点偏到储物盒旁修复）：地面兜底改为「以终端为心、前向优先的 8 向环搜」，
        //   半径 150→500cm 由近及远，优先终端前向(正前方)的最近可站平台点；被储物盒/结构挡则顺
        //   8 向外扩——落点贴终端、正前方，不再单向 600cm 甩到储物盒侧。仍只防比终端高 600cm(砸头顶)。
        if (!bHubPlaced)
        {
            TArray<FVector> FallbackDirs;
            FallbackDirs.Add(HubFinalDir);
            for (int32 HA = 1; HA < 8; ++HA)
            {
                FallbackDirs.Add(HubFinalDir.RotateAngleAxis(static_cast<float>(HA) * 45.f, FVector::UpVector));
            }
            static const float HubGroundR[8] = { 150.f, 200.f, 250.f, 300.f, 350.f, 400.f, 450.f, 500.f };
            for (float R : HubGroundR)
            {
                if (bHubPlaced) { break; }
                for (const FVector& FD : FallbackDirs)
                {
                    const FVector WorldLand = HubAnchor + FD * R;
                    if (WT_IsInBoxXY(WorldLand, HubVehicleBoxes, HubCapR)) { continue; }
                    // ★1.0.20：最后兜底也不得指向枢纽/房间（背面），避免落枢纽顶/传进房间
                    if (ToHub.SizeSquared() > KINDA_SMALL_NUMBER && FVector::DotProduct(FD, ToHub) > 0.3f) { continue; }
                    float LZ = 0.f;
                    if (!WT_HubTraceFloorZ(WorldLand, LZ, 12000.f))
                    {
                        UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
                        FNavLocation NavLoc;
                        if (NavSys && NavSys->ProjectPointToNavigation(
                                FVector(WorldLand.X, WorldLand.Y, WorldLand.Z + 88.f),
                                NavLoc, FVector(400.f, 400.f, 800.f)))
                        {
                            LZ = NavLoc.Location.Z;
                        }
                        else
                        {
                            WT_WARNF(TEXT("[teleport-hub] ground-fallback no-floor at world=%s r=%.0f dir=%s - skip"),
                                     *WorldLand.ToString(), R, *FD.ToString());
                            continue;
                        }
                    }
                    // ★z11v3g2 凸起检测：落在储物盒顶则拒，逼到平台侧
                    if (WT_HubIsBumpTop(WorldLand, LZ))
                    {
                        WT_WARNF(TEXT("[teleport-hub] ground-fallback bump-top at world=%s r=%.0f dir=%s - skip"),
                                 *WorldLand.ToString(), R, *FD.ToString());
                        continue;
                    }
                    // 落地兜底只防「比终端还高」（砸头顶/落屋顶）；下界放开（允许落地面/坑底旁）
                    if (LZ > TLoc.Z + 600.f) { continue; }
                    const FVector TryDest = FVector(WorldLand.X, WorldLand.Y, LZ + HubCapH + 20.f);
                    if (World->OverlapBlockingTestByChannel(TryDest, FQuat::Identity, ECC_Pawn,
                            HubTestShape, LandOverlapQ))
                    {
                        TArray<FOverlapResult> Hb;
                        FString Hbn;
                        if (World->OverlapMultiByChannel(Hb, TryDest, FQuat::Identity, ECC_Pawn,
                                HubTestShape, LandOverlapQ))
                        {
                            for (const FOverlapResult& OV : Hb)
                            {
                                if (AActor* BA = OV.GetActor()) { Hbn += BA->GetName() + TEXT(" "); }
                                else if (UPrimitiveComponent* BC = OV.GetComponent()) { Hbn += BC->GetName() + TEXT(" "); }
                            }
                        }
                        WT_WARNF(TEXT("[teleport-hub] ground-fallback overlap at world=%s r=%.0f by=[%s] - skip"),
                                 *WorldLand.ToString(), R, *Hbn);
                        continue;
                    }
                    if (!Character->SetActorLocation(TryDest, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics))
                    {
                        WT_WARNF(TEXT("[teleport-hub] ground-fallback move-reject at %s r=%.0f"), *TryDest.ToString(), R);
                        continue;
                    }
                    bHubPlaced = true;
                    HubDest = TryDest;
                    HubCandTag = TEXT("ground");
                    WT_WARNF(TEXT("[teleport-hub] ground-fallback placed at world=%s r=%.0f dir=%s floorZ=%.0f"),
                             *WorldLand.ToString(), R, *FD.ToString(), LZ);
                    break;
                }
            }
        }
        if (bHubPlaced)
        {
            WT_SaveHubSpot(HubKey, HubDest);
            WT_WARNF(TEXT("[teleport-result] '%s' -> dest=%s (hub directed %s dist=%.0f dirSrc=%s)"),
                     *Target->GetName(), *HubDest.ToString(),
                     (Cast<AFGBuildableHubTerminal>(Target) != nullptr) ? TEXT("terminal") : TEXT("craft"),
                     (HubDest - HubAnchor).Size(), *HubCandTag);
            WT_FaceTarget(Character, Target->GetActorLocation());
            Client_NotifyTeleport(Target, true, WTTeleportFail::None);
            return;
        }
        // ★z11v3c 用户铁律：必须精确落在终端/制作台正前方；前方 3m 内无安全落点 ⇒ 全部拒绝
        // (不落 8 向、不落空隙、不砸终端顶)，玩家原地不动，UI 提示"无安全落点"。
        WT_WARNF(TEXT("[teleport-hub] no safe front landing near hub '%s' (dirSrc=%s) - teleport refused"),
                 *RefTarget->GetName(), DirSrc);
        Client_NotifyTeleport(Target, false, WTTeleportFail::NoSafeLanding);
        return;
    }
    else if (bIsPickup)
    {
        const float ItemTopZ = bHaveBox ? Box.Max.Z : Center.Z;
        if (!WT_FindCollectibleLandingSpot(World, Character, Center, ItemTopZ, /*bHasActor=*/true, Destination))
        {
            WT_WARNF(TEXT("Teleport refused: no safe landing near pickup '%s' at %s"),
                     *Target->GetName(), *Center.ToString());
            Client_NotifyTeleport(Target, false, WTTeleportFail::NoSafeLanding);
            return;
        }
    }
    else
    {
        // ★0.2.153q 建筑/机器/载具统一走枢纽那套落点搜索（用户定案「所有建筑传送逻辑也按枢纽
        // 传送走」）。旧路径（WT_FindSafeLandingSpot 环搜 + 搜不到就砸建筑上方 300）与「卡在枢纽
        // 背面的墙里」同源：建筑 pivot 常在结构内部/底座之下，砸正上方 = 把人塞进结构里。
        // 主方向 = 玩家所在侧（ToPlayer），方向扇 8 条全试，近场 ≤3m 优先。
        FCollisionQueryParams BldGround(SCENE_QUERY_STAT(WT_BldLanding), /*bTraceComplex=*/false);
        BldGround.AddIgnoredActor(Target);
        BldGround.AddIgnoredActor(Character);
        {
            TArray<AActor*> Attached;
            Target->GetAttachedActors(Attached, /*bResetArray=*/true,
                                      /*bRecursivelyIncludeAttachedActors=*/true);
            for (AActor* A : Attached)
            {
                if (A)
                {
                    BldGround.AddIgnoredActor(A);
                }
            }
        }
        // 查卡不卡用另一套查询：只忽略玩家与目标本体，保留其它一切真实碰撞体。
        FCollisionQueryParams BldOverlap(SCENE_QUERY_STAT(WT_BldLandOverlap), /*bTraceComplex=*/false);
        BldOverlap.AddIgnoredActor(Target);
        BldOverlap.AddIgnoredActor(Character);

        const FString BldKey = FString::Printf(TEXT("Bld|%s"), *Target->GetPathName());
        const FWT_LandSpot Spot = WT_SearchLandingSpot(World, Character, Target, ToPlayer,
                                                       TEXT("to-player"), BldGround, BldOverlap,
                                                       BldKey, TEXT("teleport-bld"), 0.f, 0.f,
                                                       nullptr, nullptr);
        if (Spot.bFound)
        {
            WT_FaceTarget(Character, Target->GetActorLocation());  // 落地转身面向建筑
            if (Spot.bFromCache)
            {
                WT_ScheduleTeleportGroundFix(World, Character, BldKey, Target);
            }
            WT_WARNF(TEXT("[teleport-result] '%s' -> dest=%s (bld landing path)"),
                     *Target->GetName(), *Spot.Dest.ToString());
            Client_NotifyTeleport(Target, true, WTTeleportFail::None);
            return;
        }
        // 最后手段：建筑正上方 300 砸落 + ground-fix 贴地（与枢纽兜底同策略）
        Destination = Center + FVector(0.f, 0.f, 300.f);
        WT_WARNF(TEXT("[teleport-bld] last-resort drop onto building at %s"), *Destination.ToString());
        WT_ScheduleTeleportGroundFix(World, Character, FString(), Target);
    }

    const bool bMoved = Character->SetActorLocation(Destination, /*bSweep=*/false, nullptr,
                                                    ETeleportType::TeleportPhysics);
    // ★0.2.153o 通用路径也转身面向目标（机器/收集品/枢纽兜底都走这里）：落点算法只管脚下与卡不卡，
    // 朝向沿用传送前旧 yaw 会让玩家落地后对着空气。只改水平 yaw，不动 pitch/roll。
    if (bMoved)
    {
        WT_FaceTarget(Character, Target->GetActorLocation());
    }

    // ★0.2.149 诊断（强制 Warning）：所有传送的最终落点与是否真的移动，便于对比理论锚点定位偏差。
    WT_WARNF(TEXT("[teleport-result] '%s' -> dest=%s bMoved=%s"),
             *Target->GetName(), *Destination.ToString(), bMoved ? TEXT("true") : TEXT("false"));

    WT_LOGF(TEXT("Teleport: %s -> '%s' at %s (ok=%s)"),
            *Character->GetName(), *Target->GetName(), *Destination.ToString(),
            bMoved ? TEXT("true") : TEXT("false"));

    Client_NotifyTeleport(Target, bMoved, bMoved ? WTTeleportFail::None : WTTeleportFail::TargetGone);
}

void URemote_FactoryRCO::Client_NotifyTeleport_Implementation(AActor* Target, bool bSuccess, int32 Reason)
{
    OnTeleportResult.Broadcast(Target, bSuccess, Reason);
}

void URemote_FactoryRCO::Server_TeleportToLocation_Implementation(FVector Location)
{
    // 传送到文件坐标：没有 actor 可引用（对应单元格未流送进内存），只有 collectibles.json 里的坐标。
    WT_ZONE_COARSE(TEXT("RCO.TeleportToLocation"));

    if (!WT_HasAuthority(this))
    {
        WT_WARNF(TEXT("Server_TeleportToLocation called without authority — ignored."));
        return;
    }

    AFGCharacterPlayer* Character = GetOwnerPlayerCharacter();
    if (!Character)
    {
        Client_NotifyTeleport(nullptr, false, WTTeleportFail::TargetGone);
        return;
    }

    UWorld* World = GetWorld();

    // ★0.2.153f 收集品坐标直传三档落点（与 Server_TeleportToCoord 同构；日志实锤：旧版只做
    // 2m/4m 环搜、搜不到直接拒——远处收集品 actor 未复制到客户端全走这里，面板内收集品
    // 超 relevancy 距离全军覆没 "TeleportToLocation refused: no safe landing"）。
    // 废除 0.2.135 的「搜不到即拒」（那是为「玩家没进洞硬落=掉虚空」设计的）；现在砸落由
    // ground-fix 贴地兜住，与机器坐标直传同策略：
    //   ① 查表（RF_teleport_spots.json，键前缀 Col| 区分收集品）→ sweep=false 直传 + ground-fix；
    //   ② 环搜（区域已加载时走这条）→ 成功即写表；
    //   ③ 兜底：坐标上方 3m 砸落（sweep=false）+ ground-fix 补写真落点。
    const FString Key = FString::Printf(TEXT("Col|%d,%d,%d"),
        FMath::RoundToInt(Location.X), FMath::RoundToInt(Location.Y), FMath::RoundToInt(Location.Z));

    // ① 查表直传：手里有坐标就不需要那片区域已加载
    const TMap<FString, FVector> Spots = WT_LoadHubSpots();
    if (const FVector* Cached = Spots.Find(Key))
    {
        const bool bCacheMoved = Character->SetActorLocation(*Cached, /*bSweep=*/false, nullptr,
                                                             ETeleportType::TeleportPhysics);
        WT_WARNF(TEXT("[teleport-col] cache-landed key='%s' dest=%s ok=%s"),
                 *Key, *Cached->ToString(), bCacheMoved ? TEXT("true") : TEXT("false"));
        if (bCacheMoved)
        {
            WT_ScheduleTeleportGroundFix(World, Character, Key);
            Client_NotifyTeleport(nullptr, true, WTTeleportFail::None);
            return;
        }
        WT_WARNF(TEXT("[teleport-col] cache-move-failed key='%s' — re-resolving"), *Key);
    }

    // ② 环搜（沿用 0.2.135 的收集品专用落点：坐标点 2m 外、洞穴内强制站物品上方）
    FVector Destination;
    if (WT_FindCollectibleLandingSpot(World, Character, Location, Location.Z,
                                      /*bHasActor=*/false, Destination))
    {
        Character->SetActorLocation(Destination, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
        WT_SaveHubSpot(Key, Destination); // 近距首传即入表
        WT_WARNF(TEXT("[teleport-col] resolved dest=%s spot-saved key='%s'"),
                 *Destination.ToString(), *Key);
        Client_NotifyTeleport(nullptr, true, WTTeleportFail::None);
        return;
    }

    // ③ 兜底：收集品坐标上方 3m 砸落（sweep=false：sweep 查的是「当前位置→落点」的路径，
    // 远距必被沿途地形误挡；终点合法性交给落地后 ground-fix），fix 成功补写落点表。
    Destination = Location + FVector(0.f, 0.f, 300.f);
    const bool bMoved = Character->SetActorLocation(Destination, /*bSweep=*/false, nullptr,
                                                    ETeleportType::TeleportPhysics);
    WT_WARNF(TEXT("[teleport-col] fallback-drop dest=%s bMoved=%s key='%s'"),
             *Destination.ToString(), bMoved ? TEXT("true") : TEXT("false"), *Key);
    if (bMoved)
    {
        WT_ScheduleTeleportGroundFix(World, Character, Key);
    }
    Client_NotifyTeleport(nullptr, bMoved, bMoved ? WTTeleportFail::None : WTTeleportFail::TargetGone);
}

/**
 * ★0.2.153e 坐标直传（第十轮定案，1.0.0 前完工）：不依赖 actor 引用的机器传送。
 * 客户端未复制远端机器（网络 relevancy）时 Entry->Target 为 null，Server_TeleportTo 发不出来；
 * 本 RPC 只收坐标+半宽（扫描时随 FWT_MachineEntry 复制，坐标永远有）。
 * 落点策略（与 hub 落点表同一张 RF_teleport_spots.json，键前缀 Coord| 区分）：
 *   ① 查表命中 → sweep=false 直传 + 0.6s ground-fix（fix 后写回刷新表）；
 *   ② 未命中且区域已加载 → 环搜落点，成功即写表（近距首传即入表）；
 *   ③ 环搜也失败（远端未流送）→ 兜底 Z300 砸落 + ground-fix，fix 成功把真落点补写进表
 *      （大肥鱼：「存档留个字段，归档进来是空表，首次到那再补写」）。
 * ★0.2.153f 全部长距瞬移 sweep=false（日志实锤）：sweep 查的是「当前位置→落点」路径而非终点，
 * 站高处时扫掠线穿山被地形挡 → 表里存好的落点也 cache-sweep-blocked 拒传。ground-fix 已提取为
 * 共享助手 WT_ScheduleTeleportGroundFix（与收集品坐标直传共用）。
 */
// ★2026-11-02 远距传送卡顿缓解（World Partition 流式预加载）：
// 远距瞬移把玩家落到未加载单元格，引擎阻塞式同步流式加载 ⇒ 肉眼可见冻结（>500m 越远越久）。
// 真正移动前，于目的地注入一个临时流式源并触发一次流式状态更新，再在有限预算内轮询
// IsStreamingCompleted，让后台异步加载先把目的地格子加载好；随后 SetActorLocation 落点几乎不再阻塞。
// 预算用尽仍未加载完则直接放行（不会比现在更差）。仅对 >300m 的远距生效（短距无卡顿，省开销）。
static void WT_PreStreamDestination(UWorld* World, const FVector& Dest)
{
    if (!World)
    {
        return;
    }
    UWorldPartitionSubsystem* WPS = World->GetSubsystem<UWorldPartitionSubsystem>();
    if (!WPS)
    {
        return;
    }
    // 距离门槛：与玩家当前位置 < 300m 不预流式（短距无卡顿，省开销）
    if (APlayerController* PC = World->GetFirstPlayerController())
    {
        if (AActor* Pawn = PC->GetPawn())
        {
            if ((Pawn->GetActorLocation() - Dest).Size() < 30000.f)
            {
                return;
            }
        }
    }

    FActorSpawnParameters SP;
    SP.bNoFail = true;
    SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* Temp = World->SpawnActor<AActor>(AActor::StaticClass(), Dest, FRotator::ZeroRotator, SP);
    if (!Temp)
    {
        return;
    }
    USceneComponent* Root = NewObject<USceneComponent>(Temp, TEXT("TempTeleportStreamRoot"));
    Root->SetWorldLocation(Dest);
    Temp->SetRootComponent(Root);
    Root->RegisterComponent();

    UWorldPartitionStreamingSourceComponent* Src = NewObject<UWorldPartitionStreamingSourceComponent>(Temp, TEXT("TempTeleportStreamSrc"));
    if (!Src)
    {
        Temp->Destroy();
        return;
    }
    // TargetState 是私有字段（默认即 Activated），无需也不允许直接赋值；Enable 后 GetStreamingSource 取 Activated
    Src->RegisterComponent();
    Src->EnableStreamingSource();
    WPS->RegisterStreamingSourceProvider(Src);  // 显式注册，确保被收集
    WPS->OnUpdateStreamingState();               // 强制重算流式状态，开始加载目的地格子

    FWorldPartitionStreamingQuerySource Query(Dest);
    Query.bUseGridLoadingRange = true;
    TArray<FWorldPartitionStreamingQuerySource> Queries;
    Queries.Add(Query);

    bool bPreloaded = false;
    for (int32 i = 0; i < 20; ++i)  // 最多 ~1.0s（20 × 0.05s），背景线程仍会加载
    {
        if (WPS->IsStreamingCompleted(EWorldPartitionRuntimeCellState::Loaded, Queries, /*bExactState=*/false))
        {
            bPreloaded = true;
            break;
        }
        FPlatformProcess::Sleep(0.05f);
    }

    WT_WARNF(TEXT("[teleport-prestream] dest=%s preloaded=%d"), *Dest.ToString(), bPreloaded ? 1 : 0);

    // 清理临时源（玩家随即落点，自身即为该处流式源，格子不会立刻卸载）
    WPS->UnregisterStreamingSourceProvider(Src);
    Src->DestroyComponent();
    Temp->Destroy();
}

void URemote_FactoryRCO::Server_TeleportToCoord_Implementation(FVector Location, float BaseRadius)
{
    WT_ZONE_COARSE(TEXT("RCO.TeleportToCoord"));

    if (!WT_HasAuthority(this))
    {
        WT_WARNF(TEXT("Server_TeleportToCoord called without authority — ignored."));
        return;
    }

    AFGCharacterPlayer* Character = GetOwnerPlayerCharacter();
    if (!Character)
    {
        Client_NotifyTeleport(nullptr, false, WTTeleportFail::TargetGone);
        return;
    }

    UWorld* World = GetWorld();
    const FVector Center = Location;
    const FString Key = FString::Printf(TEXT("Coord|%d,%d,%d"),
        FMath::RoundToInt(Center.X), FMath::RoundToInt(Center.Y), FMath::RoundToInt(Center.Z));

    // ① 查表直传：坐标在手，不需要那片区域已加载（大肥鱼：手里有坐标就不用等那块加载）
    const TMap<FString, FVector> Spots = WT_LoadHubSpots();
    if (const FVector* Cached = Spots.Find(Key))
    {
        // ★0.2.153f sweep=false：sweep 查路径不查终点，表里验证过的落点也被沿途地形挡掉
        WT_PreStreamDestination(World, *Cached);
        if (Character->SetActorLocation(*Cached, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics))
        {
            WT_WARNF(TEXT("[teleport-coord] cache-landed key='%s' dest=%s"), *Key, *Cached->ToString());
            WT_ScheduleTeleportGroundFix(World, Character, Key);
            Client_NotifyTeleport(nullptr, true, WTTeleportFail::None);
            return;
        }
        WT_WARNF(TEXT("[teleport-coord] cache-move-failed key='%s' at %s — re-resolving"),
                 *Key, *Cached->ToString());
        // 极罕见（sweep=false 基本不会失败）→ 落到下面重新算，算出的新点会覆盖表项
    }

    // ② 环搜（区域已加载时走这条；坐标模式没有机器 forward，用 ToPlayer 为基准：
    //    候选序 -ToPlayer 优先=机器远离玩家侧，玩家落地后面向机器，与普通机器行为同风格）
    FVector ToPlayer = Character->GetActorLocation() - Center;
    ToPlayer.Z = 0.f;
    if (ToPlayer.SizeSquared() < KINDA_SMALL_NUMBER)
    {
        ToPlayer = FVector(0.f, 1.f, 0.f);
    }
    ToPlayer.Normalize();

    FVector Destination;
    if (WT_FindSafeLandingSpot(World, Character, Center, FMath::Max(BaseRadius, 0.f),
                               ToPlayer, ToPlayer, Destination))
    {
        WT_PreStreamDestination(World, Destination);
        Character->SetActorLocation(Destination, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
        WT_SaveHubSpot(Key, Destination); // 近距首传即入表
        WT_WARNF(TEXT("[teleport-coord] resolved dest=%s spot-saved key='%s'"),
                 *Destination.ToString(), *Key);
        Client_NotifyTeleport(nullptr, true, WTTeleportFail::None);
        return;
    }

    // ③ 兜底：机器坐标上方 3m 砸落（★0.2.153f sweep=false：sweep=true 时远距扫掠线被沿途
    // 地形挡 → bMoved=false 拒传，正是「超距传不过去」的真凶之一），落地 ground-fix 后补写真落点
    Destination = Center + FVector(0.f, 0.f, 300.f);
    WT_PreStreamDestination(World, Destination);
    const bool bMoved = Character->SetActorLocation(Destination, /*bSweep=*/false, nullptr,
                                                    ETeleportType::TeleportPhysics);
    WT_WARNF(TEXT("[teleport-coord] fallback-drop dest=%s bMoved=%s key='%s'"),
             *Destination.ToString(), bMoved ? TEXT("true") : TEXT("false"), *Key);
    if (bMoved)
    {
        WT_ScheduleTeleportGroundFix(World, Character, Key);
    }
    Client_NotifyTeleport(nullptr, bMoved, bMoved ? WTTeleportFail::None : WTTeleportFail::TargetGone);
}

// ★1.0.26 专用服务器禁用：wt.DumpCollectibles 是 FAutoConsoleCommand 静态初始化注册的
// （DLL 一加载就注册，运行时闸门拦不住），编译期整个剔除——FactoryServer 目标
// （UE_SERVER=1）不编这段。整段 = helper（匿名 namespace）+ 命令，lambda 是唯一调用方，
// 一并剔除不会留未引用函数。
#if !UE_SERVER
namespace
{
    /**
     * ★0.2.69: one-shot whole-map dump. Scans the currently-streamed collectibles (slugs, spheres,
     * loops) and drop pods, and merges their spawn points into collectibles.json at the mod root via
     * FWTCollectibles::MergeSpawns. Because World Partition only keeps nearby cells resident, the
     * caller should run this repeatedly (or from a fully-loaded editor) to accumulate the entire map.
     */
    void WT_DumpCollectiblesToJson(UWorld* World)
    {
        if (!World)
        {
            WT_WARNF(TEXT("wt.DumpCollectibles: no world available."));
            return;
        }

        TArray<FWT_CollectibleSpawn> Spawns;
        Spawns.Reserve(2048);

        for (TActorIterator<AFGItemPickup> It(World); It; ++It)
        {
            AFGItemPickup* Pickup = *It;
            if (!IsValid(Pickup))
            {
                continue;
            }
            FText Label;
            const uint8 Kind = FWTCollectibles::ClassifyCollectibleItem(
                Pickup->GetPickupItemClass().Get(), Label);
            if (Kind == 0xFF)
            {
                continue;
            }
            FWT_CollectibleSpawn S;
            S.Kind = Kind;
            S.Location = Pickup->GetActorLocation();
            Spawns.Add(S);
        }

        for (TActorIterator<AFGDropPod> It(World); It; ++It)
        {
            AFGDropPod* Pod = *It;
            if (!IsValid(Pod))
            {
                continue;
            }
            FWT_CollectibleSpawn S;
            S.Kind = static_cast<uint8>(EWT_CollectibleKind::DropPod);
            S.Location = Pod->GetActorLocation();
            Spawns.Add(S);
        }

        const bool bOk = FWTCollectibles::MergeSpawns(Spawns);
        if (bOk)
        {
            WT_LOGF(TEXT("wt.DumpCollectibles: merged %d spawns into %s"),
                    Spawns.Num(), *FWTCollectibles::GetCollectiblesFilePath());
        }
        else
        {
            WT_WARNF(TEXT("wt.DumpCollectibles: failed to write collectibles.json (check path / permissions)"));
        }
    }
}

// ★0.2.69: regenerate collectibles.json from the currently-loaded map. See WT_DumpCollectiblesToJson.
static FAutoConsoleCommand GDumpCollectiblesCmd(
    TEXT("wt.DumpCollectibles"),
    TEXT("Remote_Factory: scan the currently-loaded map for alien-energy collectibles and merge "
         "their spawn points into collectibles.json at the mod root."),
    FConsoleCommandDelegate::CreateLambda([]()
    {
        UWorld* World = nullptr;
        if (GEngine)
        {
            for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
            {
                if (UWorld* W = Ctx.World())
                {
                    if (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE)
                    {
                        World = W;
                        break;
                    }
                }
            }
            // Fallback: any non-null world (e.g. editor) if no game/PIE world was found.
            if (!World)
            {
                for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
                {
                    if (UWorld* W = Ctx.World())
                    {
                        World = W;
                        break;
                    }
                }
            }
        }
        WT_DumpCollectiblesToJson(World);
    }));
#endif // !UE_SERVER

// ----------------------------------------------------------------------------
// Form 3 "A": remote switch control
//
// Both entry points funnel through the same three checks, in this order:
//   1. authority        -- switches are server-owned; a client write would be overwritten
//   2. valid + is-a     -- the NetGUID may have resolved to null, or to the wrong kind of building
//   3. wired            -- an unwired bridge does literally nothing when toggled, so say so
// Permission (Form 3 "D") sits between 2 and 3: only the shared-session owner may write.
// ----------------------------------------------------------------------------

namespace
{
    /**
     * Form 3 "D" permission gate.
     *
     * "Weak sharing" means every player has their own panel but they all see the same factory, so
     * the interesting question is not "who owns the widget" but "who may CHANGE things". The answer
     * this mod ships is: the first player to open the terminal (the session owner) writes, everyone
     * else reads. That keeps item destinations and switch flips unambiguous without inventing a
     * vote or a lock.
     *
     * The owner is tracked on the game state rather than in this RCO because an RCO is per-player:
     * it has no way to see the other players' RCOs. See FWTSharedSessionState in
     * Remote_FactoryRCO.h's companion file for the replicated store.
     */
    bool WT_MayWrite(const URemote_FactoryRCO* RCO, FText& OutReason)
    {
        const AFGPlayerController* PC = RCO ? RCO->GetOwnerPlayerController() : nullptr;
        if (!PC)
        {
            OutReason = WT_LOCTEXT("Reason_NoPlayerController", "Could not find the player controller.");
            return false;
        }
        // Single-player and listen-host always pass: the session owner is the only player, and on a
        // dedicated server this check falls through to the shared-state lookup below.
        if (PC->HasAuthority())
        {
            return true;
        }

        if (!Remote_FactoryShared::IsSessionOwner(PC->GetWorld(), PC))
        {
            OutReason = WT_LOCTEXT("Reason_ViewerReadOnly",
                "Only the console host can make changes (you are in view-only mode).");
            return false;
        }
        return true;
    }
}

void URemote_FactoryRCO::Server_SetSwitchOn_Implementation(AFGBuildable* Target, bool bOn)
{
    WT_ZONE_COARSE(TEXT("RCO.SetSwitchOn"));

    if (!WT_HasAuthority(this))
    {
        WT_WARNF(TEXT("Server_SetSwitchOn called without authority — ignored."));
        return;
    }

    AFGBuildableCircuitSwitch* Switch = Cast<AFGBuildableCircuitSwitch>(Target);
    if (!Switch)
    {
        WT_WARNF(TEXT("Server_SetSwitchOn: target is not a circuit switch."));
        Client_NotifySwitchResult(Target, false, WT_LOCTEXT("Msg_NotASwitch", "Target is not a power switch."));
        return;
    }

    FText Reason;
    if (!WT_MayWrite(this, Reason))
    {
        Client_NotifySwitchResult(Target, false, Reason);
        return;
    }

    // Unwired switches accept the state change but the circuits never actually merge, so the player
    // would see the button flip and nothing happen. Report it as a failure with the real reason
    // instead of a silent success.
    if (!Switch->IsSwitchConnected())
    {
        Client_NotifySwitchResult(Target, false,
            WT_LOCTEXT("Msg_SwitchNotWired", "The switch is not wired on both ends, so this has no effect."));
        return;
    }

    Switch->SetSwitchOn(bOn);

    WT_LOGF(TEXT("Switch '%s' -> %s"), *Switch->GetName(), bOn ? TEXT("ON") : TEXT("OFF"));
    Client_NotifySwitchResult(Target, true,
        bOn ? WT_LOCTEXT("Msg_SwitchedOn", "Switched on") : WT_LOCTEXT("Msg_SwitchedOff", "Switched off"));
}

void URemote_FactoryRCO::Server_SetSwitchPriority_Implementation(AFGBuildable* Target, int32 Priority)
{
    WT_ZONE_COARSE(TEXT("RCO.SetSwitchPriority"));

    if (!WT_HasAuthority(this))
    {
        WT_WARNF(TEXT("Server_SetSwitchPriority called without authority — ignored."));
        return;
    }

    AFGBuildablePriorityPowerSwitch* PrioritySwitch = Cast<AFGBuildablePriorityPowerSwitch>(Target);
    if (!PrioritySwitch)
    {
        WT_WARNF(TEXT("Server_SetSwitchPriority: target is not a priority power switch."));
        Client_NotifySwitchResult(Target, false,
            WT_LOCTEXT("Msg_NoPrioritySupport", "This switch has no power-limit priority."));
        return;
    }

    FText Reason;
    if (!WT_MayWrite(this, Reason))
    {
        Client_NotifySwitchResult(Target, false, Reason);
        return;
    }

    PrioritySwitch->SetPriority(Priority);

    WT_LOGF(TEXT("Switch '%s' priority -> %d"), *PrioritySwitch->GetName(), Priority);
    // 0 or less has a specific in-game meaning worth surfacing: the switch is exempt from the
    // automatic load shedding that a power shortage triggers.
    Client_NotifySwitchResult(Target, true, Priority > 0
        ? FText::Format(WT_LOCTEXT("Msg_PrioritySet", "Priority set to {0}"), FText::AsNumber(Priority))
        : WT_LOCTEXT("Msg_NoAutoLimit", "Set to never auto-limit"));
}

void URemote_FactoryRCO::Server_AnnounceViewer_Implementation(bool bViewing, AFGBuildable* Terminal)
{
    WT_ZONE_COARSE(TEXT("RCO.AnnounceViewer"));

    if (!WT_HasAuthority(this))
    {
        return;
    }

    AFGPlayerController* PC = GetOwnerPlayerController();
    if (!PC)
    {
        return;
    }

    // The shared-session store lives on the game state so every player's RCO can read it; the
    // server is the only writer. bViewing=false is the panel closing (NativeDestruct), which is why
    // this is a single "announce" call rather than separate open/close RPCs.
    Remote_FactoryShared::SetViewer(GetWorld(), PC, bViewing, Terminal);

    WT_LOGF(TEXT("Viewer announce: %s viewing=%s"), *PC->GetName(), bViewing ? TEXT("yes") : TEXT("no"));
}

void URemote_FactoryRCO::Client_NotifySwitchResult_Implementation(AFGBuildable* Target, bool bSuccess, const FText& Message)
{
    OnSwitchResult.Broadcast(Target, bSuccess, Message);
}
