#include "Remote_FactoryGameWorldModule.h"

#include "Remote_FactoryLog.h"
#include "Remote_FactoryCrashTrace.h"
#include "Remote_FactoryRCO.h"
#include "Remote_FactoryRegistry.h"
#include "Remote_FactoryUpgradeRegistry.h"
#include "Remote_FactoryUpgradeResearch.h"
#include "Remote_FactoryResearchTree.h"
#include "FGResearchManager.h"
#include "FGSchematicManager.h"
#include "FGResearchTree.h"
#include "Remote_FactorySharedSession.h"
#include "FGGameMode.h"
#include "UObject/UnrealType.h"

// The LogRemote_Factory category is defined in Remote_FactoryModule.cpp (the module's own
// translation unit) so it is live before any other object in the mod is constructed.

// ----------------------------------------------------------------------------
// ★v0.2.25 里程碑字段 dump（HUB 里卡片在、点开既无费用也无奖励的取证）。
//
// 背景：0.2.24 已经给里程碑写上了图标（日志 [icon] ... OK），用户实测 HUB 详情区**依然空白** ——
// 说明图标不是（唯一）原因，我此前的判断不完整。与其继续猜，不如把里程碑上跟 HUB 显示有关的字段
// 全部导出来，并且**同时导一个原版里程碑做对照**，一次进游戏就能 diff 出差在哪个字段。
//
// 不用 ExportText（宏/签名随版本变），改为按字段类型精确读值。
// ----------------------------------------------------------------------------
static void WT_DumpMilestone(UClass* Cls, const TCHAR* Tag)
{
    if (!Cls)
    {
        return;
    }
    UObject* CDO = Cls->GetDefaultObject();
    if (!CDO)
    {
        return;
    }

    int32 CostNum = -1;
    if (FArrayProperty* AP = CastField<FArrayProperty>(Cls->FindPropertyByName(TEXT("mCost"))))
    {
        FScriptArrayHelper H(AP, AP->ContainerPtrToValuePtr<void>(CDO));
        CostNum = H.Num();
    }

    int32 UnlockNum = -1;
    FString UnlockList;
    if (FArrayProperty* AP = CastField<FArrayProperty>(Cls->FindPropertyByName(TEXT("mUnlocks"))))
    {
        FScriptArrayHelper H(AP, AP->ContainerPtrToValuePtr<void>(CDO));
        UnlockNum = H.Num();
        FObjectProperty* Inner = CastField<FObjectProperty>(AP->Inner);
        for (int32 i = 0; i < UnlockNum && Inner; ++i)
        {
            UObject* U = Inner->GetObjectPropertyValue(H.GetRawPtr(i));
            UnlockList += FString::Printf(TEXT("[%s]"), U ? *U->GetClass()->GetName() : TEXT("null"));
        }
    }

    FString CatName = TEXT("?");
    if (FObjectProperty* OP = CastField<FObjectProperty>(Cls->FindPropertyByName(TEXT("mSchematicCategory"))))
    {
        UObject* C = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(CDO));
        CatName = C ? C->GetName() : TEXT("NULL");
    }

    FString SmallIcon = TEXT("?");
    if (FObjectProperty* OP = CastField<FObjectProperty>(Cls->FindPropertyByName(TEXT("mSmallSchematicIcon"))))
    {
        UObject* V = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(CDO));
        SmallIcon = V ? V->GetName() : TEXT("NULL");
    }

    int64 Tier = -1;
    if (FNumericProperty* NP = CastField<FNumericProperty>(Cls->FindPropertyByName(TEXT("mTechTier"))))
    {
        if (NP->IsInteger())
        {
            Tier = NP->GetSignedIntPropertyValue(NP->ContainerPtrToValuePtr<void>(CDO));
        }
    }

    int64 TypeVal = -1;
    if (FNumericProperty* NP = CastField<FNumericProperty>(Cls->FindPropertyByName(TEXT("mType"))))
    {
        if (NP->IsInteger())
        {
            TypeVal = NP->GetSignedIntPropertyValue(NP->ContainerPtrToValuePtr<void>(CDO));
        }
    }

    WT_WARNF(TEXT("[ms-dump] %s cls=%s type=%d tier=%d cost=%d unlocks=%d%s cat=%s smallIcon=%s"),
             Tag, *Cls->GetName(), (int32)TypeVal, (int32)Tier, CostNum, UnlockNum, *UnlockList,
             *CatName, *SmallIcon);
}

