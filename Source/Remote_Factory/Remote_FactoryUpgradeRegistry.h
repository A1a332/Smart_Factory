#pragma once

#include "CoreMinimal.h"
#include "Resources/FGBuildingDescriptor.h"
#include "Resources/FGPowerShardDescriptor.h"
#include "FGRecipe.h"
#include "FGSchematic.h"
#include "Unlocks/FGUnlockGiveItem.h"   // ★0.2.36h UWTUnlockGiveItemShard 的基类（abstract）
#include "Unlocks/FGCentralStorageUnlock.h"   // ★0.2.47 UWTUnlockCloudQuota 的基类（abstract）
#include "Remote_FactoryUpgradeRegistry.generated.h"

class AFGHologram;
class AFGBuildable;
class UFGUnlockRecipe;
class UFGSchematic;

/**
 * ★v0.2.24 给 schematic 写图标（mSmallSchematicIcon + mSchematicIcon）。
 *
 * HUB 对"图标无效"的 schematic 会把费用行和奖励面板一起藏掉（引擎 FGSchematic.cpp IsDataValid
 * 原话："Invalid icon set for schematic. This will cause the milestone to not display its unlocks
 * or cost in the HUB."）—— 这正是用户截图里"里程碑卡片在、点开一片空白"的直接原因。我们的三个
 * 里程碑此前两个图标槽都没写。
 *
 * 图标用本 mod 自己烤好的纹理（包内资产 => GC 安全），并额外 AddToRoot —— WirelessPower 的
 * Slate 图标 GC 悬垂崩溃（v1.0.43）定下的双保险。供两个里程碑文件共用（T1 在 Registry.cpp）。
 *
 * 已知瑕疵：HUB 会把 schematic 图标整体染色，全彩图会被染成一整块橙色；WirelessPower 用的是
 * 白色剪影贴图。先用现成的手持终端彩色图标把"面板空白"修掉，白剪影资产后续再补。
 */
void WT_ApplySchematicIcon(UFGSchematic* Schematic);

/**
 * ★0.2.36g 研究节点贴图分档（用户定案）：
 *   * **三功能（三轨）各用一张不同贴图**（云端储存上传站 / 超级蓄电池 / 强化能量碎片）；
 *   * **末段两条路线按颜色区分**：消耗强化能量碎片的「碎片路线」= 红，不消耗的「常规路线」= 黄。
 *
 * 实现是**双保险**（因为本项目已实测：HUB 对 schematic 图标会做整体染色，全彩图会被压成一整块
 * 橙色 —— 见上方 WT_ApplySchematicIcon 注释。既然存在"染色由控件样式接管"的可能，单靠贴图颜色
 * 未必显示得出来，单靠 TintColor 也未必）：
 *   ① **贴图**：每个档位先在候选路径表里取第一张能加载成功的纹理；全都载不到就回落到模组自带
 *      图标 —— **绝不写空**，否则 HUB/MAM 会把费用行 + 奖励面板整块藏掉（v0.2.24 修的就是这个）。
 *   ② **染色**：同时把 `FSlateBrush.TintColor` 的 `SpecifiedColor` 写成该档位颜色，并把
 *      `ColorUseRule` 置为 `UseColor_Specified`（默认值是 `UseColor_ColorTable`，
 *      只写颜色不改它的话会被颜色表吞掉，等于白写）。
 * 两条路都铺上，哪种在游戏里真正生效就用哪种；解析结果与回读值全部进日志，便于实测判定。
 */
enum class EWTSchematicIconStyle : uint8
{
    Default    = 0,   // 里程碑等：模组自带图标，不染色
    Track0     = 1,   // 云端储存上传站
    Track1     = 2,   // 超级蓄电池
    Track2     = 3,   // 强化能量碎片
    FinalPlain = 4,   // 末段 · 常规路线（不消耗能量碎片）= 黄
    FinalBoost = 5,   // 末段 · 碎片路线（消耗强化能量碎片）= 红
    ShardRainbow = 6, // ★0.2.36h 强化碎片本体/商店条目：模组彩虹贴图，不染色（染色会把彩虹压成单色）
};

