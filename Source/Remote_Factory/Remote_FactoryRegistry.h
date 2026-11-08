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
 * ★★1.0.7（2026-10-26 已翻）：实测 1.0.6 包内碎片里程碑/商店/配方三样全缺，日志
 *    `[wt-diag] ... T5accel=0` + `[ms-dump]` 只有 T1/T3storage/T5accel 三条 ⇒ 本闸门仍是 false，
 *    上一轮加在 `if (WT_FEATURE_POWER_SHARD)` 里的注册/补发全是死代码。已改为 true。
 *    注意：闸门 false 期间每局读档都会把碎片配方 prune 掉（Retired 分支），老存档里原有的
 *    碎片配方条目已被清空，翻闸后靠 `WT_RepairRecipeAvailability` 的 Wanted 补发重新长回来。
 * ★★最终形态（用户同日改口「算了，跟终端放一起吧」）：碎片三条配方不再单开 T5 卡，而是和
 *    手持终端同一张 T1「智能工厂」里程碑一起发放（`URemote_FactoryMilestone::AddUnlockRecipes`）；
 *    `UWTUpgradePowerShardMilestone` 类保留编译与 CDO 配置但**不注册**。⇒ 测试流程 =
 *    买 T1 卡（若还没买）→ 三条配方同时出现，无需再去 T5 找卡。
 */
constexpr bool WT_FEATURE_POWER_SHARD = true;

/**
 * ★1.0.7 云端站总闸（用户 2026-10-26「顺便把四个云端一起写进去」→ 同日追加「先把云端液体的移除，
 * 这个先不修」⇒ **本版已彻底移除云端液体两台站**，仅保留固体两台）。
 *
 * 两台建筑（0.2.36 建成、0.2.56 起随升级建筑一起下架，1.0.7 恢复发放）：
 *   ① UWTUpgradeStorageRecipe             → Cloud Storage Upload Station   云端储存上传站（固体上传）★本版开放
 *   ② UWTUpgradeCloudDownloaderRecipe     → Cloud Storage Download Station 云端储存下载站（固体下载）★本版开放
 *
 * 发放口径与强化碎片**完全一致**：随 T1「智能工厂」里程碑（URemote_FactoryMilestone）一次给齐，
 * 不单开卡、不进 MAM。三处引用点必须同步（缺一处就会出现「里程碑给了但读档被 prune」或反之）：
 *   ① Remote_FactoryRegistry.cpp  URemote_FactoryMilestone::AddUnlockRecipes（买卡时发放）
 *   ② Remote_FactoryUpgradeRegistry.cpp  WT_RepairRecipeAvailability 的 Live（防 prune）
 *   ③ 同上函数的 Wanted（读档补发，门槛 = bT1Unlocked）
 *
 * 配方成本与「进建造枪」的填充（ApplyIngredients / ApplyProducedIn）**不在闸门内** ——
 * 它们在 RegisterUpgradeRecipes 里无条件执行，关闸只是不发解锁，不会让配方变成无料白送。
 * 成本口径：原版位面存储站同序料单 + 1 块强化碎片（⇒ 本闸门隐含依赖 WT_FEATURE_POWER_SHARD，
 * 碎片拿不到就造不出云端站，这是设计意图，不是 bug）。
 */
constexpr bool WT_FEATURE_CLOUD_STATIONS = true;

/**
 * ★1.0.8AO 云端**液体**两台单开关 —— **重新开放**（用户 2026-10-28「流体也发放了，现在，修流体」）。
 *
 * 历史：1.0.7 曾按用户「先把云端液体的移除，这个先不修」关成 false（只发固体两台）。
 * 本版连带把「流体为什么不能用」修掉后重新打开：
 *   ① 上传站 / 下载站原先**每 2 秒才调一次** `Factory_Pull/PushPipeInput`，却传的是**帧 dt**
 *      （≈0.016s）—— 原版内部按 `流量 × dt` 结算时间债，等于每 2 秒只认 1/125 的量
 *      ⇒ 接上管子也一滴都过不去。现改为**每帧调用 + 真实 dt**（原版所有管道建筑的惯例，
 *      精炼厂/萃取机都是每帧一次）。
 *   ② 下载站选型面板原先**只读原版云端池**，可上传站的液体按 0.2.36b 定案是记在
 *      **我们自己的账本**里、从不进原版池 ⇒ 面板永远「云端没有液体」⇒ 选不了型 ⇒ 整台死锁。
 *      现改为「各上传站账本汇总 + 原版池液体」合并成目录。
 *
 * ★排除的假设（别再走一次）：一度怀疑是「SML 的 FactoryGame 是空桩 ⇒ 管道函数不可用」。
 *   实测证伪 —— 解析 mod DLL 的 PE 导入表可见 `?Factory_PullPipeInput@UFGPipeConnectionComponent@@...`
 *   与 `?GetFluidBox@AFGBuildablePipeReservoir@@...` 都**从游戏 DLL 导入**（`Private
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
 * ★★2026-11-05 用户定案：**MAM 研究树上架**（同轮定的三件事）：
 *   ① 「走第二条路」⇒ 建筑不再靠 T1 卡，按 HUB 层级分档发放
 *      （T5 云端固体 / T7 云端液体 / T9 强化能量碎片，见 UpgradeRegistry 的三张卡）；
 *   ② 「成本表不要动，这个最开始定好的」⇒ GStageItemPath / GStageBaseAmount / GLevelMult 一律不动；
 *   ③ 「云端的问题最后修（这个云端最麻烦）」⇒ 容量轨的云端配额加成
 *      （CloudQuota.h `WT_FEATURE_CLOUD_QUOTA_MAM`）仍是 false，留到最后单独一轮接。
 *   ⇒ 结论：MAM 树这一版**只做加成**（储能倍率 / 碎片参数 / 云端配额），不再发放建筑配方。
 *   必须同时成立的两件事（漏一件 = MAM 里依旧什么都没有）：
 *     A. 本开关 = true（否则 56 条不进 mSchematics，SchematicManager 里查不到）；
 *     B. WT_GetAllResearchTrees() 返回 UWTResearchTreeMerged（已同步改，ResearchTree.cpp）。
 */
constexpr bool WT_FEATURE_MAM_RESEARCH = true;

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
