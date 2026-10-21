#pragma once

#include "CoreMinimal.h"
#include "Resources/FGBuildingDescriptor.h"
#include "FGRecipe.h"
#include "FGBuildCategory.h"
#include "FGBuildSubCategory.h"
#include "FGSchematic.h"
#include "Remote_FactoryRegistry.generated.h"

class AFGHologram;
class AFGBuildable;
class ARemote_FactoryBuildable;
class UFGUnlockRecipe;

/**
 * ★0.2.134 【强化能量碎片 · 单开关闸门】测试构建整体下架，正式发布前整体恢复。
 *
 * 背景（用户定案）：测试版不发放强化能量碎片，源码里的配方类/配方定义/物品类一律保留
 * （旧存档已购实例要能继续加载），只是玩家暂时**拿不到、造不了、买不了**。
 *
 * 为什么必须有这个开关：碎片配方是编译进 DLL 的 C++ 类，且 WT_RepairRecipeAvailability
 * 有一条「强化碎片无视 T1 门槛、无条件补发」的逻辑（Remote_FactoryUpgradeRegistry.cpp），
 * 部署层没有任何配置能关掉它 —— 只摘里程碑奖励不够，读档时会被补发回去。
 *
 * 关闭(false)时的效果，共 4 处引用点：
 *   ① Remote_FactoryRegistry.cpp      T1 里程碑不再发放两条碎片配方；
 *   ② Remote_FactoryUpgradeRegistry.cpp  碎片两条从 Live 移到 Retired ⇒ 老存档里的条目被 prune，
 *      制作台/构造机/量子编码器里不再出现；
 *   ③ Remote_FactoryUpgradeRegistry.cpp  碎片两条不入 Wanted ⇒ 读档不再无条件补发；
 *   ④ Remote_FactoryGameWorldModule.cpp  AWESOME 商店条目 UWTShopPowerShardSchematic 不注册
 *      （连同 CONSTRUCTION 阶段的售价/发货数据填充一并跳过）。
 *
 * ★v1.0.0 正式发布：把下面的 false 改回 true，四处同时恢复，无需动任何配方定义。
 */
constexpr bool WT_FEATURE_POWER_SHARD = false;

/**
 * ★0.2.139 MAM 升级研究总闸（用户定案：v1.0.0 之前，MAM 科技全部下架）。
 *
 * 背景（一手核实，2026-10-20）：5 个升级建筑已在 0.2.56 从 mod 彻底移除（Wanted 不补发、
 * 里程碑不注册、研究树不注册），所以这 60 条 EST_MAM 研究节点（3 轨 x 20 级）既看不见
 * （没有研究树 ⇒ MAM 里没有「智能工厂」树，Server_InitiateResearch 必须带发起树）、
 * 也买不了，更没有任何建筑等级可作用 —— 纯粹是僵尸注册 + 启动期 CDO 配置开销 + 日志噪音
 * （每局 56 条 "[research] reward text <= 0/4 field(s) written" 警告）。
 *
 * 关闭(false)时的效果，共 2 处引用点（都在 Remote_FactoryUpgradeResearch.gen.cpp，
 * 生成源 gen_wt_research.py 同步）：
 *   ① WT_ConfigureAllResearch()  直接返回 ⇒ 不再配 60 个研究 CDO（省掉资产加载与 56 条警告）；
 *   ② WT_GetAllUpgradeResearch() 返回空数组 ⇒ 60 条不进 mSchematics ⇒ SML 不 RegisterSchematic。
 *
 * ★v1.0.0 恢复提醒（重要，别只翻这一个开关就算完）：
 *   把这个 false 改回 true 只会让 60 条节点重新进 SchematicManager —— 它们在 MAM 里**仍然看不见**，
 *   因为 URemote_FactoryGameWorldModule::WT_GetAllResearchTrees()（Remote_FactoryResearchTree.cpp）
 *   自 0.2.56 起固定返回空数组、没有挂在本闸门上。要让 MAM 里真正出现「智能工厂」研究树，
 *   必须同时让 WT_GetAllResearchTrees() 返回 UWTResearchTreeMerged（或把该函数也挂到本闸门）。
 *   另：0.2.138 起末段 Boost 路线（L17~L20）另受 WT_FEATURE_POWER_SHARD 约束，
 *   v1.0.0 若只想恢复常规路线、继续下架碎片，可只翻本闸门而保持 POWER_SHARD=false。
 */
constexpr bool WT_FEATURE_MAM_RESEARCH = false;

