#include "Remote_FactoryCrashTrace.h"

#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"

// ★0.2.36g 正式档（WT_ENABLE_TRACE=0）下整个实现体被剔除 ⇒ 本 TU 编译成空文件，
//   [wt-trace] 的全部格式串与 zone 名都不进 .rdata。测试档（默认）行为不变。
#if WT_ENABLE_TRACE

namespace
{
    constexpr int32 WT_TRACE_RING = 256;    // completed-zone history
    constexpr int32 WT_TRACE_DEPTH = 32;    // live nesting limit
    constexpr int32 WT_TRACE_DUMP_ROWS = 28;
    constexpr double WT_TRACE_MIN_DUMP_GAP_SECONDS = 5.0;   // anti-flood
    constexpr double WT_TRACE_WARMUP_SECONDS = 3.0;         // ignore the first frames after arming

    struct FWTZoneEvent
    {
        const TCHAR* Zone = nullptr;
        double Begin = 0.0;
        double End = 0.0;
        int32 Depth = 0;
    };

    /**
     * All tracer state. A plain global: every member is POD or a weak pointer, so this can never
     * participate in GC and can never dangle. No UObject is stored.
     */
    struct FWTZoneState
    {
        FWTZoneEvent Events[WT_TRACE_RING];
        int32 Head = 0;          // next write slot
        int32 Count = 0;         // valid entries, capped at WT_TRACE_RING

        const TCHAR* Stack[WT_TRACE_DEPTH] = {};
        double StackBegin[WT_TRACE_DEPTH] = {};
        int32 StackTop = 0;
        int32 StackOverflow = 0; // nested past WT_TRACE_DEPTH

        bool bEnabled = true;
        bool bInDump = false;    // reentrancy guard

        // ---- watchdog ----
        TWeakObjectPtr<UWorld> World;
        FTSTicker::FDelegateHandle Handle;
        double LastFrameSeconds = 0.0;
        double ArmedSeconds = 0.0;
        double LastDumpSeconds = 0.0;
        float ThresholdMs = 200.f;
    };

    FWTZoneState GWT;

    /** Innermost live frame that matches `Zone`, or INDEX_NONE. */
    int32 FindLive(const TCHAR* Zone)
    {
        for (int32 i = GWT.StackTop - 1; i >= 0; --i)
        {
            if (GWT.Stack[i] == Zone)
            {
                return i;
            }
        }
        return INDEX_NONE;
    }
}

namespace WTTrace
{
    /** Per-frame watchdog body. Forward-declared here because StartWatchdog's ticker lambda calls
     *  it during StartWatchdog's own definition. */
    void TickWatchdog();

    double Now()
    {
        // FPlatformTime::Seconds is the monotonic wall clock; the engine frame counter is not
        // available from static context, and QueryPerformanceCounter-derived time is enough to
        // attribute a hitch to a function.
        return FPlatformTime::Seconds();
    }

    void SetEnabled(bool bEnabled) { GWT.bEnabled = bEnabled; }
    bool IsEnabled() { return GWT.bEnabled; }
    int32 RecordedCount() { return GWT.Count; }

    void Enter(const TCHAR* Zone, bool bCoarse)
    {
        if (!GWT.bEnabled || !Zone)
        {
            return;
        }

        if (GWT.StackTop < WT_TRACE_DEPTH)
        {
            GWT.Stack[GWT.StackTop] = Zone;
            GWT.StackBegin[GWT.StackTop] = FPlatformTime::Seconds();
            ++GWT.StackTop;
        }
        else
        {
            ++GWT.StackOverflow;
        }

        if (bCoarse)
        {
            // Unconditional: this is the breadcrumb a hard crash leaves behind. The last `>` without
            // a matching `<` in the log names the function that was executing when the process died.
            WT_WARNF(TEXT("[wt-trace] > %s"), Zone);
        }
    }

