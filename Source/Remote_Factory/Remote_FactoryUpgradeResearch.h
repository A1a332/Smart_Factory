#pragma once

#include "CoreMinimal.h"
#include "FGSchematic.h"
#include "Remote_FactoryLoc.h"
#include "Remote_FactoryLog.h"
#include "Remote_FactoryUpgradeRegistry.h"   // UWTUpgradeStorageRecipe / PowerStorage / Accelerator
#include "AvailabilityDependencies/FGSchematicPurchasedDependency.h"
#include "AvailabilityDependencies/FGAvailabilityDependency.h"
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

public:
    /** 从**本类之外**（研究树类等）配置依赖清单。
     *  ★为什么需要它：`mSchematics` / `mRequireAllSchematicsToBePurchased` 是基类的
     *  **protected** 成员，外部类访问需要 AccessTransformer 友谊；目前只给
     *  `UWTUpgradeResearchBase` 开过（见 Config/AccessTransformers.ini）。
     *  研究树那边要复用同一个依赖类型，因此在这里开一个 public 入口，避免再加一条友谊
     *  （友谊不继承、每加一次都要重新烤模块）。
     *  派生类成员函数访问继承来的 protected 成员是合法的，故实现就放在本类里。 */
    void WT_Configure(const TArray<TSubclassOf<UFGSchematic>>& InSchematics, bool bRequireAll);
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
 * ★2026-11-05 「非空但恒为真」的可用性依赖 —— **研究树解锁**专用。
 *
 * 为什么必须存在这个类（两条实测结论把路都堵死了，只剩中间这条）：
 *   ① `mUnlockDependencies` **不能为空**：原版 `BPW_MAMTree_ListButtonInfo:SetContent`
 *      第一行就是 `GetUnlockDependencies()[0]`，数组空 ⇒ 蓝图抛异常、函数中断 ⇒
 *      研究树按钮只剩空壳，且顺带打出 `FGSchematic::GetType: class was nullpeter`。
 *   ② 也不能挂「玩家必须已购某张卡」（我们上一版挂 T1 里程碑）：没买卡的存档整棵树
 *      被判为 Locked ⇒ 图区全灰、什么都点不了。
 *      节点自身的入口门禁是**另一套**（`UFGSchematic::mSchematicDependencies`），
 *      该拦的地方照拦，用不着让整棵树背这个锅。
 * ⇒ 需要一个「非空、但永远成立」的依赖对象。
 *
 * 为什么不能直接 NewObject<UFGAvailabilityDependency>：
 *   基类是 `UCLASS(DefaultToInstanced, abstract, editinlinenew, ...)`，abstract ⇒
 *   NewObject 会触发 "Creating an instance of an abstract class is not allowed!" 断言
 *   （本项目在 `UFGSchematicPurchasedDependency` 上已踩过一次，见上面注释）。
 *
 * 三个虚函数全部显式返回 true：基类注释虽写着 "Base implementations ... default to
 * 'true'"，但真正跑在游戏里的实现不归我们控制，显式覆写才不依赖别人的默认值。
 */
UCLASS()
class REMOTE_FACTORY_API UWT_AlwaysMetDep : public UFGAvailabilityDependency
{
    GENERATED_BODY()

public:
    virtual bool AreDependenciesMet(UObject* worldContext) const override { return true; }
    virtual bool ArePlayerSpecificDependenciesMet(const class AFGPlayerState* playerState) const override { return true; }
    virtual bool AreLocalDependenciesMet(UObject* worldContext) const override { return true; }
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

/** 末段 Lv17~20：三条轨合并，由两条互斥路线（常规 / 碎片）承担。 */
constexpr int32 WT_FinalMinLevel = 17;

/**
 * ★2026-10-07 末段路线（用户定案：「17 级使用了能量碎片研究，则剩余都需要消耗能量碎片，反之一样」）。
 * 按设计表：末段仅 2 条互斥路线（常规 / 碎片），玩家在 Lv17 二选一。
 *
 * Lv1~Lv16 每级只属于一条轨，Route 恒为 None；
 * Lv17~Lv20 三轨合并成「最终强化」节点，玩家在 Lv17 二选一：
 *   Plain = 常规路线（不消耗碎片，走原曲线）
 *   Boost = 碎片路线（每级额外消耗强化能量碎片，效果 x1.5 外推）
 * 两条路线在 MAM 树里各占一个方块、**并列可见**；Lv17 互相锁定，
 * 买了任一条另一条整段永久失效（Lv18~20 各自只依赖本路线上一级）。
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
/** 一条前置条件（写进节点描述用）：Label 是给人看的说法，Classes 是要全部已购的 schematic。 */
struct FWTSchematicPrereq
{
    FString         Label;
    TArray<UClass*> Classes;
    /** true = 列表内**全部**已购才算达成；false = **任一**已购即可（末段入口那种 OR 门禁）。 */
    bool            bRequireAll = true;
};

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

