#include "Remote_FactoryTranslations.h"
#include "Remote_FactoryLog.h"

#include "Interfaces/IPluginManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Internationalization/PolyglotTextData.h"
#include "Internationalization/TextLocalizationManager.h"

// Ships loose next to Content/ScreenLogo.png. Same folder-name fallbacks as the logo loader.
static FString WT_FindTranslationDirectory()
{
    TArray<FString> Candidates;
    if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SmartFactory")))
    {
        Candidates.Add(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Content/Localization")));
    }
    Candidates.Add(FPaths::Combine(FPaths::ProjectModsDir(), TEXT("SmartFactory/Content/Localization")));
    Candidates.Add(FPaths::Combine(FPaths::ProjectModsDir(), TEXT("Smart_Factory/Content/Localization")));
    for (const FString& Candidate : Candidates)
    {
        if (IFileManager::Get().DirectoryExists(*Candidate))
        {
            return Candidate;
        }
    }
    return FString();
}

void WT_RegisterTranslations()
{
    const FString Directory = WT_FindTranslationDirectory();
    if (Directory.IsEmpty())
    {
        WT_WARNF(TEXT("[translations] Content/Localization folder not found -- English only."));
        return;
    }

    TArray<FString> FileNames;
    IFileManager::Get().FindFiles(FileNames, *FPaths::Combine(Directory, TEXT("*.json")), /*Files=*/true, /*Directories=*/false);

    // One entry per key, holding every language for that key.
    TMap<FString, FPolyglotTextData> TextByKey;
    for (const FString& FileName : FileNames)
    {
        const FString Path = FPaths::Combine(Directory, FileName);
        FString Raw;
        if (!FFileHelper::LoadFileToString(Raw, *Path))
        {
            WT_WARNF(TEXT("[translations] could not read %s"), *Path);
            continue;
        }
        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            WT_WARNF(TEXT("[translations] %s is not valid JSON"), *Path);
            continue;
        }

        TArray<FString> Cultures;
        const TSharedPtr<FJsonObject>* Entries = nullptr;
        if (!Root->TryGetStringArrayField(TEXT("Cultures"), Cultures) || Cultures.Num() == 0
            || !Root->TryGetObjectField(TEXT("Entries"), Entries))
        {
            WT_WARNF(TEXT("[translations] %s needs \"Cultures\" and \"Entries\""), *Path);
            continue;
        }

        for (const auto& Pair : (*Entries)->Values)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            FString Source;
            FString Translation;
            if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Entry)
                || !(*Entry)->TryGetStringField(TEXT("Source"), Source)
                || !(*Entry)->TryGetStringField(TEXT("Translation"), Translation)
                || Source.IsEmpty() || Translation.IsEmpty())
            {
                continue;   // untranslated: the English from the code shows
            }

            FPolyglotTextData* TextData = TextByKey.Find(Pair.Key);
            if (!TextData)
            {
                // Namespace must match WT_LOCTEXT in Remote_FactoryLoc.h.
                TextData = &TextByKey.Add(Pair.Key, FPolyglotTextData(
                    ELocalizedTextSourceCategory::Game, TEXT("Remote_Factory"), Pair.Key, Source,
                    /*NativeCulture=*/FString()));
            }
            for (const FString& Culture : Cultures)
            {
                TextData->AddLocalizedString(Culture, Translation);
            }
        }
    }

    TArray<FPolyglotTextData> AllText;
    TextByKey.GenerateValueArray(AllText);
    FTextLocalizationManager::Get().RegisterPolyglotTextData(AllText, /*AddDisplayStrings=*/true);
    WT_LOGF(TEXT("[translations] registered %d keys from %d file(s) in %s"),
            AllText.Num(), FileNames.Num(), *Directory);
}