/** 自带分档的重载；Default 等价于旧的 WT_ApplySchematicIcon。 */
void WT_ApplySchematicIconStyled(UFGSchematic* Schematic, EWTSchematicIconStyle Style);

/**
 * Building descriptor for a Form 3 "C" upgrade.
 *
 * ## Why there are three subclasses and not one shared class
 *
 * The build gun lists one entry per DESCRIPTOR CLASS. Two descriptors of the same class are the same
 * item as far as the inventory, the recipe manager and the recipe product list are concerned -- a
 * recipe's mProduct names a class, so three recipes all producing UWTUpgradeDescriptor would produce
 * literally the same item three times.
 *
 * So the base holds everything shared, and the three subclasses below differ only in their
 * constructor body. That is the smallest amount of duplication that still gives three distinct
 * items, and it is the same shape URemote_FactoryDescriptor already uses.
 */
UCLASS(Abstract)
class REMOTE_FACTORY_API UWTUpgradeDescriptorBase : public UFGBuildingDescriptor
{
    GENERATED_BODY()
public:
    UWTUpgradeDescriptorBase();

    virtual TSubclassOf<AFGHologram> GetHologramClassInternal() const override;
    virtual FText GetItemNameInternal() const override;
    virtual FText GetItemDescriptionInternal() const override;

protected:
    /** ★ CRASH GUARD: same rule as URemote_FactoryDescriptor -- the two TSubclassOf members
     *  hold UClass* and must be reflected or GC cannot see them. */
    UPROPERTY()
    TSubclassOf<AFGBuildable> BuildableClass;
    UPROPERTY()
    TSubclassOf<AFGHologram> HologramClass;
    UPROPERTY()
    FText DisplayName;
    UPROPERTY()
    FText Desc;

    /** Wires mBuildableClass + the build-gun category / sub-category. Call at the end of the ctor. */
    void InitCommon();
};

/** 储能单元 -- 1000 MWh battery bank. */
UCLASS()
class REMOTE_FACTORY_API UWTUpgradePowerStorageDescriptor : public UWTUpgradeDescriptorBase
{
    GENERATED_BODY()
public:
    UWTUpgradePowerStorageDescriptor();
};

/** 大容量仓库 -- 8 x 12 container. ★0.2.36 改名「云端储存上传站」。 */
UCLASS()
class REMOTE_FACTORY_API UWTUpgradeStorageDescriptor : public UWTUpgradeDescriptorBase
{
    GENERATED_BODY()
public:
    UWTUpgradeStorageDescriptor();
};

/** 云端存储下载站（★0.2.36 新增）：从云端池往本地搬货的配套仓库。 */
UCLASS()
class REMOTE_FACTORY_API UWTUpgradeCloudDownloaderDescriptor : public UWTUpgradeDescriptorBase
{
    GENERATED_BODY()
public:
    UWTUpgradeCloudDownloaderDescriptor();
};

/** 云端液态上传站（★0.2.36 新增）：管道流体 -> 云端池的入口。 */
UCLASS()
class REMOTE_FACTORY_API UWTUpgradeCloudFluidUploaderDescriptor : public UWTUpgradeDescriptorBase
{
    GENERATED_BODY()
public:
    UWTUpgradeCloudFluidUploaderDescriptor();
};

/** 云端液态下载站（★0.2.36 新增）：云端池 -> 管网流体的出口（面板选型）。 */
UCLASS()
class REMOTE_FACTORY_API UWTUpgradeCloudFluidDownloaderDescriptor : public UWTUpgradeDescriptorBase
{
    GENERATED_BODY()
public:
    UWTUpgradeCloudFluidDownloaderDescriptor();
};

/** 产能加速器 -- standing production boost. */
UCLASS()
class REMOTE_FACTORY_API UWTUpgradeAcceleratorDescriptor : public UWTUpgradeDescriptorBase
{
    GENERATED_BODY()
public:
    UWTUpgradeAcceleratorDescriptor();
};

