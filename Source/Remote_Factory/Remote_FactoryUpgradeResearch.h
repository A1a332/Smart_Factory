#pragma once

#include "CoreMinimal.h"
#include "FGSchematic.h"
#include "Remote_FactoryLoc.h"
#include "Remote_FactoryLog.h"
#include "Remote_FactoryUpgradeRegistry.h"   // UWTUpgradeStorageRecipe / PowerStorage / Accelerator
#include "AvailabilityDependencies/FGSchematicPurchasedDependency.h"
#include "Remote_FactoryUpgradeResearch.generated.h"

/**
 * Concrete (non-abstract) subclass of the abstract UFGSchematicPurchasedDependency.
 *
 * Satisfactory 1.0 marks the base class abstract (UCLASS(Blueprintable, abstract)), so it
 * cannot be instantiated with NewObject directly — doing so tripped the
 *   "Creating an instance of an abstract class is not allowed!"
 * assert at world-load. This thin subclass inherits AreDependenciesMet() plus the
 * mSchematics / mRequireAllSchematicsToBePurchased members, giving us an instantiable
 * type to author the per-level "previous level must be purchased" dependency chain.
 */
UCLASS()
class REMOTE_FACTORY_API UWT_SchematicPurchasedDep : public UFGSchematicPurchasedDependency
{
    GENERATED_BODY()
};

/**
 * Concrete (non-abstract) subclass of UFGSchematicNotPurchasedDependency —— 末段「Lv17 二选一」用。
 *
 * ★2026-10-07 核实：`UFGSchematicNotPurchasedDependency` **不是**独立头文件，它定义在
 * `AvailabilityDependencies/FGSchematicPurchasedDependency.h` 里，且**继承自**
 * `UFGSchematicPurchasedDependency`（同样是 UCLASS(Blueprintable, abstract)）——
 * 所以 `mSchematics` / `mRequireAllSchematicsToBePurchased` 都是白送的。
 *
 * AreDependenciesMet() 取反：mSchematics 里列出的 schematic **必须尚未被购买**才满足。
 * 我们把 FinalBoost_L17 放进 FinalPlain_L17 的这个依赖里，反向同理 ⇒ 两条互斥。
 */
UCLASS()
class REMOTE_FACTORY_API UWT_SchematicNotPurchasedDep : public UFGSchematicNotPurchasedDependency
{
    GENERATED_BODY()
};

/**
 * The three upgrade tracks that the Remote Factory progressively unlocks through
 * the MAM (分子分析机) research tree, "Dyson-sphere style": each track has 20 levels,
 * level 1 costs only the vanilla milestone raw materials, and every further level
 * multiplies the building's numbers. The level is the highest *purchased* research
 * node for that track, so it is saved natively by the game's research system.
 */
UENUM()
enum class EWTUpgradeTrack : uint8
{
    Storage,       // 超大容量仓库  (grid)
    PowerStorage,  // 储能单元      (MWh + charge rate)
    Accelerator,   // 产能加速器    (production boost)
};

/** Hard cap chosen with the user: 20 levels per track is "deep enough, never feels walled". */
constexpr int32 WT_MaxUpgradeLevel = 20;

/** 阶段 1~4 覆盖 Lv1~Lv16（3 轨各自 4 个节点 x 4 级）—— 见 WT_MAM_研究树结构图.svg。 */
constexpr int32 WT_Stage4MaxLevel = 16;

/** 末段 Lv17~20：三条轨合并，由两条互斥路线承担。 */
constexpr int32 WT_FinalMinLevel = 17;

/**
 * ★2026-10-07 末段双路线（用户定案：「17 级使用了能量碎片研究，则剩余都需要消耗能量碎片，反之一样」）。
 *
 * Lv1~Lv16 每级只属于一条轨，Route 恒为 None；
 * Lv17~Lv20 三轨合并成「最终强化」节点，玩家在 Lv17 二选一：
 *   Plain = 常规路线（不消耗碎片，走原曲线）
 *   Boost = 碎片路线（每级额外消耗强化能量碎片，效果 x1.5 外推）
 * 两条路线在 MAM 树里各占一个方块、**并列可见**；Lv17 互相锁定，
 * 买了一条另一条整段永久失效（Lv18~20 各自只依赖本路线上一级）。
 */
UENUM()
enum class EWTFinalRoute : uint8
{
    None  = 0,   // Lv1~Lv16：普通轨内节点
    Plain = 1,   // 常规路线（UWTUpg_FinalPlain_*）
    Boost = 2,   // 碎片路线（UWTUpg_FinalBoost_*）
};

