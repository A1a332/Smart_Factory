#include "Remote_FactoryUpgradeResearch.h"

#include "Remote_FactoryUpgradeRegistry.h"   // WT_ApplySchematicIcon / recipe classes
#include "Unlocks/FGUnlockInfoOnly.h"          // ★0.2.43 奖励区「文字行」= BP_UnlockInfoOnly_C（其基类）
#include "UObject/TextProperty.h"              // FTextProperty（反射写 mUnlockName/mUnlockDescription）
#include "Engine/Texture2D.h"                  // mUnlockIconBig/Small 写的是 UTexture2D*
#include "FGSchematicManager.h"
#include "Unlocks/FGUnlockRecipe.h"
#include "FGRecipe.h"
#include "FGSchematicCategory.h"
#include "FGSchematic.h"
#include "AvailabilityDependencies/FGSchematicPurchasedDependency.h"

// ================================================================================================
// 成本表（2026-10-07 重构：5 阶段 x 3 轨 x 3 材料 = 39 项，全树不重复）
//
// 规则（见 WT_MAM_研究树设计表.md §二）：
//   阶段内第 k 级 = 该阶段基础值 x {1, 2, 4, 6}    （第 4 级取前两级之和，避免尾级爆炸）
//   同一阶段三条轨**金额相同、材料不同**。
//
// 路径全部用官方 zh-Hans.json 反查后核对过（几个反直觉的映射记在这里，免得下次又猜错）：
//   钢梁       = Desc_SteelPlate              （不是 Desc_SteelBeam —— 本体没有这个资产）
//   钦金锭     = Desc_GoldIngot               （内部名是 GoldIngot）
//   钢筋混凝土梁 = Desc_SteelPlateReinforced
//   AI限制器    = Desc_CircuitBoardHighSpeed
//   散热器     = Desc_AluminumPlateReinforced
//   涡轮马达    = Desc_MotorLightweight
//   焊接模块化框架 = Desc_ModularFrameFused
//   复合铝板    = Desc_AluminumPlate
//   神经量子处理器 = Desc_TemporalProcessor
//   叠加态振荡器  = Desc_QuantumOscillator
//   奇点电池    = Desc_SingularityCell
// ================================================================================================
static const TCHAR* GStageItemPath[5][3][3] = {
    // ---- 阶段 1 (T1/T2) ----
    {
        { TEXT("/Game/FactoryGame/Resource/Parts/ModularFrame/Desc_ModularFrame.Desc_ModularFrame_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/IronPlateReinforced/Desc_IronPlateReinforced.Desc_IronPlateReinforced_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/Cement/Desc_Cement.Desc_Cement_C") },
        { TEXT("/Game/FactoryGame/Resource/Parts/CopperSheet/Desc_CopperSheet.Desc_CopperSheet_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/Wire/Desc_Wire.Desc_Wire_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/Cable/Desc_Cable.Desc_Cable_C") },
        { TEXT("/Game/FactoryGame/Resource/Parts/IronPlate/Desc_IronPlate.Desc_IronPlate_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/IronRod/Desc_IronRod.Desc_IronRod_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/IronScrew/Desc_IronScrew.Desc_IronScrew_C") },
    },
    // ---- 阶段 2 (T3/T4) ----
    {
        { TEXT("/Game/FactoryGame/Resource/Parts/SteelPlateReinforced/Desc_SteelPlateReinforced.Desc_SteelPlateReinforced_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/SteelPlate/Desc_SteelPlate.Desc_SteelPlate_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/SteelPipe/Desc_SteelPipe.Desc_SteelPipe_C") },
        { TEXT("/Game/FactoryGame/Resource/Parts/Stator/Desc_Stator.Desc_Stator_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/SteelIngot/Desc_SteelIngot.Desc_SteelIngot_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/GoldIngot/Desc_GoldIngot.Desc_GoldIngot_C") },
        { TEXT("/Game/FactoryGame/Resource/Parts/Motor/Desc_Motor.Desc_Motor_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/QuartzCrystal/Desc_QuartzCrystal.Desc_QuartzCrystal_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/GunPowder/Desc_Gunpowder.Desc_Gunpowder_C") },
    },
    // ---- 阶段 3 (T5/T6) ----
    {
        { TEXT("/Game/FactoryGame/Resource/Parts/ModularFrameHeavy/Desc_ModularFrameHeavy.Desc_ModularFrameHeavy_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/Plastic/Desc_Plastic.Desc_Plastic_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/Rubber/Desc_Rubber.Desc_Rubber_C") },
        { TEXT("/Game/FactoryGame/Resource/Parts/CircuitBoard/Desc_CircuitBoard.Desc_CircuitBoard_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/CircuitBoardHighSpeed/Desc_CircuitBoardHighSpeed.Desc_CircuitBoardHighSpeed_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/HighSpeedConnector/Desc_HighSpeedConnector.Desc_HighSpeedConnector_C") },
        { TEXT("/Game/FactoryGame/Resource/Parts/Computer/Desc_Computer.Desc_Computer_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/Silica/Desc_Silica.Desc_Silica_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/PolymerResin/Desc_PolymerResin.Desc_PolymerResin_C") },
    },
    // ---- 阶段 4 (T7/T8) ----
    {
        { TEXT("/Game/FactoryGame/Resource/Parts/ModularFrameFused/Desc_ModularFrameFused.Desc_ModularFrameFused_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/AluminumCasing/Desc_AluminumCasing.Desc_AluminumCasing_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/AluminumPlate/Desc_AluminumPlate.Desc_AluminumPlate_C") },
        { TEXT("/Game/FactoryGame/Resource/Parts/Battery/Desc_Battery.Desc_Battery_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/ElectromagneticControlRod/Desc_ElectromagneticControlRod.Desc_ElectromagneticControlRod_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/CrystalOscillator/Desc_CrystalOscillator.Desc_CrystalOscillator_C") },
        { TEXT("/Game/FactoryGame/Resource/Parts/MotorLightweight/Desc_MotorLightweight.Desc_MotorLightweight_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/AluminumPlateReinforced/Desc_AluminumPlateReinforced.Desc_AluminumPlateReinforced_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/CoolingSystem/Desc_CoolingSystem.Desc_CoolingSystem_C") },
    },
    // ---- 阶段 5 (T9) —— 三轨合并，三条轨用同一组材料 ----
    {
        { TEXT("/Game/FactoryGame/Resource/Parts/TemporalProcessor/Desc_TemporalProcessor.Desc_TemporalProcessor_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/QuantumOscillator/Desc_QuantumOscillator.Desc_QuantumOscillator_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/SingularityCell/Desc_SingularityCell.Desc_SingularityCell_C") },
        { TEXT("/Game/FactoryGame/Resource/Parts/TemporalProcessor/Desc_TemporalProcessor.Desc_TemporalProcessor_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/QuantumOscillator/Desc_QuantumOscillator.Desc_QuantumOscillator_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/SingularityCell/Desc_SingularityCell.Desc_SingularityCell_C") },
        { TEXT("/Game/FactoryGame/Resource/Parts/TemporalProcessor/Desc_TemporalProcessor.Desc_TemporalProcessor_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/QuantumOscillator/Desc_QuantumOscillator.Desc_QuantumOscillator_C"),
          TEXT("/Game/FactoryGame/Resource/Parts/SingularityCell/Desc_SingularityCell.Desc_SingularityCell_C") },
    },
};