// ----------------------------------------------------------------------------
// ★v0.2.29/30 MAM 节点数据 A/B dump。
//
// 0.2.29 实锤（[wt-nd] VANILLA ... Schematic=<prop missing>）：MAMTree_NodeData_Struct 是
// **UserDefinedStruct**，成员 FName 带 GUID 后缀（pak 名称表里的 `Schematic_27_...` 就是本名），
// 按精确名找不到 —— 必须**前缀匹配**。这也正是 withSchematic=0 → 树被标"已完究"→ MAM
// 图区空白的直接原因（树里一个"可研究节点"都没有）。
// ----------------------------------------------------------------------------
static FProperty* WT_FindNodeMember(UStruct* Scope, const TCHAR* BaseName)
{
    for (TFieldIterator<FProperty> It(Scope); It; ++It)
    {
        if (It->GetName().StartsWith(BaseName))
        {
            return *It;
        }
    }
    return nullptr;
}

static void WT_DumpNodeData(UFGResearchTreeNode* Node, const TCHAR* Tag)
{
    if (!Node)
    {
        WT_WARNF(TEXT("[wt-nd] %s: <null>"), Tag);
        return;
    }
    UClass* Cls = Node->GetClass();

    void* Data = Node;
    UStruct* Scope = Cls;
    if (FStructProperty* NodeDataProp = FindFProperty<FStructProperty>(Cls, TEXT("mNodeDataStruct")))
    {
        Data = NodeDataProp->ContainerPtrToValuePtr<void>(Node);
        Scope = NodeDataProp->Struct;
    }

    // ★v0.2.31 一次性 dump 节点数据结构体的**完整反射表（真名 + 类型）**。
    // 起因：0.2.30 实测 Parents / ChildrenAndRoads / NodesToUnhide 前缀匹配**仍然查不到**
    // （连原版节点也一样）—— 说明从 pak 名称表"相邻字符串"推断类型/名字不可靠。
    // 按铁律"猜属性名必错"：一次进游戏把真名全打出来，别再猜。
    static bool GWT_NodeDataScopeDumped = false;
    if (!GWT_NodeDataScopeDumped)
    {
        GWT_NodeDataScopeDumped = true;
        int32 MemberCount = 0;
        for (TFieldIterator<FProperty> It(Scope); It; ++It) { ++MemberCount; }
        WT_WARNF(TEXT("[wt-nd-prop] scope=%s members=%d"), *Scope->GetName(), MemberCount);
        for (TFieldIterator<FProperty> It(Scope); It; ++It)
        {
            WT_WARNF(TEXT("[wt-nd-prop]   %s (%s)"), *It->GetName(), *It->GetClass()->GetName());
        }
    }

    FString S;
    if (FClassProperty* P = CastField<FClassProperty>(WT_FindNodeMember(Scope, TEXT("Schematic"))))
    {
        UClass* V = Cast<UClass>(P->GetObjectPropertyValue_InContainer(Data));
        S += FString::Printf(TEXT("Schematic=%s "), V ? *V->GetName() : TEXT("<null>"));
    }
    else
    {
        S += TEXT("Schematic=<prop missing> ");
    }

    if (FStructProperty* P = CastField<FStructProperty>(WT_FindNodeMember(Scope, TEXT("Coordinates"))))
    {
        void* CoordData = P->ContainerPtrToValuePtr<void>(Data);
        int32 X = INDEX_NONE;
        int32 Y = INDEX_NONE;
        if (FIntProperty* XP = CastField<FIntProperty>(WT_FindNodeMember(P->Struct, TEXT("X"))))
        {
            X = *XP->ContainerPtrToValuePtr<int32>(CoordData);
        }
        if (FIntProperty* YP = CastField<FIntProperty>(WT_FindNodeMember(P->Struct, TEXT("Y"))))
        {
            Y = *YP->ContainerPtrToValuePtr<int32>(CoordData);
        }
        S += FString::Printf(TEXT("Coords=(%d,%d) "), X, Y);
    }
    else
    {
        S += TEXT("Coords=<prop missing> ");
    }

    // ---- 内层类型一次性 dump（每个属性只打一次，避免刷屏） ----
    auto DumpInnerOnce = [](const TCHAR* Tag, FProperty* Inner, FProperty* Value)
    {
        static TSet<FString> Dumped;
        const FString Key(Tag);
        if (Dumped.Contains(Key)) { return; }
        Dumped.Add(Key);
        WT_WARNF(TEXT("[wt-nd-inner] %s inner=%s value=%s"), Tag,
                 Inner ? *Inner->GetClass()->GetName() : TEXT("<none>"),
                 Value ? *Value->GetClass()->GetName() : TEXT("<none>"));
    };

    // ★★0.2.32 按 [wt-nd-prop] 反射表**实测真类型**读取。
    //   上一版这四处全按错类型读（Parents 当对象、ChildrenAndRoads 当数组、NodesToUnhide 当 Map、
    //   UnhiddenBy 当对象），结果连**原版节点**也一律报 <prop missing> —— 这正是"MAM 有方块、
    //   没有连线"的根因。真类型（0.2.31 [wt-nd-prop] 实锤）：
    //     Parents          = ArrayProperty
    //     ChildrenAndRoads = MapProperty
    //     NodesToUnhide    = ArrayProperty
    //     UnhiddenBy       = ArrayProperty
    //   内层 Key/Value 类型也一并 dump，避免继续猜。

    // Parents = Array<对象引用>
    if (FArrayProperty* P = CastField<FArrayProperty>(WT_FindNodeMember(Scope, TEXT("Parents"))))
    {
        FScriptArrayHelper H(P, P->ContainerPtrToValuePtr<void>(Data));
        S += FString::Printf(TEXT("Parents=%d "), H.Num());
        if (FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(P->Inner))
        {
            for (int32 Ei = 0; Ei < H.Num() && Ei < 4; ++Ei)
            {
                const UObject* V = Inner->GetObjectPropertyValue(H.GetRawPtr(Ei));
                S += FString::Printf(TEXT("[%d]=%s "), Ei, V ? *V->GetName() : TEXT("<null>"));
            }
        }
        else
        {
            S += FString::Printf(TEXT("(inner=%s) "), *P->Inner->GetClass()->GetName());
        }
        DumpInnerOnce(TEXT("Parents"), P->Inner, nullptr);
    }
    else
    {
        S += TEXT("Parents=<prop missing> ");
    }

    // ChildrenAndRoads = Map<对象引用, 结构体…>
    if (FMapProperty* P = CastField<FMapProperty>(WT_FindNodeMember(Scope, TEXT("ChildrenAndRoads"))))
    {
        FScriptMapHelper H(P, P->ContainerPtrToValuePtr<void>(Data));
        S += FString::Printf(TEXT("ChildrenAndRoads=%d "), H.Num());
        DumpInnerOnce(TEXT("ChildrenAndRoads"), P->KeyProp, P->ValueProp);
    }
    else
    {
        S += TEXT("ChildrenAndRoads=<prop missing> ");
    }

    // NodesToUnhide = Array（不是 Map）
    if (FArrayProperty* P = CastField<FArrayProperty>(WT_FindNodeMember(Scope, TEXT("NodesToUnhide"))))
    {
        FScriptArrayHelper H(P, P->ContainerPtrToValuePtr<void>(Data));
        S += FString::Printf(TEXT("NodesToUnhide=%d "), H.Num());
        DumpInnerOnce(TEXT("NodesToUnhide"), P->Inner, nullptr);
    }
    else
    {
        S += TEXT("NodesToUnhide=<prop missing> ");
    }

    // UnhiddenBy = Array（不是单个对象）
    if (FArrayProperty* P = CastField<FArrayProperty>(WT_FindNodeMember(Scope, TEXT("UnhiddenBy"))))
    {
        FScriptArrayHelper H(P, P->ContainerPtrToValuePtr<void>(Data));
        S += FString::Printf(TEXT("UnhiddenBy=%d "), H.Num());
        if (FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(P->Inner))
        {
            for (int32 Ei = 0; Ei < H.Num() && Ei < 4; ++Ei)
            {
                const UObject* V = Inner->GetObjectPropertyValue(H.GetRawPtr(Ei));
                S += FString::Printf(TEXT("[%d]=%s "), Ei, V ? *V->GetName() : TEXT("<null>"));
            }
        }
        DumpInnerOnce(TEXT("UnhiddenBy"), P->Inner, nullptr);
    }
    else
    {
        S += TEXT("UnhiddenBy=<prop missing> ");
    }

    WT_WARNF(TEXT("[wt-nd] %s cls=%s %s"), Tag, *Cls->GetName(), *S);
}