/**
 * Base class for the 56 generated research schematics
 * (3 tracks x 4 stages x 4 levels = 48，加末段 2 路线 x 4 级 = 8).
 *
 * Every write in Apply() touches UFGSchematic's protected members through `this`
 * (standard C++ protected access for a derived class) -- so no AccessTransformers
 * friend is needed for UFGSchematic itself. The only extra friends required are for
 * writing UFGUnlockRecipe::mRecipes and UFGSchematicPurchasedDependency's members,
 * which live on *separate* objects we construct. See Config/AccessTransformers.ini.
 */
UCLASS()
class REMOTE_FACTORY_API UWTUpgradeResearchBase : public UFGSchematic
{
    GENERATED_BODY()
public:
    UWTUpgradeResearchBase();

    /**
     * Configure this schematic as a research node.
     *
     * @param Track  Lv1~16 = 所属轨；Lv17~20 无意义（传 Storage，显示名走 Route）
     * @param Level  全局等级 1~20
     * @param Route  Lv1~16 传 None；Lv17~20 传 Plain / Boost
     */
    void Apply(EWTUpgradeTrack Track, int32 Level, EWTFinalRoute Route = EWTFinalRoute::None);

private:
    // ★0.2.47 运行时 unlock 实例（UPROPERTY ⇒ GC 追踪、CDO 持有、跨世界复用）。
    //   Apply 每次世界加载重跑一次：复用同一实例 = 不泄漏、mUnlocks 重建后不重复。
    //   （[0]/[1] 的配方/文字行是每次 NewObject + AddToRoot，每次 Apply 泄漏一个钉死对象 ——
    //     既有行为本轮不动；新增的两处用成员持有，走 GC 正道。）

    /** 云端配额 unlock（容量轨 Lv1~16 + 末段两条路线；实例可能是原版 BP 类，故按基类存）。 */
    UPROPERTY(Transient)
    TObjectPtr<class UFGUnlock> mQuotaUnlock;

    /** 碎片补偿发货 unlock（产能轨 Lv1~16，§6.6 逐级表）。 */
    UPROPERTY(Transient)
    TObjectPtr<class UWTUnlockGiveItemShard> mGrantUnlock;
};

// Declares one generated research class. The ctor forwards (Track, Level) to Apply().
// Macro is defined here so the generated .gen.h (included below) can expand it.
#define WT_RCLASS(Cls, Trk, Lvl) \
    UCLASS() \
    class REMOTE_FACTORY_API Cls : public UWTUpgradeResearchBase \
    { \
        GENERATED_BODY() \
    public: \
        Cls(); \
    };

// Defines the constructor body for one generated research class.
//
// ★ CRITICAL: the constructor MUST stay content-free. Apply() loads game assets
// (StaticLoadClass) and creates subobjects (NewObject) — both are ILLEGAL inside a
// UObject/CDO constructor and trigger the fatal
//   "NewObject with same name can't be used to create default subobjects
//    (Inside of UObject derived class constructor)"
// The (Track, Level) data is applied later, once per CDO, by WT_ConfigureAllResearch()
// which runs at the GWM CONSTRUCTION phase (content mounted, non-constructor context).
// This mirrors the milestone pattern (URemote_FactoryMilestone::InstallCostCategoryAndUnlock
// is likewise deferred to ApplyMilestoneData()).
#define WT_RIMPL(Cls, Trk, Lvl) \
    Cls::Cls() \
    { \
    }

