#include "Remote_FactoryModule.h"
#include "Remote_FactoryLog.h"
#include "Remote_FactoryVersion.h"
// 仅为拿到 WT_ENABLE_TRACE 的兜底默认值（未定义时 #if 会当 0 ⇒ 静默走错档位）。
#include "Remote_FactoryCrashTrace.h"

// The category is defined here, in the module's own translation unit, so it exists before anything
// else in the mod runs (including the startup banner below).
DEFINE_LOG_CATEGORY(LogRemote_Factory);

/**
 * Single load banner so the game log names the build that was actually loaded. Every other
 * log-level call in this mod goes through WT_LOGF() and is compiled out by default
 * (see Remote_FactoryLog.h); this one is the fingerprint and stays unconditional.
 */
void FRemote_FactoryModule::StartupModule()
{
    UE_LOG(LogRemote_Factory, Log,
        TEXT("Remote Factory %s loaded: whole-map machine console with live output, native UI, "
             "output pull and teleport; plus a full-map alien-energy browser (from collectibles.json) "
             "with teleport."),
        REMOTE_FACTORY_VERSION_TEXT);

    LogBuildProfile();   // ★0.2.36g 档位指纹，紧跟加载横幅（见下方函数注释）
}

/**
 * ★0.2.36g 档位指纹（测试档 / 正式档各一条，互斥 ⇒ 一份 dll 里**恰好**有一条）。
 *
 * 为什么要专门做一条指纹：判定"这份 dll 属于哪个档"必须是**证实**，不能是
 * "没搜到 `[wt-trace]` 串"这种**缺证**推断 —— 缺证就是自证式假阴性（搜不到也可能是
 * 搜索方式错了、或文件读错了）。有了各自专属的串，构建脚本 / `_verify_0236.py` /
 * `make_release_terminal.py` 三方都能拿到"这份包到底是什么档"的正证据，
 * 从而**拒绝把测试档当成正式版发出去**（反之亦然）。
 *
 * 用 `Log` 级别：与上面的加载横幅一致 —— UE_LOG 不会按级别把字面量编掉，
 * 读档时日志里也就能直接看到当前载入的是哪个档。
 */
void FRemote_FactoryModule::LogBuildProfile()
{
#if WT_ENABLE_TRACE
    UE_LOG(LogRemote_Factory, Log,
        TEXT("[wt-profile] TEST build: crash/hitch chain tracer ENABLED (WT_ENABLE_TRACE=1)."));
#else
    UE_LOG(LogRemote_Factory, Log,
        TEXT("[wt-profile] RELEASE build: crash/hitch chain tracer COMPILED OUT (WT_ENABLE_TRACE=0)."));
#endif
}

void FRemote_FactoryModule::ShutdownModule()
{
    UE_LOG(LogRemote_Factory, Log, TEXT("Remote Factory unloaded."));
}

// Without this, UE can load the DLL but has no module to initialise: the plugin manager then
// fails the whole plugin and the process dies on the way out (seen as an access violation inside
// CoreUObject during cook). Every plugin module needs exactly one of these.
IMPLEMENT_MODULE(FRemote_FactoryModule, Remote_Factory)
