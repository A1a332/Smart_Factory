#pragma once

#include "CoreMinimal.h"
#include "Tickable.h"
#include "Remote_FactoryCollectibles.h"

/**
 * ★0.2.127: global pickup monitor -- the reliable "already collected" source.
 *
 * WHY: pickup actors (slugs / spheres / loops / forage foods) leave the live scan's visibility
 * the instant they are picked up (mDestroyOnPickup destroys the actor), so any scheme that
 * DERIVES "picked" from two successive 15 s scans (present -> absent) misses pickups that
 * happen between scans. The monitor instead POLLS AFGItemPickup::IsPickedUp() directly every
 * 0.5 s: the picked-up state (mPickupItems emptied) becomes visible BEFORE the actor is
 * destroyed, so a real pickup event is almost always observed at least once.
 *
 * A matched pickup is written STRAIGHT into the persisted collected set (RF_collected.json) --
 * no dependency on the panel being open, no scan-window arithmetic. Spawns are matched by
 * kind + a 3x3x3 metre-neighbourhood key lookup against collectibles.json so only spawns that
 * actually exist on the full-map board are recorded (guards against seed-state pickups with
 * empty stacks and any stray same-class pickups outside the board).
 */
class FWTPickupMonitor : public FTickableGameObject
{
public:
    /** Lazily create the singleton (no-op after the first call). */
    static void EnsureCreated();

    //~ FTickableGameObject
    virtual void Tick(float DeltaTime) override;
    virtual ETickableTickType GetTickableTickType() const override
    {
        // Always tick even when the game is paused (a paused world must still record pickups).
        return ETickableTickType::Always;
    }
    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(FWTPickupMonitor, STATGROUP_Tickables);
    }
    //~ End FTickableGameObject

private:
    FWTPickupMonitor() = default;

    /** Refresh the spawn-key cache from collectibles.json if stale. */
    void RefreshSpawnCache();

    /** Find the board spawn index for (Kind, Loc) within a +-1 m neighbourhood, or INDEX_NONE. */
    int32 FindSpawnIndex(uint8 Kind, const FVector& Loc) const;

    /** Current game world to scan (first game world in the world context list). */
    UWorld* GetPlayWorld() const;

    /** Local player pawn location, used to gate present->absent against stream-out false positives. */
    FVector GetPlayerLocation(UWorld* World) const;

    float Accum = 0.f;

    /** kind|rx|ry|rz -> index into SpawnList (same key scheme as FWTCollectibles::CollectibleKey). */
    TMap<FString, int32> SpawnIdxByKey;
    /** kind|rx|ry|rz -> board spawn location (for present->absent proximity gating). */
    TMap<FString, FVector> SpawnLocByKey;
    TArray<FWT_CollectibleSpawn> SpawnList;
    double SpawnCacheTime = -1e9;

    /** Board spawns seen present AND near the player in the PREVIOUS poll. Used for present->absent
     *  detection; only near+present counts as a baseline so a far-away, streamed-out spawn is never
     *  mistaken for a collection. (0.2.130) */
    TSet<FString> PreviousPresent;

    /** Spawns that were near + absent for exactly ONE poll. Confirmed as collected on the next poll
     *  so teleport-right-next-to-an-item-and-grab-it (which is never observed "present") is still
     *  caught, without false-positiving on items that simply have not streamed in yet. (0.2.130) */
    TSet<FString> PendingAbsent;

    /** Parallel to SpawnList: the CollectibleKey for each board spawn, cached in RefreshSpawnCache so
     *  the per-poll board scan does not re-allocate a key string for every one of the ~7000 spawns. (0.2.130) */
    TArray<FString> CachedKeys;

    /** In-memory collected set, kept in sync with RF_collected.json. */
    TSet<FString> CollectedKeys;
    bool bCollectedLoaded = false;
};