/** 每阶段的基础值（阶段内第 1 级）；同阶段三轨金额相同。 */
static const int32 GStageBaseAmount[5][3] = {
    { 100, 50, 25 },    // 阶段 1
    { 150, 75, 50 },    // 阶段 2
    { 200, 100, 75 },   // 阶段 3
    { 250, 150, 100 },  // 阶段 4
    { 300, 200, 150 },  // 阶段 5
};

/** 阶段内第 1~4 级的数量倍率（第 4 级 = 前两级之和）。 */
static const int32 GLevelMult[4] = { 1, 2, 4, 6 };

/**
 * ★0.2.36f 研究耗时（用户定案）：按**层级**递增，每层 +30 秒。
 *
 * 「层级」= MAM 节点名里的罗马数字（如「大容量仓库 Ⅰ · Lv1」的 Ⅰ），也就是阶段 1~4；
 * **同一层级内的 4 级耗时相同**：
 *     层级 Ⅰ（阶段 1）= 30s   层级 Ⅱ（阶段 2）= 60s
 *     层级 Ⅲ（阶段 3）= 90s   层级 Ⅳ（阶段 4）= 120s
 * 末段（三轨合并 · 终极强化）不在此表内，按路线分两档，见下方两个常量。
 */
static const float GStageResearchSeconds[4] = { 30.f, 60.f, 90.f, 120.f };

/**
 * ★0.2.36f 末段（Lv17~20，三轨合并 · 终极强化）研究耗时，按**路线**分两档（用户定案）：
 *   常规路线（不消耗能量碎片）= 150s；碎片路线（额外消耗强化能量碎片）= 180s。
 * 之所以碎片路线更久：它每一级都多一份碎片门槛（50 x Mult），给玩家留出攒碎片的时间。
 */
constexpr float WT_FinalResearchSecondsPlain = 150.f;
constexpr float WT_FinalResearchSecondsBoost = 180.f;

/**
 * 强化能量碎片：★0.2.36 起物品线落地为原生类 UWTUpgradePowerShardDescriptor
 * （见 Remote_FactoryUpgradeRegistry.h），碎片路线 Lv17~20 的门槛材料直接引用该类，
 * 不再走资产路径解析。
 */

/** 阶段 1 的入口门禁：本模组自己的 T1 里程碑（先有终端，才有升级研究）。 */
static const TCHAR* GEntryMilestonePath =
    TEXT("/Script/Remote_Factory.Remote_FactoryMilestone");

// ================================================================================================
// 显示名辅助
// ================================================================================================
static const TCHAR* GRomanNumeral[4] = { TEXT("Ⅰ"), TEXT("Ⅱ"), TEXT("Ⅲ"), TEXT("Ⅳ") };

/** 轨名（★0.2.36h 用户定案：第一条轨改叫「云端容量」—— 它加的是云端池的每种物品组数，
 *  而这个池子由**四类站**共享：固体上传 / 液体上传 / 固体下载 / 液体下载。
 *  原名 "Cloud Storage Upload Station" 只提了固体上传站，用户实测后指出要"固体，液体，上传，
 *  下载都含进去"。四类站的完整列举放在每个节点的奖励说明里（见 WTRewardDescription）。）。 */
static FText WTTrackDisplayName(int32 ti)
{
    switch (ti)
    {
    case 0:  return WT_LOCTEXT("Track_Storage",      "Cloud Capacity");
    case 1:  return WT_LOCTEXT("Track_PowerStorage", "Super Battery");
    default: return WT_LOCTEXT("Track_PowerShard",   "Enhanced Power Shard");
    }
}

/** 节点名 = 「轨名 + 阶段罗马数字」，例如「大容量仓库 Ⅰ」。 */
static FText WTTrackNodeName(int32 ti, int32 Stage)
{
    if (Stage < 1 || Stage > 4)
    {
        return WTTrackDisplayName(ti);
    }
    return FText::Format(
        WT_LOCTEXT("ResearchNodeNameFmt", "{0} {1}"),
        WTTrackDisplayName(ti),
        FText::FromString(GRomanNumeral[Stage - 1]));
}

/** 末段两条路线的名字。 */
static FText WTRouteDisplayName(EWTFinalRoute Route)
{
    return (Route == EWTFinalRoute::Boost)
        ? WT_LOCTEXT("Route_FinalBoost", "Final Reinforcement · Shard")
        : WT_LOCTEXT("Route_FinalPlain", "Final Reinforcement · Standard");
}

/**
 * ★0.2.36h（用户定案「奖励区，写说明和物品」）：每个节点在奖励区的**说明文字**。
 *
 * 数字只写能在代码/设计表里核实的：云端容量 = 2×Lv（末段碎片路线 = 3×Lv-16，即每级再 +1）。
 * ★第一条轨的说明必须把**四类站**都点名列出（用户 2026-10-07：「图一需要固体，液体，上传，
 * 下载都含进去」）—— 云端池是四类站共享的，只提固体上传站会误导。
 */
