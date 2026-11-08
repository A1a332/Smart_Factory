#include "Remote_FactoryUpgradeResearch.h"

#include "Remote_FactoryUpgradeRegistry.h"   // WT_ApplySchematicIcon / recipe classes
#include "Remote_FactoryRegistry.h"          // ★URemote_FactoryMilestone（前置条件文案要引用它的类）
#include "Remote_FactoryCloudQuota.h"         // ★1.0.7 云端配额等级系数（基线 + 每级倍数）
#include "Unlocks/FGUnlockInfoOnly.h"          // ★0.2.43 奖励区「文字行」= BP_UnlockInfoOnly_C（其基类）
#include "UObject/TextProperty.h"              // FTextProperty（反射写 mUnlockName/mUnlockDescription）
#include "Engine/Texture2D.h"                  // mUnlockIconBig/Small 写的是 UTexture2D*
#include "FGSchematicManager.h"
#include "Unlocks/FGUnlockRecipe.h"
#include "FGRecipe.h"
#include "FGSchematicCategory.h"
#include "FGSchematic.h"
#include "AvailabilityDependencies/FGSchematicPurchasedDependency.h"
#include "Kismet/GameplayStatics.h"          // ★2026-11-07 显式发碎片：UGameplayStatics::GetPlayerController
#include "FGCharacterPlayer.h"                // ★2026-11-07 AFGCharacterPlayer::GetInventory()
#include "FGInventoryComponent.h"             // ★2026-11-07 FInventoryStack / AddStack（发碎片到背包）

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

/** §6.6 ③ 强化能量碎片补偿发放逐级表：Lv1~16 随研究一次性发放（单位 = 我们的强化能量碎片
 *  UWTUpgradePowerShardDescriptor，非原版 Desc_CrystalShard）。合计 1602，与设计表 §6.6 ③ 逐行一致。
 *  同时供奖励区文案 WTRewardDescription 引用以写明每级补偿数量。 */
static const int32 GShardGrant[16] = {
    36,  38,  38,  40,    // Ⅰ LV1~4   = 154（Lv1 经 Clamp 取 38，非 36）
    62,  65,  66,  68,    // Ⅱ LV5~8   = 261
    105, 108, 111, 112,   // Ⅲ LV9~12  = 436
    180, 185, 190, 196    // Ⅳ LV13~16 = 751（Lv1~16 合计 1602）
};

// 碎片补偿表：Lv1 经 WT_ShardGrantAt 的 Clamp(Lv,2,16)-1 取 index1 ⇒ 发 38（与旧 Lv2 同值，但机制是 Clamp 而非前移数组）。
// 集中管理，三处引用点（文案/奖励名/发放）共用，避免三份漂移。
static FORCEINLINE int32 WT_ShardGrantAt(int32 Level)
{
    return GShardGrant[FMath::Clamp(Level, 2, 16) - 1];
}

// ★2026-11-07 强化碎片补偿：改为「研究完成时显式发放」。
//   UFGUnlockGiveItem 在 MAM 路径下只是容器（SDK 头注释 "Doesn't actually do anything on unlock"），
//   研究系统不会读它来发物品 ⇒ 光把 mGrantUnlock 塞进 mUnlocks 进不了背包。改为登记
//   「schematic 类 → 发放量」，由 WT_GivePendingShardCompensation 在研究完成时 AddStack 到玩家背包。
static TMap<UClass*, int32> GWTShardGrantBySchematic;   // 注册表：schematic 类 → 当级发放量
static TSet<UClass*>        GWTShardGranted;            // 已发放集合（幂等去重）

void WT_RegisterShardGrant(UClass* SchematicClass, int32 Amount)
{
    if (SchematicClass && Amount > 0)
    {
        GWTShardGrantBySchematic.Add(SchematicClass, Amount);
    }
}

// 把碎片加进玩家主背包（即用户说的「背包」）。AddStack 为 server-only，研究完成在服务器侧触发，安全。
static void WT_GiveShardsToPlayer(UWorld* W, int32 Amount)
{
    if (!W || Amount <= 0)
    {
        return;
    }
    APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0);
    AFGCharacterPlayer* Char = (PC && PC->GetCharacter()) ? Cast<AFGCharacterPlayer>(PC->GetCharacter()) : nullptr;
    UFGInventoryComponent* Inv = Char ? Char->GetInventory() : nullptr;
    if (!Inv)
    {
        WT_WARNF(TEXT("[wt-shard] player backpack unavailable; %d shards NOT granted (retry on next completion)."), Amount);
        return;
    }
    FInventoryStack Stack(Amount, TSubclassOf<UFGItemDescriptor>(UWTUpgradePowerShardDescriptor::StaticClass()));
    const int32 Added = Inv->AddStack(Stack, /*allowPartialAdd=*/true);
    WT_WARNF(TEXT("[wt-shard] granted %d enhanced power shards (added %d to backpack)."), Amount, Added);
}

// 研究完成时调用：把「已购买、尚未发放」的碎片轨 schematic 对应碎片发到背包。幂等（GWTShardGranted 去重）。
// ★2026-11-07 修复「LV1 没有、LV2 起才有」错位：研究完成事件触发时，SchematicManager 的「已购集合」
//   往往还没提交完**刚那一级**的购买，导致 IsSchematicPurchased(本级) 当次返回 false → 本级补偿被漏掉，
//   要等下一级完成时（那时本级已提交）才补发，呈现错位一级。故对 JustCompleted（本次刚研究完的
//   schematic，此刻 100% 已购）**直接发放、跳过 IsSchematicPurchased 判定**；循环仅作跨会话兜底。
void WT_GivePendingShardCompensation(UWorld* W, UClass* JustCompleted)
{
    if (!W || GWTShardGrantBySchematic.Num() == 0)
    {
        return;
    }
    const AFGSchematicManager* SM = AFGSchematicManager::Get(W);
    if (!SM)
    {
        return;
    }
    auto TryGrant = [&](UClass* Key)
    {
        if (!Key)
        {
            return;
        }
        const int32* pAmt = GWTShardGrantBySchematic.Find(Key);
        if (!pAmt || GWTShardGranted.Contains(Key))
        {
            return;
        }
        WT_GiveShardsToPlayer(W, *pAmt);
        GWTShardGranted.Add(Key);
    };
    // ① 先发「刚研究完」这一级（绕过 IsSchematicPurchased 时序窗口）。
    if (JustCompleted)
    {
        TryGrant(JustCompleted);
    }
    // ② 再补发所有「已购但未发」的（跨会话 / 延迟补偿兜底）。
    for (const TPair<UClass*, int32>& E : GWTShardGrantBySchematic)
    {
        if (GWTShardGranted.Contains(E.Key))
        {
            continue;
        }
        if (SM->IsSchematicPurchased(E.Key))
        {
            WT_GiveShardsToPlayer(W, E.Value);
            GWTShardGranted.Add(E.Key);
        }
    }
}

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

