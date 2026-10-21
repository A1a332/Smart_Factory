#pragma once

#include "Buildables/FGBuildable.h"
#include "Remote_FactoryBuildable.generated.h"

/**
 * The Remote Factory: a placeable console that opens the factory-wide control panel.
 *
 * Building it is the whole point of the mod — until this exists there is nothing in the world
 * that can reach URemote_FactoryPanelWidget, so the panel would be dead code.
 *
 * Opening it deliberately reuses the vanilla interaction path: AFGBuildable::mIsUseable plus
 * mInteractWidgetSoftClass is exactly the pair the game checks when the player looks at a
 * building and presses E. Both members are private in the shipped SDK header, so they are set
 * through reflection (see the .cpp) — same technique the WirelessPower buildables use.
 *
 * There is no power draw and no recipe behaviour beyond being built: the terminal is a UI
 * front-end, and every actual action (scan / open / pull) is a server RPC on
 * URemote_FactoryRCO.
 */
UCLASS()
class REMOTE_FACTORY_API ARemote_FactoryBuildable : public AFGBuildable
{
    GENERATED_BODY()

public:
    ARemote_FactoryBuildable();

    /** Human readable label, also used by the scan list. */
    static FText GetTerminalDisplayName();

protected:
    virtual void BeginPlay() override;

private:
    /** Turn on mIsUseable and point mInteractWidgetSoftClass at our panel class. */
    void MakeUseableWithPanel();
};