    void Exit(const TCHAR* Zone, bool bCoarse, double BeginSeconds)
    {
        if (!GWT.bEnabled || !Zone)
        {
            return;
        }

        const double EndSeconds = FPlatformTime::Seconds();
        const double ElapsedMs = (EndSeconds - BeginSeconds) * 1000.0;

        // Strict LIFO, but tolerate a mismatched name: a zone can legitimately be left on the stack
        // if something threw/asserted its way out of the scope. Pop the innermost match, and if
        // there is none, keep the stack as it is rather than corrupting it.
        const int32 Live = FindLive(Zone);
        int32 Depth = GWT.StackTop;
        if (Live != INDEX_NONE)
        {
            Depth = Live;
            GWT.StackTop = Live;
        }

        // Record the completed zone (ring buffer, oldest overwritten).
        FWTZoneEvent& E = GWT.Events[GWT.Head];
        E.Zone = Zone;
        E.Begin = BeginSeconds;
        E.End = EndSeconds;
        E.Depth = Depth;
        GWT.Head = (GWT.Head + 1) % WT_TRACE_RING;
        GWT.Count = FMath::Min(GWT.Count + 1, WT_TRACE_RING);

        if (bCoarse)
        {
            if (ElapsedMs >= static_cast<double>(GWT.ThresholdMs))
            {
                WT_WARNF(TEXT("[wt-trace] < %s (%.1f ms)  ** over the %.0f ms budget **"),
                         Zone, ElapsedMs, GWT.ThresholdMs);
                DumpChain(Zone);
            }
            else
            {
                WT_WARNF(TEXT("[wt-trace] < %s (%.1f ms)"), Zone, ElapsedMs);
            }
        }

        // A fine zone that blows the budget on its own is also worth naming -- otherwise a hitch
        // inside a coarse zone would only ever blame the coarse zone.
        if (!bCoarse && ElapsedMs >= static_cast<double>(GWT.ThresholdMs))
        {
            WT_WARNF(TEXT("[wt-trace] ! fine zone %s took %.1f ms (budget %.0f ms)"),
                     Zone, ElapsedMs, GWT.ThresholdMs);
        }
    }

    void Mark(const TCHAR* Text)
    {
        if (!GWT.bEnabled)
        {
            return;
        }
        WT_WARNF(TEXT("[wt-trace] * %s"), Text ? Text : TEXT("<null>"));
    }

    void DumpChain(const TCHAR* Reason)
    {
        if (GWT.bInDump)
        {
            return;   // logging can re-enter tick code; never recurse
        }
        GWT.bInDump = true;

        WT_WARNF(TEXT("[wt-trace] ===== CHAIN DUMP (%s) ====="), Reason ? Reason : TEXT("manual"));

        // 1) the live stack, outermost -> innermost, innermost marked as still running
        if (GWT.StackTop > 0)
        {
            const double Stamp = FPlatformTime::Seconds();
            FString Live;
            for (int32 i = 0; i < GWT.StackTop; ++i)
            {
                if (i > 0)
                {
                    Live += TEXT("  >  ");
                }
                Live += GWT.Stack[i];
                Live += FString::Printf(TEXT("[%.1fms]"), (Stamp - GWT.StackBegin[i]) * 1000.0);
            }
            WT_WARNF(TEXT("[wt-trace] live stack (outermost first): %s"), *Live);
            WT_WARNF(TEXT("[wt-trace] innermost live zone: %s  <-- look here first"),
                     GWT.Stack[GWT.StackTop - 1]);
        }
        else
        {
            WT_WARNF(TEXT("[wt-trace] live stack is empty -- the cost is outside every traced zone "
                          "(engine/asset load, GC, or an untraced path in this mod)"));
        }

        if (GWT.StackOverflow > 0)
        {
            WT_WARNF(TEXT("[wt-trace] note: %d zone(s) exceeded the %d-frame nesting limit"),
                     GWT.StackOverflow, WT_TRACE_DEPTH);
        }

        // 2) the most recently completed zones, newest first
        const int32 Rows = FMath::Min(GWT.Count, WT_TRACE_DUMP_ROWS);
        if (Rows > 0)
        {
            WT_WARNF(TEXT("[wt-trace] last %d completed zone(s), newest first:"), Rows);
            for (int32 k = 0; k < Rows; ++k)
            {
                const int32 idx = ((GWT.Head - 1 - k) % WT_TRACE_RING + WT_TRACE_RING) % WT_TRACE_RING;
                const FWTZoneEvent& E = GWT.Events[idx];
                if (!E.Zone)
                {
                    continue;
                }
                const double Ms = (E.End - E.Begin) * 1000.0;
                // Deliberately no width/left-align format specifiers: UE_LOG's varargs formatter is
                // its own parser and only `%s` / `%d` / `%f`-family specifiers are guaranteed. Pad
                // the name by hand instead of betting the log line on `%-38s`.
                FString Name(E.Zone ? E.Zone : TEXT("?"));
                FString Padded = Name + FString::ChrN(FMath::Max(0, 38 - Name.Len()), TEXT(' '));
                WT_WARNF(TEXT("[wt-trace]    d%d  %s  %8.2f ms"), E.Depth, *Padded, Ms);
            }
        }

        WT_WARNF(TEXT("[wt-trace] ===== END CHAIN ====="));
        GWT.bInDump = false;
    }

