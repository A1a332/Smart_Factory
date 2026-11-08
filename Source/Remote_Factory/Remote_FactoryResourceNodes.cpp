#include "Remote_FactoryResourceNodes.h"
#include "Remote_FactoryLog.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"   // TActorIterator<AFGResourceNode>
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceNodeFrackingCore.h"
#include "Resources/FGResourceNodeFrackingSatellite.h"
#include "Resources/FGItemDescriptor.h"
// ★纯度改造判定（2026-10-25）：游戏模式强制信号来自 GameState 的 NodePuritySettings
// （一手核实：FGGameState.h:333 `GetNodePuritySettings()` + :797 `mNodePuritySettings = NPS_NoChange`）。
#include "FGGameState.h"
// ★mod 自建节点纯度反射兜底：mPurity / mPurityOverride 是 private UPROPERTY
// （FGResourceNode.h:165/169，TEnumAsByte 布局=单 uint8 ⇒ FByteProperty 读取；private 不影响反射）。
#include "UObject/UnrealType.h"
// ★AFGResourceNodeBase 是资源节点族的总根：普通矿点(AFGResourceNode)、间歇泉
// (AFGResourceNodeGeyser)、矿床(AFGResourceDeposit)、资源井卫星(AFGResourceNodeFrackingSatellite)
// 全都派生自它；而「资源井主节点」AFGResourceNodeFrackingCore 派生自 AFGResourceNodeBase
// （一手核实：FGResourceNodeFrackingCore.h:14 `class ... : public AFGResourceNodeBase`）。
// ⇒ 只迭代 AFGResourceNode 会整类漏掉资源井主节点。故这里一律迭代总根。
#include "Resources/FGResourceNodeBase.h"
// ★1.0.29 调试命令 wt.GiveNodeStacks（把可放置矿点物品发到背包）用到的类型：
//   AFGBuildable::GetBuiltWithDescriptor（FGBuildable.h:329）→ 建造该建筑所用的物品描述符；
//   AFGCharacterPlayer::GetInventory（FGCharacterPlayer.h:722）→ 本地玩家背包；
//   UFGInventoryComponent::AddStack（FGInventoryComponent.h:271）；FInventoryStack(数量, 描述符)（:101）；
//   UFGItemDescriptor::GetStackSize（FGItemDescriptor.h:251）。
#include "Buildables/FGBuildable.h"
#include "FGBuildableSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGInventoryComponent.h"
#include "UObject/UObjectIterator.h"

namespace
{
    constexpr const TCHAR* Schema      = TEXT("Remote_Factory.ResourceNodes/v1");
    constexpr const TCHAR* GeneratedBy = TEXT("wt.DumpResourceNodes");

    /** 坐标四舍五入到米，作为去重 / 合并键（与 collectibles 同思路）。 */
    FString RoundKey(const FVector& Loc)
    {
        const int32 X = FMath::RoundToInt(Loc.X / 100.f);
        const int32 Y = FMath::RoundToInt(Loc.Y / 100.f);
        const int32 Z = FMath::RoundToInt(Loc.Z / 100.f);
        return FString::Printf(TEXT("%d_%d_%d"), X, Y, Z);
    }

    /** EResourcePurity → 面板展示用纯度文本（与 gen_resourcenodes.py 的 PURITY_MAP 同口径）。 */
    const TCHAR* WT_PurityText(EResourcePurity P)
    {
        switch (P)
        {
        case EResourcePurity::RP_Inpure: return TEXT("贫瘠");
        case EResourcePurity::RP_Pure:   return TEXT("纯");
        case EResourcePurity::RP_Normal:
        default:                         return TEXT("普通");
        }
    }

    /** 纯度来源 → 短标签（审计块 / 面板 / 日志用）。 */
    const TCHAR* WT_PuritySourceText(EWT_PuritySource Source)
    {
        switch (Source)
        {
        case EWT_PuritySource::Vanilla:        return TEXT("原版");
        case EWT_PuritySource::GameModeForced: return TEXT("游戏模式强制");
        case EWT_PuritySource::Modded:         return TEXT("MOD 改造");
        default:                               return TEXT("未知");
        }
    }

    /** ENodePuritySettings → 可读名（审计 detail 文案用；值名一手核实 FGGameState.h:49-62）。 */
    const TCHAR* WT_NodePuritySettingsText(ENodePuritySettings S)
    {
        switch (S)
        {
        case ENodePuritySettings::NPS_AllPure:   return TEXT("All Pure（全纯）");
        case ENodePuritySettings::NPS_AllNormal: return TEXT("All Normal（全普通）");
        case ENodePuritySettings::NPS_AllImpure: return TEXT("All Impure（全贫瘠）");
        case ENodePuritySettings::NPS_AllRandom: return TEXT("All Random（全随机）");
        case ENodePuritySettings::NPS_Increase:  return TEXT("Increase（偏纯）");
        case ENodePuritySettings::NPS_Decrease:  return TEXT("Decrease（偏贫）");
        case ENodePuritySettings::NPS_NoChange:
        default:                                 return TEXT("No Change（未改）");
        }
    }

    /**
     * 反射兜底读节点纯度（Base 支系 mod 矿点用，2026-10-25 用户需求「MOD 自建节点【纯度】」）。
     *
     * 标准 mod 矿点派生自 AFGResourceNode ⇒ 调用方直接 Cast 后 GetResourcePurity()（快路径）。
     * 但 mod 也可以让自己的矿点直接派生 AFGResourceNodeBase（如自定义 Geyser 类），此时没有
     * GetResourcePurity() 可用；而 mPurity / mPurityOverride 是 UPROPERTY（private 不影响反射读取），
     * 布局 TEnumAsByte<EResourcePurity> = 单 uint8 ⇒ 用 FByteProperty 读原始值，并复刻
     * GetResourcePurity() 的 override 语义：mPurityOverride == RP_MAX 时取 mPurity。
     * 属性缺失 / 值越界 ⇒ 返回 false（调用方落「普通」）。
     *
     * 契约（一手核实）：CastField<FByteProperty> + UStruct::FindPropertyByName + ContainerPtrToValuePtr<uint8>
     * ——同款用法见原版 Private/FGResearchTree.cpp:67/115 与本 mod Remote_FactoryBuildable.cpp:61。
     */
    bool WT_ReadPurityByReflection(const AActor* Node, EResourcePurity& OutPurity)
    {
        if (!Node)
        {
            return false;
        }
        const UClass* Cls = Node->GetClass();
        const FByteProperty* PurityProp = CastField<FByteProperty>(Cls->FindPropertyByName(TEXT("mPurity")));
        if (!PurityProp)
        {
            return false;
        }
        uint8 Raw = 0;
        if (const FByteProperty* OverrideProp = CastField<FByteProperty>(Cls->FindPropertyByName(TEXT("mPurityOverride"))))
        {
            Raw = *OverrideProp->ContainerPtrToValuePtr<uint8>(Node);
        }
        // override == RP_MAX(3) 表示未设置 ⇒ 回落到 mPurity（与 GetResourcePurity() 同语义）
        if (Raw == static_cast<uint8>(EResourcePurity::RP_MAX))
        {
            Raw = *PurityProp->ContainerPtrToValuePtr<uint8>(Node);
        }
        if (Raw >= static_cast<uint8>(EResourcePurity::RP_MAX))
        {
            return false;
        }
        OutPurity = static_cast<EResourcePurity>(Raw);
        return true;
    }

