#pragma once

#include "Hologram/FGBuildableHologram.h"
#include "Remote_FactoryHologram.generated.h"

/**
 * Placement hologram for the Remote Factory.
 *
 * Relaxes the floor requirement the same way Wireless Power does, so the console can be dropped
 * onto a foundation, a platform edge or plain terrain without fighting the placement validation.
 * The base AFGBuildableHologram copies the buildable's "MainMesh" component automatically, so the
 * preview geometry needs no code here.
 */
UCLASS()
class REMOTE_FACTORY_API ARemote_FactoryHologram : public AFGBuildableHologram
{
    GENERATED_BODY()
public:
    ARemote_FactoryHologram();
};
