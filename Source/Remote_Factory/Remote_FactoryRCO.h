#pragma once

#include "FGRemoteCallObject.h"
#include "Remote_FactoryTypes.h"
#include "Remote_FactoryRCO.generated.h"

class AFGBuildable;
class AActor;
class UFGItemDescriptor;

/** Server -> client: here is a freshly scanned machine list. */
DECLARE_MULTICAST_DELEGATE_OneParam(FWT_OnMachineListReceived, const FWT_MachineListReply&);

/** Server -> client: open this buildable's native interact widget. */
DECLARE_MULTICAST_DELEGATE_OneParam(FWT_OnInteractUIRequested, AActor* /*Target*/);

/** Server -> client: a remote pull finished. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FWT_OnPullResult, AActor* /*Target*/, int32 /*ItemsMoved*/, bool /*bSuccess*/);

/**
 * ★0.2.135 teleport failure reasons.
 *
 * None         — the move happened (bSuccess == true).
 * TargetGone   — the target actor could not be resolved / the character was missing.
 * NoSafeLanding— safe-landing search found no spot with ground and a clear body capsule.
 *                In practice this means "that cell is not streamed in" (deep caves, the classic
 *                case: teleporting at a cave coordinate used to drop the player into the void).
 *                The panel must NOT move the player in this case — see Server_TeleportTo*.
 */
namespace WTTeleportFail
{
    enum Type : int32 { None = 0, TargetGone = 1, NoSafeLanding = 2 };
}

/** Server -> client: a teleport request finished (or was refused). */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FWT_OnTeleportResult, AActor* /*Target*/, bool /*bSuccess*/, int32 /*Reason*/);

/**
 * Server -> client: a switch / priority write finished.
 *
 * bSuccess is false when the target was not a switch, was unwired, or the caller lacked permission
 * (Form 3 "D": only the shared-session owner may write). The panel turns that into a status line.
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FWT_OnSwitchResult, AFGBuildable* /*Target*/, bool /*bSuccess*/, const FText& /*Message*/);

/** One entry of the smart-warehouse contribution ledger: how many of ItemClass in the central pool came from vanilla storage containers. (TMap is not RPC-serializable, so the RPC carries an array of these; the client rebuilds the map from it.) */
USTRUCT(BlueprintType)
struct FWT_SWContributionEntry
{
    GENERATED_BODY()
    UPROPERTY() TSubclassOf<UFGItemDescriptor> ItemClass;
    UPROPERTY() int32 Amount = 0;
};

/** Per-item smart-warehouse contribution ledger: amount that came from vanilla storage containers, keyed by item class. Aliased so the delegate macro below does not see a comma inside the type. */
typedef TMap<TSubclassOf<UFGItemDescriptor>, int32> FWT_SWContributionMap;

/** Server -> client: the smart-warehouse contribution ledger (per-item amount that came from vanilla storage containers). Carries a rebuilt TMap on the client side. */
DECLARE_MULTICAST_DELEGATE_OneParam(FWT_OnSWContributionReceived, const FWT_SWContributionMap&);

/**
 * Remote Call Object for the Remote Factory.
 *
 * WHY AN RCO (same reasoning as WirelessPower): buildables are server-owned, so a client cannot
 * call Server RPCs on them directly — the RPC is dropped for lack of an owning net connection.
 * The standard Satisfactory pattern is a UFGRemoteCallObject "Within AFGPlayerController": the RCO
 * lives on the player's own controller, so its Server RPCs ride the player's connection.
 *
 * ## How a machine is identified across the wire
 *
 * Two tempting approaches are both WRONG and are recorded here so nobody retries them:
 *
 *   1. Send the buildable's world location. Satisfactory explicitly allows stacking structures
 *      (belts over machines, pipes through floors, poles on platforms), so several buildables can
 *      share a spot and the lookup becomes ambiguous.
 *   2. Keep the scan result on the server and ship an index. This works on a listen server but
 *      breaks on a dedicated server: the client would receive an index it has no array for.
 *
 * The correct approach is the one the game itself uses — see
 * AGameplayAbility / FGBuildGunDismantle::Client_NotifyActorsFailedDismantle, which sends
 * `const TArray<AActor*>&` over a Client RPC. Unreal replicates UObject pointer parameters by
 * NetGUID and resolves them on the receiving side, yielding null when the object is not relevant
 * or is gone. So we simply pass the AFGBuildable* directly and let the engine do the work.
 */