static FText WTRewardDescription(int32 ti, int32 Level, bool bFinal, EWTFinalRoute Route)
{
    if (bFinal)
    {
        return (Route == EWTFinalRoute::Boost)
            ? WT_LOCTEXT("RewardDescFinalBoost",
                "Final reinforcement (shard route): all three tracks advance together. Each level "
                "additionally consumes Enhanced Power Shards and grants a 1.5x effect.")
            : WT_LOCTEXT("RewardDescFinalPlain",
                "Final reinforcement (standard route): all three tracks advance together along the "
                "original curve. Consumes no power shards.");
    }

    switch (ti)
    {
    case 0:
    {
        const int32 Stacks = 2 * Level;   // 每级 +2 组；末段碎片路线再每级 +1（3×Lv-16）
        return FText::Format(
            WT_LOCTEXT("RewardDescCapacity",
                "Cloud capacity: the central pool now holds {0} stacks per item type "
                "(this level +2). The pool is shared by all four stations -- the solid "
                "upload station, the fluid upload station, the solid download station "
                "and the fluid download station."),
            FText::AsNumber(Stacks));
    }
    case 1:
        return WT_LOCTEXT("RewardDescBattery",
            "Super battery: each level raises the battery's stored energy and its charge "
            "rate by 25%. Batteries link up into one grid-wide buffer.");
    default:
        return WT_LOCTEXT("RewardDescShard",
            "Enhanced power shard: each stage raises every machine's shard-slot potential "
            "ceiling (250% -> 300% -> 350% -> 400%) together with the per-shard increment. "
            "Power draw still follows the vanilla curve.");
    }
}

// ================================================================================================
// ★0.2.43 奖励区「文字行」写入（反射）
//
// 用户定案：MAM 奖励区要「图标在上、文字说明在下」。原版的做法（官方 Docs JSON 逐条取证）：
//   * 文字的数据源在 **schematic 自己**身上 —— mUnlockName / mUnlockDescription / mUnlockIcon*；
//   * 再由 mUnlocks 里一条 **BP_UnlockInfoOnly_C** 条目把它画出来。
//     原版 **21/21 处** BP_UnlockInfoOnly_C 条目全都是裸的（只写 Class、零自定义字段）
//     ⇒ 数据确实不在条目上，在 schematic 上。例：MAM 节点 Research_Caterium_2_C 的
//     mUnlockName = "New Shop Products!"；Schematic_2-5_C = "Build Gun Upgrade: Customizer"。
//   * 共 20 条原版 schematic 带非空 mUnlockName。
//
// ★为什么必须走反射：本项目编译用的 FGSchematic.h 桩文件里**没有**这几个字段
//   （只有 mDisplayName/mDescription/mSchematicIcon），但运行时的 UFGSchematic 有
//   （原版 20 个 schematic 在用；utoc 目录索引里也确有 BP_UnlockInfoOnly.uasset）。
//   找不到就记日志、不崩。
// ================================================================================================
/** 写 FText 属性。FTextProperty 继承 TProperty<FText,FProperty>，**没有** FText 专属的
 *  SetPropertyValue 重载（引擎 TextProperty.h 实测），所以直接对容器地址上的 FText 对象赋值。 */
static bool WT_SetRewardTextProp(UObject* Obj, const TCHAR* PropName, const FText& Value)
{
    if (!Obj || !PropName)
    {
        return false;
    }
    FTextProperty* P = CastField<FTextProperty>(Obj->GetClass()->FindPropertyByName(PropName));
    if (!P)
    {
        return false;
    }
    *static_cast<FText*>(P->ContainerPtrToValuePtr<void>(Obj)) = Value;
    return true;
}

/** 写 UObject* 属性（与 UpgradeRegistry.cpp 的图标写法同款）。 */
static bool WT_SetRewardObjectProp(UObject* Obj, const TCHAR* PropName, UObject* Value)
{
    if (!Obj || !PropName || !Value)
    {
        return false;
    }
    FObjectPropertyBase* P =
        CastField<FObjectPropertyBase>(Obj->GetClass()->FindPropertyByName(PropName));
    if (!P)
    {
        return false;
    }
    P->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(Obj), Value);
    return true;
}

/** 把「奖励区文字」写到 schematic 自己身上，并回读一条日志 —— 下一轮实测就靠它判定
 *  「字段存在且写进去了」还是「运行时根本没有这个字段（反射没命中）」。 */
static void WT_ApplyRewardText(UFGSchematic* Self, const FText& Name, const FText& Desc, UTexture2D* Icon)
{
    if (!Self)
    {
        return;
    }
    int32 Written = 0;
    Written += WT_SetRewardTextProp(Self, TEXT("mUnlockName"), Name) ? 1 : 0;
    Written += WT_SetRewardTextProp(Self, TEXT("mUnlockDescription"), Desc) ? 1 : 0;
    if (Icon)
    {
        Written += WT_SetRewardObjectProp(Self, TEXT("mUnlockIconBig"), Icon) ? 1 : 0;
        Written += WT_SetRewardObjectProp(Self, TEXT("mUnlockIconSmall"), Icon) ? 1 : 0;
    }

    FString ReadBack = TEXT("<no mUnlockName property>");
    if (FTextProperty* P =
            CastField<FTextProperty>(Self->GetClass()->FindPropertyByName(TEXT("mUnlockName"))))
    {
        const FText* Addr = static_cast<const FText*>(P->ContainerPtrToValuePtr<void>(Self));
        ReadBack = Addr ? Addr->ToString() : TEXT("<null>");
    }

    WT_WARNF_ONCE(TEXT("reward-text"), Self,
        TEXT("[research] reward text <= %d/4 field(s) written "
             "(mUnlockName/mUnlockDescription/mUnlockIconBig/mUnlockIconSmall); readback=\"%s\""),
        Written, *ReadBack.Left(96));
}

// ================================================================================================
// 依赖构造辅助
// ================================================================================================
/**
 * ★0.2.36h（用户 2026-10-07 定案，纠正 0.2.36g）：**三轨并行**。
 *
 * 0.2.36g 曾把这里设为 true（"三条轨只能走一条"），用户随后实测发现不对：
 * 他要的是「最终阶段需要前面的 3×4×4 = 48 段全研究完才开放」—— 这与"永久锁死另两轨"
 * **逻辑互斥**（锁死 ⇒ 永远凑不齐 48 ⇒ 末段永久打不开）。当时为避免死锁把 ④ 末段入口
 * 从 48 全满放宽成了「任一轨 Lv16」，末段门槛被悄悄降掉了，用户没被知会。
 *
 * 现在改回 **false** = 三轨并行：
 *   * ② 阶段同步回到「上一阶段全部 12 级」（链式等价于 48 全满）
 *   * ④ 末段入口回到「阶段 4 全部 12 级」（= 48 全满）
 *   * 同一存档可达 52/56（只有末段另一条路线 4 段因二选一拿不到）
 *
 * ★「三功能一次只能研究一个」**不需要这里做任何事**：引擎的
 *   AFGSchematicManager::mActiveSchematic 是单值（FGSchematicManager.h:406），
 *   同一时刻本来就只能跑一个研究。之前的"永久锁死"是自行加码，已撤销。
 *
 * 玩法后果（知情）：某一存档只能拿到 52/56 —— 另一条末段路线（4 段）因二选一不可得。
 * 改回 `true` 即恢复 0.2.36g 的单轨制（同存档 20/56，末段门槛 16 段）。
 */
