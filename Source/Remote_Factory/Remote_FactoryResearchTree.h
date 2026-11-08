#pragma once

#include "CoreMinimal.h"
#include "FGResearchManager.h"   // EResearchState, AFGResearchManager, OnResearchStateChanged/Completed 委托
#include "FGSchematic.h"         // UFGSchematic（HandleResearchCompleted 参数）
#include "FGResearchTreeNode.h"
#include "FGResearchTree.h"
#include "Remote_FactoryUpgradeResearch.h"   // EWTUpgradeTrack, WT_MaxUpgradeLevel, WT_ResearchClassFor
#include "Remote_FactoryResearchTree.generated.h"

/**
 * A single MAM research-tree node carrying the schematic it represents.
 *
 * ★v0.2.27 定案（2026-10-06 从游戏 pak 解包实锤）：原版**所有** MAM 节点共用同一个蓝图数据资产
 * `/Game/FactoryGame/Schematics/Research/BPD_ResearchTreeNode`，它的蓝图变量是
 *   Schematic(ClassProperty) / Coordinates(IntVector2D) / Parents / ChildrenAndRoads /
 *   NodesToUnhide / UnhiddenBy
 * 且 GetNodeSchematic() 返回 Schematic。MAM 图区正是靠 Coordinates 布局 + Parents 连线。
 *
 * UFGResearchTreeNode::GetNodeSchematic() 是 BlueprintImplementableEvent（非虚），所以原生 C++
 * 类无法提供返回值 —— 这就是为什么现在**直接实例化原版那个 BP 类**、再用反射按属性名写
 * Schematic/Coordinates/Parents（见 Remote_FactoryResearchTree.cpp 的 WT_WriteNodeData）。
 * 本 C++ 类保留为回退（原版类加载失败时用），其 mNodeSchematic 仅作备份字段。
 */
UCLASS(Blueprintable)
class REMOTE_FACTORY_API UWTResearchTreeNode : public UFGResearchTreeNode
{
    GENERATED_BODY()
public:
    /** 备份字段：只有走纯 C++ 回退路径时才会被读取（原版路径写的是 BP 的 Schematic）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Research Node")
    TSubclassOf<UFGSchematic> mNodeSchematic;
};

/**
 * Base for the three per-track MAM trees. Holds the (protected) members of
 * UFGResearchTree (mDisplayName / mNodes / ...) so ConfigureTree() can write them
 * directly -- a free function could not, since they are protected.
 *
 * We need three *distinct classes* (not three instances) because the research
 * manager's mAvailableResearchTrees stores TSubclassOf<UFGResearchTree> and MAM
 * reads each tree's nodes off its class default object.
 */
UCLASS()
class REMOTE_FACTORY_API UWTResearchTreeBase : public UFGResearchTree
{
    GENERATED_BODY()
public:
    /** Populate this tree's display text and the 20 nodes for one upgrade track. */
    void ConfigureTree(EWTUpgradeTrack Track, const FText& DisplayName);

    /** ★v0.2.24 三合一版：三条轨道的 20 级节点全部挂进同一棵树（60 节点）。 */
    void ConfigureMergedTree(const FText& DisplayName, const FText& Description);

