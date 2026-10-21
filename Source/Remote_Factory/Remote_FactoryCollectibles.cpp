#include "Remote_FactoryCollectibles.h"
#include "Remote_FactoryTypes.h"
#include "Remote_FactoryLog.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Internationalization/Text.h"
// ★0.2.127: shared classify needs the descriptor classes (moved here from the RCO so the pickup
// monitor and the live scan share ONE classification table).
#include "Resources/FGPowerShardDescriptor.h"
#include "Resources/FGItemDescriptor.h"
#include "Remote_FactoryPickupMonitor.h"

namespace
{
    // Schema / meta constants. Kept in sync with the hand-authored scaffold collectibles.json so a
    // regenerated file stays readable by the same loader.
    constexpr const TCHAR* SchemaName   = TEXT("Remote_Factory.Collectibles/v1");
    constexpr const TCHAR* GameVersion  = TEXT("1.1");
    constexpr const TCHAR* GeneratedBy  = TEXT("wt.DumpCollectibles");
    constexpr const TCHAR* NoteStr      = TEXT("spawns array is filled from the SmartFactoryLib "
                                               "data file (Satisfactory-Calculator game-level export). "
                                               "kind matches EWT_CollectibleKind: 0=BlueSlug 1=YellowSlug "
                                               "2=PurpleSlug 3=MercerSphere 4=Somersloop 5=DropPod "
                                               "6=BerylNut 7=PaleBerry 8=BaconAgaric. region is computed "
                                               "at load time.");

    // A stable merge key: kind + coordinate rounded to the nearest metre.
    FString SpawnKey(const FWT_CollectibleSpawn& S)
    {
        return FString::Printf(TEXT("%d|%d|%d|%d"),
            static_cast<int32>(S.Kind),
            FMath::RoundToInt(S.Location.X / 100.f),
            FMath::RoundToInt(S.Location.Y / 100.f),
            FMath::RoundToInt(S.Location.Z / 100.f));
    }

    // ★0.2.126: same rounding as SpawnKey, but from a raw Kind + location (for live/display rows).
    FString LocKey(uint8 Kind, const FVector& Loc)
    {
        return FString::Printf(TEXT("%d|%d|%d|%d"),
            static_cast<int32>(Kind),
            FMath::RoundToInt(Loc.X / 100.f),
            FMath::RoundToInt(Loc.Y / 100.f),
            FMath::RoundToInt(Loc.Z / 100.f));
    }

    FString KindName(uint8 Kind)
    {
        switch (Kind)
        {
            case 0: return TEXT("BlueSlug");
            case 1: return TEXT("YellowSlug");
            case 2: return TEXT("PurpleSlug");
            case 3: return TEXT("MercerSphere");
            case 4: return TEXT("Somersloop");
            case 5: return TEXT("DropPod");
            case 6: return TEXT("BerylNut");
            case 7: return TEXT("PaleBerry");
            case 8: return TEXT("BaconAgaric");
            default: return TEXT("Unknown");
        }
    }

    /** Serialize the full spawn set (with regenerated meta) back to collectibles.json. */
    bool WriteSpawns(const TArray<FWT_CollectibleSpawn>& All)
    {
        TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("schema"), SchemaName);
        Root->SetStringField(TEXT("gameVersion"), GameVersion);
        Root->SetStringField(TEXT("generatedBy"), GeneratedBy);
        Root->SetStringField(TEXT("generatedAt"), FDateTime::Now().ToString());