// >>> AUTO-GEN WT RESEARCH CLASSES START >>>
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Cap_S1_L01 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Cap_S1_L01();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Cap_S1_L02 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Cap_S1_L02();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Cap_S1_L03 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Cap_S1_L03();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Cap_S1_L04 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Cap_S1_L04();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Cap_S2_L05 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Cap_S2_L05();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Cap_S2_L06 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Cap_S2_L06();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Cap_S2_L07 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Cap_S2_L07();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Cap_S2_L08 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Cap_S2_L08();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Cap_S3_L09 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Cap_S3_L09();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Cap_S3_L10 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Cap_S3_L10();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Cap_S3_L11 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Cap_S3_L11();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Cap_S3_L12 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Cap_S3_L12();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Cap_S4_L13 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Cap_S4_L13();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Cap_S4_L14 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Cap_S4_L14();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Cap_S4_L15 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Cap_S4_L15();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Cap_S4_L16 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Cap_S4_L16();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Sto_S1_L01 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Sto_S1_L01();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Sto_S1_L02 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Sto_S1_L02();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Sto_S1_L03 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Sto_S1_L03();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Sto_S1_L04 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Sto_S1_L04();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Sto_S2_L05 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Sto_S2_L05();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Sto_S2_L06 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Sto_S2_L06();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Sto_S2_L07 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Sto_S2_L07();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Sto_S2_L08 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Sto_S2_L08();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Sto_S3_L09 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Sto_S3_L09();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Sto_S3_L10 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Sto_S3_L10();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Sto_S3_L11 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Sto_S3_L11();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Sto_S3_L12 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Sto_S3_L12();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Sto_S4_L13 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Sto_S4_L13();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Sto_S4_L14 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Sto_S4_L14();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Sto_S4_L15 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Sto_S4_L15();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Sto_S4_L16 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Sto_S4_L16();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Shd_S1_L01 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Shd_S1_L01();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Shd_S1_L02 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Shd_S1_L02();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Shd_S1_L03 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Shd_S1_L03();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Shd_S1_L04 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Shd_S1_L04();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Shd_S2_L05 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Shd_S2_L05();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Shd_S2_L06 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Shd_S2_L06();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Shd_S2_L07 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Shd_S2_L07();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Shd_S2_L08 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Shd_S2_L08();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Shd_S3_L09 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Shd_S3_L09();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Shd_S3_L10 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Shd_S3_L10();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Shd_S3_L11 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Shd_S3_L11();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Shd_S3_L12 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Shd_S3_L12();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Shd_S4_L13 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Shd_S4_L13();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Shd_S4_L14 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Shd_S4_L14();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Shd_S4_L15 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Shd_S4_L15();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_Shd_S4_L16 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_Shd_S4_L16();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_FinalPlain_L17 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_FinalPlain_L17();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_FinalPlain_L18 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_FinalPlain_L18();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_FinalPlain_L19 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_FinalPlain_L19();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_FinalPlain_L20 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_FinalPlain_L20();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_FinalBoost_L17 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_FinalBoost_L17();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_FinalBoost_L18 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_FinalBoost_L18();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_FinalBoost_L19 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_FinalBoost_L19();
};
UCLASS()
class REMOTE_FACTORY_API UWTUpg_FinalBoost_L20 : public UWTUpgradeResearchBase
{
    GENERATED_BODY()
public:
    UWTUpg_FinalBoost_L20();
};
// <<< AUTO-GEN WT RESEARCH CLASSES END <<<

/** (Track, Level) -> schematic class for Lv1~Lv16, or nullptr. Defined in the generated .cpp. */
TSubclassOf<UFGSchematic> WT_ResearchClassFor(EWTUpgradeTrack Track, int32 Level);

/** (Route, Level) -> 末段 schematic class for Lv17~Lv20, or nullptr. Defined in the generated .cpp. */
TSubclassOf<UFGSchematic> WT_FinalResearchClassFor(EWTFinalRoute Route, int32 Level);

/** All 56 research classes, for registration in URemote_FactoryGameWorldModule::mSchematics. */
TArray<TSubclassOf<UFGSchematic>> WT_GetAllUpgradeResearch();

/**
 * Populate every research schematic's CDO (cost / category / unlock / dependency) by calling
 * Apply(Track, Level) on each. MUST be invoked at a SAFE time — the GWM CONSTRUCTION phase,
 * after content is mounted but before SML reads mSchematics at INITIALIZATION. Never from a
 * constructor. Idempotent (process-static guard) so repeated worlds don't leak rooted subobjects.
 */
void WT_ConfigureAllResearch();

/** 全局等级 -> 阶段号（Lv1~4 -> 1、Lv5~8 -> 2、Lv9~12 -> 3、Lv13~16 -> 4、Lv17~20 -> 5）。 */
int32 WT_StageForLevel(int32 Level);

/** Highest purchased level for a track (>= 1). Lv17~20 三轨合并，两条路线任意一条都算推进。 */
int32 WT_GetTrackLevel(UWorld* World, EWTUpgradeTrack Track);

/** 末段两条路线里玩家实际买下的那条；尚未做末段研究时返回 None。 */
EWTFinalRoute WT_GetFinalRoute(UWorld* World);

/**
 * Multiplier applied to a track's level-1 base value at the given level.
 * 碎片路线（Boost）在 Lv17~20 走 1.5 倍增量外推，所以必须把路线一起传进来。
 */
float WT_TrackScale(EWTUpgradeTrack Track, int32 Level, EWTFinalRoute Route = EWTFinalRoute::None);

/** Accelerator production boost (clamped) at a given level, route-aware（同上）。 */
float WT_AcceleratorBoostAtLevel(int32 Level, EWTFinalRoute Route = EWTFinalRoute::None);
