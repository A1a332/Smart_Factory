#include "Remote_FactorySharedSession.h"

#include "Remote_FactoryLoc.h"
#include "Remote_FactoryLog.h"
#include "FGPlayerController.h"
#include "Buildables/FGBuildable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"

namespace
{
    /** One player currently looking at a terminal. */
    struct FWTViewerEntry
    {
        TWeakObjectPtr<AFGPlayerController> Controller;

        /** Which terminal they are looking at. The owner is the first viewer of the SAME terminal,
         *  so two independent terminals in a big base each get their own owner. */
        TWeakObjectPtr<AFGBuildable> Terminal;

        /** Cached for the roster readout; a display name is pure UI sugar. */
        FString DisplayName;
    };

    /**
     * Process-wide roster.
     *
     * Keyed by world so a client that travels between levels (or a host that reloads) starts from a
     * clean slate rather than inheriting a roster full of dead controllers. In practice there is
     * exactly one world at a time; the map exists to make that assumption explicit and cheap to
     * unwind rather than relying on it implicitly.
     */
    TMap<TWeakObjectPtr<UWorld>, TArray<FWTViewerEntry>> GViewers;

    TArray<FWTViewerEntry>* FindRoster(UWorld* World)
    {
        if (!World)
        {
            return nullptr;
        }
        return GViewers.Find(TWeakObjectPtr<UWorld>(World));
    }

    /** Drop entries whose controller or terminal has been destroyed. */
    void PruneRoster(TArray<FWTViewerEntry>& Roster)
    {
        Roster.RemoveAll([](const FWTViewerEntry& E)
        {
            return !E.Controller.IsValid();
        });
    }

    /** The owner is the first live viewer of the given terminal. */
    const FWTViewerEntry* FindOwnerFor(const TArray<FWTViewerEntry>& Roster, const AFGBuildable* Terminal)
    {
        for (const FWTViewerEntry& E : Roster)
        {
            if (E.Terminal.Get() == Terminal && E.Controller.IsValid())
            {
                return &E;
            }
        }
        return nullptr;
    }

    /** A player's name for the roster readout, tolerant of a missing player state. */
    FString NameOf(const AFGPlayerController* PC)
    {
        if (!PC)
        {
            // The player name itself is user data and must not be translated; only this
            // fallback is our copy, and it is never shown without a name beside it.
            return WT_LOCTEXT("Player_Unknown", "Unknown player").ToString();
        }
        if (const APlayerState* PS = PC->PlayerState)
        {
            const FString PlayerName = PS->GetPlayerName();
            if (!PlayerName.IsEmpty())
            {
                return PlayerName;
            }
        }
        return PC->GetName();
    }
}

namespace Remote_FactoryShared
{
    void SetViewer(UWorld* World, AFGPlayerController* PC, bool bViewing, AFGBuildable* Terminal)
    {
        if (!World || !PC)
        {
            return;
        }

        TArray<FWTViewerEntry>& Roster = GViewers.FindOrAdd(TWeakObjectPtr<UWorld>(World));
        PruneRoster(Roster);

        // Always clear an existing entry first: re-announcing is legal (a rescan, a terminal switch)
        // and "remove then insert" is the only version that cannot leave a stale terminal behind.
        Roster.RemoveAll([PC](const FWTViewerEntry& E)
        {
            return E.Controller.Get() == PC;
        });

        if (!bViewing)
        {
            WT_LOGF(TEXT("[shared] viewer left: %s (roster=%d)"), *NameOf(PC), Roster.Num());
            return;
        }

        FWTViewerEntry Entry;
        Entry.Controller = PC;
        Entry.Terminal = Terminal;
        Entry.DisplayName = NameOf(PC);
        Roster.Add(MoveTemp(Entry));

        WT_LOGF(TEXT("[shared] viewer joined: %s (roster=%d)"), *NameOf(PC), Roster.Num());
    }

    bool IsSessionOwner(UWorld* World, const AFGPlayerController* PC)
    {
        TArray<FWTViewerEntry>* Roster = FindRoster(World);
        if (!Roster || !PC)
        {
            // No roster at all means nobody has announced; never lock a player out of their own
            // terminal over missing bookkeeping.
            return true;
        }
        PruneRoster(*Roster);

        // Find this player's terminal, then ask who owns that terminal.
        const AFGBuildable* Terminal = nullptr;
        for (const FWTViewerEntry& E : *Roster)
        {
            if (E.Controller.Get() == PC)
            {
                Terminal = E.Terminal.Get();
                break;
            }
        }
        if (!Terminal)
        {
            return true;
        }

        const FWTViewerEntry* Owner = FindOwnerFor(*Roster, Terminal);
        return Owner == nullptr || Owner->Controller.Get() == PC;
    }

    void DescribeFor(UWorld* World, const AFGPlayerController* ViewerPC,
                     FString& OutOwnerName, TArray<FString>& OutViewers, bool& bOutIsOwner)
    {
        OutOwnerName.Reset();
        OutViewers.Reset();
        bOutIsOwner = true;

        TArray<FWTViewerEntry>* Roster = FindRoster(World);
        if (!Roster)
        {
            return;
        }
        PruneRoster(*Roster);

        const AFGBuildable* Terminal = nullptr;
        for (const FWTViewerEntry& E : *Roster)
        {
            if (E.Controller.Get() == ViewerPC)
            {
                Terminal = E.Terminal.Get();
                break;
            }
        }
        if (!Terminal)
        {
            return;
        }

        const FWTViewerEntry* Owner = FindOwnerFor(*Roster, Terminal);
        if (Owner)
        {
            OutOwnerName = Owner->DisplayName;
        }

        for (const FWTViewerEntry& E : *Roster)
        {
            if (E.Terminal.Get() == Terminal && E.Controller.IsValid())
            {
                OutViewers.Add(E.DisplayName);
            }
        }

        bOutIsOwner = (Owner == nullptr || Owner->Controller.Get() == ViewerPC);
    }

    void Reset(UWorld* World)
    {
        if (!World)
        {
            return;
        }
        GViewers.Remove(TWeakObjectPtr<UWorld>(World));
    }
}