/** 储能单元（Super Battery）奖励区文案基数：必须与 AWTUpgradePowerStorage 本体一致。
 *  储电量基数 1600 MWh（BeginPlay 乘 WT_TrackScale，前移一级 ⇒ Lv1 研究后 = 2000 MWh）。
 *  ★充电速率（mPowerInputCapacity）2026-11-07 定案改为「按里程碑档位固定上限」，不再乘 WT_TrackScale；
 *   具体数值见 WT_PowerStorageChargeCapMW（Lv1~4=200 / 5~8=500 / 9~12=1000 / 13~16=2500 / 17~20=5000 MW），
 *   与建筑本体共用，故此处不再保留 400 基类常量。 */
static constexpr float GPowerStorageBaseMWh = 1600.f;
// 前向声明：定义见下方 WT_TrackScale（与建筑缩放用同一函数，保证文案 = 实际值，不漂移）。
float WT_TrackScale(EWTUpgradeTrack Track, int32 Level, EWTFinalRoute Route);

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
    case 0:  return WT_LOCTEXT("Track_Storage",      "大容量仓库");
    case 1:  return WT_LOCTEXT("Track_PowerStorage", "储能单元");
    default: return WT_LOCTEXT("Track_PowerShard",   "强化能量碎片");
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
    if (Route == EWTFinalRoute::Boost)
        return WT_LOCTEXT("Route_FinalBoost", "最终强化 · 碎片");
    return WT_LOCTEXT("Route_FinalPlain", "最终强化 · 常规");
}

/**
 * ★0.2.36h（用户定案「奖励区，写说明和物品」）：每个节点在奖励区的**说明文字**。
 *
 * 数字只写能在代码里核实的：云端容量由 Remote_FactoryCloudQuota::TargetStacks(Level) 算出
 *（1.0.7 起改成等级系数 —— 基线 10 组 × (1.0 / 1.1 / 1.2 …)，末段碎片路线每级再 +0.1）。
 * ★第一条轨的说明必须把**四类站**都点名列出（用户 2026-10-07：「图一需要固体，液体，上传，
 * 下载都含进去」）—— 云端池是四类站共享的，只提固体上传站会误导。
 */
