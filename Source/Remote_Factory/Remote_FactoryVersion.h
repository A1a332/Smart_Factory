#pragma once

// ---------------------------------------------------------------------------------------------
// Single source of truth for the in-game banner version. Mirrors Remote_Factory.uplugin
// "VersionName" / mod.json "version" — bump all three together (the D01 lesson from
// WirelessPower v1.1.2: the startup banner must never drift from the packaged version).
// ---------------------------------------------------------------------------------------------

#define REMOTE_FACTORY_VERSION_TEXT TEXT("1.0.12")