constexpr bool WT_bSingleTrackMode = false;

/** 三条轨的枚举表（顺序 = ti 0/1/2）。 */
static const EWTUpgradeTrack GWTTracks[3] = {
    EWTUpgradeTrack::Storage,
    EWTUpgradeTrack::PowerStorage,
    EWTUpgradeTrack::Accelerator,
};

/**
 * 收集**单条轨**在某阶段的 4 个 schematic —— 单轨制下的阶段门禁用。
 * 与 WT_CollectStageSchematics（12 个 = 三轨并行）相对：单轨制下进入下一阶段只要求
 * **本轨上一阶段的 4 级**跑完。若继续要求 12 级，选了单轨的玩家永远凑不齐 ⇒ 整树卡死。
 */
static void WT_CollectTrackStageSchematics(EWTUpgradeTrack Track, int32 Stage,
                                           TArray<TSubclassOf<UFGSchematic>>& Out)
{
    if (Stage < 1 || Stage > 4)
    {
        return;
    }
    for (int32 k = 1; k <= 4; ++k)
    {
        const int32 L = (Stage - 1) * 4 + k;
        if (TSubclassOf<UFGSchematic> C = WT_ResearchClassFor(Track, L))
        {
            Out.Add(C);
        }
    }
}

/** 收集某个阶段全部 12 个 schematic（3 轨 x 4 级）—— 阶段同步门禁用。 */
static void WT_CollectStageSchematics(int32 Stage, TArray<TSubclassOf<UFGSchematic>>& Out)
{
    if (Stage < 1 || Stage > 4)
    {
        return;
    }
    static const EWTUpgradeTrack Tracks[3] = {
        EWTUpgradeTrack::Storage,
        EWTUpgradeTrack::PowerStorage,
        EWTUpgradeTrack::Accelerator,
    };
    for (int32 t = 0; t < 3; ++t)
    {
        for (int32 k = 1; k <= 4; ++k)
        {
            const int32 L = (Stage - 1) * 4 + k;
            if (TSubclassOf<UFGSchematic> C = WT_ResearchClassFor(Tracks[t], L))
            {
                Out.Add(C);
            }
        }
    }
}

UWTUpgradeResearchBase::UWTUpgradeResearchBase()
{
}