    /**
     * ★2026-11-05 用户要求：「前置条件要写出来，已达成也要标注（已达成）」。
     *
     * 难点：`Apply()` 在 CDO 构造期跑，那时**没有存档、没有玩家**，算不出哪条已达成。
     * 所以 Apply 只登记「基础描述 + 前置条件清单」，真实达成状态由世界侧周期刷新
     * （与方块推进共用一个 1s 定时器，见 `WT_AdvanceResearchBlocks`）。
     */
    static void WT_RegisterPrereqText(UClass* SchematicClass, const FString& BaseDesc,
                                      const TArray<FWTSchematicPrereq>& Prereqs);

    /** 按当前已购状态重写所有节点的 `mDescription`（幂等：内容没变就不写）。 */
    static void WT_RefreshPrereqText(class AFGSchematicManager* SM);

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

/**
 * ★2026-11-07 本轨「是否真的研究过至少一级」。
 * 用途：区分「未研究」与「真的 Lv1」—— `WT_GetTrackLevel` 在什么都没研究时会兜底返回 1
 * （注释写的是「建筑本来也不会存在」），但建筑其实可能已由里程碑解锁（如超级蓄电池由 T3
 * 里程碑发配方）。此时若直接用 L=1 去乘 WT_TrackScale（Level<1 会钳成 1 ⇒ 1.25×），
 * 未研究电池就会被算成 1600×1.25=2000，面板显示「研究前依旧 2000」。
 * 本函数只查存档里有没有该轨任意一级（含末段任一路线）被买过，**不兜底**。
 */
bool WT_IsTrackResearched(UWorld* World, EWTUpgradeTrack Track);

/** 末段两条路线里玩家实际买下的那条；尚未做末段研究时返回 None。 */
EWTFinalRoute WT_GetFinalRoute(UWorld* World);

/**
 * Multiplier applied to a track's level-1 base value at the given level.
 * 碎片路线（Boost）在 Lv17~20 走 1.5 倍增量外推，所以必须把路线一起传进来。
 */
float WT_TrackScale(EWTUpgradeTrack Track, int32 Level, EWTFinalRoute Route = EWTFinalRoute::None);

/** ★2026-11-07 充电速率定案：储能单元（超级电池）的「最大充电功率上限」（mPowerInputCapacity）。
 *  不再随等级 ×1.25 暴涨（Lv20≈34.7GW 任何电网都供不上，标称速率成摆设），改为按里程碑档位固定上限：
 *  Lv1~4 -> 200、Lv5~8 -> 500、Lv9~12 -> 1000、Lv13~16 -> 2500、Lv17~20 -> 5000 MW。
 *  该上限是吸收电网盈余电力的软上限，不影响放电；余电不足时自动慢充、不跳闸。
 *  建筑本体 ApplyResearchedCapacity 与奖励区文案共用本函数，保证「文案写的 = 实际给的」。 */
float WT_PowerStorageChargeCapMW(int32 Level);

/** 百位四舍五入取整：个位十位归零，十位≥5 进百位。用于电池储电量等展示/实际量统一取整。 */
static inline int32 WT_RoundToHundred(int32 V)
{
    const bool bNeg = V < 0;
    const int32 A = bNeg ? -V : V;
    const int32 R = (A / 100) * 100 + (((A % 100) >= 50) ? 100 : 0);
    return bNeg ? -R : R;
}

/** 电池（储能单元）储电量 MWh（已按百位取整），作为「奖励名 / 奖励描述 / 建筑实际加成」的唯一数据源，
 *  保证三者完全一致（名字 = 实际）。非末段 Route 恒为 None；末段路线由调用方传入。
 *  ★2026-11-08 用户定案：电池研究所有数量按百位四舍五入取整。 */
int32 WT_PowerStorageMWh(int32 Level, EWTFinalRoute Route = EWTFinalRoute::None);

/** Accelerator production boost (clamped) at a given level, route-aware（同上）。 */
float WT_AcceleratorBoostAtLevel(int32 Level, EWTFinalRoute Route = EWTFinalRoute::None);

// ★2026-11-07 强化碎片补偿：登记「schematic 类 → 当级发放量」（Apply 时调用），
// 以及「研究完成时显式发放到玩家背包」（刷新钩子调用，幂等去重）。
// 替代 MAM 下不生效的 UFGUnlockGiveItem（它只是容器，研究系统不读它发物品）。
void WT_RegisterShardGrant(UClass* SchematicClass, int32 Amount);
// JustCompleted 为本次刚研究完的 schematic（可空）：对它直接发放、跳过 IsSchematicPurchased 判定，
// 避免「本级购买未提交 → 补偿错位到下一级才发」的 LV1 漏发问题。
void WT_GivePendingShardCompensation(UWorld* W, UClass* JustCompleted = nullptr);
