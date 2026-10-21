#pragma once

#include "CoreMinimal.h"

class AFGPlayerController;
class AFGBuildable;
class UWorld;

/**
 * Form 3 "D": shared-session bookkeeping for the wireless terminal.
 *
 * ## What "sharing" means here
 *
 * The mod ships WEAK sharing, not a mirrored UI. Every player still opens their own panel; what is
 * shared is the factory underneath it. Two consequences fall out of that choice and both are
 * deliberate:
 *
 *   * Nothing about the widget is replicated. The panel is rebuilt from a scan reply that each
 *     client requests for itself, so D adds no widget state, no widget replication, and no
 *     "someone else moved my cursor" class of bug.
 *   * The only thing D has to decide is WHO MAY WRITE. Switch flips take effect for everyone the
 *     instant they replicate (that is the weak-sharing payoff you get for free), but a pull-output
 *     or a teleport is personal -- it moves items into ONE player's backpack -- so it needs an
 *     unambiguous owner.
 *
 * ## Why this is a namespace and not a UObject
 *
 * The natural home for "who is viewing what" is the game state, and the natural implementation
 * would be a replicated UPROPERTY there. We cannot add one: AFGGameState is a native game class and
 * a mod cannot inject a member into it. The available options were
 *
 *   1. a new replicated AActor spawned by the game mode, or
 *   2. a plain server-side registry keyed by player controller,
 *
 * and this is (2). It is strictly less machiner: the roster is only ever READ by the server (to
 * answer "may this player write?" and to stamp the reply's viewer list), and the answers travel
 * back to each client inside the machine-list reply they already receive. So there is nothing to
 * replicate, nothing to spawn, and nothing to clean up on a level transition beyond a reset.
 *
 * The trade-off is honest and worth stating: the roster does not survive a server restart and is
 * not queryable from a client. Neither is needed today.
 *
 * ## Lifetime
 *
 * Entries are held as TWeakObjectPtr, so a player who disconnects simply vanishes from the roster
 * the next time it is read. No explicit teardown hook is required, which is one fewer thing to get
 * wrong in a mod that already has to be careful about teardown.
 */
namespace Remote_FactoryShared
{
    /**
     * Record or clear this player's presence at a terminal.
     *
     * Called from URemote_FactoryRCO::Server_AnnounceViewer, i.e. server-side only. bViewing
     * false clears the entry; the first player to announce becomes the session owner.
     */
    void SetViewer(UWorld* World, AFGPlayerController* PC, bool bViewing, AFGBuildable* Terminal);

    /**
     * Is this player the owner of the currently shared session?
     *
     * Returns TRUE when nobody holds the session, so a solo player (and the very first player to
     * open a panel, before their own announce lands) is never locked out of their own terminal.
     */
    bool IsSessionOwner(UWorld* World, const AFGPlayerController* PC);

    /**
     * Fill a reply's shared-session fields for the player who is about to receive it.
     *
     * This is what makes the sharing VISIBLE: the panel can then show "Host: Alice (2 others watching)"
     * instead of silently refusing a teammate's button press.
     */
    void DescribeFor(UWorld* World, const AFGPlayerController* ViewerPC,
                     FString& OutOwnerName, TArray<FString>& OutViewers, bool& bOutIsOwner);

    /** Drop every viewer. Called on world teardown so a new level starts clean. */
    void Reset(UWorld* World);
}