    /**
     * 纯度来源判定（2026-10-25 用户需求：识别 MOD 改造过的纯度）。双信号，任一命中即报：
     *   信号1（强）：GameState 的 NodePuritySettings != NPS_NoChange ⇒ 自定义游戏模式强制
     *     （「随机节点」模式的 All Pure / All Normal / ... 档位）。
     *   信号2（强）：扫描分布异常——单一档占比 >= 85% ⇒ MOD 改造。
     *     原版 1.0 三纯度混合（th.gl 459 节点 = wiki 12/12 一致）：贫~25% / 普~42% / 纯~33%。
     *     实测对照：装全纯 MOD 时 507/507 全纯（100%）；卸载后恢复 129/215/163 且与 th.gl 逐项一致。
     *   样本 < 20 ⇒ Unknown（World Partition 未驻留完，避免误判）。
     */
    EWT_PuritySource WT_DetectPuritySource(UWorld* World, const TArray<FWT_ResourceNodeSpawn>& Spawns,
                                           FWT_PurityAudit& OutAudit)
    {
        OutAudit = FWT_PurityAudit();
        OutAudit.Total = Spawns.Num();
        for (const FWT_ResourceNodeSpawn& S : Spawns)
        {
            if (S.bIsMod)
            {
                ++OutAudit.ModNodes;
            }
            if (S.Purity == TEXT("贫瘠"))
            {
                ++OutAudit.Impure;
            }
            else if (S.Purity == TEXT("纯"))
            {
                ++OutAudit.Pure;
            }
            else
            {
                ++OutAudit.Normal;
            }
        }

        if (OutAudit.Total < 20)
        {
            OutAudit.Source = EWT_PuritySource::Unknown;
            OutAudit.Detail = FString::Printf(TEXT("样本不足（%d < 20），无法判定"), OutAudit.Total);
            return OutAudit.Source;
        }

        // 信号1：自定义游戏模式强制纯度（GameState 存档属性，非 MOD）。
        if (World)
        {
            if (const AFGGameState* GS = World->GetGameState<AFGGameState>())
            {
                const ENodePuritySettings Setting = GS->GetNodePuritySettings();
                if (Setting != ENodePuritySettings::NPS_NoChange)
                {
                    OutAudit.Source = EWT_PuritySource::GameModeForced;
                    OutAudit.Detail = FString::Printf(
                        TEXT("自定义游戏模式强制纯度：%s（分布 贫%d/普%d/纯%d）"),
                        WT_NodePuritySettingsText(Setting),
                        OutAudit.Impure, OutAudit.Normal, OutAudit.Pure);
                    return OutAudit.Source;
                }
            }
        }

        // 信号2：分布异常（单一档占比 >= 85% ⇒ 被 MOD 整体改造）。
        const float PurePct   = 100.f * OutAudit.Pure   / OutAudit.Total;
        const float NormalPct = 100.f * OutAudit.Normal / OutAudit.Total;
        const float ImpurePct = 100.f * OutAudit.Impure / OutAudit.Total;
        if (FMath::Max3(PurePct, NormalPct, ImpurePct) >= 85.f)
        {
            OutAudit.Source = EWT_PuritySource::Modded;
            OutAudit.Detail = FString::Printf(
                TEXT("纯度分布异常：纯 %.1f%% / 普 %.1f%% / 贫 %.1f%%（%d 节点）；原版三纯度混合（贫~25%%/普~42%%/纯~33%%）⇒ MOD 改造"),
                PurePct, NormalPct, ImpurePct, OutAudit.Total);
            return OutAudit.Source;
        }

        OutAudit.Source = EWT_PuritySource::Vanilla;
        OutAudit.Detail = FString::Printf(
            TEXT("原版三纯度混合：纯 %.1f%% / 普 %.1f%% / 贫 %.1f%%（%d 节点）"),
            PurePct, NormalPct, ImpurePct, OutAudit.Total);
        return OutAudit.Source;
    }

    /**
     * 资源井主节点的展示纯度 = 其卫星子节点纯度众数。
     *
     * AFGResourceNodeFrackingCore 派生自 AFGResourceNodeBase（不在 AFGResourceNode 支系），
     * 本身没有 mPurity 属性；一口井的产出由主节点 + 若干卫星共同决定，故用卫星众数代表整口井
     * （与 Python 侧 gen_resourcenodes.py 对 th.gl frackingCores[].satellites 的处理同口径）。
     * 卫星尚未注册（理论上 BeginPlay 后已注册）时退化为「普通」。
     */
    const TCHAR* WT_WellCorePurityText(const AFGResourceNodeFrackingCore* Core)
    {
        if (!Core)
        {
            return TEXT("普通");
        }
        TArray<AFGResourceNodeFrackingSatellite*> Satellites;
        Core->GetSatellites(Satellites);
        if (Satellites.Num() == 0)
        {
            return TEXT("普通");
        }
        int32 Counts[3] = { 0, 0, 0 };
        for (const AFGResourceNodeFrackingSatellite* Sat : Satellites)
        {
            if (!IsValid(Sat))
            {
                continue;
            }
            const int32 P = static_cast<int32>(Sat->GetResourcePurity());
            if (P >= 0 && P < 3)
            {
                ++Counts[P];
            }
        }
        int32 Best = 0;
        for (int32 I = 1; I < 3; ++I)
        {
            if (Counts[I] > Counts[Best])
            {
                Best = I;
            }
        }
        return WT_PurityText(static_cast<EResourcePurity>(Best));
    }

    /** EResourceNodeType → 可读名（仅诊断日志用）。 */
    const TCHAR* WT_NodeTypeName(EResourceNodeType T)
    {
        switch (T)
        {
        case EResourceNodeType::Node:              return TEXT("Node");
        case EResourceNodeType::FrackingSatellite: return TEXT("FrackingSatellite");
        case EResourceNodeType::FrackingCore:      return TEXT("FrackingCore");
        case EResourceNodeType::Geyser:            return TEXT("Geyser");
        case EResourceNodeType::Deposit:           return TEXT("Deposit");
        default:                                   return TEXT("Invalid");
        }
    }