// ----------------------------------------------------------------------------
// Recipes. One per building, each producing its own descriptor.
//
// mProducedIn cannot be set from a constructor: the Build Gun class only exists once the game
// content is mounted, so ApplyProducedIn() runs at POST_INITIALIZATION. The same contract
// URemote_FactoryRecipe documents.
// ----------------------------------------------------------------------------

UCLASS()
class REMOTE_FACTORY_API UWTUpgradePowerStorageRecipe : public UFGRecipe
{
    GENERATED_BODY()
public:
    UWTUpgradePowerStorageRecipe();
    void ApplyProducedIn(UClass* BuildGunClass);
    void ApplyIngredients();
};

UCLASS()
class REMOTE_FACTORY_API UWTUpgradeStorageRecipe : public UFGRecipe
{
    GENERATED_BODY()
public:
    UWTUpgradeStorageRecipe();
    void ApplyProducedIn(UClass* BuildGunClass);
    void ApplyIngredients();
};

UCLASS()
class REMOTE_FACTORY_API UWTUpgradeCloudDownloaderRecipe : public UFGRecipe
{
    GENERATED_BODY()
public:
    UWTUpgradeCloudDownloaderRecipe();
    void ApplyProducedIn(UClass* BuildGunClass);
    void ApplyIngredients();
};

UCLASS()
class REMOTE_FACTORY_API UWTUpgradeCloudFluidUploaderRecipe : public UFGRecipe
{
    GENERATED_BODY()
public:
    UWTUpgradeCloudFluidUploaderRecipe();
    void ApplyProducedIn(UClass* BuildGunClass);
    void ApplyIngredients();
};

UCLASS()
class REMOTE_FACTORY_API UWTUpgradeCloudFluidDownloaderRecipe : public UFGRecipe
{
    GENERATED_BODY()
public:
    UWTUpgradeCloudFluidDownloaderRecipe();
    void ApplyProducedIn(UClass* BuildGunClass);
    void ApplyIngredients();
};

// ----------------------------------------------------------------------------
// 强化能量碎片物品线（★0.2.36 落地，用户定案）
//
// * 派生原版 UFGPowerShardDescriptor ⇒ 插进原版机器的能量碎片插槽走**原版强化逻辑**
//   （PST_Overclock，耗电按原版指数曲线自动跟随）。
// * ★方案A/插槽新版（10-07 定案）：每块潜能增量 +0.5 起、随产能研究阶段涨到 +1.0，
//   同时全场机器 mMaxPotential 250%→300%→350%→400%（见 cpp 的 ApplyShardStage）。
//   1 块 200% / 2 块 300% / 3 块 400%，正好对应原版 1.5/2/2.5 的新版阶梯。
// * ★0.2.39（用户最终定案）：配方 = **两条**（详见下面 UWTUpgradePowerShardRecipe 处的说明）：
//     ① 构造机：**2 × 原版能量碎片 → 1 × 强化碎片**；
//     ② 量子编码器：原版 `Recipe_SyntheticPowerShard`（原版「合成能量碎片」）**全部材料 ×2**
//        → 1 × 强化碎片（**无暗物质残渣副产**）。
//   ① 的材料用原版碎片类（`WT_FindVanillaShardClass`）；② 的材料在运行时从原版合成配方
//   **读出来**再翻倍（含气体：光子物质 12m³ → 24m³）—— 原版改数值我们自动跟随，不用写死。
//   （0.2.36h~0.2.38 的「原版蓝/黄/紫 Slug 三条配方镜像」与更早的「制造机合成配方」都已退役。）
// * 产能研究 Lv17~20 碎片路线的门槛材料自动生效（研究 cpp 改引本类）。
// ----------------------------------------------------------------------------
UCLASS()
class REMOTE_FACTORY_API UWTUpgradePowerShardDescriptor : public UFGPowerShardDescriptor
{
    GENERATED_BODY()
public:
    UWTUpgradePowerShardDescriptor();

    static FText GetDisplayName();
    static FText GetItemDescription();

