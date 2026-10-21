#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "Remote_FactoryMakeNodeCommandlet.generated.h"

/**
 * Editor-only commandlet (run via UnrealEditor-Cmd.exe <project> -run=WTMakeNode) that
 * authors the research-tree node Blueprint asset Mods/Remote_Factory/Content/Research/
 * BP_WT_ResearchNode.uasset.
 *
 * WHY A COMMANDLET (not Python): UFGResearchTreeNode::GetNodeSchematic() is a
 * BlueprintImplementableEvent, so a node MUST be a Blueprint whose graph returns the
 * schematic. The node parent UWTResearchTreeNode (and FactoryGame classes in general) is
 * not resolvable by name from the headless Python `unreal` module in this engine build,
 * but is trivially available in C++ (UWTResearchTreeNode::StaticClass()). This commandlet
 * uses FBlueprintEditorUtils / FKismetEditorUtilities (editor-only) to build the asset.
 *
 * The resulting Blueprint: child of UWTResearchTreeNode, implements GetNodeSchematic()
 * to return its inherited mNodeSchematic property. The C++ side (WT_BuildResearchTrees)
 * instantiates this BP class 60 times and sets mNodeSchematic per instance.
 */
UCLASS()
class REMOTE_FACTORY_API UWTMakeNodeCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UWTMakeNodeCommandlet();

    virtual int32 Main(const FString& Params) override;
};