// ================================================================================================
// Apply —— 把一个 CDO 配成研究节点
// ================================================================================================
void UWTUpgradeResearchBase::Apply(EWTUpgradeTrack Track, int32 Level, EWTFinalRoute Route)
{
    const int32 ti = FMath::Clamp((int32)Track, 0, 2);
    const bool  bFinal = (Route != EWTFinalRoute::None);

    // 阶段号（1~5）、阶段内第几级（1~4）、数量倍率
    const int32 Stage   = bFinal ? 5 : FMath::Clamp((Level - 1) / 4 + 1, 1, 4);
    const int32 InStage = bFinal ? FMath::Clamp(Level - WT_Stage4MaxLevel, 1, 4)
                                 : FMath::Clamp((Level - 1) % 4 + 1, 1, 4);
    const int32 Mult    = GLevelMult[FMath::Clamp(InStage - 1, 0, 3)];

    // 图标：MAM 节点的图就是该节点 schematic 的图标（没图标会被引擎把费用/奖励面板一起藏掉）
    // ★0.2.36g 分档（用户定案）：三功能（三轨）各用一张不同贴图；末段两条路线按颜色区分 ——
    //   消耗强化能量碎片的「碎片路线」= 红，不消耗的「常规路线」= 黄。贴图+染色双保险，
    //   都铺上（本项目已实测 HUB 会对图标整体染色，所以单靠一种未必显示得出来）。
    WT_ApplySchematicIconStyled(this,
        bFinal ? ((Route == EWTFinalRoute::Boost) ? EWTSchematicIconStyle::FinalBoost
                                                 : EWTSchematicIconStyle::FinalPlain)
               : (EWTSchematicIconStyle)((int32)EWTSchematicIconStyle::Track0 + ti));

    mType            = ESchematicType::EST_MAM;
    // ★v0.2.48 修 bug F（MAM 研究在游戏 Tier 1 看不到 WT T1/T2 节点）：
    // mTechTier 是 int32，映射到游戏 hub 层级/阶段（FGGamePhaseManager::GetGamePhaseFromTechTier）。
    // 旧代码写成 mTechTier = Stage（1~5）→ 每个节点被锁在游戏 Tier 1~5，于是只有 Stage1(T1)
    // 在 Tier1 可见、T2~T5 要更高 tier 才解锁，与「WT 研究应在 Tier1 开放」相悖。
    // 改为全部 = 1：整棵 WT MAM 树从游戏 Tier1 起即可研究；树内 Lv1→Lv4 的进阶由
    // NodesToUnhide（已在 Remote_FactoryResearchTree.cpp 写好）独立控制，不依赖游戏层级。
    mTechTier        = 1;
    mMenuPriority    = bFinal ? (9000.f + (float)Level) : ((float)ti * 1000.f + (float)Level);
    mTimeToComplete  = bFinal
        ? ((Route == EWTFinalRoute::Boost) ? WT_FinalResearchSecondsBoost
                                           : WT_FinalResearchSecondsPlain)
        : GStageResearchSeconds[FMath::Clamp(Stage - 1, 0, 3)];
    mIsPlayerSpecific = false;
    mDependenciesBlocksSchematicAccess = true;
    mHiddenUntilDependenciesMet = (InStage > 1);   // 每个方块只有第一级常显，其余靠 NodesToUnhide 揭示

    mDisplayName = FText::Format(
        WT_LOCTEXT("ResearchDisplayNameFmt", "{0} · Lv{1}"),
        bFinal ? WTRouteDisplayName(Route) : WTTrackNodeName(ti, Stage),
        FText::AsNumber(Level));

    // 节点描述（标题下面那块）。★0.2.42：48 个非末段节点原先共用一句通用文案 ⇒ 看不出各轨差异；
    // 现写入 WTRewardDescription（含各轨具体加成数字）作为**保底可见**的文字。
    // ★0.2.43 起奖励区另有同文的「文字行」（见下方 mUnlocks[1]）—— 两处都有文字，宁可重复不漏。
    mDescription = WTRewardDescription(ti, Level, bFinal, Route);

    // ---- 成本：阶段材料表 x 阶段内倍率 ----
    mCost.Empty();
    for (int32 i = 0; i < 3; ++i)
    {
        UClass* Item = StaticLoadClass(UFGItemDescriptor::StaticClass(), nullptr, GStageItemPath[Stage - 1][ti][i]);
        if (!Item)
        {
            WT_WARNF(TEXT("[research] cost item '%s' did not resolve (stage %d track %d)."),
                     GStageItemPath[Stage - 1][ti][i], Stage, ti);
            continue;
        }
        Item->AddToRoot();   // 裸 UClass* 不是 GC 引用（新存档 80s 崩溃根因）
        const int32 Amt = GStageBaseAmount[Stage - 1][i] * Mult;
        mCost.Add(FItemAmount(TSubclassOf<UFGItemDescriptor>(Item), Amt));
    }

    // ---- 碎片路线的额外门槛：强化能量碎片 ----
    // ★0.2.36：物品线落地为原生类（UWTUpgradePowerShardDescriptor），不再按资产路径解析。
    if (Route == EWTFinalRoute::Boost)
    {
        UClass* ShardItem = UWTUpgradePowerShardDescriptor::StaticClass();
        if (ShardItem)
        {
            ShardItem->AddToRoot();   // 原生类本就 GC 安全，AddToRoot 幂等无害
            mCost.Add(FItemAmount(TSubclassOf<UFGItemDescriptor>(ShardItem), 50 * Mult));
        }
    }

    // ---- 分类：沿用原版 Logistics 分类 ----
    static UClass* LogCat = nullptr;
    if (!LogCat)
    {
        LogCat = StaticLoadClass(UFGSchematicCategory::StaticClass(), nullptr,
            TEXT("/Game/FactoryGame/Schematics/SchematicCategories/SC_Logistics.SC_Logistics_C"));
        if (LogCat)
        {
            LogCat->AddToRoot();
        }
    }
    mSchematicCategory = TSubclassOf<UFGSchematicCategory>(LogCat);

    // ---- 奖励（mUnlocks）★0.2.43 重排 ----
    // 用户定案「图标下面写文字说明」⇒ 采用**原版顺序**：[0] 图标行、[1] 文字行。
    //   图标行 = UFGUnlockRecipe（`BP_UnlockRecipe_C` 实例，挂 mRecipes）；
    //   文字行 = **`BP_UnlockInfoOnly_C` 实例** + schematic 自身的 mUnlockName/mUnlockDescription。
    // ★0.2.36h 的 UWTUnlockInfo（自建原生 C++ 子类）渲染不出来 —— 原因同 skill §11.6：
    //   奖励面板的显示逻辑长在原版**蓝图**里，原生 unlock 类没有蓝图图 ⇒ spawn 不出 widget。
    //   （官方 Docs JSON 取证：原版 unlock 实例**无一例外**都是 BP 类。）
    //
    // Lv1~16：每条轨每级都挂自己的配方解锁（幂等，保证建筑永远可造）。
    // 末段 Lv17：两条路线都一次性发放三件物品的配方。
    static UClass* BPUnlock = nullptr;
    if (!BPUnlock)
    {
        BPUnlock = StaticLoadClass(UFGUnlockRecipe::StaticClass(), nullptr,
            TEXT("/Game/FactoryGame/Unlocks/BP_UnlockRecipe.BP_UnlockRecipe_C"));
        if (BPUnlock)
        {
            BPUnlock->AddToRoot();
        }
    }
    mUnlocks.Empty();

    // ---- [0] 图标行：物品/配方（必须排最前，文字行才会落在图标下面）----
    if (BPUnlock)
    {
        // ★0.2.60 建筑已彻底移除（0.2.56 定案）：研究节点不再发放任何建筑配方，
        // 既断旧档已购节点的回灌（用户实测 1123 建筑反复出现），也符合「mod 只保留 终端+强化碎片」终态。
        // 原 Storage/PowerStorage/Accelerator 三轨奖励（UWTUpgradeStorageRecipe 等）整段移除。
        TArray<TSubclassOf<UFGRecipe>> Rewards;

        if (Rewards.Num() > 0)
        {
            UFGUnlockRecipe* Unlock = NewObject<UFGUnlockRecipe>(this, BPUnlock);
            if (Unlock)
            {
                Unlock->AddToRoot();
                Unlock->mRecipes.Empty();
                for (const TSubclassOf<UFGRecipe>& R : Rewards)
                {
                    Unlock->mRecipes.AddUnique(R);
                }
                mUnlocks.Add(Unlock);
            }
        }
    }
    else
    {
        WT_WARNF(TEXT("[research] BP_UnlockRecipe did not resolve; reward panel will be blank."));
    }

    // ---- [1] 文字行：说明（排在图标下面）★0.2.43 ----
    // 文字数据写在 **schematic 自己**（反射，见本文件顶部的 WT_ApplyRewardText）；
    // 这条 BP 条目只负责把它渲染出来（原版同款：条目裸挂、数据在 schematic）。
    {
        static UClass* BPInfoOnly = nullptr;
        if (!BPInfoOnly)
        {
            BPInfoOnly = StaticLoadClass(UFGUnlockInfoOnly::StaticClass(), nullptr,
                TEXT("/Game/FactoryGame/Unlocks/BP_UnlockInfoOnly.BP_UnlockInfoOnly_C"));
            if (BPInfoOnly)
            {
                BPInfoOnly->AddToRoot();   // 静态裸 UClass* 不是 GC 引用
            }
        }
        if (BPInfoOnly)
        {
            UFGUnlockInfoOnly* Info = NewObject<UFGUnlockInfoOnly>(this, BPInfoOnly);
            if (Info)
            {
                Info->AddToRoot();
                mUnlocks.Add(Info);
            }
            else
            {
                WT_WARNF(TEXT("[research] NewObject<BP_UnlockInfoOnly_C> returned null; "
                              "reward text line skipped."));
            }
        }
        else
        {
            WT_WARNF(TEXT("[research] BP_UnlockInfoOnly_C did not resolve; reward text line missing."));
        }

        WT_ApplyRewardText(this, mDisplayName,
                           WTRewardDescription(ti, Level, bFinal, Route),
                           Cast<UTexture2D>(mSchematicIcon.GetResourceObject()));
    }

    // ---- [2] 云端配额（★0.2.47 设计表 §6.1/§6.2/§6.3/§6.5 —— **给四座云端站的加成**）----
    // 用户定案（2026-10-08 截图圈 §6.2「容量：云端配额」列）：MAM 容量轨每升一级，
    // 原版云端池（AFGCentralStorageSubsystem）每个物品类可囤的组数按表上涨 —— 四座云端站
    // （固态上传 / 固态下载 / 液态上传 / 液态下载）共用这一条加成（§6.5 文案本就写着
    // "shared by all four stations"，本轮把这句话从「承诺」变成「实现」—— 此前全项目
    // 搜不到 mItemStackLimitIncrease 的任何写入点）。
    //
    // 累计口径（§6.2 / §6.2b）：Lv1=2 组 … Lv16=32 组；末段常规 34/36/38/40、碎片 35/38/41/44。
    // 落成 unlock 增量（原版语义 = 累加，基点 = 原版默认 1 组）：
    //   Lv1 = +1；Lv2~16 = 每级 +2；末段 常规 +2 / 碎片 +3。
    //   复核：1+1=2 ✓ … 1+1+2×15=32 ✓；末段 32+2k→40 ✓ / 32+3k→44 ✓ —— 与 §6.2 逐行一致。
    // ★累加语义安全性（§5.2b 已论证）：Lv17 两条路线二选一、永不同时生效 ⇒ 不存在双份累加。
    // ★实例复用：mQuotaUnlock 是 UPROPERTY（GC 追踪、CDO 持有），Apply 重跑复用同一实例；
    //   主路径 NewObject 自原版 BP_UnlockCentralStorageItemLimit_C（渲染兼容 + Apply 走真机代码），
    //   BP 解析不到才落 UWTUnlockCloudQuota 原生兜底。
    if (bFinal || (ti == 0))   // 末段三轨一起涨 ⇒ 容量在内；非末段只有容量轨（ti=0）
    {
        const int32 Increase = (Level <= 1) ? 1
                             : ((bFinal && Route == EWTFinalRoute::Boost) ? 3 : 2);

        static UClass* BPQuota = nullptr;
        if (!BPQuota)
        {
            BPQuota = StaticLoadClass(UFGUnlockCentralStorageItemLimit::StaticClass(), nullptr,
                TEXT("/Game/FactoryGame/Unlocks/BP_UnlockCentralStorageItemLimit.BP_UnlockCentralStorageItemLimit_C"));
            if (BPQuota)
            {
                BPQuota->AddToRoot();   // 静态裸 UClass* 不是 GC 引用
            }
        }

        if (!mQuotaUnlock)
        {
            mQuotaUnlock = BPQuota ? NewObject<UFGUnlock>(this, BPQuota)
                                   : NewObject<UWTUnlockCloudQuota>(this);
            if (mQuotaUnlock)
            {
                mQuotaUnlock->AddToRoot();   // 与全工程「运行时 NewObject 必钉」纪律一致（双保险）
            }
        }

        if (mQuotaUnlock)
        {
            bool bSet = false;
            if (FIntProperty* P = CastField<FIntProperty>(
                    mQuotaUnlock->GetClass()->FindPropertyByName(TEXT("mItemStackLimitIncrease"))))
            {
                P->SetPropertyValue(P->ContainerPtrToValuePtr<void>(mQuotaUnlock), Increase);
                bSet = true;
            }
            if (bSet)
            {
                mUnlocks.Add(mQuotaUnlock);
            }
            else
            {
                WT_WARNF(TEXT("[research] mItemStackLimitIncrease reflection missing -- cloud quota skipped."));
            }
        }
        else
        {
            WT_WARNF(TEXT("[research] cloud quota unlock unavailable (BP class did not resolve) -- cloud quota skipped."));
        }
    }

    // ---- [3] 碎片补偿发放（★0.2.47 设计表 §6.6 —— 用户定案「根据全图能量蛞蝓计算，在 MAM 中补偿」）----
    // 产能轨 Lv1~16 每级随研究一次性发放强化能量碎片（§6.6 ③ 逐级表，合计 1600 精确配平）；
    // Lv17~20 不发放（末段改为碎片路线额外消耗 50/100/200/300 —— 已在上方成本段实现）。
    // 发货走 UWTUnlockGiveItemShard（商店同款，UFGUnlockGiveItem abstract ⇒ 子类）。
    if (!bFinal && ti == 2)
    {
        static const int32 GShardGrant[16] =
        {
            36,  38,  38,  40,    // Ⅰ LV1~4   = 152
            62,  65,  66,  68,    // Ⅱ LV5~8   = 261
            105, 108, 111, 112,   // Ⅲ LV9~12  = 436
            180, 185, 190, 196    // Ⅳ LV13~16 = 751（合计 1600）
        };

        const int32 Grant = GShardGrant[FMath::Clamp(Level, 1, 16) - 1];

        if (!mGrantUnlock)
        {
            mGrantUnlock = NewObject<UWTUnlockGiveItemShard>(this);
            if (mGrantUnlock)
            {
                mGrantUnlock->AddToRoot();   // ROOT-PIN（0.2.40 同款纪律）
            }
        }

        if (mGrantUnlock)
        {
            TArray<FItemAmount> GrantItems;
            GrantItems.Add(FItemAmount(
                TSubclassOf<UFGItemDescriptor>(UWTUpgradePowerShardDescriptor::StaticClass()), Grant));
            mGrantUnlock->AddItemsToGive(GrantItems);
            mUnlocks.Add(mGrantUnlock);
        }
        else
        {
            WT_WARNF(TEXT("[research] NewObject<UWTUnlockGiveItemShard> failed -- Lv%d shard grant skipped."), Level);
        }
    }

    // ---- 依赖（四层门禁）----
    mSchematicDependencies.Empty();

    // 构造一个「必须已购买」依赖
    auto AddPurchasedDep = [this](const TArray<TSubclassOf<UFGSchematic>>& List)
    {
        if (List.Num() == 0)
        {
            return;
        }
        UWT_SchematicPurchasedDep* Dep = NewObject<UWT_SchematicPurchasedDep>(this);
        if (!Dep)
        {
            // ★自检加固（2026-10-07）：不判空 ⇒ 下一行 AddToRoot() 必崩。
            WT_WARNF(TEXT("[research] NewObject<UWT_SchematicPurchasedDep> returned null; 前置门禁缺失。"));
            return;
        }
        Dep->AddToRoot();   // 运行时 NewObject 必须钉住，否则 GC 后悬垂（WirelessPower v1.0.45 原型）
        Dep->mSchematics.Empty();
        for (const TSubclassOf<UFGSchematic>& S : List)
        {
            Dep->mSchematics.Add(S);
        }
        Dep->mRequireAllSchematicsToBePurchased = true;
        mSchematicDependencies.Add(Dep);
    };

    // 构造一个「必须尚未购买」依赖（★0.2.36g：末段二选一 + 三轨单轨锁定共用）。
    // ★语义核对（引擎 AvailabilityDependencies/FGSchematicPurchasedDependency.h 原文注释）：
    //   NotPurchasedDependency 下 mRequireAllSchematicsToBePurchased = true 表示
    //   "列出的**全部**都必须尚未被购买"才满足 —— 正是"锁死另外两条轨"所需。
    auto AddNotPurchasedDep = [this](const TArray<TSubclassOf<UFGSchematic>>& List)
    {
        if (List.Num() == 0)
        {
            return;
        }
        UWT_SchematicNotPurchasedDep* Dep = NewObject<UWT_SchematicNotPurchasedDep>(this);
        if (!Dep)
        {
            WT_WARNF(TEXT("[research] NewObject<UWT_SchematicNotPurchasedDep> returned null; "
                          "互斥门禁缺失（本该只能走一条，现在两条都放开）。"));
            return;
        }
        Dep->AddToRoot();   // 运行时 NewObject 必须钉住，否则 GC 后悬垂
        Dep->mSchematics.Empty();
        for (const TSubclassOf<UFGSchematic>& S : List)
        {
            Dep->mSchematics.Add(S);
        }
        Dep->mRequireAllSchematicsToBePurchased = true;
        mSchematicDependencies.Add(Dep);
    };

    // 构造一个「任一已购买」依赖（★0.2.36g：单轨制下的末段入口 = 任一轨走完阶段 4）。
    // mRequireAllSchematicsToBePurchased = false ⇒ 列表里**任一**被购买即满足（OR 语义）。
    auto AddAnyPurchasedDep = [this](const TArray<TSubclassOf<UFGSchematic>>& List)
    {
        if (List.Num() == 0)
        {
            return;
        }
        UWT_SchematicPurchasedDep* Dep = NewObject<UWT_SchematicPurchasedDep>(this);
        if (!Dep)
        {
            WT_WARNF(TEXT("[research] NewObject<UWT_SchematicPurchasedDep> returned null; "
                          "末段入口门禁缺失。"));
            return;
        }
        Dep->AddToRoot();
        Dep->mSchematics.Empty();
        for (const TSubclassOf<UFGSchematic>& S : List)
        {
            Dep->mSchematics.Add(S);
        }
        Dep->mRequireAllSchematicsToBePurchased = false;   // ANY-of
        mSchematicDependencies.Add(Dep);
    };

    if (!bFinal)
    {
        // ① 入口门禁：阶段 1 的每个首级都要求先拿到本模组 T1 里程碑
        if (Stage == 1 && InStage == 1)
        {
            static UClass* EntryMilestone = nullptr;
            static bool    bEntryTried = false;
            if (!bEntryTried)
            {
                bEntryTried = true;
                EntryMilestone = StaticLoadClass(UFGSchematic::StaticClass(), nullptr, GEntryMilestonePath);
                if (EntryMilestone)
                {
                    EntryMilestone->AddToRoot();
                }
                else
                {
                    WT_WARNF(TEXT("[research] entry milestone '%s' not found -- 阶段 1 无入口门禁."),
                             GEntryMilestonePath);
                }
            }
            if (EntryMilestone)
            {
                TArray<TSubclassOf<UFGSchematic>> One;
                One.Add(TSubclassOf<UFGSchematic>(EntryMilestone));
                AddPurchasedDep(One);
            }
        }

        // ③ 段内门禁：本级依赖同轨上一级
        if (InStage > 1)
        {
            TArray<TSubclassOf<UFGSchematic>> One;
            if (TSubclassOf<UFGSchematic> Prev = WT_ResearchClassFor(Track, Level - 1))
            {
                One.Add(Prev);
                AddPurchasedDep(One);
            }
        }

        // ② 阶段同步门禁：每阶段首级要求上一阶段跑完
        //    ★0.2.36g 单轨制下改为「**本轨**上一阶段 4 级」（三轨并行模式仍用原来的 12 级）。
        //    不改这一步的话：选了单轨的玩家永远凑不齐 12 级 ⇒ 阶段 2 起整段永久 Locked。
        if (InStage == 1 && Stage > 1)
        {
            TArray<TSubclassOf<UFGSchematic>> PrevStage;
            if (WT_bSingleTrackMode)
            {
                WT_CollectTrackStageSchematics(Track, Stage - 1, PrevStage);
            }
            else
            {
                WT_CollectStageSchematics(Stage - 1, PrevStage);
            }
            AddPurchasedDep(PrevStage);
        }

        // ⑤ 三轨单轨锁定（★0.2.36g 用户定案「三条轨只能走一条」，即选项 2+3 取更强者的并集）
        //    每个阶段的**首级**要求另外两条轨同阶段的首级都尚未购买。配合 ③（同轨上一级）
        //    与 ②（同轨上一阶段），一旦在阶段 1 选定了某轨，另两轨整棵自动永久锁死。
        if (WT_bSingleTrackMode && InStage == 1)
        {
            TArray<TSubclassOf<UFGSchematic>> OtherTracks;
            for (int32 t = 0; t < 3; ++t)
            {
                if (GWTTracks[t] == Track)
                {
                    continue;
                }
                if (TSubclassOf<UFGSchematic> C = WT_ResearchClassFor(GWTTracks[t], (Stage - 1) * 4 + 1))
                {
                    OtherTracks.Add(C);
                }
            }
            AddNotPurchasedDep(OtherTracks);
        }
    }
    else
    {
        if (Level == WT_FinalMinLevel)
        {
            // ④ 末段入口：阶段 4 跑完
            //    ★0.2.36g 单轨制下改成「**任一轨**走到 Lv16 即解锁」（OR 语义）；
            //    三轨并行模式仍要求阶段 4 全部 12 级。
            //    不改这一步的话：单轨制下永远凑不齐 12 级 ⇒ 两个最终节点永久 Locked。
            if (WT_bSingleTrackMode)
            {
                TArray<TSubclassOf<UFGSchematic>> AnyTrackL16;
                for (int32 t = 0; t < 3; ++t)
                {
                    if (TSubclassOf<UFGSchematic> C = WT_ResearchClassFor(GWTTracks[t], WT_Stage4MaxLevel))
                    {
                        AnyTrackL16.Add(C);
                    }
                }
                AddAnyPurchasedDep(AnyTrackL16);
            }
            else
            {
                TArray<TSubclassOf<UFGSchematic>> S4;
                WT_CollectStageSchematics(4, S4);
                AddPurchasedDep(S4);
            }

            // ④ 末段路线门禁：与另一条路线的 Lv17 互斥（末段唯一互斥点）
            const EWTFinalRoute Other =
                (Route == EWTFinalRoute::Boost) ? EWTFinalRoute::Plain : EWTFinalRoute::Boost;
            TArray<TSubclassOf<UFGSchematic>> OtherRoute;
            if (TSubclassOf<UFGSchematic> OtherCls = WT_FinalResearchClassFor(Other, Level))
            {
                OtherRoute.Add(OtherCls);
            }
            AddNotPurchasedDep(OtherRoute);
        }
        else
        {
            // Lv18~20：只依赖本路线的上一级 ⇒ 买了哪条 Lv17，就只有那条能继续
            TArray<TSubclassOf<UFGSchematic>> One;
            if (TSubclassOf<UFGSchematic> Prev = WT_FinalResearchClassFor(Route, Level - 1))
            {
                One.Add(Prev);
                AddPurchasedDep(One);
            }
        }
    }

    WT_LOGF(TEXT("[research] configured %s Lv%d (stage=%d route=%d, cost=%d lines, deps=%d)."),
            *mDisplayName.ToString(), Level, Stage, (int32)Route, mCost.Num(), mSchematicDependencies.Num());
}