    bool IsWatchdogRunning()
    {
        return GWT.Handle.IsValid();
    }

    bool StartWatchdog(UWorld* World, float ThresholdMs)
    {
        if (GWT.Handle.IsValid() && GWT.World.Get() == World)
        {
            return true;   // already armed for this world
        }
        StopWatchdog();

        GWT.World = World;
        GWT.ThresholdMs = ThresholdMs;
        const double Stamp = FPlatformTime::Seconds();
        GWT.LastFrameSeconds = Stamp;
        GWT.ArmedSeconds = Stamp;

        GWT.Handle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda([](float) -> bool
            {
                if (!GWT.World.IsValid())
                {
                    // The world went away (back to the main menu / level change). Let the ticker
                    // remove itself by returning false; do NOT call RemoveTicker from inside.
                    GWT.Handle.Reset();
                    GWT.World.Reset();
                    return false;
                }
                TickWatchdog();
                return true;
            }),
            0.f);

        WT_WARNF(TEXT("[wt-trace] hitch watchdog armed (budget %.0f ms/frame)."), ThresholdMs);
        return GWT.Handle.IsValid();
    }

    void StopWatchdog()
    {
        if (GWT.Handle.IsValid())
        {
            FTSTicker::GetCoreTicker().RemoveTicker(GWT.Handle);
            GWT.Handle.Reset();
        }
        GWT.World.Reset();
    }
}

// ---------------------------------------------------------------------------------------------
// The per-frame entry point (declared at the top of the namespace so StartWatchdog can call it).
// ---------------------------------------------------------------------------------------------
namespace WTTrace
{
    void TickWatchdog()
    {
        const double Stamp = FPlatformTime::Seconds();
        const double DeltaSeconds = Stamp - GWT.LastFrameSeconds;
        GWT.LastFrameSeconds = Stamp;

        if (!GWT.bEnabled)
        {
            return;
        }
        // The first frames after arming include whatever triggered the registration (world load),
        // which is legitimately slow; do not blame a zone for it.
        if (Stamp - GWT.ArmedSeconds < WT_TRACE_WARMUP_SECONDS)
        {
            return;
        }
        const double DeltaMs = DeltaSeconds * 1000.0;
        if (DeltaMs < static_cast<double>(GWT.ThresholdMs))
        {
            return;
        }
        // Anti-flood: one dump per gap window.
        if (Stamp - GWT.LastDumpSeconds < WT_TRACE_MIN_DUMP_GAP_SECONDS)
        {
            return;
        }
        GWT.LastDumpSeconds = Stamp;

        WT_WARNF(TEXT("[wt-trace] FRAME HITCH: %.1f ms since the previous frame (budget %.0f ms)"),
                 DeltaMs, GWT.ThresholdMs);
        DumpChain(TEXT("frame hitch"));
    }
}

#endif  // WT_ENABLE_TRACE