static FText WTRewardDescription(int32 ti, int32 Level, bool bFinal, EWTFinalRoute Route)
{
    if (bFinal)
    {
        FText Body;
        if (Route == EWTFinalRoute::Boost)
        {
            // ★第5层（阶段5 / 末段 Lv17~20）「强化路线」（碎片路线）的额外门槛：
            //   消耗的强化能量碎片 = 我们的 UWTUpgradePowerShardDescriptor，**不是**原版 Desc_CrystalShard；
            //   与 Lv1~16 补偿发放的是同一枚 mod 物品。数量 = 50 × GLevelMult[段内级-1]，
            //   与 Apply() 末段成本、设计表 §6.6 ④ / 节点14 的 50/100/200/300（合计 650）一致。
            const int32 ShardCost = 50 * GLevelMult[FMath::Clamp(Level - WT_Stage4MaxLevel, 1, 4) - 1];
            Body = FText::Format(
                WT_LOCTEXT("RewardDescFinalBoost",
                    "终段强化（碎片路线）：三条轨同时推进，效果为 1.5 倍。本等级额外消耗 {0} 片强化能量碎片——"
                    "即本模组的碎片（非原版能量碎片），与 Lv1~16 补偿发放的是同一枚物品。"
                    "消耗节奏：Lv17/18/19/20 分别为 50/100/200/300 片（合计 650 片）。"),
                FText::AsNumber(ShardCost));
        }
        else
        {
            Body = WT_LOCTEXT("RewardDescFinalPlain",
                "终段强化（常规路线）：三条轨沿原曲线同时推进。不消耗任何能量碎片。");
        }
        // ★LV17 一次性选择警示（用户要求：三条轨完成后、LV17 二选一前给予提示，防误触）
        if (Level == WT_FinalMinLevel)
        {
            const FText Warn = WT_LOCTEXT("RewardDescFinalWarn",
                "⚠ 重要：这是末段路线的「一次性最终选择」。选定后，Lv18~Lv20 将整段跟随本路线，"
                "且无法再切换到另一条（另一条会被永久锁定）。请确认后再研究。");
            return FText::Format(WT_LOCTEXT("RewardDescFinalWarnFmt", "{0}\n\n{1}"), Warn, Body);
        }
        return Body;
    }

    switch (ti)
    {
    case 0:
    {
        // ★1.0.7 容量改按等级系数（Lv2 = ×1.1、Lv3 = ×1.2 …）：文案直接给出「目标组数 + 倍数」。
        //   原版那句 "this level +2" 在新口径下已经不成立（Lv1 是 +0、末段碎片是 +2），
        //   写死增量必然说谎。bFinal 在上面已经提前 return，这里恒为非末段。
        const int32 Stacks = Remote_FactoryCloudQuota::TargetStacks(Level, /*bShard=*/false);

        // ★★★1.0.7 用户定案「MAM 现在不加上去」：总闸关着 ⇒ 每一级都不涨容量。
        //   此时绝不能沿用下面「容量系数 1.0 倍」那句（会读成「研究有收益但倍数是 1」），
        //   直接明说「固定 N 组、本阶段不随研究提升」—— 数字仍取自同一个 TargetStacks，
        //   与 unlock 段天然一致（闸开/闸关都不会出现文案和实际值不符）。
        if (!Remote_FactoryCloudQuota::WT_FEATURE_CLOUD_QUOTA_MAM)
        {
            return FText::Format(
                WT_LOCTEXT("RewardDescCapacityFixed",
                    "云端容量：中央仓库每个物品类型暂存 {0} 组，且暂不随研究提升。该仓库由四类站点共享——"
                    "固体上传站、流体上传站、固体下载站与流体下载站。"),
                FText::AsNumber(Stacks));
        }

        const int32 Percent = Remote_FactoryCloudQuota::FactorPercent(Level, /*bShard=*/false);
        const FText Factor  = FText::FromString(FString::Printf(TEXT("%.1f"),
                                  static_cast<double>(Percent) / 100.0));
        return FText::Format(
            WT_LOCTEXT("RewardDescCapacity",
                "云端容量：中央仓库每个物品类型现在可存 {0} 组（容量系数 {1} 倍）。该仓库由四类站点共享——"
                "固体上传站、流体上传站、固体下载站与流体下载站。你已放置的上传站与下载站会在研究完成后"
                "立即生效这组新容量——是实打实的提升，不只是账面数字。"),
            FText::AsNumber(Stacks), Factor);
    }
    case 1:
    {
        // ★储能单元（ti==1，Lv1~16 非末段）奖励区：写明每级**实际**储电量 / 充电速率上限。
        //   储电量：基数 1600 MWh × WT_TrackScale（非末段 = 1.25^Level，前移一级 ⇒ Lv1 研究后 = 2000 MWh）。
        //   充电速率上限：2026-11-07 定案改为「按里程碑档位固定上限」(WT_PowerStorageChargeCapMW)，
        //   不再随等级 ×1.25 增长，避免 Lv20≈34.7GW 任何电网都供不上、标称速率成摆设。
        const int32 MWh = WT_PowerStorageMWh(Level, EWTFinalRoute::None);
        const int32 MW  = WT_RoundToHundred(FMath::RoundToInt(WT_PowerStorageChargeCapMW(Level)));
        return FText::Format(
            WT_LOCTEXT("RewardDescBattery",
                "Super battery: researching this level raises stored energy to {0} MWh "
                "(+25% over the previous level). The charge-rate cap follows your milestone tier and "
                "is now {1} MW -- it stays fixed within a tier and only jumps when you reach a new "
                "milestone tier, so it always matches the surplus power your grid can actually spare "
                "(and never trips the grid). Batteries link up into one grid-wide buffer, and "
                "already-placed super batteries take this new capacity the moment research finishes -- "
                "the boost is real, not merely on paper."),
            FText::AsNumber(MWh), FText::AsNumber(MW));
    }
    default:
        // ★强化碎片轨（ti==2，Lv1~16）奖励区：写明「补偿的是我们的强化碎片 + 具体数量 + 最多3片」。
        //   原版能量碎片（Desc_CrystalShard）只是 §6.6 折算基准，MAM 实际发放的是我们的
        //   UWTUpgradePowerShardDescriptor。数量取自 §6.6 ③ 逐级表（WT_ShardGrantAt 经 Clamp(Lv,2,16)-1 取 index1，故 LV1 即发 38，前移定案）。
        //   每台机器最多装 3 片强化碎片（沿用原版 3 插槽），超频上限 400%（1/2/3 片 = 200/300/400%）。
        return FText::Format(
            WT_LOCTEXT("RewardDescShard",
                "强化能量碎片（本模组的碎片，不是原版能量碎片）：完成本研究补偿 {0} 片强化能量碎片。"
                "每台机器最多装 3 片，超频上限 400%（1/2/3 片分别对应 200%/300%/400%）。"
                "耗电仍沿用原版曲线。补偿到的碎片可立即装入你已放置的机器（每台最多 3 片、最高 400% 超频）——"
                "是直接、实打实的性能提升。"),
            FText::AsNumber(WT_ShardGrantAt(Level)));
    }
}

/**
 * ★2026-11-07（Bug A 修复）：奖励区只渲染 mUnlockName（BP_UnlockInfoOnly 控件不渲染 mUnlockDescription，
 * 已实测），原「提升多少」写在描述里玩家看不到。本函数把每轨当级的**关键提升量**浓缩成后缀，
 * 拼进奖励名（mUnlockName），使玩家在奖励区一眼看到「提升多少」。数值全部复用 WTRewardDescription
 * 的同一来源（TargetStacks / WT_TrackScale / WT_ShardGrantAt），保证「名字写的 = 实际给的」。
 */
