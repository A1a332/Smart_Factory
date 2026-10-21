#pragma once

// ---------------------------------------------------------------------------------------------
// Crash / hitch chain tracer (added 2026-10-07, user request: "增加一个崩溃/卡顿链快速定位").
//
// The problem this solves
// ----------------------
// When the game crashes or hitches, the log tells you *where in the log* it happened, which is not
// the same as *which of our functions* was running. Two failure shapes are indistinguishable today:
//
//   * a hard crash  -> the log simply stops; you cannot tell whether the last entry was the end of
//                      a step or the beginning of the next one;
//   * a frame hitch -> nothing is logged at all, so a 400 ms stall from a whole-world scan looks
//                      exactly like a 400 ms stall from a synchronous asset load.
//
// What this gives you
// -------------------
//   1. ZONE MARKERS. `WT_ZONE("...")` / `WT_ZONE_COARSE("...")` put a named scope on a stack.
//      Coarse zones print `[wt-trace] > name` on entry and `[wt-trace] < name (12.3 ms)` on exit,
//      so after a hard crash **the last `>` with no matching `<` names the exact function**.
//      Fine zones stay silent (no log spam) but are still recorded in the ring buffer, so a hitch
//      dump can still report them.
//   2. HITCH WATCHDOG. A core ticker measures the gap between frames; anything over the threshold
//      dumps both the live stack and the last N completed zones as `[wt-trace] CHAIN DUMP`.
//      That is the "stutter chain": the innermost live zone plus whatever finished just before it.
//   3. `WTTrace::DumpChain(reason)` can be called manually from anywhere.
//
// Design constraints (all of them are load-bearing)
// -------------------------------------------------
//   * SHIPPING-SAFE OUTPUT. Everything goes through WT_WARNF / WT_ERRF. WT_LOGF is compiled out
//     when WT_LOG_VERBOSE is false, which is the default -- instrumentation behind WT_LOGF would
//     be absent from the very builds we ship and test.
//   * NO UOBJECT REFERENCES in the trace state. Zone names are `const TCHAR*` pointing at string
//     literals, so the buffer can never hold a dangling UObject and can never keep one alive.
//   * NO ALLOCATION on the hot path: a fixed ring buffer and a fixed-size stack. The only heap use
//     is inside DumpChain, which by definition runs when something has already gone wrong.
//   * EXACTLY ONE WATCHDOG, keyed to a world. A function-local `static bool` would survive a level
//     change and leave the guard permanently true (see the mod's other cross-world notes); the
//     start function therefore compares the stored TWeakObjectPtr<UWorld> instead.
//   * REENTRANCY GUARD on DumpChain: logging can itself re-enter tick code.
// ---------------------------------------------------------------------------------------------

#include "CoreMinimal.h"
#include "Remote_FactoryLog.h"

// ---------------------------------------------------------------------------------------------
// ★0.2.36g 档位开关（用户定案：「崩溃/卡顿链快速定位」只在测试版保留，正式版移除）。
//   WT_ENABLE_TRACE 由 Remote_Factory.Build.cs 注入：测试档=1，出正式版时设环境变量
//   WT_TRACE=0 再构建 ⇒ 0。这里给兜底默认值，避免头文件被单独解析时未定义。
//
//   注意"移除"的分量：关掉时 WT_ZONE / WT_ZONE_COARSE / WT_TRACE_MARK 展开成 `((void)0)`，
//   **宏参数根本不会被展开进产物** —— 所以 49 处 zone 名字面量与 CHAIN DUMP 的格式串都不会
//   进 .rdata。这正是"正式版移除"与"运行时 SetEnabled(false)"的本质区别（后者只是不干活，
//   字符串照样带着）。
// ---------------------------------------------------------------------------------------------
#ifndef WT_ENABLE_TRACE
    #define WT_ENABLE_TRACE 1
#endif

class UWorld;

#if WT_ENABLE_TRACE
namespace WTTrace
{
    /** Record a zone entry. Fine zone: ring buffer only. */
    void Enter(const TCHAR* Zone, bool bCoarse);

    /** Record the matching zone exit (and, for a coarse zone, emit the `< name (ms)` line). */
    void Exit(const TCHAR* Zone, bool bCoarse, double BeginSeconds);