/**
 * Building descriptor for the Remote Factory. This is the "item" form the build gun holds:
 * it names the buildable to place, the hologram used as the placement preview, and the icon /
 * display strings the build menu shows.
 */
UCLASS()
class REMOTE_FACTORY_API URemote_FactoryDescriptor : public UFGBuildingDescriptor
{
    GENERATED_BODY()
public:
    URemote_FactoryDescriptor();

    virtual TSubclassOf<AFGHologram> GetHologramClassInternal() const override;
    virtual FText GetItemNameInternal() const override;
    virtual FText GetItemDescriptionInternal() const override;

protected:
    /**
     * ★ CRASH GUARD: these four MUST stay UPROPERTY.
     * BuildableClass / HologramClass are TSubclassOf (i.e. they hold a UClass*), so without
     * reflection the garbage collector cannot see the reference and a hot-reload / world
     * teardown could recycle the class out from under a live descriptor. The two FText members
     * are not GC-tracked themselves, but they are marked too so this block stays a single,
     * copy-safe pattern for any member added later.
     */
    UPROPERTY()
    TSubclassOf<AFGBuildable> BuildableClass;
    UPROPERTY()
    TSubclassOf<AFGHologram> HologramClass;
    UPROPERTY()
    FText DisplayName;
    UPROPERTY()
    FText Desc;

    /** Wires mBuildableClass + build category / sub-category. Call at the end of the ctor. */
    void InitCommon();
};

/**
 * Build-gun recipe: product = the terminal descriptor, produced in the Build Gun.
 *
 * mProducedIn cannot be set from the constructor, because the Build Gun class only exists once
 * the game content is mounted. ApplyProducedIn() is called from
 * Remote_Factory::RegisterBuildables() at POST_INITIALIZATION instead.
 */
UCLASS()
class REMOTE_FACTORY_API URemote_FactoryRecipe : public UFGRecipe
{
    GENERATED_BODY()
public:
    URemote_FactoryRecipe();

    /** Point mProducedIn at the resolved Build Gun class. Idempotent. */
    void ApplyProducedIn(UClass* BuildGunClass);

    /**
     * Fill mIngredients from /Game/... item descriptor paths resolved at runtime.
     * Without this the building shows as FREE in the build gun.
     */
    void ApplyIngredients();
};

/** Build category tab for the terminal in the build gun. */
UCLASS()
class REMOTE_FACTORY_API URemote_FactoryBuildCategory : public UFGBuildCategory
{
    GENERATED_BODY()
public:
    URemote_FactoryBuildCategory();
};

UCLASS()
class REMOTE_FACTORY_API URemote_FactoryBuildSubCategory : public UFGBuildSubCategory
{
    GENERATED_BODY()
public:
    URemote_FactoryBuildSubCategory();
};

/**
 * HUB milestone that unlocks the terminal.
 *
 * Tech tier 2 ("Part Assembly") with a cost drawn only from parts that tier can already make
 * (reinforced iron plate / cable / circuit board), so the milestone is both purchasable and
 * immediately buildable when it first appears rather than a mid-game quality-of-life upgrade
 * gated behind parts the player cannot produce yet.
 * Registered from the GameWorldModule constructor via mSchematics, which is what puts it into the
 * HUB; SML's base DispatchLifecycleEvent pushes that list into UModContentRegistry.
 */
UCLASS()
class REMOTE_FACTORY_API URemote_FactoryMilestone : public UFGSchematic
{
    GENERATED_BODY()
public:
    URemote_FactoryMilestone();

    /**
     * Fill in everything that lives behind a protected access specifier.
     *
     * WHY THIS IS A MEMBER and not a free function: mCost / mSchematicCategory / mUnlocks are
     * protected on UFGSchematic, and AccessTransformers.ini only opens them up to THIS class via
     * `Friend=(Class="UFGSchematic", FriendClass="URemote_FactoryMilestone")`. A free helper in
     * an anonymous namespace is not that friend, so the writes have to happen in here. Same for
     * UFGUnlockRecipe::mRecipes, which is opened to this class too.
     *
     * Returns a short human-readable status for logging (empty on success).
     */
    FString InstallCostCategoryAndUnlock();

    /** Append the recipes this milestone should grant (friend access to UFGUnlockRecipe::mRecipes). */
    void AddUnlockRecipes(UFGUnlockRecipe* Unlock) const;
};

namespace Remote_Factory
{
    /** Register the descriptor + recipe with the live AFGRecipeManager. Idempotent. */
    void RegisterBuildables(class UWorld* World);

    /** Resolve the /Game/... cost lines on the milestone. Needs the game content mounted. */
    void ApplyMilestoneData();
}