    /**
     * ★0.2.36h（用户定案「它可以被回收」）：设置回收点数。
     * 回收点数 = 原版能量碎片的 **2 倍**（与「配方材料 ×2」同一套定价逻辑）；原版数值在运行时
     * 从原版碎片配方产物描述符的 CDO 读出来，原版改数值我们自动跟随。读不到时兜底 150000。
     * 幂等（static 守卫）；必须在 CONSTRUCTION 或之后调用（要加载原版类）。
     */
    void ApplySinkPoints();

    /**
     * ★0.2.59（用户实测「强化碎片放不进机器碎片槽/不能超频，外观要原版红色」）：
     * 从原版能量碎片 CDO **镜像**三件套到本类 CDO——
     *   ① mForm（我们的 C++ 类从不设置 ⇒ RF_INVALID(0)，入库/插槽按 Form 校验直接拒绝；
     *      原版蓝图类设 RF_SOLID）；
     *   ② mConveyorMesh（传送带/掉落/手持的展示建模，原版红色水晶建模）；
     *   ③ mSmallIcon / mPersistentBigIcon（原版红色贴图，替换 0.2.36h 的彩虹贴图）。
     * 原版类在世界加载后按 IsChildOf 解析（同计算器 ResolveShard 判据），零硬编码路径。
     * 必须在 POST_INITIALIZATION 或之后调用（原版描述符须已加载）。
     */
    void ApplyVanillaMirror(UClass* VanillaShardClass);
};

/**
 * 强化能量碎片 · 购买发货解锁（★0.2.36h 用户定案「可以出现在 AWESOME 商店」）。
 * UFGUnlockGiveItem 是 abstract（引擎头文件实测），必须自建子类 —— 与 UWTUnlockInfo 同一套路。
 * 商店购买一次 = 按下表发货一次。
 */
UCLASS()
class REMOTE_FACTORY_API UWTUnlockGiveItemShard : public UFGUnlockGiveItem
{
    GENERATED_BODY()
};

/**
 * ★0.2.47 云端配额解锁（设计表 §6.1/§6.2/§6.3 —— **给四座云端站的加成**，用户 2026-10-08
 * 截图圈 §6.2「容量：云端配额」列定案「这个给四个云端的加成」）。
 *
 * UFGUnlockCentralStorageItemLimit 是 abstract（引擎头实测：UCLASS(abstract, DefaultToInstanced)），
 * 必须派生非抽象子类才能 NewObject —— 与 UWTUnlockGiveItemShard 同一套路。
 * 主路径用**原版** BP_UnlockCentralStorageItemLimit_C 实例（奖励面板渲染兼容、Apply 走游戏
 * DLL 里的真实现）；本类只作 BP 解析失败时的兜底。
 * Apply() 刻意**不覆写** —— 子类不覆写时 vtable 指向游戏 DLL 导出的真实现
 * （SDK .cpp 全是 UHT 桩，不是行为依据；链接期经导入表解析到真机代码）。
 */
UCLASS()
class REMOTE_FACTORY_API UWTUnlockCloudQuota : public UFGUnlockCentralStorageItemLimit
{
    GENERATED_BODY()
};

/**
 * 强化能量碎片 · AWESOME 商店条目（EST_ResourceSink）。
 * 商店按类型枚举 schematic，把本类注册进 GameWorldModule::mSchematics 即可上架；
 * 售价 / 发货在 ApplyShopSchematicData()（CONSTRUCTION 阶段）填。
 */
UCLASS()
class REMOTE_FACTORY_API UWTShopPowerShardSchematic : public UFGSchematic
{
    GENERATED_BODY()
public:
    UWTShopPowerShardSchematic() {}
    void ApplyShopSchematicData();
};

/**
 * 强化能量碎片 · 配方（★用户最终定案：**两条**）。
 *
 *   ① UWTUpgradePowerShardRecipe      —— 构造机：**原版能量碎片 ×2 → 强化碎片 ×1**
 *   ② UWTUpgradePowerShardSynthRecipe —— 量子编码器：镜像原版 Recipe_SyntheticPowerShard
 *                                        （合成能量碎片）的**全部材料 ×2** → 强化碎片 ×1（无副产）
 *
 * ★0.2.37：新增 ApplyProducedIn(ProducerClass) —— 生产机器改为**运行时显式解析**
 * （StaticLoadClass + 迭代器兜底）。原先只在构造函数里塞一条裸软路径，一旦解析失败配方就
 * 不属于任何机器、玩家在机器里永远看不到（表现为「无法制作」）。构造函数里保留软路径兜底。
 *
 * ★0.2.39（回退 0.2.36h 的三条镜像）：0.2.36h 曾把配方改成「原版三条构造机碎片配方（蓝/黄/紫
 * Slug）材料×2 镜像」共三条。用户 2026-10-08 复核确认**只应有两条**（构造机 + 编码器）。
 */