// ================================================================================================
// 运行时查询（升级建筑在 BeginPlay 时用来自我缩放）
// ================================================================================================

int32 WT_StageForLevel(int32 Level)
{
    return FMath::Clamp((Level - 1) / 4 + 1, 1, 5);
}

EWTFinalRoute WT_GetFinalRoute(UWorld* World)
{
    if (!World)
    {
        return EWTFinalRoute::None;
    }
    const AFGSchematicManager* SM = AFGSchematicManager::Get(World);
    if (!SM)
    {
        return EWTFinalRoute::None;
    }
    // 只需看 Lv17（二选一），两条里买了哪条就是哪条
    const TSubclassOf<UFGSchematic> P = WT_FinalResearchClassFor(EWTFinalRoute::Plain, WT_FinalMinLevel);
    const TSubclassOf<UFGSchematic> B = WT_FinalResearchClassFor(EWTFinalRoute::Boost, WT_FinalMinLevel);
    if (B && SM->IsSchematicPurchased(B)) return EWTFinalRoute::Boost;
    if (P && SM->IsSchematicPurchased(P)) return EWTFinalRoute::Plain;
    return EWTFinalRoute::None;
}

int32 WT_GetTrackLevel(UWorld* World, EWTUpgradeTrack Track)
{
    if (!World)
    {
        return 1;
    }
    const AFGSchematicManager* SM = AFGSchematicManager::Get(World);
    if (!SM)
    {
        return 1;
    }

    // 末段 Lv17~20 三轨合并：两条路线任意一条推进，三轨等级一起走
    for (int32 L = WT_MaxUpgradeLevel; L >= WT_FinalMinLevel; --L)
    {
        const TSubclassOf<UFGSchematic> P = WT_FinalResearchClassFor(EWTFinalRoute::Plain, L);
        const TSubclassOf<UFGSchematic> B = WT_FinalResearchClassFor(EWTFinalRoute::Boost, L);
        if ((P && SM->IsSchematicPurchased(P)) || (B && SM->IsSchematicPurchased(B)))
        {
            return L;
        }
    }
    // Lv1~16：本轨自己的链
    for (int32 L = WT_Stage4MaxLevel; L >= 1; --L)
    {
        const TSubclassOf<UFGSchematic> C = WT_ResearchClassFor(Track, L);
        if (C && SM->IsSchematicPurchased(C))
        {
            return L;
        }
    }
    return 1;   // 什么都没研究 -> 按 1 级算（建筑本来也不会存在）
}