    /**
     * 资源描述符短名（如 Desc_OreIron_C）+ 节点类型 → 里程碑排序序位 / 分组头文案。
     *
     * 与 Python 侧 gen_resourcenodes.py 的 MILESTONE / WELL_MAP / GEYSER_* 是同一口径的两侧实现
     * （C++ 侧供「加载存档自动扫描」现场定 tier；Python 侧供静态库生成）。
     * 依据：Wiki 的 Milestones / Space Elevator 阶段解锁关系
     * （Tier3=SE阶段1、Tier4=阶段2、Tier5=阶段3、Tier6=阶段4、Tier7=阶段5、Tier8=阶段5/6）；
     * 资源井汲取器 + 加压器在 Tier 8 解锁，故井主节点独立成段、落在铀之后。
     */
    void WT_LookupNodeTier(const FString& ShortName, EResourceNodeType Type,
                           int32& OutTier, FString& OutLabel, bool& bOutIsWell)
    {
        bOutIsWell = false;

        if (Type == EResourceNodeType::FrackingCore)
        {
            bOutIsWell = true;
            OutTier  = 9;
            OutLabel = TEXT("Tier 8 · 资源井（太空电梯 阶段5/6，需资源井加压器）");
            return;
        }
        if (ShortName == TEXT("Desc_Geyser_C"))
        {
            OutTier  = 10;
            OutLabel = TEXT("间歇泉（地图库未标注资源类型）");
            return;
        }
        if (ShortName == TEXT("Desc_Stone_C") || ShortName == TEXT("Desc_OreIron_C") ||
            ShortName == TEXT("Desc_OreCopper_C"))
        {
            OutTier  = 0;
            OutLabel = TEXT("Tier 0 · 起步资源（枢纽升级）");
            return;
        }
        if (ShortName == TEXT("Desc_Coal_C") || ShortName == TEXT("Desc_OreGold_C"))
        {
            OutTier  = 3;
            OutLabel = TEXT("Tier 3 · 燃煤发电（太空电梯 阶段1）");
            return;
        }
        if (ShortName == TEXT("Desc_LiquidOil_C"))
        {
            OutTier  = 4;
            OutLabel = TEXT("Tier 4 · 石油加工（太空电梯 阶段2）");
            return;
        }
        if (ShortName == TEXT("Desc_Sulfur_C"))
        {
            OutTier  = 5;
            OutLabel = TEXT("Tier 5 · 工业制造（太空电梯 阶段3）");
            return;
        }
        if (ShortName == TEXT("Desc_SAM_C"))
        {
            OutTier  = 6;
            OutLabel = TEXT("Tier 6 · 扩展电力（太空电梯 阶段4）");
            return;
        }
        if (ShortName == TEXT("Desc_OreBauxite_C") || ShortName == TEXT("Desc_RawQuartz_C"))
        {
            OutTier  = 7;
            OutLabel = TEXT("Tier 7 · 铝土矿精炼（太空电梯 阶段5）");
            return;
        }
        if (ShortName == TEXT("Desc_OreUranium_C"))
        {
            OutTier  = 8;
            OutLabel = TEXT("Tier 8 · 核能（太空电梯 阶段5/6）");
            return;
        }
        // 未识别（MOD 矿点等）→ 沉底、无阶段头
        OutTier  = 99;
        OutLabel = TEXT("");
    }

    /**
     * tier fallback：按资源显示名再查一次原版资源表（2026-10-25 用户需求「MOD 自建节点」）。
     *
     * mod 常复用原版资源但自带描述符类（如 Desc_OreIron_Mod_C）⇒ ShortName 查不中、tier 落 99 沉底。
     * 显示名（UFGItemDescriptor::GetItemName）仍是「铁矿石」等原名 ⇒ 用名字反查可把这类节点
     * 归到正确里程碑组。mod 独有资源（名字不在表内）保持 99。
     */
    void WT_LookupNodeTierByResourceName(const FString& ResourceName, int32& OutTier, FString& OutLabel)
    {
        struct FEntry { const TCHAR* Name; int32 Tier; const TCHAR* Label; };
        static const FEntry Table[] = {
            { TEXT("铁矿石"),   0, TEXT("Tier 0 · 起步资源（枢纽升级）") },
            { TEXT("石灰石"),   0, TEXT("Tier 0 · 起步资源（枢纽升级）") },
            { TEXT("铜矿石"),   0, TEXT("Tier 0 · 起步资源（枢纽升级）") },
            { TEXT("煤"),      3, TEXT("Tier 3 · 燃煤发电（太空电梯 阶段1）") },
            { TEXT("钦金矿石"), 3, TEXT("Tier 3 · 燃煤发电（太空电梯 阶段1）") },
            { TEXT("原油"),    4, TEXT("Tier 4 · 石油加工（太空电梯 阶段2）") },
            { TEXT("硫磺"),    5, TEXT("Tier 5 · 工业制造（太空电梯 阶段3）") },
            { TEXT("SAM物质"), 6, TEXT("Tier 6 · 扩展电力（太空电梯 阶段4）") },
            { TEXT("铝土矿"),  7, TEXT("Tier 7 · 铝土矿精炼（太空电梯 阶段5）") },
            { TEXT("粗石英"),  7, TEXT("Tier 7 · 铝土矿精炼（太空电梯 阶段5）") },
            { TEXT("铀"),      8, TEXT("Tier 8 · 核能（太空电梯 阶段5/6）") },
        };
        for (const FEntry& E : Table)
        {
            if (ResourceName == E.Name)
            {
                OutTier  = E.Tier;
                OutLabel = E.Label;
                return;
            }
        }
    }
}

FString FWTResourceNodes::GetResourceNodesFilePath()
{
    // ★本轮：与 collectibles.json 同源，落在 SmartFactoryLib 根（IPluginManager 解析），面板/loader 直接读库。
    if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SmartFactoryLib")))
    {
        return FPaths::Combine(Plugin->GetBaseDir(), TEXT("resourcenodes.json"));
    }
    // 兜底：可写位置，调用方干净失败而非空路径崩溃。
    return FPaths::Combine(FPlatformProcess::ApplicationSettingsDir(), TEXT("resourcenodes.json"));
}

EWT_PuritySource FWTResourceNodes::DetectPuritySource(UWorld* World,
                                                      const TArray<FWT_ResourceNodeSpawn>& Spawns,
                                                      FWT_PurityAudit& OutAudit)
{
    // 转发到匿名 namespace 的实现（扫描/探针/自动扫描共用同一套判定）。
    return WT_DetectPuritySource(World, Spawns, OutAudit);
}

const TCHAR* FWTResourceNodes::PuritySourceText(EWT_PuritySource Source)
{
    return WT_PuritySourceText(Source);
}

FString FWTResourceNodes::NodeKey(const FWT_ResourceNodeSpawn& S)
{
    return FString::Printf(TEXT("%s@%s"), *S.ResourceName, *RoundKey(S.Location));
}