        TArray<int32> Counts;
        Counts.Init(0, 9);
        for (const FWT_CollectibleSpawn& S : All)
        {
            if (S.Kind < 9) ++Counts[S.Kind];
        }
        TSharedPtr<FJsonObject> Cats = MakeShared<FJsonObject>();
        Cats->SetNumberField(TEXT("BlueSlug"),     Counts[0]);
        Cats->SetNumberField(TEXT("YellowSlug"),    Counts[1]);
        Cats->SetNumberField(TEXT("PurpleSlug"),    Counts[2]);
        Cats->SetNumberField(TEXT("MercerSphere"),  Counts[3]);
        Cats->SetNumberField(TEXT("Somersloop"),    Counts[4]);
        Cats->SetNumberField(TEXT("DropPod"),       Counts[5]);
        Cats->SetNumberField(TEXT("BerylNut"),      Counts[6]);
        Cats->SetNumberField(TEXT("PaleBerry"),     Counts[7]);
        Cats->SetNumberField(TEXT("BaconAgaric"),   Counts[8]);
        Root->SetObjectField(TEXT("categories"), Cats);

        Root->SetStringField(TEXT("_note"), NoteStr);

        TArray<TSharedPtr<FJsonValue>> Arr;
        Arr.Reserve(All.Num());
        for (const FWT_CollectibleSpawn& S : All)
        {
            TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
            O->SetNumberField(TEXT("kind"), static_cast<int32>(S.Kind));
            O->SetNumberField(TEXT("x"), S.Location.X);
            O->SetNumberField(TEXT("y"), S.Location.Y);
            O->SetNumberField(TEXT("z"), S.Location.Z);
            Arr.Add(MakeShared<FJsonValueObject>(O));
        }
        Root->SetArrayField(TEXT("spawns"), Arr);

