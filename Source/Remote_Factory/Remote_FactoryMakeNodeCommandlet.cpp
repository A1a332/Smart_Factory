#include "Remote_FactoryMakeNodeCommandlet.h"

#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

// Log category must be visible to BOTH the WITH_EDITOR implementation and the
// non-editor stub below, so it is defined at file scope (outside the #if guard).
DEFINE_LOG_CATEGORY_STATIC(LogWTMakeNode, Log, All);

#if WITH_EDITOR

#include "Remote_FactoryResearchTree.h"   // UWTResearchTreeNode
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_VariableGet.h"

namespace
{
    // Find the first non-exec pin of a node matching the given direction.
    UEdGraphPin* FindPinByName(UK2Node* Node, const FName& Name, EEdGraphPinDirection Dir)
    {
        if (!Node) return nullptr;
        for (UEdGraphPin* P : Node->Pins)
        {
            if (P && P->PinName == Name && P->Direction == Dir)
            {
                return P;
            }
        }
        return nullptr;
    }
}

UWTMakeNodeCommandlet::UWTMakeNodeCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UWTMakeNodeCommandlet::Main(const FString& Params)
{
    // ★0.2.137 改名：编辑器按插件目录名挂载，目录已改 Smart_Factory
    const FString AssetPath = TEXT("/Smart_Factory/Research/BP_WT_ResearchNode");
    const FName   AssetName = FName("BP_WT_ResearchNode");

    // --- clear any stale on-disk asset so SavePackage doesn't refuse ---------------
    // The mod's Content is auto-mounted by the editor; a previously generated
    // BP_WT_ResearchNode.uasset left on disk causes SavePackage to fail with
    // "asset only partially loaded". (The deployment pipeline deletes the file before
    // launching this commandlet, so this is a belt-and-suspenders cleanup.)
    const FString ExistingFilename = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());
    if (IFileManager::Get().FileExists(*ExistingFilename))
    {
        UE_LOG(LogWTMakeNode, Warning, TEXT("Deleting stale asset file %s"), *ExistingFilename);
        IFileManager::Get().Delete(*ExistingFilename, /*bRequireExists=*/false, /*bEvenReadOnly=*/true);
    }

    UClass* ParentClass = UWTResearchTreeNode::StaticClass();
    UE_LOG(LogWTMakeNode, Warning, TEXT("Parent class: %s"), *ParentClass->GetPathName());

    UPackage* Pkg = CreatePackage(*AssetPath);
    if (!Pkg)
    {
        UE_LOG(LogWTMakeNode, Error, TEXT("Failed to create package %s"), *AssetPath);
        return 1;
    }

    // --- create the Blueprint -----------------------------------------------------
    UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
        ParentClass, Pkg, AssetName, BPTYPE_Normal,
        UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), FName());
    if (!BP)
    {
        UE_LOG(LogWTMakeNode, Error, TEXT("FKismetEditorUtilities::CreateBlueprint failed"));
        return 1;
    }

    // --- create the GetNodeSchematic override function graph ----------------------
    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(
        BP, FName("GetNodeSchematic"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    if (!Graph)
    {
        UE_LOG(LogWTMakeNode, Error, TEXT("CreateNewGraph failed"));
        return 1;
    }
    // AddFunctionGraph<UClass> uses ParentClass as the signature source, so the entry/result
    // nodes are scaffolded for the inherited GetNodeSchematic (incl. its ReturnValue pin).
    FBlueprintEditorUtils::AddFunctionGraph<UClass>(BP, Graph, /*bIsUserCreated=*/false, ParentClass);

    // --- locate the auto-generated scaffold nodes --------------------------------
    UK2Node_FunctionEntry*      Entry = nullptr;
    UK2Node_CallParentFunction* ParentCall = nullptr;
    UK2Node_FunctionResult*     Result = nullptr;
    for (UEdGraphNode* N : Graph->Nodes)
    {
        if (!Entry)      Entry      = Cast<UK2Node_FunctionEntry>(N);
        if (!ParentCall) ParentCall = Cast<UK2Node_CallParentFunction>(N);
        if (!Result)     Result     = Cast<UK2Node_FunctionResult>(N);
    }
    UE_LOG(LogWTMakeNode, Warning, TEXT("Scaffold nodes: Entry=%s ParentCall=%s Result=%s"),
        *GetNameSafe(Entry), *GetNameSafe(ParentCall), *GetNameSafe(Result));

    // --- add a variable-get for the inherited mNodeSchematic property ------------
    // Use SetSelfMember (not SetExternalMember): mNodeSchematic is a member of this
    // node instance (inherited from UWTResearchTreeNode), so the compiler supplies the
    // function's "self" context automatically. SetExternalMember would force an explicit
    // object pin that a BlueprintImplementableEvent override's entry node does not expose,
    // which makes the compiler cull the node -> "invalid target" compile error.
    FGraphNodeCreator<UK2Node_VariableGet> VarGetCreator(*Graph);
    UK2Node_VariableGet* VarGet = VarGetCreator.CreateNode();
    VarGet->VariableReference.SetSelfMember(FName("mNodeSchematic"));
    VarGet->AllocateDefaultPins();
    VarGet->NodePosX = -400;
    VarGet->NodePosY = 0;
    VarGetCreator.Finalize();

    for (UEdGraphPin* P : VarGet->Pins)
    {
        UE_LOG(LogWTMakeNode, Warning, TEXT("  VarGet pin: %s dir=%d cat=%s"),
            *P->PinName.ToString(), (int32)P->Direction, *P->PinType.PinCategory.ToString());
    }
    // NOTE: mNodeSchematic is referenced via SetSelfMember, so the compiler auto-resolves the
    // function's "self" context for the variable-get. No explicit self-pin wiring is required
    // (the BlueprintImplementableEvent override's entry node does not expose a self pin).

    // --- break the default ParentCall -> Result data link so VarGet can drive it --
    if (ParentCall && Result)
    {
        if (UEdGraphPin* ParentOut = FindPinByName(ParentCall, TEXT("ReturnValue"), EGPD_Output))
        {
            if (UEdGraphPin* ResultIn = FindPinByName(Result, TEXT("ReturnValue"), EGPD_Input))
            {
                ParentOut->BreakLinkTo(ResultIn);
                UE_LOG(LogWTMakeNode, Warning, TEXT("Broke ParentCall->Result ReturnValue link"));
            }
        }
    }

    // --- wire VarGet -> Result.ReturnValue ---------------------------------------
    UEdGraphPin* VarOut = FindPinByName(VarGet, TEXT("mNodeSchematic"), EGPD_Output);
    UEdGraphPin* ResultIn = Result ? FindPinByName(Result, TEXT("ReturnValue"), EGPD_Input) : nullptr;
    if (VarOut && ResultIn)
    {
        VarOut->MakeLinkTo(ResultIn);
        UE_LOG(LogWTMakeNode, Warning, TEXT("Wired VarGet.%s -> Result.ReturnValue (pin cat=%s, sub=%s)"),
            *VarOut->PinName.ToString(), *VarOut->PinType.PinCategory.ToString(),
            *VarOut->PinType.PinSubCategory.ToString());
    }
    else
    {
        UE_LOG(LogWTMakeNode, Error, TEXT("Wiring failed: VarOut=%s ResultIn=%s"),
            VarOut ? *VarOut->PinName.ToString() : TEXT("null"),
            ResultIn ? *ResultIn->PinName.ToString() : TEXT("null"));
        return 1;
    }

    // --- compile ----------------------------------------------------------------
    FKismetEditorUtilities::CompileBlueprint(BP);
    UE_LOG(LogWTMakeNode, Warning, TEXT("Compile status=%d (UpToDate=%d)"),
        (int32)BP->Status, (int32)(BP->Status == BS_UpToDate || BP->Status == BS_UpToDateWithWarnings));

    // --- save --------------------------------------------------------------------
    Pkg->MarkPackageDirty();
    const FString Filename = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());
    if (Filename.IsEmpty())
    {
        UE_LOG(LogWTMakeNode, Error, TEXT("Could not resolve filename for %s"), *AssetPath);
        return 1;
    }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Standalone;
    if (UPackage::SavePackage(Pkg, BP, *Filename, SaveArgs))
    {
        UE_LOG(LogWTMakeNode, Warning, TEXT("Saved BP to %s"), *Filename);
    }
    else
    {
        UE_LOG(LogWTMakeNode, Error, TEXT("SavePackage failed for %s"), *Filename);
        return 1;
    }

    UE_LOG(LogWTMakeNode, Warning, TEXT("DONE: BP_WT_ResearchNode created."));
    return 0;
}

#else // !WITH_EDITOR

// Shipping/non-editor builds still link this class (the header declares it
// unconditionally), so the constructor must have a definition here even though
// the commandlet can never actually run. Without it the linker fails with
// LNK2019: unresolved external symbol UWTMakeNodeCommandlet::UWTMakeNodeCommandlet().
UWTMakeNodeCommandlet::UWTMakeNodeCommandlet()
{
}

int32 UWTMakeNodeCommandlet::Main(const FString& Params)
{
    UE_LOG(LogWTMakeNode, Error, TEXT("WTMakeNode commandlet requires a WITH_EDITOR build."));
    return 1;
}

#endif // WITH_EDITOR