static FText WT_RewardNameBoost(int32 ti, int32 Level, bool bFinal, EWTFinalRoute Route)
{
    if (bFinal)
    {
        return FText::GetEmpty();   // 末段已在本体文案说清，名字不再追加
    }
    switch (ti)
    {
    case 0:
    {
        const int32 Stacks = Remote_FactoryCloudQuota::TargetStacks(Level, /*bShard=*/false);
        const int32 Pct    = Remote_FactoryCloudQuota::FactorPercent(Level, /*bShard=*/false);
        return FText::Format(
            WT_LOCTEXT("BoostCap", "  (+{0}% → {1} 组)"),
            FText::AsNumber(Pct - 100), FText::AsNumber(Stacks));
    }
    case 1:
    {
        // ★2026-11-07 修正（用户实测）：奖励名写「本级的增量」而非「绝对总量」。
        //   LV1（建筑等级 2）相对未研究的 +400 MWh，而非显示绝对 2000 MWh。
        // ★2026-11-07 末 #46 修正：WT_TrackScale 内部把 Level<1 钳成 1（⇒ 1.25^1），若直接传
        //   FMath::Max(1, Level-1)，Lv1 时 Level-1=0 也被钳成 1，sPrev 与 s 同为 1.25 ⇒ 增量算成
        //   +0 MWh（实错，应 +400）。Lv1 的「上一级」是未研究基线（1.0×），单独用 1.0 作被减数。
        // 增量 = 本级取整后储电量 − 上一级取整后储电量（Lv1 以未研究基线 1600 为被减数），
        // 保证「奖励名写的」与「实际加成」完全一致（名字 = 实际）。均已按百位取整。
        const int32 MWh = WT_PowerStorageMWh(Level, EWTFinalRoute::None)
                        - (Level <= 1 ? WT_RoundToHundred((int32)GPowerStorageBaseMWh)
                                      : WT_PowerStorageMWh(Level - 1, EWTFinalRoute::None));
        return FText::Format(
            WT_LOCTEXT("BoostBatt", "  (+{0} MWh)"), FText::AsNumber(MWh));
    }
    default:
    {
        const int32 Grant = WT_ShardGrantAt(Level);
        return FText::Format(
            WT_LOCTEXT("BoostShard", "  (+{0} 片)"), FText::AsNumber(Grant));
    }
    }
}

// ================================================================================================
// ★0.2.43 奖励区「文字行」写入（反射）；★2026-11-05 修正目标对象
//
// 用户定案：MAM 奖励区要「图标在上、文字说明在下」。原版的做法：
//   * 文字/图标的**数据源在 unlock 条目自己身上** —— `UFGUnlockInfoOnly` 的
//     mUnlockName / mUnlockDescription / mUnlockIconBig / mUnlockIconSmall；
//   * 再把它挂进 schematic 的 `mUnlocks`，由原版蓝图把它画出来。
//
// ★★★旧注释（0.2.43 那版）把这两件事合成了「数据在 schematic 上」，是**错的**，已由实测推翻：
//   连续 56 条 `[research] reward text <= 0/4 field(s) written; readback="<no mUnlockName property>"`。
//   一手核实：`FGSchematic.h` 只有 mDisplayName(:206)/mDescription(:210)，四个 mUnlock* 全部在
//   `Unlocks/FGUnlockInfoOnly.h:22/26/30/34`。⇒ 必须写 `BP_UnlockInfoOnly_C` 实例（见 WT_ApplyRewardText）。
//
// ★为什么走反射而不是直接成员访问：这四个字段在 C++ 里是 protected，且它们既不在本项目
//   编译用的 FGSchematic.h 桩里、也不属于已拿到友谊的 UFGSchematic/UFGUnlockRecipe。
//   反射按名找 UPROPERTY，命中即可写；找不到就记日志、不崩（不阻断整棵树的装配）。
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

/** 把「奖励区文字」写到 **UFGUnlockInfoOnly 实例**上，并回读一条日志。
 *
 * ★★★2026-11-05 实测纠错（原写法的目标对象从一开始就是错的）：
 *   旧代码把目标写成 `this`（schematic），日志连续 56 条
 *     `[research] reward text <= 0/4 field(s) written; readback="<no mUnlockName property>"`
 *   —— 一个字段都没写进去。
 *   一手核实（SDK 头逐条）：
 *     · `FGSchematic.h` 的 UFGSchematic 只有 `mDisplayName`(:206) / `mDescription`(:210)，
 *       **没有** mUnlockName / mUnlockDescription / mUnlockIconBig / mUnlockIconSmall；
 *     · 这四个字段真正的主人是 `Unlocks/FGUnlockInfoOnly.h:22/26/30/34`
 *       的 **UFGUnlockInfoOnly**（UFGUnlock 子类，Blueprintable/abstract）。
 *   ⇒ 文字/图标必须写在 `mUnlocks` 里那条 `BP_UnlockInfoOnly_C` 的**实例**上，写 schematic 永远 0/4。
 *
 * 反射仍是唯一通路：四个字段在 C++ 里是 protected，且我们不想再开 AccessTransformer 友谊。
 * 它们都是 UPROPERTY ⇒ `FindPropertyByName` 在**实例**上照样命中。 */
