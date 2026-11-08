#pragma once

#include "CoreMinimal.h"
// ★1.0.28k LookupMilestone 的形参用到 EResourceNodeType：它是 UENUM 枚举，声明在本头
// （一手核实：FGResourceNodeBase.h:24 `enum class EResourceNodeType : uint8`）。
// 只 include CoreMinimal.h 时 C++ 编译会报 C2061「未标识符 EResourceNodeType」，
// 连带 LookupMilestone 定义报 C2244（与声明不匹配）——故此处显式引入。
#include "Resources/FGResourceNodeBase.h"

/**
 * 文件驱动的全图资源节点（矿点）注册表 —— 镜像 FWTCollectibles。
 *
 * 矿点不是建筑（AFGResourceNode 不派生 AFGBuildable），且 World Partition 只驻留玩家附近的格子，
 * 实时扫描只能看到局部。为给终端一个真正的全图矿点视图，矿点坐标预先 dump 进
 * SmartFactoryLib 根的 resourcenodes.json 随发布包分发；面板在收到扫描回包后读取并并入【建筑】组下的
 * 资源节点子目，控制台命令 wt.DumpResourceNodes 扫描当前已加载地图并合并进文件。
 *
 * 模组矿点只要派生自 AFGResourceNodeBase（资源节点族总根）即被收进此类，绝不进 MOD建筑（MOD建筑只在
 * AFGBuildable 循环判定）。与 collectibles.json 同源：路径经 IPluginManager::FindPlugin("SmartFactoryLib") 解析。
 */
struct FWT_ResourceNodeSpawn
{
    /** 世界坐标（厘米）。 */
    FVector Location = FVector::ZeroVector;

    /** 资源显示名（dump 时由 UFGItemDescriptor::GetItemName 取，如「铁矿石」）。 */
    FString ResourceName;

    /** 纯度文本（枚举映射：贫瘠 / 普通 / 纯）。 */
    FString Purity;

    /** 资源物品描述符路径（best-effort 用于图标解析；mod 资源描述符未加载时解析失败则无图标）。 */
    FString ResourceClassPath;

    /** 是否模组矿点（包路径不在 /Script/FactoryGame 与 /Game/FactoryGame 之下）。 */
    bool bIsMod = false;

    /** 里程碑排序序位（HUB 升级层 + 太空电梯阶段）。0=最早；缺失默认 99（沉底）。 */
    int32 MilestoneTier = 99;

    /** 里程碑 / 太空电梯阶段标签（如「Tier 3 · 燃煤发电（太空电梯 阶段1）」），仅面板分组头展示用。 */
    FString MilestoneLabel;
};

/**
 * 纯度来源判定（2026-10-25 用户需求：识别 MOD 改造过的纯度）。
 *
 * 原版 1.0 三纯度混合（th.gl 459 节点实测：贫瘠 25% / 普通 42% / 纯 33%，与 wiki 12/12 一致）。
 * 若世界里纯度被改造（用户装的全纯 MOD、或「随机节点」自定义游戏模式），扫描出的纯度就不是原版值，
 * 静态库/面板必须能标明，否则对外发布时会误导其他玩家。
 */
enum class EWT_PuritySource : uint8
{
    /** 原版三纯度混合（未改造）。 */
    Vanilla = 0,
    /** 自定义游戏模式强制（GameState 的 NodePuritySettings != NPS_NoChange，如「随机节点·全纯」）。 */
    GameModeForced = 1,
    /** MOD 改造（扫描分布异常：单一档占比 >= 85%，如全纯 MOD 把全部节点设成纯）。 */
    Modded = 2,
    /** 样本不足（<20 个节点），无法判定。 */
    Unknown = 3,
};

/** 一次扫描的纯度审计摘要（写进 resourcenodes.json 顶层 purityAudit 块 + 探针日志）。 */
struct FWT_PurityAudit
{
    EWT_PuritySource Source = EWT_PuritySource::Unknown;

    /** 人类可读判据说明（如「纯 100.0% (507/507)；原版三纯度混合（贫~25%/普~42%/纯~33%）⇒ MOD 改造」）。 */
    FString Detail;

    int32 Impure = 0;
    int32 Normal = 0;
    int32 Pure   = 0;
    int32 Total  = 0;

    /** bIsMod 节点数（mod 自建矿点）。 */
    int32 ModNodes = 0;

    /** 井主节点数（纯度取自卫星众数，非自身属性）。 */
    int32 WellCores = 0;

    /** 纯度走了反射兜底读取的节点数（Cast<AFGResourceNode> 失败的 Base 支系 mod 节点）。 */
    int32 ReflectionReads = 0;
};

namespace FWTResourceNodes
{
    /** resourcenodes.json 绝对路径（SmartFactoryLib 根，经 IPluginManager 解析）。 */
    FString GetResourceNodesFilePath();

    /** 解析 resourcenodes.json 的 nodes 数组到 Out。文件存在且解析成功返回 true（Out 可为空）。
     *   AuditOut 接收顶层 purityAudit 审计块（旧文件无此块时保持 Unknown/空值）。 */
    bool LoadSpawns(TArray<FWT_ResourceNodeSpawn>& Out, FWT_PurityAudit* AuditOut = nullptr);

    /** 把 NewOnes 去重（资源名 + 坐标四舍五入到米）合并进现有文件并重写。重复运行累积全图。
     *  Audit 为本次扫描的纯度审计摘要，写入顶层 purityAudit 块；
     *  仅当「有新增节点」或「审计内容变化」才重写文件（无变化时保持原样，避免时间戳抖动）。
     *  ★1.0.28k MOD 矿点（bIsMod）永不入库：手动放置的节点是"活体"（随时可能被拆除），
     *  合并进静态快照后拆除也无法移除（用户实测残留+阶段/图标错乱）⇒ 跳过不合并，
     *  且加载时把历史版本混入的 mod 条目一并清洗；展示全走实时扫描条目（拆除即消失）。 */
    bool MergeSpawns(const TArray<FWT_ResourceNodeSpawn>& NewOnes, const FWT_PurityAudit& Audit);

    /** ★1.0.28k 里程碑排序序位/分组头查表（供实时扫描条目与静态库同口径定 tier）。
     *  先按描述符短名（Desc_OreIron_C 等）+ 节点类型查；未命中且为普通矿点时按资源显示名
     *  （铁矿石/SAM物质 等）兜底；仍查不中（mod 独有资源）返回 99 + 空标签（沉底、无阶段头）。 */
    void LookupMilestone(const FString& ShortName, const FString& ResourceName,
                         EResourceNodeType Type, int32& OutTier, FString& OutLabel);

    /** 判定本次扫描的纯度来源（原版 / 游戏模式强制 / MOD 改造 / 样本不足），并填充审计摘要。 */
    EWT_PuritySource DetectPuritySource(UWorld* World, const TArray<FWT_ResourceNodeSpawn>& Spawns,
                                        FWT_PurityAudit& OutAudit);

    /** 纯度来源 → 短标签（面板 / 日志用，如「MOD 改造」）。 */
    const TCHAR* PuritySourceText(EWT_PuritySource Source);

    /** 稳定合并键（资源名 + 坐标四舍五入到米）。 */
    FString NodeKey(const FWT_ResourceNodeSpawn& S);

    /** 加载存档后自动扫描当前已驻留的全图资源节点并去重合并进 resourcenodes.json
     *  （资源井只收主节点 AFGResourceNodeFrackingCore，不收卫星子节点）。 */
    void AutoScanMergeIntoFile(UWorld* World);
}