float WT_TrackScale(EWTUpgradeTrack Track, int32 Level, EWTFinalRoute Route)
{
    if (Level < 1) Level = 1;

    const float Growth =
        (Track == EWTUpgradeTrack::Storage)      ? 1.10f :
        (Track == EWTUpgradeTrack::PowerStorage) ? 1.25f :
                                                   1.14f;

    const float PlainScale = FMath::Pow(Growth, (float)(Level - 1));

    // 常规路线 / Lv1~16：原曲线
    if (Route != EWTFinalRoute::Boost || Level <= WT_Stage4MaxLevel)
    {
        return PlainScale;
    }

    // ★碎片路线 Lv17~20：以 Lv16 为基点做 1.5 倍**增量**外推（设计表 §6.1 末段双路线）
    const float Base16 = FMath::Pow(Growth, (float)(WT_Stage4MaxLevel - 1));
    return Base16 + 1.5f * (PlainScale - Base16);
}

float WT_AcceleratorBoostAtLevel(int32 Level, EWTFinalRoute Route)
{
    // 与 Remote_FactoryUpgrades::MaxProductionBoost() 的封顶必须保持一致。
    const auto PlainBoost = [](int32 L)
    {
        return FMath::Clamp(2.0f * FMath::Pow(1.14f, (float)(L - 1)), 1.0f, 30.0f);
    };

    const float Plain = PlainBoost(FMath::Max(Level, 1));
    if (Route != EWTFinalRoute::Boost || Level <= WT_Stage4MaxLevel)
    {
        return Plain;
    }
    const float Base16 = PlainBoost(WT_Stage4MaxLevel);
    return Base16 + 1.5f * (Plain - Base16);
}
