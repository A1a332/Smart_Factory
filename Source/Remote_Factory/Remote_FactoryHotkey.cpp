#include "Remote_FactoryHotkey.h"

#include "Remote_FactoryLog.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

// ----------------------------------------------------------------------------
// FWTInputProcessor
// ----------------------------------------------------------------------------

FWTInputProcessor::FWTInputProcessor()
{
    // Ctrl+T. See the header for why a chord rather than a bare key.
    Hotkey = EKeys::T;
}

FWTInputProcessor::~FWTInputProcessor() = default;

void FWTInputProcessor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
    // The processor is event-driven; there is no per-frame work to do. An empty Tick is the normal
    // shape for a key interceptor (the game's own UFGPlayerInputPreProcessor does the same, with
    // only a key-state table to maintain).
}

bool FWTInputProcessor::ShouldIgnoreInput(FSlateApplication& SlateApp) const
{
    // ★ The single most important line in this file.
    //
    // A preprocessor runs BEFORE Slate dispatches the key, so without this check the hotkey would
    // fire while the player is typing "t" into the chat box -- and worse, so would any bound key.
    // Slate reports whether it has a widget that wants keyboard input; if it does, the key belongs
    // to that widget and we must not touch it.
    //
    // GetUserFocusedWidget covers text fields, focused buttons and the like. Checking it (rather
    // than only checking for an editable text) is the conservative choice: it also protects the
    // game's own rebinding menu and any future UI, at the cost of the hotkey not working while some
    // non-text widget happens to hold focus -- which is the right trade.
    const TSharedPtr<SWidget> Focused = SlateApp.GetUserFocusedWidget(0);
    if (Focused.IsValid() && Focused->SupportsKeyboardFocus())
    {
        return true;
    }

    // Also ignore while a modal-ish window owns the keyboard: GetUserFocusedWidget can return null
    // during a focus transition, and firing the hotkey into that gap would open the chat window on
    // top of whatever just opened.
    if (SlateApp.GetActiveModalWindow().IsValid())
    {
        return true;
    }

    return false;
}

bool FWTInputProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
    // Cheap rejection first: this runs for every keystroke in the game.
    if (InKeyEvent.GetKey() != Hotkey)
    {
        return false;
    }

    // Require Ctrl. Any of the two physical Ctrl keys counts; the left one is what a player will
    // press, but accepting both costs nothing and a chord that only works on one side is a
    // papercut for anyone who happens to use the right one.
    const bool bCtrlDown =
        InKeyEvent.IsControlDown()
        || FSlateApplication::Get().GetModifierKeys().IsControlDown();

    if (!bCtrlDown)
    {
        return false;
    }

    if (ShouldIgnoreInput(SlateApp))
    {
        return false;
    }

    // Auto-repeat: holding the chord would otherwise toggle the window every ~30 ms. Ignoring the
    // repeat (rather than debouncing with a timer) is what the engine's own handlers do.
    if (InKeyEvent.IsRepeat())
    {
        return true;
    }

    // Calculator removed from this version (0.2.66): the hotkey no longer opens it, so the
    // event is not consumed and falls through to the game as it would without this handler.
    return false;
}

// ----------------------------------------------------------------------------
// URemote_FactoryHotkeySubsystem
// ----------------------------------------------------------------------------

void URemote_FactoryHotkeySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Register the processor with Slate directly rather than through the game's
    // AddBlueprintInputPreprocessor helper.
    //
    // WHY: that helper takes a TScriptInterface<UFGBlueprintInputPreprocessor>, i.e. it wants a
    // UObject implementing the Blueprint interface, and it parks the object in a
    // TArray<TWeakObjectPtr<UObject>> on the player input. The Slate-level interface is what we
    // actually need (it is the only layer that can consume a key before Enhanced Input sees it),
    // and it is registered through the standard FSlateApplication entry point that every engine
    // input processor uses.
    //
    // Note the ordering constraint: Slate must exist. On a dedicated server it does not, so this
    // whole subsystem is a no-op there -- which is correct, since a dedicated server has no keyboard.
    if (!FSlateApplication::IsInitialized())
    {
        WT_LOGF(TEXT("[hotkey] Slate not initialised (dedicated server?); hotkey disabled."));
        return;
    }

    Processor = MakeShared<FWTInputProcessor>();
    FSlateApplication::Get().RegisterInputPreProcessor(Processor);

    WT_LOGF(TEXT("[hotkey] registered Ctrl+T chat hotkey."));
}

void URemote_FactoryHotkeySubsystem::Deinitialize()
{
    if (Processor.IsValid() && FSlateApplication::IsInitialized())
    {
        // Unregister before dropping the reference. Without this the Slate application keeps a
        // shared pointer to a processor whose only remaining use is a dead widget pointer, and the
        // next keypress walks into it.
        FSlateApplication::Get().UnregisterInputPreProcessor(Processor);
        WT_LOGF(TEXT("[hotkey] unregistered chat hotkey."));
    }
    Processor.Reset();

    Super::Deinitialize();
}
