#pragma once

#include "Module/GameWorldModule.h"
#include "FGResearchTree.h"
#include "Remote_FactoryGameWorldModule.generated.h"

/**
 * Root game-world module for the Remote Factory.
 *
 * SML discovers the mod through this class (bRootModule = true) and gives it lifecycle callbacks.
 *
 *   * CONSTRUCTION        — fill the milestone's cost/category (needs /Game content mounted;
 *                           INITIALIZATION, which reads mSchematics, runs later).
 *   * POST_INITIALIZATION — register the buildable descriptor + recipe with AFGRecipeManager,
 *                           install the RCO on the game mode, and register the MAM research trees.
 */
UCLASS()
class REMOTE_FACTORY_API URemote_FactoryGameWorldModule : public UGameWorldModule
{
    GENERATED_BODY()
public:
    URemote_FactoryGameWorldModule();

    virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;
};
