#pragma once

#include "CoreMinimal.h"

/**
 * File-backed full-map alien-energy collectible registry.
 *
 * Satisfactory 1.1 uses World Partition. A Shipping build has NO lightweight API to enumerate actors
 * that are not currently streamed into memory, so a live scan only ever sees the cells near the
 * player. To give the terminal a TRUE whole-map view, the collectible spawn points are pre-generated
 * into `collectibles.json` at the mod root and shipped with the release. This module owns that file:
 * the panel loads it at open, and the one-shot console command `wt.DumpCollectibles` scans whatever
 * is currently loaded and merges it into the file.
 *
 * The file lives in the dedicated dependency mod SmartFactoryLib's root directory (shipped
 * alongside Remote_Factory). Alpakit packages loose files at that mod's root into the release, so
 * a player who downloads the mods gets the coordinates immediately -- no scan, no save game
 * required. Smart_Factory resolves the path via IPluginManager::FindPlugin("SmartFactoryLib").
 * See also Remote_FactoryPanelWidget::RebuildDisplayCollectibles.
 */
struct FWT_CollectibleSpawn
{
    /** EWT_CollectibleKind value (0..5). Kept as uint8 to stay header-light. */
    uint8 Kind = 0;

    /** World-space spawn location (centimetres). */
    FVector Location = FVector::ZeroVector;

    /**
     * ★0.2.136 TRUE when this spawn sits inside a cave (地下溶洞).
     *
     * Why it matters: teleporting at a cave coordinate from outside used to drop the player into the
     * void (0.2.135 fixed the fallout), and a cave spawn simply cannot be reached until the player
     * physically enters that cave, so the panel now tags these rows.
     *
     * Source of truth: collectibles.json may carry an explicit "cave" bool (written by a future
     * dump that can ask the world directly). When absent we fall back to the altitude heuristic
     * below — see WT_CAVE_Z_THRESHOLD for how that number was validated.
     */
    bool bCave = false;
};

/**
 * ★0.2.136 cave altitude heuristic (centimetres, i.e. -60 m).
 *
 * Validated against the shipped collectibles.json (7065 spawns): 213 spawns sit below -6000 and
 * **100% of them** have a distinctly higher spawn within a 6 km grid cell above them — exactly the
 * signature of "underground, below the surface" — while they cluster into 4-5 tight XY groups (the
 * game's caves). Looser thresholds stop being caves and start being ordinary low ground:
 * Z<-3000 catches 583 spawns scattered over 105 grid cells, Z<-5000 catches 348 over 71 cells.
 * Tune this ONE constant if a future game update changes the map; nothing else needs to change.
 */
constexpr float WT_CAVE_Z_THRESHOLD = -6000.f;

/** ★0.2.136 cheapest possible cave test: deep enough below the map's surface band to be a cave. */
inline bool WT_IsCaveLocation(const FVector& Loc)
{
    return Loc.Z < WT_CAVE_Z_THRESHOLD;
}

namespace FWTCollectibles
{
    /** Absolute path to collectibles.json in this mod's root (resolved via IPluginManager). */
    FString GetCollectiblesFilePath();

    /**
     * Parse collectibles.json's `spawns` array into Out. Returns true if the file existed and parsed
     * (Out may still be empty when spawns is []), false when the file is missing or not valid JSON.
     */
    bool LoadSpawns(TArray<FWT_CollectibleSpawn>& Out);

    /**
     * Union NewOnes into the existing collectibles.json (de-duplicated by kind + rounded location),
     * then rewrite the file. Repeated runs therefore ACCUMULATE the whole map across sessions.
     * Returns true on a successful write.
     */
    bool MergeSpawns(const TArray<FWT_CollectibleSpawn>& NewOnes);

    /**
     * ★0.2.126: per-pickup "already collected" memory.
     *
     * Pickup actors (slugs / spheres / loops / foragable flora) are DESTROYED the instant the player
     * picks them up, so a fresh live scan can never re-detect them as collected -- the static
     * collectibles.json only knows spawn points, not picked state. To make the 【已拾取】 list real we
     * persist two key sets (kind + 1 m-rounded coord, same scheme as SpawnKey) in a writable JSON
     * under ProjectSavedDir (never inside the pak):
     *   - Collected : spawns the player has picked up (survives panel close + game restart).
     *   - Present   : spawns matched to a live actor in the PREVIOUS scan; a key that was Present
     *                 last scan but is absent this scan (and not just streamed out -- see the merge
     *                 logic) is promoted into Collected.
     */
    /** Writable path for the collected-state JSON (ProjectSavedDir/Remote_Factory/RF_collected.json). */
    FString GetCollectedFilePath();

    /** Stable merge key (kind + coord rounded to the nearest metre) -- mirrors SpawnKey. */
    FString CollectibleKey(uint8 Kind, const FVector& Loc);

    /** Load both sets; returns true if the file existed and parsed (sets may be empty). */
    bool LoadCollectedState(TSet<FString>& OutCollected, TSet<FString>& OutPresent);

    /** Persist both sets; returns true on a successful write. */
    bool SaveCollectedState(const TSet<FString>& Collected, const TSet<FString>& Present);

    // ---- ★0.2.127: shared classify + pickup monitor ------------------------------

    /**
     * Classify an item descriptor as one of the alien collectibles (kinds 0..8 incl. the three
     * forage foods), or 0xFF (= EWT_CollectibleKind::Count) if it is not one of them. Shared by
     * the RCO live scan AND the pickup monitor so both agree on what a collectible is.
     * On match, OutLabel receives the kind's display text (empty when unmatched).
     * @return the EWT_CollectibleKind value as uint8, or 0xFF when unmatched.
     */
    uint8 ClassifyCollectibleItem(const UClass* ItemClass, FText& OutLabel);

    /**
     * ★0.2.127: lazily create the global pickup monitor (a FTickableGameObject that polls
     * AFGItemPickup::IsPickedUp() and writes pickups straight into the collected set). Cheap to
     * call repeatedly; creation happens once per process. Called from the RCO scan handler and
     * the panel widget so the monitor is alive on any session that uses the collectible board.
     */
    void EnsurePickupMonitor();
}