// ★1.0.28k 实时矿点条目与静态库同口径定里程碑序位：查表函数在匿名 namespace（仅本 TU 可见），
// RCO 的 AppendLiveNode 无法直接调用 ⇒ 在此封装公共入口（WT_ScanResourceNodes 682-687 同口径）。
void FWTResourceNodes::LookupMilestone(const FString& ShortName, const FString& ResourceName,
                                       EResourceNodeType Type, int32& OutTier, FString& OutLabel)
{
    int32 Tier = 99;
    FString Label;
    bool bIsWell = false;
    WT_LookupNodeTier(ShortName, Type, Tier, Label, bIsWell);
    if (Tier == 99 && Type == EResourceNodeType::Node)
    {
        WT_LookupNodeTierByResourceName(ResourceName, Tier, Label);
    }
    OutTier = Tier;
    OutLabel = Label;
}

bool FWTResourceNodes::LoadSpawns(TArray<FWT_ResourceNodeSpawn>& Out, FWT_PurityAudit* AuditOut)
{
    Out.Reset();

    const FString Path = GetResourceNodesFilePath();
    FString Raw;
    if (!FFileHelper::LoadFileToString(Raw, *Path))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
    if (!Root->TryGetArrayField(TEXT("nodes"), Nodes) || !Nodes)
    {
        return false;
    }

    for (const TSharedPtr<FJsonValue>& V : *Nodes)
    {
        const TSharedPtr<FJsonObject> Obj = V->AsObject();
        if (!Obj.IsValid())
        {
            continue;
        }
        FWT_ResourceNodeSpawn S;
        double X = 0.0, Y = 0.0, Z = 0.0;
        Obj->TryGetNumberField(TEXT("x"), X);
        Obj->TryGetNumberField(TEXT("y"), Y);
        Obj->TryGetNumberField(TEXT("z"), Z);
        S.Location = FVector(X, Y, Z);
        Obj->TryGetStringField(TEXT("name"), S.ResourceName);
        Obj->TryGetStringField(TEXT("purity"), S.Purity);
        Obj->TryGetStringField(TEXT("cls"), S.ResourceClassPath);
        bool bMod = false;
        Obj->TryGetBoolField(TEXT("mod"), bMod);
        S.bIsMod = bMod;
        // ★本轮 里程碑排序（资源节点按里程碑/太空电梯阶段从上往下排）。缺失默认 99（沉底）。
        double Md = 99.0;
        Obj->TryGetNumberField(TEXT("milestoneTier"), Md);
        S.MilestoneTier = FMath::FloorToInt(Md);
        FString MLabel;
        Obj->TryGetStringField(TEXT("milestoneLabel"), MLabel);
        S.MilestoneLabel = MLabel;
        Out.Add(S);
    }

    // ★纯度审计块（2026-10-25）：旧文件无此块时保持 Unknown / 空值（调用方按「无审计」处理）。
    if (AuditOut)
    {
        *AuditOut = FWT_PurityAudit();
        const TSharedPtr<FJsonObject>* AuditObj = nullptr;
        if (Root->TryGetObjectField(TEXT("purityAudit"), AuditObj) && AuditObj && AuditObj->IsValid())
        {
            FString Src;
            if ((*AuditObj)->TryGetStringField(TEXT("source"), Src))
            {
                if (Src == TEXT("原版"))
                {
                    AuditOut->Source = EWT_PuritySource::Vanilla;
                }
                else if (Src == TEXT("游戏模式强制"))
                {
                    AuditOut->Source = EWT_PuritySource::GameModeForced;
                }
                else if (Src == TEXT("MOD 改造"))
                {
                    AuditOut->Source = EWT_PuritySource::Modded;
                }
            }
            (*AuditObj)->TryGetStringField(TEXT("detail"), AuditOut->Detail);
            double V = 0.0;
            if ((*AuditObj)->TryGetNumberField(TEXT("impure"), V))    { AuditOut->Impure  = FMath::FloorToInt(V); }
            if ((*AuditObj)->TryGetNumberField(TEXT("normal"), V))    { AuditOut->Normal  = FMath::FloorToInt(V); }
            if ((*AuditObj)->TryGetNumberField(TEXT("pure"), V))      { AuditOut->Pure    = FMath::FloorToInt(V); }
            if ((*AuditObj)->TryGetNumberField(TEXT("total"), V))     { AuditOut->Total   = FMath::FloorToInt(V); }
            if ((*AuditObj)->TryGetNumberField(TEXT("modNodes"), V))  { AuditOut->ModNodes = FMath::FloorToInt(V); }
        }
    }
    return true; // 解析 OK；Out 可能合法为空（nodes: []）
}