        FString OutString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
        if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
        {
            return false;
        }
        const FString Path = FWTCollectibles::GetCollectiblesFilePath();
        return FFileHelper::SaveStringToFile(OutString, *Path,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
}

FString FWTCollectibles::GetCollectiblesFilePath()
{
    // ★0.2.84: the collectibles data now ships inside the dedicated dependency mod
    // SmartFactoryLib (so it can be shared / updated independently of Remote_Factory). Resolve
    // the path through that plugin; the panel and loader therefore read straight from the library.
    if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SmartFactoryLib")))
    {
        return FPaths::Combine(Plugin->GetBaseDir(), TEXT("collectibles.json"));
    }
    // Fallback: somewhere writable so the caller fails cleanly rather than crashing on a null path.
    return FPaths::Combine(FPlatformProcess::ApplicationSettingsDir(), TEXT("collectibles.json"));
}

bool FWTCollectibles::LoadSpawns(TArray<FWT_CollectibleSpawn>& Out)
{
    Out.Reset();

    const FString Path = GetCollectiblesFilePath();
    FString Raw;
    if (!FFileHelper::LoadFileToString(Raw, *Path))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Spawns = nullptr;
    if (!Root->TryGetArrayField(TEXT("spawns"), Spawns) || !Spawns)
    {
        return false;
    }

    for (const TSharedPtr<FJsonValue>& V : *Spawns)
    {
        const TSharedPtr<FJsonObject> Obj = V->AsObject();
        if (!Obj.IsValid())
        {
            continue;
        }
        FWT_CollectibleSpawn S;
        int32 Kind = 0;
        if (Obj->TryGetNumberField(TEXT("kind"), Kind))
        {
            S.Kind = static_cast<uint8>(FMath::Clamp(Kind, 0, 8));
        }
        double X = 0.0, Y = 0.0, Z = 0.0;
        Obj->TryGetNumberField(TEXT("x"), X);
        Obj->TryGetNumberField(TEXT("y"), Y);
        Obj->TryGetNumberField(TEXT("z"), Z);
        S.Location = FVector(X, Y, Z);

        // ★0.2.136 cave flag: trust an explicit "cave" field when the file carries one (a future
        // dump can ask the world directly); otherwise fall back to the altitude heuristic.
        bool bCaveField = false;
        if (Obj->TryGetBoolField(TEXT("cave"), bCaveField))
        {
            S.bCave = bCaveField || WT_IsCaveLocation(S.Location);
        }
        else
        {
            S.bCave = WT_IsCaveLocation(S.Location);
        }

        if (S.Kind < 9)
        {
            Out.Add(S);
        }
    }
    return true; // parsed OK; Out may legitimately be empty (spawns: [])
}

bool FWTCollectibles::MergeSpawns(const TArray<FWT_CollectibleSpawn>& NewOnes)
{
    TArray<FWT_CollectibleSpawn> All;
    LoadSpawns(All); // existing file (may be empty)

    TSet<FString> Seen;
    for (const FWT_CollectibleSpawn& S : All)
    {
        Seen.Add(SpawnKey(S));
    }

    int32 Added = 0;
    for (const FWT_CollectibleSpawn& S : NewOnes)
    {
        if (S.Kind >= 9)
        {
            continue;
        }
        const FString Key = SpawnKey(S);
        if (!Seen.Contains(Key))
        {
            Seen.Add(Key);
            All.Add(S);
            ++Added;
        }
    }

    if (Added == 0)
    {
        // Nothing new to merge. Keep the file as-is (no needless churn / timestamp bump).
        return All.Num() > 0 || LoadSpawns(All);
    }
    return WriteSpawns(All);
}

// ----------------------------------------------------------------------------
// ★0.2.126: per-pickup "already collected" memory (see header for the design).
// ----------------------------------------------------------------------------

namespace
{
    constexpr const TCHAR* CollectedSchema = TEXT("Remote_Factory.Collected/v1");
}

FString FWTCollectibles::GetCollectedFilePath()
{
    // ProjectSavedDir is always writable (unlike the pak'd mod root), so the per-session
    // collected state survives restarts without touching shipped content.
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Remote_Factory"), TEXT("RF_collected.json"));
}

FString FWTCollectibles::CollectibleKey(uint8 Kind, const FVector& Loc)
{
    return LocKey(Kind, Loc);
}

bool FWTCollectibles::LoadCollectedState(TSet<FString>& OutCollected, TSet<FString>& OutPresent)
{
    OutCollected.Reset();
    OutPresent.Reset();

    const FString Path = GetCollectedFilePath();
    FString Raw;
    if (!FFileHelper::LoadFileToString(Raw, *Path))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
    if (Root->TryGetArrayField(TEXT("collected"), Arr) && Arr)
    {
        for (const TSharedPtr<FJsonValue>& V : *Arr)
        {
            const FString S = V->AsString();
            if (!S.IsEmpty())
            {
                OutCollected.Add(S);
            }
        }
    }
    if (Root->TryGetArrayField(TEXT("present"), Arr) && Arr)
    {
        for (const TSharedPtr<FJsonValue>& V : *Arr)
        {
            const FString S = V->AsString();
            if (!S.IsEmpty())
            {
                OutPresent.Add(S);
            }
        }
    }
    return true;
}

bool FWTCollectibles::SaveCollectedState(const TSet<FString>& Collected, const TSet<FString>& Present)
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("schema"), CollectedSchema);
    Root->SetStringField(TEXT("generatedAt"), FDateTime::Now().ToString());

    auto ToArr = [](const TSet<FString>& Set) -> TArray<TSharedPtr<FJsonValue>>
    {
        TArray<TSharedPtr<FJsonValue>> Arr;
        Arr.Reserve(Set.Num());
        for (const FString& S : Set)
        {
            Arr.Add(MakeShared<FJsonValueString>(S));
        }
        return Arr;
    };
    Root->SetArrayField(TEXT("collected"), ToArr(Collected));
    Root->SetArrayField(TEXT("present"), ToArr(Present));

    FString OutString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
    if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
    {
        return false;
    }

    const FString Path = GetCollectedFilePath();
    // Make sure the subdirectory exists before writing.
    const FString Dir = FPaths::GetPath(Path);
    if (!IFileManager::Get().DirectoryExists(*Dir))
    {
        IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
    }
    return FFileHelper::SaveStringToFile(OutString, *Path,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

// ----------------------------------------------------------------------------
// ★0.2.127: shared classify table (moved from the RCO + the three forage foods added).
// The live scan (RCO) and the pickup monitor MUST agree on what a collectible is; having one
// table is also what makes kinds 6..8 (forage foods) visible to both sides for the first time.
// ----------------------------------------------------------------------------

namespace
{
    /** True when a descriptor's package path contains the given token (case-insensitive). */
    bool WTC_PathHas(const UClass* DescriptorClass, const TCHAR* Token)
    {
        return DescriptorClass && DescriptorClass->GetPathName().Contains(Token, ESearchCase::IgnoreCase);
    }
}

uint8 FWTCollectibles::ClassifyCollectibleItem(const UClass* ItemClass, FText& OutLabel)
{
    OutLabel = FText::GetEmpty();
    if (!ItemClass)
    {
        return 0xFF; // EWT_CollectibleKind::Count
    }

    // Power slugs. All three flavours share the Desc_Crystal stem; the suffix names the colour.
    //   Desc_Crystal     = 蓝 (1 Power Shard)
    //   Desc_Crystal_mk2 = 黄 (2)
    //   Desc_Crystal_mk3 = 紫 (5)
    if (ItemClass->IsChildOf(UFGPowerShardDescriptor::StaticClass()))
    {
        const FString Path = ItemClass->GetPathName();
        if (Path.Contains(TEXT("mk3"), ESearchCase::IgnoreCase))
        {
            OutLabel = NSLOCTEXT("Remote_Factory", "Col_SlugPurple", "Power Slug (Purple)");
            return static_cast<uint8>(EWT_CollectibleKind::PurpleSlug);
        }
        if (Path.Contains(TEXT("mk2"), ESearchCase::IgnoreCase))
        {
            OutLabel = NSLOCTEXT("Remote_Factory", "Col_SlugYellow", "Power Slug (Yellow)");
            return static_cast<uint8>(EWT_CollectibleKind::YellowSlug);
        }
        OutLabel = NSLOCTEXT("Remote_Factory", "Col_SlugBlue", "Power Slug (Blue)");
        return static_cast<uint8>(EWT_CollectibleKind::BlueSlug);
    }

    // Somersloop / Mercer Sphere. Both are plain FGItemDescriptor assets under Prototype/WAT.
    if (WTC_PathHas(ItemClass, TEXT("WAT1")))
    {
        OutLabel = NSLOCTEXT("Remote_Factory", "Col_Somersloop", "Somersloop");
        return static_cast<uint8>(EWT_CollectibleKind::Somersloop);
    }
    if (WTC_PathHas(ItemClass, TEXT("WAT2")))
    {
        OutLabel = NSLOCTEXT("Remote_Factory", "Col_MercerSphere", "Mercer Sphere");
        return static_cast<uint8>(EWT_CollectibleKind::MercerSphere);
    }

    // ★0.2.127: the three forage foods (kinds 6..8) -- previously NOT classified at all, which is
    // why the live scan never saw them and pickups of them could never be recorded. Path stems
    // follow the Desc_<Name> asset convention (same family as the other descriptors).
    if (WTC_PathHas(ItemClass, TEXT("BerylNut")))
    {
        OutLabel = NSLOCTEXT("Remote_Factory", "Col_BerylNut", "绿石果");
        return static_cast<uint8>(EWT_CollectibleKind::BerylNut);
    }
    if (WTC_PathHas(ItemClass, TEXT("PaleBerry")))
    {
        OutLabel = NSLOCTEXT("Remote_Factory", "Col_PaleBerry", "古莓");
        return static_cast<uint8>(EWT_CollectibleKind::PaleBerry);
    }
    if (WTC_PathHas(ItemClass, TEXT("BaconAgaric")))
    {
        OutLabel = NSLOCTEXT("Remote_Factory", "Col_BaconAgaric", "培根伞菌");
        return static_cast<uint8>(EWT_CollectibleKind::BaconAgaric);
    }

    return 0xFF;
}

void FWTCollectibles::EnsurePickupMonitor()
{
    FWTPickupMonitor::EnsureCreated();
}