static void WT_ApplyRewardText(UObject* Target, const FText& Name, const FText& Desc, UTexture2D* Icon)
{
    if (!Target)
    {
        return;
    }
    int32 Written = 0;
    Written += WT_SetRewardTextProp(Target, TEXT("mUnlockName"), Name) ? 1 : 0;
    Written += WT_SetRewardTextProp(Target, TEXT("mUnlockDescription"), Desc) ? 1 : 0;
    if (Icon)
    {
        Written += WT_SetRewardObjectProp(Target, TEXT("mUnlockIconBig"), Icon) ? 1 : 0;
        Written += WT_SetRewardObjectProp(Target, TEXT("mUnlockIconSmall"), Icon) ? 1 : 0;
    }

    FString ReadBack = TEXT("<no mUnlockName property>");
    if (FTextProperty* P =
            CastField<FTextProperty>(Target->GetClass()->FindPropertyByName(TEXT("mUnlockName"))))
    {
        const FText* Addr = static_cast<const FText*>(P->ContainerPtrToValuePtr<void>(Target));
        ReadBack = Addr ? Addr->ToString() : TEXT("<null>");
    }

    WT_WARNF_ONCE(TEXT("reward-text"), Target,
        TEXT("[research] reward text <= %d/4 field(s) written on '%s' "
             "(mUnlockName/mUnlockDescription/mUnlockIconBig/mUnlockIconSmall); readback=\"%s\""),
        Written, *Target->GetClass()->GetName(), *ReadBack.Left(96));
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

// ★2026-11-05：给研究树类用的 public 入口（见头文件注释）。
//   mSchematics / mRequireAllSchematicsToBePurchased 都是本类**继承来的 protected** 成员，
//   派生类成员函数通过派生类类型对象访问它们是标准 C++ 允许的，无需额外 AccessTransformer。
void UWT_SchematicPurchasedDep::WT_Configure(const TArray<TSubclassOf<UFGSchematic>>& InSchematics,
                                             bool bRequireAll)
{
    mSchematics.Empty();
    for (const TSubclassOf<UFGSchematic>& S : InSchematics)
    {
        if (S)
        {
            mSchematics.Add(S);
        }
    }
    mRequireAllSchematicsToBePurchased = bRequireAll;
}

// ================================================================================================
// ★2026-11-05 节点描述里的「前置条件 + 是否已达成」
//
// 为什么要拆成"登记 + 刷新"两步：
//   `Apply()` 在 **CDO 构造期** 跑，那时没有存档、没有玩家 ⇒ 算不出哪条已达成；
//   而节点面板要显示的是**当前存档**的达成状态。
//   所以 Apply 只登记清单（BaseDesc + Prereqs），真正写 `mDescription` 由世界侧 1s 定时器
//   调 `WT_RefreshPrereqText` 做，内容没变就不写（幂等）。
// ================================================================================================
namespace
{
    struct FWTDescEntry
    {
        FString                    BaseDesc;
        TArray<FWTSchematicPrereq> Prereqs;
        FString                    LastWritten;
    };

    TMap<UClass*, FWTDescEntry>& WT_DescTable()
    {
        static TMap<UClass*, FWTDescEntry> Table;
        return Table;
    }
}

void UWTUpgradeResearchBase::WT_RegisterPrereqText(UClass* SchematicClass, const FString& BaseDesc,
                                                   const TArray<FWTSchematicPrereq>& Prereqs)
{
    if (!SchematicClass)
    {
        return;
    }
    FWTDescEntry& E = WT_DescTable().FindOrAdd(SchematicClass);
    E.BaseDesc = BaseDesc;
    E.Prereqs  = Prereqs;
}

void UWTUpgradeResearchBase::WT_RefreshPrereqText(AFGSchematicManager* SM)
{
    if (!SM)
    {
        return;
    }

    for (TPair<UClass*, FWTDescEntry>& KV : WT_DescTable())
    {
        UClass* Cls = KV.Key;
        if (!Cls)
        {
            continue;
        }
        // ★必须转成**本类**指针：`mDescription` 是 UFGSchematic 的 protected 成员，
        //   通过基类指针访问在 C++ 里不合法（protected 只能经由派生类型对象访问）。
        UWTUpgradeResearchBase* CDO = Cast<UWTUpgradeResearchBase>(Cls->GetDefaultObject());
        if (!CDO)
        {
            continue;
        }

        FString Text = KV.Value.BaseDesc;
        if (KV.Value.Prereqs.Num() > 0)
        {
            Text += TEXT("\n\n【前置条件】");
            for (const FWTSchematicPrereq& P : KV.Value.Prereqs)
            {
                bool bMet = false;
                if (P.bRequireAll)
                {
                    bMet = P.Classes.Num() > 0;
                    for (UClass* C : P.Classes)
                    {
                        if (!C || !SM->IsSchematicPurchased(TSubclassOf<UFGSchematic>(C)))
                        {
                            bMet = false;
                            break;
                        }
                    }
                }
                else
                {
                    for (UClass* C : P.Classes)
                    {
                        if (C && SM->IsSchematicPurchased(TSubclassOf<UFGSchematic>(C)))
                        {
                            bMet = true;
                            break;
                        }
                    }
                }
                Text += FString::Printf(TEXT("\n%s %s%s"),
                                        bMet ? TEXT("✔") : TEXT("✘"),
                                        *P.Label,
                                        bMet ? TEXT("（已达成）") : TEXT(""));
            }
        }

        if (Text != KV.Value.LastWritten)
        {
            KV.Value.LastWritten = Text;
            CDO->mDescription = FText::FromString(Text);
        }
    }
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
        bFinal
            ? ((Route == EWTFinalRoute::Boost) ? EWTSchematicIconStyle::FinalBoost
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
    //   文字行 = **`BP_UnlockInfoOnly_C` 实例**，其 mUnlockName/mUnlockDescription/mUnlockIcon*
    //            **由本函数写入该实例自身**（★2026-11-05 修正：写 schematic 恒 0/4）。
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
    // 文字数据写在这条 unlock **实例自身**（反射，见本文件顶部的 WT_ApplyRewardText）；
    // ★2026-11-05 纠正：不是写在 schematic 上（写 schematic 恒 0/4，已实测）。
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

                // ★★★2026-11-05：文字/图标写在**这条 unlock 实例**上（不是 schematic 上）。
                //   见 WT_ApplyRewardText 顶部的实测纠错：写 schematic 恒 0/4。
                // ★2026-11-07（Bug A）：奖励区控件只渲染 mUnlockName（不渲染描述），原「提升多少」
                //   写在描述里玩家看不到 ⇒ 把关键提升量拼进奖励名（mUnlockName），一眼可见。
                const FText RewardName = FText::Format(
                    WT_LOCTEXT("RewardNameFmt", "{0}{1}"),
                    mDisplayName, WT_RewardNameBoost(ti, Level, bFinal, Route));
                WT_ApplyRewardText(Info, RewardName,
                                   WTRewardDescription(ti, Level, bFinal, Route),
                                   Cast<UTexture2D>(mSchematicIcon.GetResourceObject()));
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
    //
    // ★1.0.7 改口径（用户 2026-10-27 定案：「当前初始是 1000（铁板）… MAM 研究 2 级 ×1.1 倍、
    //   3 级 ×1.2 倍如此」）：**不再用原版那套每级 +N 组**，改成「固定基线 × 等级系数」。
    //   目标组数 = Remote_FactoryCloudQuota::TargetStacks(Level)：
    //     未研究 10 组 | Lv1 10 | Lv2 11 | Lv3 12 … Lv16 25 | 末段常规 26/27/28/29、
    //     碎片路线 27/28/29/30（原版碎片路线每级多 +1 组 ⇒ 系数 +0.1 = 10 组基线下正好 1 组）。
    //   unlock 是**累加**语义，所以这里写的是「本级目标 − 上一级目标」的差额，而不是目标本身。
    //   Lv1 的系数是 1.0（用户口径里 1 级不带容量增益）⇒ 增量为 0，本级不发放配额 unlock。
    // ★累加语义安全性（§5.2b 已论证）：Lv17 两条路线二选一、永不同时生效 ⇒ 不存在双份累加。
    // ★实例复用：mQuotaUnlock 是 UPROPERTY（GC 追踪、CDO 持有），Apply 重跑复用同一实例；
    //   主路径 NewObject 自原版 BP_UnlockCentralStorageItemLimit_C（渲染兼容 + Apply 走真机代码），
    //   BP 解析不到才落 UWTUnlockCloudQuota 原生兜底。
    if (bFinal || (ti == 0))   // 末段三轨一起涨 ⇒ 容量在内；非末段只有容量轨（ti=0）
    {
        // ★1.0.7 等级系数（见 Remote_FactoryCloudQuota.h）：目标组数由等级算出，
        //   unlock 记录的是「本级目标 − 上一级目标」的差额（累加语义）。
        //   ★上一级的系数必须用**同一条路线**算：碎片路线的 +0.1 只在 Lv17 首次生效
        //   （之后每级和常规一样只涨 1 组）。若 Lv18~20 拿「上一级常规值」当被减数，
        //   每级差额都会算成 +2，累加到 Lv20 就变成 33 组（目标 30）—— 深①穷举抓出来的。
        //   Lv17 的上一级是 Lv16（还不属于末段）⇒ 那一级确实是 +2。
        //   ★★★用户 2026-10-27「MAM 现在不加上去」：总闸 `WT_FEATURE_CLOUD_QUOTA_MAM=false`
        //   让 TargetStacks 恒 = 10 ⇒ TargetNow == TargetPrev ⇒ Increase 恒 0 ⇒ 一个配额 unlock
        //   都不发（下面 `Increase > 0` 才 Add 到 mUnlocks）。本段其余逻辑不必再判闸。
        const bool  bShardRoute = (bFinal && Route == EWTFinalRoute::Boost);
        const int32 TargetNow   = Remote_FactoryCloudQuota::TargetStacks(Level, bShardRoute);
        const bool  bPrevShard  = bShardRoute
                                && (Level - 1) >= Remote_FactoryCloudQuota::WT_FinalFirstLevel;
        const int32 TargetPrev  = (Level <= 1)
            ? Remote_FactoryCloudQuota::WT_InitialStacks
            : Remote_FactoryCloudQuota::TargetStacks(Level - 1, bPrevShard);
        const int32 Increase    = FMath::Max(0, TargetNow - TargetPrev);

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
            // ★1.0.7 Lv1 的等级系数是 1.0 ⇒ Increase = 0：不发放配额 unlock（否则玩家会在
            //   解锁列表里看到一条「云端容量」却什么都加不了的奖励）。
            if (bSet && Increase > 0)
            {
                mUnlocks.Add(mQuotaUnlock);
            }
            else if (!bSet)
            {
                WT_WARNF(TEXT("[research] mItemStackLimitIncrease reflection missing -- cloud quota skipped."));
            }
        }
        else
        {
            WT_WARNF(TEXT("[research] cloud quota unlock unavailable (BP class did not resolve) -- cloud quota skipped."));
        }

        // 诊断锚点（WT_LOGF 被 WT_LOG_VERBOSE=false 剔除，必须用 WT_WARNF）。
        // 期望（闸开）：Lv1 -> 10 组(+0) | Lv2 -> 11(+1) | Lv16 -> 25(+1) | 末段常规 Lv20 -> 29(+1) / 碎片 30(+2)
        // 期望（闸关）：任意级 -> 10 组(+0, mam-scaling=off)
        WT_WARNF(TEXT("[cloud-quota] research Lv%d%s -> %d stacks/class (factor %d%%, unlock +%d, mam-scaling=%s)"),
                 Level, bShardRoute ? TEXT(" shard") : TEXT(""),
                 TargetNow, Remote_FactoryCloudQuota::FactorPercent(Level, bShardRoute), Increase,
                 Remote_FactoryCloudQuota::WT_FEATURE_CLOUD_QUOTA_MAM ? TEXT("on") : TEXT("off"));
    }

    // ---- [3] 碎片补偿发放（★0.2.47 设计表 §6.6 —— 用户定案「根据全图能量蛞蝓计算，在 MAM 中补偿」）----
    // 产能轨 Lv1~16 每级随研究一次性发放强化能量碎片；Lv17~20 不发放（末段改为碎片路线额外
    // 消耗 50/100/200/300 —— 已在上方成本段实现）。发货走 UWTUnlockGiveItemShard（商店同款）。
    //
    // ★§6.6 具体补偿机制（设计表原文，改收集率/装机目标后由 F:\Workbuddy\SMLDev\_wt_compensation.py 复算）：
    //   ⚠ 两个「碎片」要分清：
    //     · 原版能量碎片（Desc_CrystalShard）—— 蛞蝓按原版配方造的，仅是**基准量**，不直接发放；
    //     · 我们的强化能量碎片（UWTUpgradePowerShardDescriptor）—— 本 mod 改成 MAM 产物，**补偿发的就是它**。
    //   ① 蛞蝓（原版 collectible）→ 原版配方 → 原版能量碎片（仅作折算基准）：
    //        蓝 596×1@80% = 477 | 黄 389×2@60% = 466 | 紫 257×5@40% = 515 ⇒ 现实 1458 原版碎片
    //        扣「能量读数」研究占用 9 ⇒ 现实可支配 1449 原版碎片（满收集 2650）。
    //   ② 把基准原版碎片按「2 原版→1 强化」折算成**我们的强化能量碎片**，再加保障额度：
    //        损耗补偿 = 1449÷2 = 724（我们的强化碎片；2 原版→1 强化，每合成净损 1 原版）；
    //        保障额度 = 876（目标装机 1500 + 末段门槛 650 + 周转 174 − 自合成 724，单位均为我们的强化碎片）；
    //        ⇒ 补偿池 = 724 + 876 = 1600 **我们的强化能量碎片**，随 LV1~16 逐级发放（§6.6 ③）。
    //   ⑥ 装机核算公式（复算口径）：补偿池 = 研究门槛 + 目标装机台数 + 周转余量 − 724。
    //   ★终点：下方 mGrantUnlock 发的是 UWTUpgradePowerShardDescriptor（我们的），不是原版 Desc_CrystalShard。
    // 下方常量把这套口径编译期固化：改收集率/装机目标时按公式重算，本段发放表随之调整。
    static constexpr int32 WT_Shards_BaselineVanilla = 1449;                                // ① 基准：现实可支配原版能量碎片（仅折算用）
    static constexpr int32 WT_Shards_CompLoss        = WT_Shards_BaselineVanilla / 2;       // ② 折算成我们的强化碎片 = 724
    static constexpr int32 WT_Shards_CompBuffer      = 876;                                 // ② 保障额度（我们的强化碎片）
    static constexpr int32 WT_Shards_CompPool        = WT_Shards_CompLoss + WT_Shards_CompBuffer; // = 1600 我们的强化碎片
    static_assert(WT_Shards_CompPool == 1600, "§6.6 我们的强化碎片补偿池必须 = 1600");
    // ---- [3] 碎片补偿发放（★0.2.47 设计表 §6.6）----
    // ★2026-11-07 改为「研究完成时显式发放」：UFGUnlockGiveItem 在 MAM 路径下只是容器
    //   （SDK 头注释 "Doesn't actually do anything on unlock"），研究系统不读它发物品，
    //   光塞进 mUnlocks 进不了背包。改为登记「schematic 类 → 发放量」，由刷新钩子
    //   WT_GivePendingShardCompensation 在研究完成时显式 AddStack 到玩家背包（幂等去重）。
    if (!bFinal && ti == 2)
    {
        const int32 Grant = WT_ShardGrantAt(Level);   // Clamp(Lv,2,16)-1：LV1 取 index1 = 38（前移定案，机制为 Clamp 非数组前移）
        WT_RegisterShardGrant(this->GetClass(), Grant);
        WT_WARNF(TEXT("[wt-shard] registered Lv%d compensation: %d enhanced shards (granted on research completion)."),
                 Level, Grant);
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

            // ④ 末段路线门禁：与「其它所有路线」的 Lv17 互斥（二选一）
            static const EWTFinalRoute AllFinal[] = { EWTFinalRoute::Plain, EWTFinalRoute::Boost };
            TArray<TSubclassOf<UFGSchematic>> OtherRoute;
            for (EWTFinalRoute R : AllFinal)
            {
                if (R == Route) continue;
                if (TSubclassOf<UFGSchematic> OtherCls = WT_FinalResearchClassFor(R, Level))
                {
                    OtherRoute.Add(OtherCls);
                }
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

    // ★★★2026-11-05 用户要求：「不可研究也写上说明原因 / 把前置条件写出来 / 已达成也标注（已达成）」。
    //   `Apply()` 在 CDO 期跑，此时没有存档、没有玩家 ⇒ 算不出哪条已达成。
    //   所以这里只**登记**「基础描述 + 前置条件清单」，达成状态交给世界侧 1s 定时器
    //   调 `WT_RefreshPrereqText` 刷新（与方块推进共用同一个 ticker）。
    {
        TArray<FWTSchematicPrereq> Prereqs;

        auto AddOne = [&Prereqs](const FString& Label, UClass* C, bool bRequireAll = true)
        {
            if (!C)
            {
                return;
            }
            FWTSchematicPrereq E;
            E.Label       = Label;
            E.bRequireAll = bRequireAll;
            E.Classes.Add(C);
            Prereqs.Add(E);
        };

        if (bFinal)
        {
            if (Level == WT_FinalMinLevel)
            {
                // 末段入口 = 任一轨走到 Lv16（OR 语义），故三个类放同一条、bRequireAll=false
                FWTSchematicPrereq E;
                E.Label       = FString::Printf(TEXT("任一轨完成阶段 Ⅳ（Lv%d）"), WT_Stage4MaxLevel);
                E.bRequireAll = false;
                for (int32 t = 0; t < 3; ++t)
                {
                    if (UClass* C = WT_ResearchClassFor(GWTTracks[t], WT_Stage4MaxLevel))
                    {
                        E.Classes.Add(C);
                    }
                }
                Prereqs.Add(E);

                // 末段二选一互斥：对每条「其它路线」各挂一条未购买前置
                static const EWTFinalRoute AllFinal[] = { EWTFinalRoute::Plain, EWTFinalRoute::Boost };
                for (EWTFinalRoute R : AllFinal)
                {
                    if (R == Route) continue;
                    if (UClass* C = WT_FinalResearchClassFor(R, Level))
                    {
                        // ★措辞是「措辞即判据」：刷新器只在前面加 ✔/✘ 和（已达成），文案本身不变。
                        //   Lv17 是末段路线唯一决策点，故其前置条件文案额外点明「选定后不可更改」，防误触。
                        FWTSchematicPrereq X;
                        X.Label = (Level == WT_FinalMinLevel)
                            ? FString::Printf(TEXT("⚠ 末段路线一次性选择（选定后 Lv18~20 不可更改）：另一条末段路线（Lv%d）未被购买"), Level)
                            : FString::Printf(TEXT("二选一：另一条末段路线（Lv%d）未被购买"), Level);
                        X.Classes.Add(C);
                        Prereqs.Add(X);
                    }
                }
            }
            else
            {
                AddOne(FString::Printf(TEXT("本路线 Lv%d"), Level - 1),
                       WT_FinalResearchClassFor(Route, Level - 1));
            }
        }
        else if (InStage > 1)
        {
            AddOne(FString::Printf(TEXT("本轨 Lv%d"), Level - 1), WT_ResearchClassFor(Track, Level - 1));
        }
        else if (Stage > 1)
        {
            // 阶段同步门禁：上一阶段整层（单轨制下 = 本轨 4 级；三轨并行 = 12 级）
            if (WT_bSingleTrackMode)
            {
                TArray<TSubclassOf<UFGSchematic>> PrevStage;
                WT_CollectTrackStageSchematics(Track, Stage - 1, PrevStage);
                FWTSchematicPrereq E;
                E.Label = FString::Printf(TEXT("本轨阶段 %d 全部 %d 级"), Stage - 1, PrevStage.Num());
                for (const TSubclassOf<UFGSchematic>& S : PrevStage)
                {
                    if (UClass* C = S.Get())
                    {
                        E.Classes.Add(C);
                    }
                }
                Prereqs.Add(E);
            }
            else
            {
                TArray<TSubclassOf<UFGSchematic>> PrevStage;
                WT_CollectStageSchematics(Stage - 1, PrevStage);
                FWTSchematicPrereq E;
                E.Label = FString::Printf(TEXT("阶段 %d 全部 %d 级（本层 3 个方块）"), Stage - 1, PrevStage.Num());
                for (const TSubclassOf<UFGSchematic>& S : PrevStage)
                {
                    if (UClass* C = S.Get())
                    {
                        E.Classes.Add(C);
                    }
                }
                Prereqs.Add(E);
            }
        }
        else
        {
            AddOne(TEXT("HUB T1 里程碑「智能工厂」已购买"), URemote_FactoryMilestone::StaticClass());
        }

        WT_RegisterPrereqText(GetClass(), mDescription.ToString(), Prereqs);
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

bool WT_IsTrackResearched(UWorld* World, EWTUpgradeTrack Track)
{
    if (!World)
    {
        return false;
    }
    const AFGSchematicManager* SM = AFGSchematicManager::Get(World);
    if (!SM)
    {
        return false;
    }
    // 末段 Lv17~20：三轨合并，任一路线买过即算研究过
    for (int32 L = WT_MaxUpgradeLevel; L >= WT_FinalMinLevel; --L)
    {
        const TSubclassOf<UFGSchematic> P = WT_FinalResearchClassFor(EWTFinalRoute::Plain, L);
        const TSubclassOf<UFGSchematic> B = WT_FinalResearchClassFor(EWTFinalRoute::Boost, L);
        if ((P && SM->IsSchematicPurchased(P)) || (B && SM->IsSchematicPurchased(B)))
        {
            return true;
        }
    }
    // Lv1~16：本轨自己的链，任一级买过即算
    for (int32 L = 1; L <= WT_Stage4MaxLevel; ++L)
    {
        const TSubclassOf<UFGSchematic> C = WT_ResearchClassFor(Track, L);
        if (C && SM->IsSchematicPurchased(C))
        {
            return true;
        }
    }
    return false;
}

float WT_TrackScale(EWTUpgradeTrack Track, int32 Level, EWTFinalRoute Route)
{
    if (Level < 1) Level = 1;

    const float Growth =
        (Track == EWTUpgradeTrack::Storage)      ? 1.10f :
        (Track == EWTUpgradeTrack::PowerStorage) ? 1.25f :
                                                   1.14f;

    const float PlainScale = FMath::Pow(Growth, (float)Level);

    // 常规路线 / Lv1~16：原曲线
    if (Route != EWTFinalRoute::Boost || Level <= WT_Stage4MaxLevel)
    {
        return PlainScale;
    }

    // ★碎片路线 Lv17~20：以 Lv16 为基点做 1.5 倍**增量**外推（设计表 §6.1 末段双路线）
    const float Base16 = FMath::Pow(Growth, (float)WT_Stage4MaxLevel);
    return Base16 + 1.5f * (PlainScale - Base16);
}

float WT_PowerStorageChargeCapMW(int32 Level)
{
    // ★2026-11-07 充电速率定案：按里程碑档位固定上限，不再随等级 ×1.25 暴涨。
    // 阶段 = (Level-1)/4：Lv1~4->0 … Lv17~20->4。Level<1 按 1 处理（与 WT_TrackScale 一致）。
    const int32 Stage = FMath::Clamp((FMath::Max(Level, 1) - 1) / 4, 0, 4);
    static constexpr float CapByStage[] = { 200.f, 500.f, 1000.f, 2500.f, 5000.f };
    return CapByStage[Stage];
}

int32 WT_PowerStorageMWh(int32 Level, EWTFinalRoute Route)
{
    // ★2026-11-08 百位取整：与实际建筑加成共用同一公式（1600 × 等级倍率），仅在此处统一取整，
    //   保证「奖励名写的 = 奖励描述写的 = 建筑实际给的」三者完全一致。
    const float s = WT_TrackScale(EWTUpgradeTrack::PowerStorage, Level, Route);
    return WT_RoundToHundred(FMath::RoundToInt(GPowerStorageBaseMWh * s));
}

float WT_AcceleratorBoostAtLevel(int32 Level, EWTFinalRoute Route)
{
    // 与 Remote_FactoryUpgrades::MaxProductionBoost() 的封顶必须保持一致。
    const auto PlainBoost = [](int32 L)
    {
        return FMath::Clamp(2.0f * FMath::Pow(1.14f, (float)L), 1.0f, 30.0f);
    };

    const float Plain = PlainBoost(FMath::Max(Level, 1));
    if (Route != EWTFinalRoute::Boost || Level <= WT_Stage4MaxLevel)
    {
        return Plain;
    }
    const float Base16 = PlainBoost(WT_Stage4MaxLevel);
    return Base16 + 1.5f * (Plain - Base16);
}