bool FWTResourceNodes::MergeSpawns(const TArray<FWT_ResourceNodeSpawn>& NewOnes, const FWT_PurityAudit& Audit)
{
    TArray<FWT_ResourceNodeSpawn> All;
    FWT_PurityAudit OldAudit;
    LoadSpawns(All, &OldAudit); // 现有文件（可能为空）

    // ★1.0.28k 清洗历史污染：1.0.28k 之前手动放置的 mod 矿点被自动扫描合并进了静态快照，
    // 拆除后条目永久残留（用户实测：MOD 栏 4 颗已拆矿点仍在列表、距离/阶段/图标错乱）。
    // 静态快照只应包含原版全图数据 ⇒ 加载后立刻剔除全部 mod 条目（Removed>0 视为变更，
    // 即使本次无新增也要重写，保证清洗落盘一次完成；之后文件不再新增 mod 条目，幂等恢复）。
    const int32 Removed = All.RemoveAll([](const FWT_ResourceNodeSpawn& S) { return S.bIsMod; });

    TSet<FString> Seen;
    for (const FWT_ResourceNodeSpawn& S : All)
    {
        Seen.Add(NodeKey(S));
    }

    int32 Added = 0;
    for (const FWT_ResourceNodeSpawn& S : NewOnes)
    {
        // ★1.0.28k mod 矿点永不入库：它们是"活体"（随时可被拆除/搬走），合并进静态快照后
        // 无法跟随拆除消失。世界中真实存在的 mod 矿点由实时扫描条目下发
        // （Server_ScanMachines AppendLiveNode：库键去重不再误拦，因为库中已无 mod 条目），
        // 拆除后下一轮扫描自动消失，距离/纯度/图标/里程碑分组全部实时且正确。
        if (S.bIsMod)
        {
            continue;
        }
        const FString Key = NodeKey(S);
        if (!Seen.Contains(Key))
        {
            Seen.Add(Key);
            All.Add(S);
            ++Added;
        }
    }

    // ★幂等（2026-10-25 扩展到审计维度）：无新增节点且纯度审计内容未变 ⇒ 保持文件原样，
    // 避免自动扫描的 4 趟定时器每趟都重写文件造成时间戳抖动（审计变了仍要重写，
    // 例如卸/装纯度 MOD 后同一次进档的扫描要刷新 purityAudit 块）。
    // ★1.0.28k 清洗（Removed>0）也算变更：污染文件首次进档即被清干净。
    const auto AuditEqual = [](const FWT_PurityAudit& X, const FWT_PurityAudit& Y)
    {
        return X.Source == Y.Source && X.Detail == Y.Detail && X.Impure == Y.Impure &&
               X.Normal == Y.Normal && X.Pure == Y.Pure && X.Total == Y.Total &&
               X.ModNodes == Y.ModNodes && X.WellCores == Y.WellCores && X.ReflectionReads == Y.ReflectionReads;
    };
    if (Added == 0 && Removed == 0 && AuditEqual(OldAudit, Audit))
    {
        // 无新增：保持文件原样（避免无谓的改动 / 时间戳抖动）。
        return All.Num() > 0 || LoadSpawns(All);
    }

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("schema"), Schema);
    Root->SetStringField(TEXT("generatedBy"), GeneratedBy);

    TArray<TSharedPtr<FJsonValue>> Arr;
    for (const FWT_ResourceNodeSpawn& S : All)
    {
        TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
        O->SetNumberField(TEXT("x"), S.Location.X);
        O->SetNumberField(TEXT("y"), S.Location.Y);
        O->SetNumberField(TEXT("z"), S.Location.Z);
        O->SetStringField(TEXT("name"), S.ResourceName);
        O->SetStringField(TEXT("purity"), S.Purity);
        O->SetStringField(TEXT("cls"), S.ResourceClassPath);
        O->SetBoolField(TEXT("mod"), S.bIsMod);
        // ★里程碑排序字段必须一起写出，否则自动扫描合并进来的节点 tier 缺省 99（沉底），
        //   面板的资源节点类目会错序。
        O->SetNumberField(TEXT("milestoneTier"), S.MilestoneTier);
        O->SetStringField(TEXT("milestoneLabel"), S.MilestoneLabel);
        Arr.Add(MakeShared<FJsonValueObject>(O));
    }
    Root->SetArrayField(TEXT("nodes"), Arr);

    // ★纯度审计块（2026-10-25 用户需求）：标明这批纯度的来源——原版 / 游戏模式强制 / MOD 改造。
    //   面板据此在资源节点类目上挂提示，避免把 MOD 改造值当原版值对外展示。
    {
        TSharedRef<FJsonObject> AO = MakeShared<FJsonObject>();
        AO->SetStringField(TEXT("source"), WT_PuritySourceText(Audit.Source));
        AO->SetStringField(TEXT("detail"), Audit.Detail);
        AO->SetNumberField(TEXT("impure"), Audit.Impure);
        AO->SetNumberField(TEXT("normal"), Audit.Normal);
        AO->SetNumberField(TEXT("pure"), Audit.Pure);
        AO->SetNumberField(TEXT("total"), Audit.Total);
        AO->SetNumberField(TEXT("modNodes"), Audit.ModNodes);
        Root->SetObjectField(TEXT("purityAudit"), AO);
    }

    FString OutString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
    if (!FJsonSerializer::Serialize(Root, Writer))
    {
        return false;
    }
    return FFileHelper::SaveStringToFile(OutString, *GetResourceNodesFilePath(),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

// ---------------------------------------------------------------------------
// ★资源节点全图扫描（加载存档自动覆盖 + 手动控制台命令共用同一条遍历逻辑）。
//
// 一手核实（2026-10-25，SDK 头文件）：
//   AFGResourceNodeBase（总根）
//     ├─ AFGResourceNode                  —— 普通矿点（铁/铜/煤/硫磺/…）
//     ├─ AFGResourceNodeGeyser            —— 间歇泉
//     ├─ AFGResourceDeposit               —— 矿床
//     └─ AFGResourceNodeFrackingSatellite —— 资源井【卫星子节点】
//   AFGResourceNodeFrackingCore           —— 资源井【主节点】（派生自总根，不在 AFGResourceNode 支系）
//
// 用户口径：资源井「只留主节点、子节点（卫星）不加」→ 默认剔除 FrackingSatellite。
// 纯度取枚举 GetResourcePurity()（不用 GetResourcePurityText()：实测 630 个节点里 608 个
// 返回未填充的 RichText 模板串 `<Bold>（高纯度）</>`，22 个返回空，根本不能用来定纯度）。
// ---------------------------------------------------------------------------
void WT_ScanResourceNodes(UWorld* World, TArray<FWT_ResourceNodeSpawn>& Out,
                          bool bIncludeFrackingSatellites, FWT_PurityAudit* AuditOut)
{
    Out.Reset();
    if (!World)
    {
        return;
    }

    // 纯度审计（2026-10-25 用户需求：识别 MOD 改造过的纯度）：本函数只负责如实采集 + 计数，
    // 来源判定由 WT_DetectPuritySource 统一做（避免每个调用点各判一套）。
    FWT_PurityAudit Audit;
    const bool bAudit = (AuditOut != nullptr);

    for (TActorIterator<AFGResourceNodeBase> It(World); It; ++It)
    {
        AFGResourceNodeBase* RN = *It;
        if (!IsValid(RN))
        {
            continue;
        }

        const EResourceNodeType Type = RN->GetResourceNodeType();
        if (Type == EResourceNodeType::FrackingSatellite && !bIncludeFrackingSatellites)
        {
            continue;
        }
        // 矿床（Resource Deposit）不可被资源扫描器（V）扫到，也不是采集器/加压器的放置目标
        // （wiki 原话："Deposits cannot be scanned for by any means"）⇒ 不进节点列表。
        // 与静态库口径一致：459 地表节点 + 31 间歇泉 + 17 井主节点 = 507。
        if (Type == EResourceNodeType::Deposit)
        {
            continue;
        }

        FWT_ResourceNodeSpawn S;
        S.Location = RN->GetActorLocation();

        FString ShortName;
        TSubclassOf<UFGItemDescriptor> ResCls = RN->GetResourceClass();
        if (ResCls)
        {
            S.ResourceClassPath = ResCls->GetPathName();
            ShortName           = ResCls->GetName();
            S.ResourceName      = UFGItemDescriptor::GetItemName(ResCls).ToString();
        }
        if (S.ResourceName.IsEmpty())
        {
            S.ResourceName = TEXT("矿点");
        }

        // 纯度：AFGResourceNode 支系（普通矿点 / 间歇泉 / 井卫星）直接读枚举；
        // 资源井主节点派生自总根、本身无纯度属性 → 取其卫星子节点纯度众数
        // （与 gen_resourcenodes.py 的 WELL_MAP 同口径：主节点代表整口井）；
        // 其它 Base 支系（mod 自定义节点类）→ 反射兜底读 mPurity/mPurityOverride UPROPERTY。
        if (const AFGResourceNode* Node = Cast<AFGResourceNode>(RN))
        {
            S.Purity = WT_PurityText(Node->GetResourcePurity());
        }
        else if (const AFGResourceNodeFrackingCore* Core = Cast<AFGResourceNodeFrackingCore>(RN))
        {
            S.Purity = WT_WellCorePurityText(Core);
            if (bAudit)
            {
                ++Audit.WellCores;
            }
        }
        else
        {
            EResourcePurity Reflected = EResourcePurity::RP_Normal;
            if (WT_ReadPurityByReflection(RN, Reflected))
            {
                S.Purity = WT_PurityText(Reflected);
                if (bAudit)
                {
                    ++Audit.ReflectionReads;
                }
            }
            else
            {
                S.Purity = TEXT("普通");
            }
        }

        int32 Tier = 99;
        FString Label;
        bool bIsWell = false;
        WT_LookupNodeTier(ShortName, Type, Tier, Label, bIsWell);
        // mod 矿点 fallback：描述符名未命中时按资源显示名再查（复用原版资源、自带描述符类的 mod 节点）。
        if (Tier == 99 && Type == EResourceNodeType::Node)
        {
            WT_LookupNodeTierByResourceName(S.ResourceName, Tier, Label);
        }
        // 原油同时存在于「地表原油节点(Tier4)」与「原油资源井主节点(Tier9)」两组，
        // 给井主节点加后缀区分（与 gen_resourcenodes.py 的 WELL_MAP 同名口径）。
        if (bIsWell && ShortName == TEXT("Desc_LiquidOil_C"))
        {
            S.ResourceName += TEXT("（资源井）");
        }
        S.MilestoneTier  = Tier;
        S.MilestoneLabel = Label;

        // 模组矿点判定：包路径不在 /Script/FactoryGame 与 /Game/FactoryGame 之下即 MOD 内容。
        const FString Pkg = RN->GetClass()->GetOutermost()->GetName();
        S.bIsMod = !(Pkg.StartsWith(TEXT("/Script/FactoryGame")) || Pkg.StartsWith(TEXT("/Game/FactoryGame")));

        Out.Add(S);
    }

    if (bAudit)
    {
        // WT_DetectPuritySource 把 Source/Detail/计数填进 Audit（引用出参），返回值只是 Source 枚举
        // ⇒ 先调判定、再把整个结构体交给调用方（不能把枚举直接赋给结构体）。
        WT_DetectPuritySource(World, Out, Audit);
        *AuditOut = Audit;
    }
}

// ★1.0.28 修 Server LNK2019：AutoScanMergeIntoFile 被 GameWorldModule 的 4 个延时定时器
// 调用（每个目标都有调用点，链接时必须解析）——1.0.26 把它误包进下方 #if !UE_SERVER 区间
// （夹在 helper 与命令之间）⇒ FactoryServer LNK2019 + LNK1120。现把它提到守卫之前：全目标
// 编译。它内部只调 WT_ScanResourceNodes + MergeSpawns（均在本行之前定义），不依赖被守卫的
// helper， Server 上被运行时闸门挡在定时器注册之前也不会真跑到，但符号必须存在。
void FWTResourceNodes::AutoScanMergeIntoFile(UWorld* World)
{
    TArray<FWT_ResourceNodeSpawn> Spawns;
    FWT_PurityAudit Audit;
    WT_ScanResourceNodes(World, Spawns, /*bIncludeFrackingSatellites=*/false, &Audit);
    if (Spawns.Num() == 0)
    {
        return;
    }
    const bool bOk = FWTResourceNodes::MergeSpawns(Spawns, Audit);
    if (bOk && Audit.Source != EWT_PuritySource::Vanilla)
    {
        // 纯度非原版时每次自动扫描都报（Warning 级不门控）：装/卸纯度 MOD 后用户能在日志直接看到。
        WT_WARNF(TEXT("[wt-autoscan] purity audit: source=%s | %s"),
                WT_PuritySourceText(Audit.Source), *Audit.Detail);
    }
}

// ★一次性全图 dump：扫描当前已加载地图的全部资源节点（原版 + 模组矿点），合并进
// SmartFactoryLib/resourcenodes.json。World Partition 只驻留附近格子，需多次运行积累全图。
//
// ★1.0.26 专用服务器禁用：wt.DumpResourceNodes / wt.NodeProbe / wt.AutoScanNodes 三个
// FAutoConsoleCommand 都是静态初始化注册的（DLL 一加载即注册，运行时闸门拦不住），
// 编译期整个剔除——FactoryServer 目标（UE_SERVER=1）不编这段。整段 = helper（匿名
// namespace）+ 3 个命令，两个 lambda 是唯一调用方，一并剔除不会留未引用函数。
// ★1.0.28 注意：守卫范围仅此段——AutoScanMergeIntoFile 是正式功能（定时器调用），已移至
// 守卫之前，勿再包进来（1.0.26~1.0.27 的 Server LNK2019 就是这么来的）。
#if !UE_SERVER
namespace
{
    void WT_DumpResourceNodesToJson(UWorld* World, bool bIncludeFrackingSatellites)
    {
        if (!World)
        {
            WT_WARNF(TEXT("wt.DumpResourceNodes: no world available."));
            return;
        }

        TArray<FWT_ResourceNodeSpawn> Spawns;
        Spawns.Reserve(2048);
        FWT_PurityAudit Audit;
        WT_ScanResourceNodes(World, Spawns, bIncludeFrackingSatellites, &Audit);

        const bool bOk = FWTResourceNodes::MergeSpawns(Spawns, Audit);
        if (bOk)
        {
            WT_WARNF(TEXT("wt.DumpResourceNodes: merged %d nodes into %s"),
                    Spawns.Num(), *FWTResourceNodes::GetResourceNodesFilePath());
            WT_WARNF(TEXT("wt.DumpResourceNodes: purity audit: source=%s | %s"),
                    WT_PuritySourceText(Audit.Source), *Audit.Detail);
        }
        else
        {
            WT_WARNF(TEXT("wt.DumpResourceNodes: failed to write resourcenodes.json (check path / permissions)"));
        }
    }
}

// ★本轮：从当前地图重新生成 resourcenodes.json（见 WT_DumpResourceNodesToJson）。
static FAutoConsoleCommand GDumpResourceNodesCmd(
    TEXT("wt.DumpResourceNodes"),
    TEXT("Remote_Factory: scan the currently-loaded map for resource nodes (vanilla + mod ore nodes) "
         "and merge their data into resourcenodes.json inside SmartFactoryLib."),
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
        WT_DumpResourceNodesToJson(World, /*bIncludeFrackingSatellites=*/false);
    }));

