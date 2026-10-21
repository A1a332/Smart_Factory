#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "InputCoreTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Remote_FactoryHotkey.generated.h"

/**
 * Form 3 "B": a global hotkey that opens the AI chat window without opening the terminal first.
 *
 * ## ★ Why this is a raw IInputProcessor and not Enhanced Input
 *
 * FORM3_RECON concluded that B would have to introduce `.uasset` files, because the "official"
 * routes all want authored input data:
 *
 *   * AFGEquipment::mDefaultEquipmentActions wants a UInputAction asset
 *   * UFGEnhancedInputComponent::BindActionByTag wants the tag bound in a mapping context
 *   * UFGChildInputMappingContext IS the sanctioned mod extension point -- and is still a primary
 *     data asset that must be cooked and packaged
 *
 * None of that is needed. `UFGPlayerInput` (the game's own UEnhancedPlayerInput subclass) exposes
 * a PUBLIC STATIC method that registers a custom input preprocessor, and IInputProcessor is the
 * engine interface for intercepting keys BEFORE Slate. Registering there needs no asset, no mapping
 * context, and no modification to any game class -- it is an extension point the game already ships
 * and already uses itself for its own key-release tracking.
 *
 * So this breaks the "B must ship .uasset" conclusion and the mod keeps its zero-asset property.
 * The price is that a preprocessor is a raw Slate-layer object rather than an Enhanced Input action,
 * so three things have to be handled by hand:
 *
 *   1. Lifetime. There is no automatic unregistration, so this object must be explicitly removed --
 *      see URemote_FactoryHotkeySubsystem::Deinitialize, which is the only reliable place.
 *   2. Text input. A preprocessor sees keys before ANY widget does, so it must not swallow typing.
 *      ShouldIgnoreInput() is what keeps the chat box and every other text field usable.
 *   3. Gamepad. A key event cannot speak for a controller; gamepad support is explicitly out of
 *      scope here. The handheld terminal's "equip to open" path is the gamepad-friendly entrance.
 */
class REMOTE_FACTORY_API FWTInputProcessor : public IInputProcessor
{
public:
    FWTInputProcessor();
    virtual ~FWTInputProcessor() override;

    //~ Begin IInputProcessor interface
    virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
    virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
    //~ End IInputProcessor interface

    /**
     * The key that opens the chat window.
     *
     * Ctrl+T by default. T alone is Flashlight in vanilla, and modified keys are exactly the case
     * UFGInputTriggerChordBinding exists for -- requiring the Ctrl modifier sidesteps the entire
     * question of which unmodified keys are taken, instead of enumerating them.
     */
    FKey Hotkey;

private:
    /** True when a text field owns focus, in which case keys must not be treated as commands. */
    bool ShouldIgnoreInput(FSlateApplication& SlateApp) const;
};

/**
 * Owns the process-wide input processor.
 *
 * FWTInputProcessor is a plain (non-UObject) Slate object, so something with a real lifetime has to
 * hold it. A UGameInstanceSubsystem is the right something: created once per game instance, it
 * survives level loads, and Deinitialize is a reliable unregistration point -- which a file-static
 * would not give us, and which matters because leaving a preprocessor registered across a level
 * transition would leak a handler that still points at a dead widget.
 */
UCLASS()
class REMOTE_FACTORY_API URemote_FactoryHotkeySubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    TSharedPtr<FWTInputProcessor> Processor;
};