UCLASS()
class REMOTE_FACTORY_API URemote_FactoryRCO : public UFGRemoteCallObject
{
    GENERATED_BODY()
public:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /**
     * Scan the world and hand back the machine list. Runs on the server because only the server
     * owns the authoritative AFGBuildableSubsystem, and because on a dedicated server the client
     * may not even have the far-away buildings streamed in.
     *
     * @param Origin          world position to measure distances from (usually the terminal)
     * @param MaxRangeMeters  0 = unlimited
     */
    UFUNCTION(Server, Reliable)
    void Server_ScanMachines(FVector Origin, float MaxRangeMeters);

    /**
     * Ask the server to open the native interact UI of a given buildable for THIS player.
     *
     * This is the heart of the mod. The server checks the buildable really has an interact widget
     * class, then tells the requesting client to spawn that widget with mInteractObject pointing
     * at the buildable — exactly what the game does when you walk up and press E, minus the walk.
     */
    UFUNCTION(Server, Reliable)
    void Server_RequestOpenUI(class AActor* Target);

    /**
     * Pull every item out of the target's output inventory into the player's inventory.
     * Server-authoritative; the client only sees the result.
     *
     * ★1.0.8 参数放宽为 AActor：死亡箱子 AFGCrate 派生自 AFGInteractActor（不是 AFGBuildable），
     * 但「远程取回死亡掉落物」正是用户要它进列表的主要用途，所以取物链不能只认建筑。
     * 实现侧按类型分派：Manufacturer 取输出库、AFGCrate 取箱子库存。
     */
    UFUNCTION(Server, Reliable)
    void Server_PullOutput(class AActor* Target);

    /**
     * ★2026-11-03 集装箱面板「取出」：把 Want 个 Cls 从全部原版存储集装箱移除并转入请求玩家的背包。
     * 服务端权威（集装箱是 actor 库存，客户端改了不同步）；内部取本玩家背包后调
     * Remote_FactorySmartWarehouse::TransferFromContainersToInventory。语义对齐原版位面面板的取出交互。
     *
     * 玩家会遇到的情况（实现侧都已覆盖，见 Server_TakeFromContainer_Implementation 注释）：
     *   · 正常 → 从多个集装箱各拿一点，直到凑够 Want 或取空
     *   · 背包满 → 一个都放不进 ⇒ 集装箱一个都不减（先加后删，绝不蒸发）
     *   · 只放得下一部分 → 只搬放得下的，剩下留箱
     *   · Want 超存量 → 按实际扫到的量取
     * 返回给客户端的是 Got（经日志可见），目前不额外回执；界面靠 0.5s 刷新自然跟随。
     */
    UFUNCTION(Server, Reliable)
    void Server_TakeFromContainer(TSubclassOf<UFGItemDescriptor> Cls, int32 Want, AActor* OnlyContainer);

    /**
     * ★2026-11-05 集装箱面板「存入」（单种）：把请求玩家背包里的 Want 个 Cls 存进全部原版存储集装箱。
     * 服务端权威；内部取本玩家背包后调 Remote_FactorySmartWarehouse::TransferFromInventoryToContainers。
     * 与取出镜像，同样"先加后删" ⇒ 箱子满/背包满都不会丢东西。
     *
     * 会被拒绝的情况（实现侧已覆盖，见 Server_StoreToContainer_Implementation 注释）：
     *   · 非建造材料（装备/消耗品/弹药/占位类）→ 直接忽略，不报错
     *   · 背包里没有该物品 / 数量传 0 或负数   → 直接忽略
     *   · 没有可用集装箱 / 全都满              → 背包一个不减
     */
    UFUNCTION(Server, Reliable)
    void Server_StoreToContainer(TSubclassOf<UFGItemDescriptor> Cls, int32 Want, AActor* OnlyContainer);