URemote_FactoryGameWorldModule::URemote_FactoryGameWorldModule()
{
    // Mark as the root module for this mod so SML discovers and loads it.
    bRootModule = true;

    // Register the HUB milestones. SML's base DispatchLifecycleEvent pushes this list into
    // UModContentRegistry from its INITIALIZATION branch, which is what makes them appear in the
    // HUB. Leaving this out means the terminal has no unlock path at all.
    //
    // ★ v0.2.2..0.2.5 -- the Remote Factory's unlocks:
    //   * URemote_FactoryMilestone  (T1 HUB)  -> the terminal + handheld. High-Capacity Storage
    //     was moved OUT of this milestone and into the MAM research tree (see below) so all three
    //     upgrade buildings unlock "Dyson-sphere style" through the MAM, not the HUB.
    //   * The 60 research nodes (3 tracks x 20 levels) below ARE the unlock + scaling path for the
    //     three upgrade buildings: each track chains L1 -> L20; L1 costs only the vanilla milestone
    //     raw materials and unlocks the building, every further level multiplies its numbers. The
    //     level is the highest PURCHASED node, saved natively by the research system.
    mSchematics.Add(URemote_FactoryMilestone::StaticClass());

    // ★v0.2.25 用户定案：三个"插件"（大容量仓库 / 储能单元 / 产能加速器）**不再走 HUB 里程碑**，
    // 一律只在 MAM 研究树里逐级解锁。原先 v0.2.22 把储能单元(T3)、产能加速器(T5)也注册进 HUB，
    // 是为了修"两条路都断"；现在 MAM 能用了，就按最初的设计收回去：
    //   HUB 里程碑  -> 只负责无线终端本身（T1，且按 v0.2.3 定案只给手持便携版）
    //   MAM 研究树  -> 三个插件建筑的解锁 + 逐级强化
    // 两个里程碑类保留（存档/兼容），只是不再注册，因此 HUB 里不会再出现它们的卡片。
    // mSchematics.Add(UWTUpgradeStorageMilestone::StaticClass());
    // mSchematics.Add(UWTUpgradeAcceleratorMilestone::StaticClass());

    for (TSubclassOf<UFGSchematic> C : WT_GetAllUpgradeResearch())
    {
        mSchematics.Add(C);
    }

    // ★0.2.36h（用户定案「可以出现AMS商店」）：强化能量碎片上 AWESOME 商店。
    // EST_ResourceSink 条目随 mSchematics 走 SML 官方注册链；售价/发货在 CONSTRUCTION 阶段
    // 的 ApplyShopSchematicData() 里填（见 DispatchLifecycleEvent）。
    // ★0.2.134 闸门（WT_FEATURE_POWER_SHARD，用户定案「一起下架」）：关=不注册该商店条目
    // （售价/发货填充同样跳过，见 DispatchLifecycleEvent 里的同开关判断）。
    if (WT_FEATURE_POWER_SHARD)
    {
        mSchematics.Add(UWTShopPowerShardSchematic::StaticClass());
    }

    // v0.2.5 MAM 里程碑修复：研究树必须用 SML 官方的 mResearchTrees 数组注册 —— SML 在加载阶段
    // 自动把它们加进 AFGResearchManager 的可用列表。之前用 PopulateResearchTreeListDelegate 运行时
    // 绑定，时机晚于管理器的广播，导致树永远加不进去（表现为 MAM 里看不到 Remote_Factory 研究树）。
    for (TSubclassOf<UFGResearchTree> T : WT_GetAllResearchTrees())
    {
        mResearchTrees.Add(T);
    }
}