UCLASS()
class REMOTE_FACTORY_API UWTUpgradePowerShardRecipe : public UFGRecipe
{
    GENERATED_BODY()
public:
    UWTUpgradePowerShardRecipe();     // ① 构造机：2 × 原版能量碎片 → 1 × 强化碎片
    void ApplyProducedIn(UClass* ProducerClass);
    void ApplyIngredients();
};

/**
 * ★0.2.39 退役空壳（**不要删！**）。
 *
 * 这两个类是 0.2.36h~0.2.38 的「原版黄/紫 Slug 配方材料×2 镜像」。用户定案回到两条配方后
 * 它们退役 —— 但**类必须保留**：0.2.36h 起它们一直被写进 T1 里程碑的解锁链，老存档的
 * `AFGRecipeManager::mAvailableRecipes`（UPROPERTY(SaveGame)，只增不减）里存着它们的类路径；
 * 删掉就会重演 0.2.37 那次「读档 / 开机器配方列表必崩」（TSubclassOf 落成 nullptr）。
 *
 * 处置与 SynthRecipe 当年一样：构造体**留空**、不进任何解锁链，
 * 并由 `WT_RepairRecipeAvailability` 主动从 mAvailableRecipes 摘除。
 */
UCLASS()
class REMOTE_FACTORY_API UWTUpgradePowerShardRecipe2 : public UFGRecipe
{
    GENERATED_BODY()
public:
    UWTUpgradePowerShardRecipe2();
};

UCLASS()
class REMOTE_FACTORY_API UWTUpgradePowerShardRecipe3 : public UFGRecipe
{
    GENERATED_BODY()
public:
    UWTUpgradePowerShardRecipe3();
};

/**
 * ★0.2.39 复活为「② 编码器配方」（0.2.37~0.2.38 期间它是一具空壳）。
 *
 * 在量子编码器（`Build_QuantumEncoder_C`）里镜像原版 `Recipe_SyntheticPowerShard`（原版
 * 「合成能量碎片」：时间水晶2 + 暗物质2 + 石英12 + 光子物质12m³，12s）的**全部材料 ×2**。
 * 产物只有强化碎片 ×1 —— **不带**原版的暗物质残渣副产（用户定案）。
 *
 * ★类名保持不变是刻意的：0.2.36 的老存档里就存着
 * `/Script/Remote_Factory.UWTUpgradePowerShardSynthRecipe`，复用它老存档天然接得上。
 */
UCLASS()
class REMOTE_FACTORY_API UWTUpgradePowerShardSynthRecipe : public UFGRecipe
{
    GENERATED_BODY()
public:
    UWTUpgradePowerShardSynthRecipe();   // ② 量子编码器：原版合成碎片配方材料 ×2 → 1 × 强化碎片
    void ApplyProducedIn(UClass* ProducerClass);
    void ApplyIngredients();
};

UCLASS()
class REMOTE_FACTORY_API UWTUpgradeAcceleratorRecipe : public UFGRecipe
{
    GENERATED_BODY()
public:
    UWTUpgradeAcceleratorRecipe();
    void ApplyProducedIn(UClass* BuildGunClass);
    void ApplyIngredients();
};