    /**
     * ★2026-11-05 集装箱面板「全部存入」：把请求玩家背包里**所有建造材料**一次存进集装箱。
     * 装备/消耗品/弹药不参与（判据见 Remote_FactorySmartWarehouse::IsStorableMaterial）。
     * 服务端先拍一张背包物品快照再逐个转移 —— 绝不在遍历背包的循环里边遍历边删（索引会错位漏物品）。
     */
    UFUNCTION(Server, Reliable)
    void Server_StoreAllToContainers();

    /**
     * Teleport the requesting player to an arbitrary actor — a machine or a collectible.
     *
     * Server-authoritative everywhere: the server re-resolves the actor, drops the player a few
     * metres above it so they land rather than clip into geometry, and calls the character's own
     * SetActorLocation. Doing it server-side means a listen/dedicated host sees the same result and
     * the client cannot use this to move itself into unloaded space.
     *
     * Target is a plain AActor so one RPC covers both the machine list and the collectible list.
     * It rides the same NetGUID serialisation as every other reference in this mod.
     */
    UFUNCTION(Server, Reliable)
    void Server_TeleportTo(class AActor* Target);

    /**
     * Teleport the requesting player to a raw world location (no actor required). Used for the
     * file-listed collectibles whose cells are not streamed in: there is no actor to reference, only
     * a coordinate from collectibles.json. The landing point is a few metres above the coordinate so
     * the player does not clip into the ground.
     */
    UFUNCTION(Server, Reliable)
    void Server_TeleportToLocation(FVector Location);

    /**
     * ★0.2.153e 坐标直传：按世界坐标传送，不依赖 actor 引用。用于客户端未复制该 actor 的远端
     * 机器（NetGUID 解析 null，OnTeleport 按钮原本直接 return 无反应）。落点走机器通用环搜
     * （WT_FindSafeLandingSpot，方向以「朝玩家」为基准），算不出则兜底 Z300 砸落 + 0.6s ground-fix，
     * 与普通机器分支行为一致。Location/BaseRadius 来自扫描时写入的 FWT_MachineEntry。
     */
    UFUNCTION(Server, Reliable)
    void Server_TeleportToCoord(FVector Location, float BaseRadius);

    /**
     * Form 3 "A": turn a power switch (AFGBuildableCircuitSwitch, incl. the priority variant) on or
     * off from the terminal.
     *
     * SERVER-ONLY by necessity, not by preference: mIsSwitchOn is
     * `UPROPERTY(SaveGame, ReplicatedUsing=OnRep_IsSwitchOn)`, so SetSwitchOn() on a client would
     * flip a local copy that the next replication pass overwrites. Running it here means the
     * change is authoritative and the OnRep fires on every client at once -- which is also exactly
     * what Form 3 "D" (weak sharing) wants: two players watching the same switch see it flip.
     */
    UFUNCTION(Server, Reliable)
    void Server_SetSwitchOn(class AFGBuildable* Target, bool bOn);

    /**
     * Form 3 "A": set a priority power switch's shed priority.
     *
     * Higher number = shed sooner during a shortage; 0 or less = never auto-shed. No-op (with a
     * failure reply) when the target is a plain switch, which has no priority to set.
     */
    UFUNCTION(Server, Reliable)
    void Server_SetSwitchPriority(class AFGBuildable* Target, int32 Priority);

    /**
     * Form 3 "D": announce that THIS player has the terminal open, so the server can tell everyone
     * who else is looking at the same one.
     *
     * Deliberately cheap: it only records a viewer and pushes the updated roster back out. It does
     * NOT replicate the widget, the selection, or anything else about the UI -- the shared thing is
     * the FACTORY STATE (switch positions, power figures), which the server already owns.
     */
    UFUNCTION(Server, Reliable)
    void Server_AnnounceViewer(bool bViewing, class AFGBuildable* Terminal);

    /** Server -> owning client: here is the machine list you asked for. */
    UFUNCTION(Client, Reliable)
    void Client_ReceiveMachineList(const FWT_MachineListReply& Reply);

    /** Server -> owning client: open this buildable's interact widget now. */
    UFUNCTION(Client, Reliable)
    void Client_OpenInteractUI(class AActor* Target);

