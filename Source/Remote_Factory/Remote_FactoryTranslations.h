#pragma once

#include "CoreMinimal.h"

/**
 * Loads the translation files from the mod's Content/Localization folder (one .json per language)
 * and hands them to the engine, which then shows the right language for every WT_LOCTEXT string
 * and switches live when the player changes language.
 *
 * A translation is only used while its "Source" matches the English in the code exactly. Run
 * check_translations.py after changing any English text.
 */
void WT_RegisterTranslations();
