#pragma once

#include "FGRemoteCallObject.h"
#include "Remote_FactoryTypes.h"
#include "Remote_FactoryRCO.generated.h"

class AFGBuildable;
class AActor;

/** Server -> client: here is a freshly scanned machine list. */
DECLARE_MULTICAST_DELEGATE_OneParam(FWT_OnMachineListReceived, const FWT_MachineListReply&);

/** Server -> client: open this buildable's native interact widget. */
DECLARE_MULTICAST_DELEGATE_OneParam(FWT_OnInteractUIRequested, AFGBuildable* /*Target*/);

/** Server -> client: a remote pull finished. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FWT_OnPullResult, AFGBuildable* /*Target*/, int32 /*ItemsMoved*/, bool /*bSuccess*/);

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
    void Server_RequestOpenUI(class AFGBuildable* Target);

    /**
     * Pull every item out of the target's output inventory into the player's inventory.
     * Server-authoritative; the client only sees the result.
     */
    UFUNCTION(Server, Reliable)
    void Server_PullOutput(class AFGBuildable* Target);

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
    void Client_OpenInteractUI(class AFGBuildable* Target);

    /** Server -> owning client: a pull finished. */
    UFUNCTION(Client, Reliable)
    void Client_NotifyPullResult(class AFGBuildable* Target, int32 ItemsMoved, bool bSuccess);

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

    // ---- local (client-side) event hooks the terminal panel subscribes to ----
    FWT_OnMachineListReceived OnMachineListReceived;
    FWT_OnInteractUIRequested  OnInteractUIRequested;
    FWT_OnPullResult           OnPullResult;
    FWT_OnTeleportResult       OnTeleportResult;
    FWT_OnSwitchResult         OnSwitchResult;

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

protected:
    /** RCOs need at least one replicated field to participate in the net driver. */
    UPROPERTY(Replicated)
    int32 DummyReplicatedField = 0;
};