void URemote_FactoryGameWorldModule::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
    // ★2026-10-07 崩溃/卡顿链定位：世界加载 = 注册配方 + 配 60 个研究 CDO + 建 3 棵研究树
    //   + 注册建筑/升级配方，全在这一个函数里串起来。进世界时的「卡一下 / 崩在这里」
    //   可以直接从日志的 `> Lifecycle` / `< Lifecycle (xxx ms)` 与 CHAIN DUMP 判定。
    WT_ZONE_COARSE(TEXT("World.Lifecycle"));
    WT_TRACE_MARK(*FString::Printf(TEXT("DispatchLifecycleEvent phase=%d"), static_cast<int32>(Phase)));

    // ★v0.2.28 注册阶段定位标记：0.2.27 首测在 INITIALIZATION 派发别的模块（WP）注册配方时崩
    // （SML AutoRegisterRecipeReferences 读到野指针 ItemClass），没有分阶段标记没法判断崩在
    // 谁家。begin/done 成对打出来，崩在谁家一翻日志便知。
    WT_WARNF(TEXT("[wt-reg] phase %d begin."), static_cast<int32>(Phase));

    // CRITICAL: the SML base implementation is what wires the module (and mSchematics) into the
    // content registry. Always let the base class run first.
    Super::DispatchLifecycleEvent(Phase);

    // CONSTRUCTION runs before INITIALIZATION, and INITIALIZATION is when SML reads mSchematics.
    // Every milestone's cost + category must therefore be populated here so the HUB's own
    // "available schematics" query sees complete entries.
    if (Phase == ELifecyclePhase::CONSTRUCTION)
    {
        // T1 HUB milestone (terminal + handheld). High-Capacity Storage is no longer unlocked here;
        // it is the L1 node of the Storage MAM research track (see Remote_FactoryUpgradeResearch).
        WT_ZONE_COARSE(TEXT("World.ApplyMilestoneData"));
        Remote_Factory::ApplyMilestoneData();

        // Populate the 60 MAM research CDOs (cost / category / unlock / dependency) at this safe
        // time. Must NOT happen in the research classes' constructors — Apply() loads assets and
        // NewObject's subobjects, which is illegal during CDO construction and crashes the game.
        {
            WT_ZONE_COARSE(TEXT("World.ConfigureAllResearch"));
            WT_ConfigureAllResearch();
        }

        // ★0.2.36h 强化碎片两条新能力（都在 CONSTRUCTION 填，SML INITIALIZATION 读 mSchematics 前就绪）：
        //   ① 回收：mResourceSinkPoints = 原版碎片 ×2（运行时镜像，见 UpgradeRegistry.cpp）。
        //   ② 商店：EST_ResourceSink 条目 CDO 的售价（50,000 券）+ 发货解锁 + 图标。
        if (UWTUpgradePowerShardDescriptor* Shard = Cast<UWTUpgradePowerShardDescriptor>(
                UWTUpgradePowerShardDescriptor::StaticClass()->GetDefaultObject()))
        {
            Shard->ApplySinkPoints();
        }
        // ★0.2.134 闸门：商店条目已下架时，售价/发货数据也不必填（与注册处同一开关）。
        if (WT_FEATURE_POWER_SHARD)
        {
            if (UWTShopPowerShardSchematic* Shop = Cast<UWTShopPowerShardSchematic>(
                    UWTShopPowerShardSchematic::StaticClass()->GetDefaultObject()))
            {
                Shop->ApplyShopSchematicData();
            }
        }

        // 构建 3 棵 MAM 研究树的节点（每棵 20 个，指向对应研究 schematic）。必须在 SML 于加载阶段
        // 读取 mResearchTrees 之前完成，否则树会是空的。注册交给构造函数的 mResearchTrees（SML 官方
        // 机制），不再用运行时 delegate 绑定。
        {
            WT_ZONE_COARSE(TEXT("World.BuildResearchTrees"));
            WT_BuildResearchTrees();
        }
    }

    // POST_INITIALIZATION: world + FactoryGame subsystems are fully up. Safe to register.
    if (Phase == ELifecyclePhase::POST_INITIALIZATION)
    {
        UWorld* World = GetWorld();
        if (!World)
        {
            WT_WARNF(TEXT("Remote Factory: no world at POST_INITIALIZATION, skip registration."));
            return;
        }

        // A fresh world means any shared-session roster from the previous one is meaningless. The
        // roster is keyed by world so this is belt-and-braces, but it costs one line and removes a
        // whole class of "ghost viewer" bug after a map reload on a host.
        Remote_FactoryShared::Reset(World);

        // ★2026-10-07「崩溃/卡顿链快速定位」：武装逐帧卡顿看门狗。
        //   200 ms/帧 ≈ 5 FPS 的停顿，是「明显卡了」与「正常加载抖动」的分界。
        //   看门狗与 World 绑定（TWeakObjectPtr 判重），换档/回主菜单时自动卸载。
        WTTrace::StartWatchdog(World, 200.f);

        WT_LOGF(TEXT("Remote Factory: registering buildables…"));
        {
            WT_ZONE_COARSE(TEXT("World.RegisterBuildables"));
            Remote_Factory::RegisterBuildables(World);
        }

        // Form 3 "C": the three upgrade buildings. Registered on the same beat as the terminal
        // because they share the build gun, the build category and the milestone machinery.
        WT_LOGF(TEXT("Remote Factory: registering Form 3 upgrades…"));
        {
            WT_ZONE_COARSE(TEXT("World.RegisterUpgradeRecipes"));
            Remote_FactoryUpgrades::RegisterUpgradeRecipes(World);
        }

        // Register the terminal RCO on the game mode. This spawns an instance on every current and
        // future player controller so panel clicks can reach the server (buildables are
        // server-owned and cannot take client RPCs directly).
        if (AFGGameMode* GM = Cast<AFGGameMode>(World->GetAuthGameMode()))
        {
            GM->RegisterRemoteCallObjectClass(URemote_FactoryRCO::StaticClass());
            WT_LOGF(TEXT("Remote Factory: RCO registered on game mode."));
        }
        else
        {
            WT_WARNF(TEXT("Remote Factory: no AFGGameMode; RCO not registered."));
        }

        // ★v0.2.22 进度链诊断（用户实测反馈"HUB 里程碑 T1/T3/T5 均无 + MAM 无研究树"后加）。
        //
        // 为什么需要它：里程碑与研究树是两条完全独立、都可能静默失败的注册链——
        //   HUB：GameWorldModule::mSchematics → SML INITIALIZATION 的 RegisterDefaultContent()
        //        → ModContentRegistry → 绑 AFGSchematicManager::PopulateSchematicListDelegate
        //   MAM：GameWorldModule::mResearchTrees → 同一处注册 → 绑 PopulateResearchTreeListDelegate
        // 任何一环断掉都只表现为"游戏里看不到"，没有任何报错。这一段直接在运行时问两个管理器
        // "你到底拿到我们的类没有"，一次进游戏就能定位断点在哪一环。
        // 全部走 WT_WARNF（Shipping 下 LOGF 会被整体编译剔除，必须用 Warn 级取证）。
        {
            if (AFGSchematicManager* SchematicManager = AFGSchematicManager::Get(World))
            {
                TArray<TSubclassOf<UFGSchematic>> AllSchematics;
                SchematicManager->GetAllSchematics(AllSchematics);
                int32 HitT1 = 0;
                int32 HitStorage = 0;
                int32 HitAccel = 0;
                for (const TSubclassOf<UFGSchematic>& S : AllSchematics)
                {
                    UClass* SClass = S.Get();
                    if (!SClass) continue;
                    if (SClass == URemote_FactoryMilestone::StaticClass())            ++HitT1;
                    else if (SClass == UWTUpgradeStorageMilestone::StaticClass())       ++HitStorage;
                    else if (SClass == UWTUpgradeAcceleratorMilestone::StaticClass())   ++HitAccel;
                }
                WT_WARNF(TEXT("[wt-diag] schematics: total=%d | T1=%d T3storage=%d T5accel=%d"),
                         AllSchematics.Num(), HitT1, HitStorage, HitAccel);

                // ★v0.2.25 里程碑字段 dump + 原版对照（见 WT_DumpMilestone 上方注释）。
                WT_DumpMilestone(URemote_FactoryMilestone::StaticClass(), TEXT("WT_T1"));
                WT_DumpMilestone(UWTUpgradeStorageMilestone::StaticClass(), TEXT("WT_T3storage"));
                WT_DumpMilestone(UWTUpgradeAcceleratorMilestone::StaticClass(), TEXT("WT_T5accel"));
                for (const TSubclassOf<UFGSchematic>& S : AllSchematics)
                {
                    UClass* SClass = S.Get();
                    if (!SClass)
                    {
                        continue;
                    }
                    const FString Path = SClass->GetPathName();
                    if (Path.Contains(TEXT("Smart_Factory")) || Path.Contains(TEXT("Remote_Factory")) || Path.Contains(TEXT("WirelessPower")))
                    {
                        continue;
                    }
                    if (!SClass->GetName().Contains(TEXT("Milestone")))
                    {
                        continue;
                    }
                    WT_DumpMilestone(SClass, TEXT("VANILLA"));
                    break;
                }
            }
            else
            {
                WT_WARNF(TEXT("[wt-diag] AFGSchematicManager unavailable."));
            }

            if (AFGResearchManager* ResearchManager = AFGResearchManager::Get(World))
            {
                TArray<TSubclassOf<UFGResearchTree>> AllTrees;
                ResearchManager->GetAllResearchTrees(AllTrees);
                int32 HitOurs = 0;
                for (const TSubclassOf<UFGResearchTree>& T : AllTrees)
                {
                    UClass* TClass = T.Get();
                    if (!TClass) continue;
                    if (TClass == UWTResearchTreeMerged::StaticClass())
                    {
                        ++HitOurs;
                    }
                }
                WT_WARNF(TEXT("[wt-diag] research trees: total=%d | ours=%d"), AllTrees.Num(), HitOurs);
            }
            else
            {
                WT_WARNF(TEXT("[wt-diag] AFGResearchManager unavailable."));
            }

            // ★v0.2.26 MAM 节点图空白取证（续）：对照原版树的节点。
            // 我们的节点是纯 C++（FGResearchTreeNode 基类反射零属性，实锤）；
            // 原版节点是 BP 子类，布局/图标/连接等属性多半定义在 BP 里。
            // 把第一棵原版树的节点类全反射表 + 对象属性值 dump 出来对照，一次进游戏看全。
            if (AFGResearchManager* ResearchManager2 = AFGResearchManager::Get(World))
            {
                TArray<TSubclassOf<UFGResearchTree>> AllTrees2;
                ResearchManager2->GetAllResearchTrees(AllTrees2);
                for (const TSubclassOf<UFGResearchTree>& T : AllTrees2)
                {
                    UClass* TC = T.Get();
                    if (!TC)
                    {
                        continue;
                    }
                    const FString TreePath = TC->GetPathName();
                    if (TreePath.Contains(TEXT("Smart_Factory")) || TreePath.Contains(TEXT("Remote_Factory")) || TreePath.Contains(TEXT("WirelessPower")))
                    {
                        continue;
                    }
                    const TArray<UFGResearchTreeNode*> VNodes = UFGResearchTree::GetNodes(T);
                    WT_WARNF(TEXT("[wt-vtree] tree '%s' path=%s nodes=%d"),
                             *TC->GetName(), *TreePath, VNodes.Num());

                    // 原版树类自身的属性链（BP 可能给 FGResearchTree 加了布局相关字段）
                    for (UStruct* S = TC; S; S = S->GetSuperStruct())
                    {
                        FString TreeProps;
                        for (TFieldIterator<FProperty> It(S); It; ++It)
                        {
                            TreeProps += FString::Printf(TEXT("%s(%s) "), *It->GetName(), *It->GetClass()->GetName());
                        }
                        WT_WARNF(TEXT("[wt-vtree] tree-class '%s' props: %s"), *S->GetName(), *TreeProps);
                    }

                    int32 DumpedNodes = 0;
                    for (UFGResearchTreeNode* N : VNodes)
                    {
                        if (!N || DumpedNodes >= 3)
                        {
                            break;
                        }
                        ++DumpedNodes;
                        UClass* NC = N->GetClass();
                        FString NodeProps;
                        for (TFieldIterator<FProperty> It(NC); It; ++It)
                        {
                            NodeProps += FString::Printf(TEXT("%s(%s) "), *It->GetName(), *It->GetClass()->GetName());
                        }
                        WT_WARNF(TEXT("[wt-vtree] node '%s' cls=%s path=%s"),
                                 *N->GetName(), *NC->GetName(), *NC->GetPathName());
                        WT_WARNF(TEXT("[wt-vtree]   props: %s"), *NodeProps);
                        // ★v0.2.29 原版节点的数据值（Schematic / 坐标 / 父节点 / 连线计数）
                        WT_DumpNodeData(N, TEXT("VANILLA"));
                        // 对象属性直接给值（mNodeSchematic / widget 类之类一眼定位）
                        for (TFieldIterator<FProperty> It(NC); It; ++It)
                        {
                            if (FObjectProperty* OP = CastField<FObjectProperty>(*It))
                            {
                                if (const UObject* V = OP->GetObjectPropertyValue_InContainer(N))
                                {
                                    WT_WARNF(TEXT("[wt-vtree]   obj %s = %s"), *It->GetName(), *V->GetPathName());
                                }
                            }
                        }
                    }
                    break; // 只对照第一棵原版树
                }
            }

            // 逐棵树：节点数 + 节点能否解出 schematic（BP 节点类没烤进去的话这里会是 0）
            // + 可用状态（0=Locked 1=Unlocked 2=Started 3=FinishedAll）。
            for (const TSubclassOf<UFGResearchTree>& TreeClass : WT_GetAllResearchTrees())
            {
                if (!TreeClass.Get()) continue;

                const TArray<UFGResearchTreeNode*> Nodes = UFGResearchTree::GetNodes(TreeClass);
                int32 WithSchematic = 0;
                for (UFGResearchTreeNode* Node : Nodes)
                {
                    if (Node && Node->GetNodeSchematic()) ++WithSchematic;
                }

                const int32 Status = static_cast<int32>(
                    UFGResearchTree::GetResearchTreeStatus(TreeClass, World));
                WT_WARNF(TEXT("[wt-diag] tree '%s': nodes=%d withSchematic=%d status=%d"),
                         *TreeClass->GetName(), Nodes.Num(), WithSchematic, Status);

                // ★v0.2.29 我们的节点数据 A/B：每条轨道抽第 1 个 + 第 2 个（看坐标/父节点/连线）
                if (Nodes.Num() > 0)
                {
                    const int32 Pick[4] = { 0, 1, WT_MaxUpgradeLevel, WT_MaxUpgradeLevel + 1 };
                    for (int32 Pi = 0; Pi < 4; ++Pi)
                    {
                        const int32 Idx = Pick[Pi];
                        if (Nodes.IsValidIndex(Idx))
                        {
                            WT_DumpNodeData(Nodes[Idx],
                                *FString::Printf(TEXT("WT[%d]"), Idx));
                        }
                    }
                }
            }

            // ★v0.2.24 MAM 节点图空白取证：树在、节点在（withSchematic=20），但 MAM 中间一片空。
            // 社区头的 UFGResearchTreeNode 只有 GetNodeSchematic()，真实引擎类多半还有我们没设的
            // 字段（位置/图标/半径…）。"猜属性名必错" —— 直接把反射表 dump 出来，一次进游戏看全。
            {
                for (UStruct* S = UWTResearchTreeNode::StaticClass(); S; S = S->GetSuperStruct())
                {
                    FString Props;
                    for (TFieldIterator<FProperty> It(S); It; ++It)
                    {
                        Props += FString::Printf(TEXT("%s(%s) "), *It->GetName(), *It->GetClass()->GetName());
                    }
                    WT_WARNF(TEXT("[wt-diag] node-class '%s' props: %s"), *S->GetName(), *Props);
                }
                for (UStruct* S = UWTResearchTreeMerged::StaticClass(); S; S = S->GetSuperStruct())
                {
                    FString Props;
                    for (TFieldIterator<FProperty> It(S); It; ++It)
                    {
                        Props += FString::Printf(TEXT("%s(%s) "), *It->GetName(), *It->GetClass()->GetName());
                    }
                    WT_WARNF(TEXT("[wt-diag] tree-class '%s' props: %s"), *S->GetName(), *Props);
                }
            }
        }
    }

    WT_WARNF(TEXT("[wt-reg] phase %d done."), static_cast<int32>(Phase));
}