// ★坐标校准探针 v2（2026-10-25）：打印当前已驻留的【全部】资源节点真值。
//
// v1 的两个坑（均已一手核实并修掉）：
//   ① 只迭代 AFGResourceNode ⇒ 整类漏掉「资源井主节点」（AFGResourceNodeFrackingCore 派生自
//      AFGResourceNodeBase，不在 AFGResourceNode 支系），水/氮气/油井主节点一个都没采到。
//   ② 纯度用 GetResourcePurityText() ⇒ 630 个节点里 608 个返回未填充的 RichText 模板串
//      `<Bold>（高纯度）</>`、22 个返回空，完全不能用来定纯度。v2 改取枚举 GetResourcePurity()。
// v2 输出字段：节点类型 / 资源类路径 / 本地化名 / 纯度枚举(0=贫瘠 1=普通 2=纯) /
//              储量枚举(0=贫乏 1=普通 2=富集 3=无限) / actor 类 / 真实世界坐标。
static FAutoConsoleCommand GNodeProbeCmd(
    TEXT("wt.NodeProbe"),
    TEXT("Remote_Factory: log the TRUE world location (GetActorLocation) of every currently streamed "
         "resource node (AFGResourceNodeBase: nodes / geysers / fracking cores / fracking satellites / "
         "deposits), with node type, resource class path, localized name, purity enum, amount enum and "
         "actor class path."),
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
        if (!World)
        {
            WT_WARNF(TEXT("wt.NodeProbe: no world available."));
            return;
        }

        int32 N = 0;
        for (TActorIterator<AFGResourceNodeBase> It(World); It; ++It)
        {
            AFGResourceNodeBase* RN = *It;
            if (!IsValid(RN))
            {
                continue;
            }
            const FVector L = RN->GetActorLocation();
            const EResourceNodeType Type = RN->GetResourceNodeType();

            TSubclassOf<UFGItemDescriptor> ResCls = RN->GetResourceClass();
            const FString Cls  = ResCls ? ResCls->GetPathName() : TEXT("?");
            const FString Name = ResCls ? UFGItemDescriptor::GetItemName(ResCls).ToString() : TEXT("?");

            // 纯度 / 储量枚举：仅 AFGResourceNode 支系有（井主节点 / 井卫星之外的类没有）→ -1 表示不适用。
            // Base 支系 mod 自定义节点类走反射兜底（mPurity/mPurityOverride UPROPERTY），
            // 与扫描侧 WT_ReadPurityByReflection 同口径，保证探针 pur 与最终入库 purity 一致。
            int32 Pur = -1;
            int32 Amt = -1;
            if (const AFGResourceNode* Node = Cast<AFGResourceNode>(RN))
            {
                Pur = static_cast<int32>(Node->GetResourcePurity());
                Amt = static_cast<int32>(Node->GetResourceAmount());
            }
            else
            {
                EResourcePurity Reflected = EResourcePurity::RP_Normal;
                if (WT_ReadPurityByReflection(RN, Reflected))
                {
                    Pur = static_cast<int32>(Reflected);
                }
            }

            const FString ActorCls = RN->GetClass()->GetPathName();
            WT_WARNF(TEXT("wt.NodeProbe: #%d type=%s(%d) cls=%s name=%s pur=%d amt=%d actor=%s X=%f Y=%f Z=%f"),
                     N, WT_NodeTypeName(Type), static_cast<int32>(Type), *Cls, *Name, Pur, Amt,
                     *ActorCls, L.X, L.Y, L.Z);
            ++N;
        }

        // ★纯度审计摘要（2026-10-25）：一次打印「这批纯度是原版还是被 MOD/游戏模式改造过」。
        TArray<FWT_ResourceNodeSpawn> Spawns;
        FWT_PurityAudit Audit;
        WT_ScanResourceNodes(World, Spawns, /*bIncludeFrackingSatellites=*/false, &Audit);
        WT_WARNF(TEXT("wt.NodeProbe: audit source=%s | %s"),
                WT_PuritySourceText(Audit.Source), *Audit.Detail);
        WT_WARNF(TEXT("wt.NodeProbe: audit counts: impure=%d normal=%d pure=%d total=%d modNodes=%d wellCores=%d reflectionReads=%d"),
                Audit.Impure, Audit.Normal, Audit.Pure, Audit.Total, Audit.ModNodes, Audit.WellCores, Audit.ReflectionReads);
        WT_WARNF(TEXT("wt.NodeProbe: done, %d nodes streamed in this area."), N);
    }));

