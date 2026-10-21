#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "Remote_FactoryMakeTreeCommandlet.generated.h"

/**
 * Editor-only commandlet (run via UnrealEditor <project> -run=WTMakeTree) that
 * authors the research-TREE Blueprint asset
 *   Mods/Remote_Factory/Content/Research/BP_WT_ResearchTree.uasset
 * as a child of UWTResearchTreeMerged, and bakes the 56 MAM nodes into it by
 * reusing the proven UWTResearchTreeBase::ConfigureMergedTree() layout logic.
 *
 * WHY THIS FIXES F1: MAM resolves research trees through their *asset identity*
 * (UFGResearchTree::GetPrimaryAssetId derives its name from GetOutermost(), which
 * for a pure-C++ class is the code module -> not a loadable package -> MAM can't
 * read the nodes -> blank gray blocks). A real Blueprint asset gives the tree a
 * loadable package outer, so MAM's asset render path resolves it and shows the
 * (baked) nodes. If baking somehow produces 0 nodes, the runtime
 * WT_BuildResearchTrees() still fills the live CDO as a fallback.
 */
UCLASS()
class REMOTE_FACTORY_API UWTMakeTreeCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UWTMakeTreeCommandlet();

    virtual int32 Main(const FString& Params) override;
};
