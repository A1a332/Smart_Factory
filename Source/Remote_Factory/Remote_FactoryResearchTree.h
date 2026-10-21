#pragma once

#include "CoreMinimal.h"
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

/** All research-tree classes, for registration. v0.2.24 起只返回合并后的那一棵。 */
TArray<TSubclassOf<UFGResearchTree>> WT_GetAllResearchTrees();

/**
 * Build the three tree CDOs and attach their (20) nodes. Safe to call at
 * POST_INITIALIZATION (content mounted, non-constructor context). Idempotent.
 */
void WT_BuildResearchTrees();