// ★手动触发一次「加载存档自动扫描」用的同一条逻辑（井只收主节点、不收卫星）。
static FAutoConsoleCommand GAutoScanNodesCmd(
    TEXT("wt.AutoScanNodes"),
    TEXT("Remote_Factory: scan the currently-loaded map for resource nodes (resource wells: main nodes "
         "only, satellite sub-nodes excluded) and merge them into resourcenodes.json. Same routine the "
         "save-load auto scan runs."),
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
        WT_DumpResourceNodesToJson(World, /*bIncludeFrackingSatellites=*/false);
    }));

// ★调试命令（2026-10-26 用户要求，仅本地单机用）：把「可放置矿点」物品各发一整组到玩家背包。
// 典型用途：装了 mod 矿点（PEP 等）后想直接拿到物品去摆放 / 复现节点扫描问题，不必手改存档。
//
// 数据源两条互补：
//   ① 已放置的 MOD 矿点建筑（AFGBuildable，且类包路径不在 /Game/FactoryGame 下）→
//      GetBuiltWithDescriptor() 反查「建造它所用的物品」——最准，一定是能摆出来的那个物品；
//   ② 兜底：已加载的 UFGItemDescriptor 类里，非原版包 + 名字含 "Node" 的。
//      ①一个都没命中时（例如矿点已全拆）用它，仍能把物品发出来。
// 发放量：默认整组 = UFGItemDescriptor::GetStackSize()；命令可带一个数字参数覆盖。
//
// ★只在 !UE_SERVER 段内注册（与 wt.DumpResourceNodes 等一致）：专用服务器没有本地玩家背包。
static FAutoConsoleCommand GGiveNodeStacksCmd(
    TEXT("wt.GiveNodeStacks"),
    TEXT("Remote_Factory (debug): give one full stack of every mod resource-node item "
         "(e.g. 'Limestone Node (Impure)') to the local player's inventory. "
         "Optional argument: items per stack (default = that item's stack size)."),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        int32 Requested = 0;
        if (Args.Num() > 0)
        {
            Requested = FCString::Atoi(*Args[0]);
        }

        // 找当前游戏世界（与本文件其它 wt.* 命令同口径：优先 Game/PIE，退化取第一个）。
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
        if (!World)
        {
            WT_WARNF(TEXT("wt.GiveNodeStacks: no world available."));
            return;
        }

        APlayerController* PC = World->GetFirstPlayerController();
        AFGCharacterPlayer* Char = PC ? Cast<AFGCharacterPlayer>(PC->GetPawn()) : nullptr;
        if (!Char || !Char->GetInventory())
        {
            WT_WARNF(TEXT("wt.GiveNodeStacks: no local player / inventory (run this in a loaded single-player world)."));
            return;
        }
        UFGInventoryComponent* Inv = Char->GetInventory();

        // 「MOD 矿点物品」判据：非原版包 + 物品名（或类名）含 Node。原版包一律排除，绝不误发。
        auto IsModNodeItem = [](TSubclassOf<UFGItemDescriptor> Desc) -> bool
        {
            UClass* C = Desc.Get();
            if (!C)
            {
                return false;
            }
            if (C->GetOutermost()->GetName().StartsWith(TEXT("/Game/FactoryGame")))
            {
                return false;
            }
            const FString ItemName = UFGItemDescriptor::GetItemName(Desc).ToString();
            return ItemName.Contains(TEXT("Node")) || C->GetName().Contains(TEXT("Node"));
        };

        TArray<TSubclassOf<UFGItemDescriptor>> Found;
        TSet<UClass*> Seen;

        // ① 已放置的 MOD 矿点建筑反查物品
        if (AFGBuildableSubsystem* BS = AFGBuildableSubsystem::Get(World))
        {
            for (AFGBuildable* B : BS->GetAllBuildablesRef())
            {
                if (!IsValid(B))
                {
                    continue;
                }
                if (B->GetClass()->GetOutermost()->GetName().StartsWith(TEXT("/Game/FactoryGame")))
                {
                    continue;
                }
                TSubclassOf<UFGItemDescriptor> Desc = B->GetBuiltWithDescriptor();
                if (!IsModNodeItem(Desc) || Seen.Contains(Desc.Get()))
                {
                    continue;
                }
                Seen.Add(Desc.Get());
                Found.Add(Desc);
            }
        }

        // ② 兜底：已加载的描述符类
        if (Found.Num() == 0)
        {
            for (TObjectIterator<UClass> It; It; ++It)
            {
                UClass* C = *It;
                if (!C || !C->IsChildOf(UFGItemDescriptor::StaticClass()) || C->HasAnyClassFlags(CLASS_Abstract))
                {
                    continue;
                }
                TSubclassOf<UFGItemDescriptor> Desc(C);
                if (!IsModNodeItem(Desc) || Seen.Contains(C))
                {
                    continue;
                }
                Seen.Add(C);
                Found.Add(Desc);
            }
        }

        if (Found.Num() == 0)
        {
            WT_WARNF(TEXT("wt.GiveNodeStacks: no mod node item found -- place at least one mod ore node "
                          "in this world, then run the command again."));
            return;
        }

        // ★1.0.30：AddStack 内部先过「准入过滤」才谈堆叠，实测日志已经给了实锤——
        //   LogInventory: Warning: Add failed cause item 'SKBK_BuildDesc_Limestone_Impure_C'
        //                            is not allowed, component 'inventory'
        // 也就是说 PEP 的 SKBK_BuildDesc_*（建筑描述符）被玩家背包的过滤器挡在门外，
        // added 恒为 0，物品从未进过背包。过滤有三层，全部在这里临时放开、加完立刻还原：
        //   ① mItemFilter / mFormFilter（public 的 FItemFilter / FFormFilter 委托，:135/:616）
        //   ② mAllowedItemDescriptors（private UPROPERTY 白名单，:660，反射按 TArray 清空）
        //   ③ 单格数量必须 ≤ GetStackSize，否则 AddStack 直接判非法（旧版 x50 即因此失败）
        FItemFilter SavedItemFilter = Inv->mItemFilter;
        FFormFilter SavedFormFilter = Inv->mFormFilter;
        const bool bItemFilterBound = Inv->mItemFilter.IsBound();
        const bool bFormFilterBound = Inv->mFormFilter.IsBound();

        FArrayProperty* AllowedProp = FindFProperty<FArrayProperty>(
            UFGInventoryComponent::StaticClass(), TEXT("mAllowedItemDescriptors"));
        FObjectPropertyBase* AllowedInner =
            (AllowedProp && AllowedProp->Inner) ? CastField<FObjectPropertyBase>(AllowedProp->Inner) : nullptr;
        TArray<UClass*> SavedAllowed;
        if (AllowedProp && AllowedInner)
        {
            FScriptArrayHelper H(AllowedProp, AllowedProp->ContainerPtrToValuePtr<void>(Inv));
            for (int32 i = 0; i < H.Num(); ++i)
            {
                if (UClass* Cls = Cast<UClass>(AllowedInner->GetObjectPropertyValue(H.GetRawPtr(i))))
                {
                    SavedAllowed.Add(Cls);
                }
            }
        }

        auto RelaxFilters = [&]()
        {
            Inv->mItemFilter.Unbind();
            Inv->mFormFilter.Unbind();
            if (AllowedProp && AllowedInner)
            {
                FScriptArrayHelper H(AllowedProp, AllowedProp->ContainerPtrToValuePtr<void>(Inv));
                H.EmptyValues();
            }
        };
        auto RestoreFilters = [&]()
        {
            if (bItemFilterBound)
            {
                Inv->mItemFilter = SavedItemFilter;
            }
            if (bFormFilterBound)
            {
                Inv->mFormFilter = SavedFormFilter;
            }
            if (AllowedProp && AllowedInner)
            {
                FScriptArrayHelper H(AllowedProp, AllowedProp->ContainerPtrToValuePtr<void>(Inv));
                H.EmptyValues();
                for (UClass* C : SavedAllowed)
                {
                    const int32 NewIdx = H.AddValue();
                    AllowedInner->SetObjectPropertyValue(H.GetRawPtr(NewIdx), C);
                }
            }
        };

        int32 Given = 0;
        for (const TSubclassOf<UFGItemDescriptor>& Desc : Found)
        {
            const int32 ItemStackSize = FMath::Max(1, UFGItemDescriptor::GetStackSize(Desc));
            int32 Amount = (Requested > 0) ? Requested : ItemStackSize;
            Amount = FMath::Clamp(Amount, 1, ItemStackSize);
            const bool bAllowedBefore = Inv->IsItemAllowed(Desc);
            const int32 FormIdx = static_cast<int32>(UFGItemDescriptor::GetForm(Desc));

            FInventoryStack Stack(Amount, Desc);
            int32 Added = Inv->AddStack(Stack, /*allowPartialAdd=*/true);

            if (Added <= 0)
            {
                RelaxFilters();
                FInventoryStack Stack2(Amount, Desc);
                Added = Inv->AddStack(Stack2, /*allowPartialAdd=*/true);
                RestoreFilters();
                WT_WARNF(TEXT("wt.GiveNodeStacks: '%s' x%d (stackSize=%d) was FILTERED "
                              "[IsItemAllowed=%d form=%d itemFilter=%d formFilter=%d allowList=%d] "
                              "-> relaxed, added=%d"),
                         *UFGItemDescriptor::GetItemName(Desc).ToString(), Amount, ItemStackSize,
                         bAllowedBefore ? 1 : 0, FormIdx, bItemFilterBound ? 1 : 0,
                         bFormFilterBound ? 1 : 0, SavedAllowed.Num(), Added);
            }
            else
            {
                WT_WARNF(TEXT("wt.GiveNodeStacks: '%s' x%d (stackSize=%d, allowed=%d form=%d, added=%d)"),
                         *UFGItemDescriptor::GetItemName(Desc).ToString(), Amount, ItemStackSize,
                         bAllowedBefore ? 1 : 0, FormIdx, Added);
            }
            ++Given;
        }
        WT_WARNF(TEXT("wt.GiveNodeStacks: done, %d item type(s) -> backpack."), Given);
    }));
#endif // !UE_SERVER (★1.0.26 专用服务器禁用: wt.* dump 命令段结束)
