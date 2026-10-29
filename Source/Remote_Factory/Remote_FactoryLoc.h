#pragma once

#include "CoreMinimal.h"

/**
 * Localization shorthand for the Remote_Factory module.
 *
 * The ENGLISH text passed here IS the source language and the fallback for any language without a
 * translation. Translations live in Content/Localization/<culture>.json, one file per language,
 * loaded at startup by WT_RegisterTranslations(). Run check_translations.py after changing any
 * English text here, or that key falls back to English.
 *
 * Never wrap a string that is not shown to a player (log lines, AI prompts, internal ids): those
 * must stay as plain TEXT() so they do not pollute the translation target.
 */
#define WT_LOCTEXT(Key, Source) NSLOCTEXT("Remote_Factory", Key, Source)
