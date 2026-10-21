#pragma once

// ---------------------------------------------------------------------------------------------
// Logging for the Remote Factory.
//
// The macros deliberately take __VA_ARGS__ and do NOT wrap the format string in TEXT() themselves.
// That matters: writing `WT_LOGF(TEXT("x %s"), y)` is the natural way to call these, and a macro body
// of the form UE_LOG(..., TEXT(Format)) would expand that to TEXT(TEXT("x %s")) -- nested TEXT() is a
// syntax error, and the cascade of errors it produces (C2065/C2143/C2059/"L is undefined") points at
// the log line rather than at the macro, which makes it painful to diagnose.
//
// So: arguments pass straight through to UE_LOG, exactly like WirelessPower's WP_LOGF.
// ---------------------------------------------------------------------------------------------

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRemote_Factory, Log, All);

// Runtime-logging master switch. While false, WT_LOGF statements compile away entirely -- because
// it is an `if`, the arguments are not evaluated either, so no FString/Printf work happens on the
// game thread for routine chatter. Set to true, repackage, reproduce, read the log, set it back.
//
// NOT gated: Warning/Error calls. They only fire when something is genuinely wrong and are never
// high-frequency, so they stay visible in a release log.
static constexpr bool WT_LOG_VERBOSE = false;

#define WT_LOGF(...)  do { if (WT_LOG_VERBOSE) { UE_LOG(LogRemote_Factory, Log,     __VA_ARGS__); } } while (0)
#define WT_WARNF(...) UE_LOG(LogRemote_Factory, Warning, __VA_ARGS__)
#define WT_ERRF(...)  UE_LOG(LogRemote_Factory, Error,   __VA_ARGS__)

// ---------------------------------------------------------------------------------------------
// ★0.2.36d 「每类一次」告警。
//
// WT_WARNF 的契约（见上）是「真出问题、且**绝不高频**」。但有几条诊断住在**构造函数**里
// （WT_CopyVanillaMeshMaterials / WT_ReuseVanillaInteractWidget / WT_UseNativeInteractPanel），
// 而构造函数是**每个实例生成时都跑** —— 读档时逐个还原建筑，存档里 N 栋就是 N 遍，
// 每遍 1~3 行 WARN。这既是同步日志 I/O，也会把真正的诊断淹掉。
//
// 这几种信息的价值是「一次性通报」（材质抄到没抄到、面板指到没指到），重复 N 遍不加信息量。
// 所以：同一 Key 全进程只报一次。Key 用「函数标签 + 类名」，让不同建筑各报各的。
//
// 注意：**不是**给高频路径用的万能解 —— 真正高频（Tick）的路径不该打任何 WARN，
// 而不是「打了再按 Key 去重」（去重本身也要走一次 TSet 查找）。
// ---------------------------------------------------------------------------------------------
inline bool WT_WarnOnce(const TCHAR* Tag, const UObject* Obj)
{
    static TSet<FString> Warned;
    const FString Key = FString::Printf(TEXT("%s:%s"), Tag,
        Obj ? *Obj->GetClass()->GetName() : TEXT("<null>"));
    if (Warned.Contains(Key))
    {
        return false;
    }
    Warned.Add(Key);
    return true;
}

#define WT_WARNF_ONCE(Tag, Obj, ...) \
    do { if (WT_WarnOnce(Tag, Obj)) { UE_LOG(LogRemote_Factory, Warning, __VA_ARGS__); } } while (0)
