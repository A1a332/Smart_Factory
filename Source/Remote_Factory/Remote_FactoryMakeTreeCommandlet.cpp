#include "Remote_FactoryMakeTreeCommandlet.h"

#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

// Log category visible to both the WITH_EDITOR implementation and the non-editor stub.
DEFINE_LOG_CATEGORY_STATIC(LogWTMakeTree, Log, All);

#if WITH_EDITOR

#include "Remote_FactoryResearchTree.h"   // UWTResearchTreeMerged, UWTResearchTreeBase, ConfigureMergedTree
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Blueprint.h"
#include "FGResearchTree.h"                  // UFGResearchTree::GetNodes

UWTMakeTreeCommandlet::UWTMakeTreeCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UWTMakeTreeCommandlet::Main(const FString& Params)
{
    // ★0.2.137 改名：编辑器按插件目录名挂载，目录已改 Smart_Factory
    const FString AssetPath = TEXT("/Smart_Factory/Research/BP_WT_ResearchTree");
    const FName   AssetName = FName("BP_WT_ResearchTree");

    // Remove a stale on-disk asset so SavePackage doesn't refuse with
    // "asset only partially loaded".
    const FString ExistingFilename = FPackageName::LongPackageNameToFilename(
        AssetPath, FPackageName::GetAssetPackageExtension());
    if (IFileManager::Get().FileExists(*ExistingFilename))
    {
        UE_LOG(LogWTMakeTree, Warning, TEXT("Deleting stale asset file %s"), *ExistingFilename);
        IFileManager::Get().Delete(*ExistingFilename, /*bRequireExists=*/false, /*bEvenReadOnly=*/true);
    }

    UClass* ParentClass = UWTResearchTreeMerged::StaticClass();
    UE_LOG(LogWTMakeTree, Warning, TEXT("Parent class: %s"), *ParentClass->GetPathName());

    UPackage* Pkg = CreatePackage(*AssetPath);
    if (!Pkg)
    {
        UE_LOG(LogWTMakeTree, Error, TEXT("Failed to create package %s"), *AssetPath);
        return 1;
    }

    // --- create the Blueprint -----------------------------------------------------
    UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
        ParentClass, Pkg, AssetName, BPTYPE_Normal,
        UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), FName());
    if (!BP)
    {
        UE_LOG(LogWTMakeTree, Error, TEXT("FKismetEditorUtilities::CreateBlueprint failed"));
        return 1;
    }

    // Compile so the GeneratedClass/CDO exist and are stable before we mutate them.
    FKismetEditorUtilities::CompileBlueprint(BP);
    UE_LOG(LogWTMakeTree, Warning, TEXT("Compile status=%d (UpToDate=%d)"),
        (int32)BP->Status, (int32)(BP->Status == BS_UpToDate || BP->Status == BS_UpToDateWithWarnings));

    // --- bake the 56 MAM nodes into the tree CDO (robust path) -------------------
    // If this produces 0 nodes (e.g. a referenced class isn't loadable in the editor),
    // we still save the empty asset; the runtime WT_BuildResearchTrees() will then
    // populate the live CDO as a fallback.
    UClass* GenClass = BP->GeneratedClass;
    if (GenClass)
    {
        if (UWTResearchTreeBase* CDO = Cast<UWTResearchTreeBase>(GenClass->GetDefaultObject()))
        {
            CDO->ConfigureMergedTree(
                FText::FromString(TEXT("智能工厂")),
                FText::FromString(TEXT("Research the Smart Factory's upgrade buildings level by level in the MAM. "
                                       "Each level costs more than the last; the matching building scales automatically.")));
            const int32 N = UFGResearchTree::GetNodes(GenClass).Num();
            UE_LOG(LogWTMakeTree, Warning, TEXT("Baked %d nodes into tree CDO (expect 56)."), N);
        }
        else
        {
            UE_LOG(LogWTMakeTree, Error, TEXT("GeneratedClass CDO is not a UWTResearchTreeBase; saving empty asset (runtime fallback)."));
        }
    }
    else
    {
        UE_LOG(LogWTMakeTree, Error, TEXT("BP->GeneratedClass is null after compile; saving empty asset (runtime fallback)."));
    }

    // --- save ---------------------------------------------------------------------
    Pkg->MarkPackageDirty();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        AssetPath, FPackageName::GetAssetPackageExtension());
    if (Filename.IsEmpty())
    {
        UE_LOG(LogWTMakeTree, Error, TEXT("Could not resolve filename for %s"), *AssetPath);
        return 1;
    }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Standalone;
    if (UPackage::SavePackage(Pkg, BP, *Filename, SaveArgs))
    {
        UE_LOG(LogWTMakeTree, Warning, TEXT("Saved tree BP to %s"), *Filename);
    }
    else
    {
        UE_LOG(LogWTMakeTree, Error, TEXT("SavePackage failed for %s"), *Filename);
        return 1;
    }

    UE_LOG(LogWTMakeTree, Warning, TEXT("DONE: BP_WT_ResearchTree created."));
    return 0;
}

#else // !WITH_EDITOR

// Shipping/non-editor builds still link this class (the header declares it
// unconditionally), so the constructor/Main must exist even though the commandlet
// can never run outside an editor build.
UWTMakeTreeCommandlet::UWTMakeTreeCommandlet()
{
}

int32 UWTMakeTreeCommandlet::Main(const FString& Params)
{
    UE_LOG(LogWTMakeTree, Error, TEXT("WTMakeTree commandlet requires a WITH_EDITOR build."));
    return 1;
}

#endif // WITH_EDITOR