    /** Wall-clock seconds. Used by the RAII scope so enter/exit cannot disagree about the clock. */
    double Now();

    /** One-off breadcrumb: `[wt-trace] * text` at Warning level. */
    void Mark(const TCHAR* Text);

    /** Dump the live stack + the most recent completed zones at Warning level. */
    void DumpChain(const TCHAR* Reason);

    /**
     * Arm the per-frame hitch watchdog. Idempotent for the same world; re-arms when the world
     * changes. Returns false when there is no core ticker available (harmless: zones still work).
     */
    bool StartWatchdog(UWorld* World, float ThresholdMs = 200.f);

    /** Disarm the watchdog. Safe to call when it is not running. */
    void StopWatchdog();

    bool IsWatchdogRunning();

    /** Global switch, for a debugger / a bisect. Defaults to true. */
    void SetEnabled(bool bEnabled);
    bool IsEnabled();

    /** Number of completed zone events currently held. */
    int32 RecordedCount();
}

/**
 * RAII zone marker. `Begin` is captured in the constructor rather than at Enter() time so the
 * elapsed time covers the whole scope even if Enter() itself was skipped (disabled tracer).
 */
struct FWTZoneScope
{
    FWTZoneScope(const TCHAR* InZone, bool bInCoarse)
        : Zone(InZone), bCoarse(bInCoarse), Begin(WTTrace::Now())
    {
        WTTrace::Enter(Zone, bCoarse);
    }

    ~FWTZoneScope()
    {
        WTTrace::Exit(Zone, bCoarse, Begin);
    }

    FWTZoneScope(const FWTZoneScope&) = delete;
    FWTZoneScope& operator=(const FWTZoneScope&) = delete;

private:
    const TCHAR* Zone;
    bool bCoarse;
    double Begin;
};

#define WT_CONCAT_IMPL_(A, B) A##B
#define WT_CONCAT_(A, B) WT_CONCAT_IMPL_(A, B)

/** Fine-grained zone: buffered only, never logged on its own. Use inside hot loops. */
#define WT_ZONE(Name)        FWTZoneScope WT_CONCAT_(WTZoneScope_, __LINE__)(Name, false)
/** Coarse zone: also prints `> name` / `< name (ms)` at Warning level. Use for anything expensive. */
#define WT_ZONE_COARSE(Name) FWTZoneScope WT_CONCAT_(WTZoneScope_, __LINE__)(Name, true)
/** One-off breadcrumb. */
#define WT_TRACE_MARK(Text)  WTTrace::Mark(Text)

#else   // ---- WT_ENABLE_TRACE == 0：正式档，整套追踪编译期剔除 --------------------------------

// 空实现但**签名保持一致**，所以唯一的直接调用点（GameWorldModule 的 WTTrace::StartWatchdog）
// 不需要 #if —— 少一处条件编译就少一个"只在某一档才编得过"的隐患。
namespace WTTrace
{
    FORCEINLINE void   Enter(const TCHAR* /*Zone*/, bool /*bCoarse*/) {}
    FORCEINLINE void   Exit(const TCHAR* /*Zone*/, bool /*bCoarse*/, double /*BeginSeconds*/) {}
    FORCEINLINE double Now() { return 0.0; }
    FORCEINLINE void   Mark(const TCHAR* /*Text*/) {}
    FORCEINLINE void   DumpChain(const TCHAR* /*Reason*/) {}
    FORCEINLINE bool   StartWatchdog(UWorld* /*World*/, float /*ThresholdMs*/ = 200.f) { return false; }
    FORCEINLINE void   StopWatchdog() {}
    FORCEINLINE bool   IsWatchdogRunning() { return false; }
    FORCEINLINE void   SetEnabled(bool /*bEnabled*/) {}
    FORCEINLINE bool   IsEnabled() { return false; }
    FORCEINLINE int32  RecordedCount() { return 0; }
}

// 宏展开成空语句：**参数不展开** ⇒ zone 名与 breadcrumb 字面量都不会进 .rdata。
#define WT_ZONE(Name)        ((void)0)
#define WT_ZONE_COARSE(Name) ((void)0)
#define WT_TRACE_MARK(Text)  ((void)0)

#endif  // WT_ENABLE_TRACE