/**
 * Shared plumbing for the two upgrade milestones.
 *
 * ★ v0.2.2 -- the single tier-4 "Upgrade Kit" milestone was split. One purchase for three
 * buildings forced every building's price down to whatever tier the milestone sat at, and any
 * building whose unlock tier was lower ended up demanding parts the player could not make yet.
 * The three are now spread over three unlock points that match what the player owns:
 *
 *   T1  "Remote Factory"        -> fixed terminal + handheld + High-Capacity Storage
 *                                     (registered in Remote_FactoryRegistry.cpp)
 *   T4  "Remote Factory · Storage Unit"    -> Power Storage Unit
 *   T5  "Remote Factory · Production Accelerator" -> Production Accelerator
 *
 * (The storage unit sat at T3 for one revision and was moved to T4 by request: steel beam,
 *  versatile framework and motor are all <= T4, so nothing about the pricing had to give.)
 *
 * ## Why a base class instead of a free helper
 *
 * InstallCostCategoryAndUnlock() writes mCost / mSchematicCategory / mUnlocks, all protected on
 * UFGSchematic, and UFGUnlockRecipe::mRecipes is protected too. AccessTransformers.ini grants that
 * access **per named class**, and a free function is not the friend -- it fails C2248.
 *
 * C++ does allow a base-class member function to touch its own protected inherited members, so the
 * write lives here ONCE and the two concrete milestones below only hand over data (a cost table and
 * their own recipe). Adding a third upgrade later means one friend line and one subclass, not
 * another copy of this method.
 */
UCLASS(Abstract)
class REMOTE_FACTORY_API UWTUpgradeMilestoneBase : public UFGSchematic
{
    GENERATED_BODY()
public:
    UWTUpgradeMilestoneBase();

    /**
     * Fill mCost / mSchematicCategory / mUnlocks.
     *
     * Takes FItemAmount by const reference rather than a cost-table pointer on purpose: the cost
     * table type is file-local (anonymous namespace) in the .cpp and cannot appear in a header.
     * Callers build the array with the same free FillIngredients() helper the recipes use.
     *
     * Returns an empty string on success, or a short status describing what failed.
     */
    FString InstallCostCategoryAndUnlock(const TArray<FItemAmount>& Costs,
                                         const TArray<UClass*>& RecipeClasses);
};

/**
 * T4 "Remote Factory · Storage Unit".
 *
 * Priced in 钢梁 Steel Beam (T3) / 多功能框架 Versatile Framework (T3) / 马达 Motor (T4) -- all
 * reachable at tier 4, and all parts a tier-4 factory is already running.
 */
UCLASS()
class REMOTE_FACTORY_API UWTUpgradeStorageMilestone : public UWTUpgradeMilestoneBase
{
    GENERATED_BODY()
public:
    UWTUpgradeStorageMilestone();

    /** Convenience wrapper: builds the cost array from the tier-3 table, then installs. */
    FString InstallCostAndUnlock();
};

/**
 * T5 "Remote Factory · Production Accelerator".
 *
 * Tier 5 ("Oil Processing") brings plastic/rubber and, more to the point, tiers 5 and 6 open on the
 * same Space Elevator phase, so heavy modular frame is reachable from this milestone.
 */
UCLASS()
class REMOTE_FACTORY_API UWTUpgradeAcceleratorMilestone : public UWTUpgradeMilestoneBase
{
    GENERATED_BODY()
public:
    UWTUpgradeAcceleratorMilestone();

    /** Convenience wrapper: builds the cost array from the tier-5 table, then installs. */
    FString InstallCostAndUnlock();
};

namespace Remote_FactoryUpgrades
{
    /** Resolve mProducedIn (build gun) + mIngredients on all three upgrade recipes. Idempotent. */
    void RegisterUpgradeRecipes(class UWorld* World);

    /** Populate both upgrade milestones' cost / category / unlock. Needs the game content mounted. */
    void ApplyUpgradeMilestoneData();

    /**
     * ★0.2.36 新版插槽系统（用户定案）：把产能研究阶段应用到世界 ——
     * 强化碎片 CDO 的每块潜能增量 + 全场可超频机器的 mMaxPotential 上限（2.5/3.0/3.5/4.0）。
     */
    void ApplyShardStage(class UWorld* World, int32 Stage);

    /** 每 10s 查一次产能研究阶段，变化时调 ApplyShardStage（FTSTicker，随世界销毁）。 */
    void StartShardStageTicker(class UWorld* World);
}