    /**
     * ★2026-11-05 崩溃修复：确保树的解锁依赖非空（恒真依赖，树恒解锁）。
     * 研究树 CDO 会被引擎在处理树时重新初始化、清空 mUnlockDependencies
     * （表现为 MAM 列表按钮 SetContent 报 "GetDependecyData of length 0"），
     * 故每次都自查并补回依赖，避免整棵树被判 Locked。
     */
    void EnsureUnlockDependencies();
};

// Three concrete tree classes, one per upgrade track.
// ★v0.2.24 起不再注册（用户定案"三合一"），类保留只为兼容旧引用与 diff 可读性。
UCLASS()
class REMOTE_FACTORY_API UWTResearchTreeStorage : public UWTResearchTreeBase { GENERATED_BODY() };
UCLASS()
class REMOTE_FACTORY_API UWTResearchTreePower   : public UWTResearchTreeBase { GENERATED_BODY() };
UCLASS()
class REMOTE_FACTORY_API UWTResearchTreeAccel   : public UWTResearchTreeBase { GENERATED_BODY() };

/**
 * ★v0.2.24 三合一：一棵树装下全部三条升级轨道（3 轨 x 20 级 = 60 节点）。
 *
 * 用户实测反馈：MAM 左侧列表里出现三条"无线终端 xxx 升级"，既占地方又不像原版的树。
 * 原版一颗 MAM 树内部本就允许多条分支，所以合并成一颗树、三条分支是更贴近原版观感的结构。
 * 节点顺序 = 轨道外层、等级内层（Storage 1..20, PowerStorage 1..20, Accelerator 1..20）。
 */
UCLASS()
class REMOTE_FACTORY_API UWTResearchTreeMerged : public UWTResearchTreeBase
{
    GENERATED_BODY()
};

/**
 * ★2026-11-07 MAM 连线根治：当前生效的研究树 UClass。
 * 优先返回**烘焙好的蓝图资产类** BP_WT_ResearchTree_C（MAM 只给资产树画连线），
 * 资产缺失则回退纯 C++ 类 UWTResearchTreeMerged（最差等于旧行为）。
 * 所有「写/读树节点」「判断是不是我们这棵树」的地方都必须走它，不得硬编码类。
 */
UClass* WT_GetTreeClass();

/** 判定给定树类是否「我们的研究树」（资产类或其回退父类）。 */
bool WT_IsOurResearchTree(UClass* TreeClass);

/** All research-tree classes, for registration. v0.2.24 起只返回合并后的那一棵。 */
TArray<TSubclassOf<UFGResearchTree>> WT_GetAllResearchTrees();

/**
 * Build the three tree CDOs and attach their (20) nodes. Safe to call at
 * POST_INITIALIZATION (content mounted, non-constructor context). Idempotent.
 */
void WT_BuildResearchTrees();

/**
 * ★2026-11-05 用户定稿的「一个方块连买 4 次」落地实现：
 * 买完 Lv1 后，把该方块**同一个坐标**上的节点换成 Lv2（Lv2 覆盖 Lv1），
 * Lv1 的数值效果不变（升级值取「该轨最高已购级」，存档里那条记录一直在）。
 *
 * 必须周期调用（世界侧），因为 MAM 图按坐标索引，同一坐标任何时刻只能有 1 个节点。
 * 幂等：没有发生推进时不重写树的 mNodes。
 */
void WT_AdvanceResearchBlocks(UWorld* World);

/**
 * ★2026-11-06 崩溃根治：强校验 + 损坏即重建。
 * 在崩溃发生的那一帧（AFGResearchManager::IsTreeFullyResearched 遍历 mNodes）之前调用，
 * 若发现 mNodes 中任何节点悬空（释放后被复用、vtable 已损坏，IsValid 会漏判），
 * 立即整树重建 + 重新推进到正确等级，保证遍历永远拿到有效节点。
 */
void WT_ValidateAndRepairTree(UWorld* World);

/**
 * ★2026-11-06 #44（落地）：研究完成「即时显示下一级」，无需手动关闭重开 MAM。
 *
 * 背景：MAM 研究完成时往往只刷新「已完成节点」的视觉，不会重新读整棵合并树，
 * 于是 WT_AdvanceResearchBlocks 即便把 mNodes 推进到下一级，已打开的面板也读不到
 * （只有彻底重开 MAM 才会走 IsTreeFullyResearched 钩子重新读树 ⇒ 才见 LV2）。
 * 本类订阅 AFGResearchManager 的动态委托 OnResearchStateChanged / OnResearchCompleted，
 * 在研究「完成」那一刻**预先**把合并树推进到下一级 + 刷新已放置升级建筑的实际容量，
 * 使 MAM 随后（其自身处理器）重读树时拿到的已是更新后的 mNodes ⇒ LV2 立刻出现。
 *
 * 我们的处理器在模块 INITIALIZATION（游戏启动）时注册，早于 MAM 控件在打开时才绑定的
 * 自身处理器，故委托按注册顺序触发时本处理器先跑、MAM 后跑，时序正确。
 * 三个推进路径（1s 定时器 / IsTreeFullyResearched 钩子 / 本委托）全部幂等，互为兜底。
 */
UCLASS()
class REMOTE_FACTORY_API URemoteFactoryResearchRefresh : public UObject
{
    GENERATED_BODY()
public:
    /** 研究状态变化（开始/完成）即推进合并树 + 刷新已放置升级建筑。 */
    UFUNCTION()
    void HandleResearchStateChanged(EResearchState State);

    /** 研究完成（带 schematic）即推进合并树 + 刷新已放置升级建筑。 */
    UFUNCTION()
    void HandleResearchCompleted(TSubclassOf<UFGSchematic> Schematic);
};
