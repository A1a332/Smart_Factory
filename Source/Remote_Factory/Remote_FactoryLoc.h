#pragma once

#include "CoreMinimal.h"

/**
 * Localization shorthand for the Remote_Factory module.
 *
 * The ENGLISH text passed here IS the source language -- it is what GatherText harvests into
 * Content/Localization/Remote_Factory/ and what ships as the fallback when the player's game
 * language has no matching .locres entry. Chinese lives in zh-Hans/Remote_Factory.po.
 *
 * Never wrap a string that is not shown to a player (log lines, AI prompts, internal ids): those
 * must stay as plain TEXT() so they do not pollute the translation target.
 */
#define WT_LOCTEXT(Key, Source) NSLOCTEXT("Remote_Factory", Key, Source)
