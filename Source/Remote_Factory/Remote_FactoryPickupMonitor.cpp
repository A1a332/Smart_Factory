#include "Remote_FactoryPickupMonitor.h"
#include "Remote_FactoryCollectibles.h"
#include "Remote_FactoryLog.h"
#include "Remote_FactoryTypes.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "FGItemPickup.h"
#include "FGDropPod.h"

// ----------------------------------------------------------------------------
// FWTPickupMonitor (★0.2.127) -- see header for the design rationale.
// ----------------------------------------------------------------------------

void FWTPickupMonitor::EnsureCreated()
{
    static FWTPickupMonitor* GMonitor = nullptr;
    if (!GMonitor)
    {
        GMonitor = new FWTPickupMonitor();
        UE_LOG(LogRemote_Factory, Warning,
               TEXT("[collect] pickup monitor created (0.5s poll, writes RF_collected.json directly)"));
    }
}

UWorld* FWTPickupMonitor::GetPlayWorld() const
{
    if (GEngine == nullptr)
    {
        return nullptr;
    }
    for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
    {
        UWorld* World = Ctx.World();
        if (World && World->IsGameWorld())
        {
            return World;
        }
    }
    return nullptr;
}

void FWTPickupMonitor::RefreshSpawnCache()
{
    const double Now = FPlatformTime::Seconds();
    if ((Now - SpawnCacheTime) < 120.0 && SpawnIdxByKey.Num() > 0)
    {
        return; // cache still fresh
    }

    SpawnList.Reset();
    SpawnIdxByKey.Reset();
    SpawnLocByKey.Reset();
    CachedKeys.Reset();
    TArray<FWT_CollectibleSpawn> Loaded;
    if (FWTCollectibles::LoadSpawns(Loaded))
    {
        for (int32 i = 0; i < Loaded.Num(); ++i)
        {
            const FWT_CollectibleSpawn& S = Loaded[i];
            const FString Key = FWTCollectibles::CollectibleKey(S.Kind, S.Location);
            SpawnIdxByKey.Add(Key, SpawnList.Num());
            SpawnLocByKey.Add(Key, S.Location);
            SpawnList.Add(S);
            CachedKeys.Add(Key);
        }
    }
    SpawnCacheTime = Now;
    UE_LOG(LogRemote_Factory, Verbose,
           TEXT("[collect] monitor spawn cache refreshed: %d board spawns"), SpawnList.Num());
}

int32 FWTPickupMonitor::FindSpawnIndex(uint8 Kind, const FVector& Loc) const
{
    // The pickup actor sits exactly at its spawn point, but rounding both sides to the nearest
    // metre can straddle a cell border (e.g. 100.6 -> 101 vs 100.4 -> 100). Probe the 3x3x3
    // neighbourhood so a sub-metre offset can never miss.
    const int32 RX = FMath::RoundToInt(Loc.X / 100.f);
    const int32 RY = FMath::RoundToInt(Loc.Y / 100.f);
    const int32 RZ = FMath::RoundToInt(Loc.Z / 100.f);
    for (int32 dx = -1; dx <= 1; ++dx)
    {
        for (int32 dy = -1; dy <= 1; ++dy)
        {
            for (int32 dz = -1; dz <= 1; ++dz)
            {
                const FString Key = FString::Printf(TEXT("%d|%d|%d|%d"), Kind, RX + dx, RY + dy, RZ + dz);
                if (const int32* Found = SpawnIdxByKey.Find(Key))
                {
                    return *Found;
                }
            }
        }
    }
    return INDEX_NONE;
}

FVector FWTPickupMonitor::GetPlayerLocation(UWorld* World) const
{
    if (!World)
    {
        return FVector::ZeroVector;
    }
    if (APlayerController* PC = World->GetFirstPlayerController())
    {
        if (APawn* Pawn = PC->GetPawn())
        {
            return Pawn->GetActorLocation();
        }
    }
    return FVector::ZeroVector;
}