    /** Server -> owning client: a pull finished. ★1.0.8 放宽为 AActor（死亡箱子不是建筑）。 */
    UFUNCTION(Client, Reliable)
    void Client_NotifyPullResult(class AActor* Target, int32 ItemsMoved, bool bSuccess);

    /**
     * Server -> owning client: a teleport finished (or was refused).
     *
     * Reason is a WTTeleportFail::Type; NoSafeLanding (2) means the server refused to move the
     * player because no safe landing spot existed (unloaded cell / cave), which the panel surfaces
     * as its own message instead of the generic "target is gone".
     */
    UFUNCTION(Client, Reliable)
    void Client_NotifyTeleport(class AActor* Target, bool bSuccess, int32 Reason);

    /** Server -> owning client: a switch / priority write finished (or was refused). */
    UFUNCTION(Client, Reliable)
    void Client_NotifySwitchResult(class AFGBuildable* Target, bool bSuccess, const FText& Message);

    /** Server -> owning client: smart-warehouse contribution ledger (per-item amount that came from vanilla storage containers). Carried as an array (TMap is not RPC-serializable); clients rebuild the map and tint build/craft cost slots blue from it. */
    UFUNCTION(Client, Reliable)
    void Client_ReceiveSWContribution(const TArray<FWT_SWContributionEntry>& Contribution);

    /**
     * ★2026-11-04 智能仓储开关的联机同步（补一个此前会真实出问题的缺口）。
     *
     * 问题：开关状态是**存档里的服务端状态**（Remote_FactorySave 在 AFGGameState::Serialize 里读写），
     * 但切换按钮 `URemote_FactoryPanelWidget::OnSmartWarehouseToggleClicked` 只改了**本地静态量**。
     * 单人游戏服务端=客户端，看不出问题；联机时会出现：
     *   · 客户端开 → 本地变量变 true，面板开始显示集装箱货，但服务端仍关
     *     ⇒ 客户端取货 RPC 过去，服务端扫到的是空聚合表 ⇒ 玩家点了没反应，看着像 bug。
     *   · 客户端关 → 存盘时服务端的值原样写回，重启后"我的设置怎么又回去了"。
     *
     * 做法：客户端切换 → 发 Server_SetSmartWarehouse（服务端权威写入 + 刷聚合表）→
     *      服务端回 Client_SetSmartWarehouse 下发权威值 → 客户端以**服务端值**为准更新 UI。
     *      Host/单机走同一套（Server RPC 在权威端直接执行），不需要额外分支。
     */
    UFUNCTION(Server, Reliable)
    void Server_SetSmartWarehouse(bool bEnable);

    /** Server -> owning client: 下发服务端权威的智能仓储开关值（客户端据此更新开关文字与面板可见性）。 */
    UFUNCTION(Client, Reliable)
    void Client_SetSmartWarehouse(bool bEnable);

    // ---- local (client-side) event hooks the terminal panel subscribes to ----
    FWT_OnMachineListReceived OnMachineListReceived;
    FWT_OnInteractUIRequested  OnInteractUIRequested;
    FWT_OnPullResult           OnPullResult;
    FWT_OnTeleportResult       OnTeleportResult;
    FWT_OnSwitchResult         OnSwitchResult;
    FWT_OnSWContributionReceived OnSWContributionReceived;

    /**
     * Convenience: resolve the RCO living on a given player controller.
     *
     * The parameter is AFGPlayerController (not the engine base) because
     * GetRemoteCallObjectOfClass<T>() is declared there, so callers downcast first.
     *
     * NOT const: the RCO is mutable state -- its Server RPCs change the world -- so a const
     * handle would be a lie anyway.
     */
    static URemote_FactoryRCO* Get(class AFGPlayerController* PC);

    /** Client-side cache of the smart-warehouse contribution ledger, updated by Client_ReceiveSWContribution. Read this from cost-slot tinting hooks. */
    static const FWT_SWContributionMap& GetClientSWContribution();

protected:
    /** RCOs need at least one replicated field to participate in the net driver. */
    UPROPERTY(Replicated)
    int32 DummyReplicatedField = 0;
};