void FWTPickupMonitor::Tick(float DeltaTime)
{
    // Poll cadence: 0.5 s.
    Accum += DeltaTime;
    if (Accum < 0.5f)
    {
        return;
    }
    Accum = 0.f;

    UWorld* World = GetPlayWorld();
    if (!World)
    {
        return;
    }

    RefreshSpawnCache();

    if (!bCollectedLoaded)
    {
        TSet<FString> IgnoredPresent;
        FWTCollectibles::LoadCollectedState(CollectedKeys, IgnoredPresent);
        bCollectedLoaded = true;
    }

    const FVector PlayerLoc = GetPlayerLocation(World);
    const bool bHavePlayer = !PlayerLoc.IsZero();
    // A spawn is only concluded "collected" when the player is within this radius of it, so a
    // far-away collectible that simply streams out is never mistaken for a collection.
    const float PickupGateRadius = 75.f * 100.f; // 75 m

    // Tighter gate for the "never seen present" branch (teleport + instant pickup). A spawn concluded
    // collected purely from absence must be very close to the player -- within streaming range so an
    // uncollected item is guaranteed loaded (no false-positive on a not-yet-streamed-in spawn). (0.2.130)
    const float PickupGateRadiusTight = 20.f * 100.f; // 20 m

    bool bDirty = false;
    // Spawns that currently have a live, UNPICKED actor in the world (== "present this poll").
    TSet<FString> CurrentLiveKeys;

    // ---- Pass 1: live collectible actors -> build the present set + catch the IsPickedUp() window. ----
    // AFGItemPickup (slugs / spheres / loops / forage foods) is DESTROYED the instant it is picked
    // up, but IsPickedUp() is briefly observable before destruction; AFGDropPod persists, so
    // HasBeenLooted() is reliably observable on the live actor.
    for (TActorIterator<AFGItemPickup> It(World); It; ++It)
    {
        AFGItemPickup* Pickup = *It;
        if (!IsValid(Pickup) || Pickup->GetItemState() == FGEItemState::ES_SEED)
        {
            continue;
        }
        FText LabelUnused;
        const uint8 Kind = FWTCollectibles::ClassifyCollectibleItem(Pickup->GetPickupItemClass().Get(), LabelUnused);
        if (Kind == 0xFF)
        {
            continue; // not one of the board's collectible kinds
        }
        const int32 SpawnIdx = FindSpawnIndex(Kind, Pickup->GetActorLocation());
        if (SpawnIdx == INDEX_NONE)
        {
            // Diagnostic (0.2.130, per 大肥鱼): a recognized collectible that is NOT on the board
            // spawns list -- lets us confirm nothing is being silently skipped. Verbose to stay quiet
            // in shipping.
            if (!Pickup->IsPickedUp())
            {
                UE_LOG(LogRemote_Factory, Verbose,
                       TEXT("[collect] skip (not on board): class=%s kind=%d loc=%.0f,%.0f,%.0f"),
                       *GetNameSafe(Pickup->GetPickupItemClass().Get()), Kind,
                       Pickup->GetActorLocation().X, Pickup->GetActorLocation().Y, Pickup->GetActorLocation().Z);
            }
            continue; // pickup outside the board (spawn not in collectibles.json)
        }
        const FString& Key = CachedKeys[SpawnIdx];
        if (Pickup->IsPickedUp())
        {
            if (!CollectedKeys.Contains(Key))
            {
                CollectedKeys.Add(Key);
                bDirty = true;
                WT_WARNF(TEXT("[collect] PICKUP EVENT -> %s (monitor poll caught IsPickedUp)"), *Key);
            }
        }
        else
        {
            CurrentLiveKeys.Add(Key);
        }
    }

    // AFGDropPod (crash pods) persists after looting, so HasBeenLooted() is reliably observable on
    // the live actor -- record directly.
    for (TActorIterator<AFGDropPod> It(World); It; ++It)
    {
        AFGDropPod* Pod = *It;
        if (!IsValid(Pod))
        {
            continue;
        }
        const int32 SpawnIdx = FindSpawnIndex(static_cast<uint8>(EWT_CollectibleKind::DropPod), Pod->GetActorLocation());
        if (SpawnIdx == INDEX_NONE)
        {
            continue;
        }
        const FString& Key = CachedKeys[SpawnIdx];
        if (Pod->HasBeenLooted())
        {
            if (!CollectedKeys.Contains(Key))
            {
                CollectedKeys.Add(Key);
                bDirty = true;
                WT_WARNF(TEXT("[collect] PICKUP EVENT -> %s (monitor poll caught DropPod looted)"), *Key);
            }
        }
        else
        {
            CurrentLiveKeys.Add(Key);
        }
    }

    // ---- Pass 2: BOARD-CENTRIC collection detection (robust against teleport + instant pickup). ----
    // The old design only promoted a spawn to "collected" when it had been seen present in a PREVIOUS
    // poll and went absent -- so teleporting right next to an item and grabbing it within one poll
    // window (streaming-in and destruction collide in the same poll) was never detected (大肥鱼, 0.2.130).
    // We now iterate every board spawn, query its live state directly, and conclude "collected" only
    // when the player is near (gate) AND the actor is absent -- confirmed across two polls
    // (PendingAbsent) so not-yet-streamed-in items are not false-flagged.
    TSet<FString> NearPresent; // live AND near this poll == the baseline for present->absent
    if (bHavePlayer)
    {
        for (int32 i = 0; i < SpawnList.Num(); ++i)
        {
            const FWT_CollectibleSpawn& S = SpawnList[i];
            const FString& Key = CachedKeys[i];
            if (CollectedKeys.Contains(Key))
            {
                continue;
            }
            const bool bLive = CurrentLiveKeys.Contains(Key);
            const float Dist = FVector::Dist(PlayerLoc, S.Location);
            if (bLive)
            {
                if (Dist <= PickupGateRadius)
                {
                    NearPresent.Add(Key); // live + near => confirmed present baseline
                }
                PendingAbsent.Remove(Key);
                continue;
            }
            // Not live in the world this poll.
            if (PreviousPresent.Contains(Key))
            {
                // Seen present+near last poll, now gone -> genuine pickup. No tight gate needed because
                // we already proved it was there; just require the player still within the wide gate so a
                // player that ran far away doesn't retroactively conclude.
                if (Dist <= PickupGateRadius)
                {
                    CollectedKeys.Add(Key);
                    bDirty = true;
                    PendingAbsent.Remove(Key);
                    WT_WARNF(TEXT("[collect] PICKUP EVENT -> %s (monitor poll caught present->absent)"), *Key);
                }
            }
            else if (Dist <= PickupGateRadiusTight)
            {
                // Never observed present (e.g. teleport right next to it and grabbed it). Use a tight
                // gate + two-poll confirmation so an uncollected, not-yet-streamed-in spawn is never
                // false-flagged (streaming loads well within 20 m).
                if (PendingAbsent.Contains(Key))
                {
                    CollectedKeys.Add(Key);
                    bDirty = true;
                    PendingAbsent.Remove(Key);
                    WT_WARNF(TEXT("[collect] PICKUP EVENT -> %s (monitor poll confirmed near+absent)"), *Key);
                }
                else
                {
                    PendingAbsent.Add(Key);
                }
            }
            else
            {
                PendingAbsent.Remove(Key); // far away: drop any pending state
            }
        }
    }
    PreviousPresent = NearPresent;

    if (bDirty)
    {
        TSet<FString> EmptyPresent;
        const bool bSaved = FWTCollectibles::SaveCollectedState(CollectedKeys, EmptyPresent);
        UE_LOG(LogRemote_Factory, Warning,
               TEXT("[collect] monitor persisted %d collected keys (saved=%s)"),
               CollectedKeys.Num(), bSaved ? TEXT("true") : TEXT("false"));
    }
}
