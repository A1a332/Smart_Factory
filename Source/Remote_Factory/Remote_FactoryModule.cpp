#include "Remote_FactoryModule.h"
#include "Remote_FactoryLog.h"
#include "Remote_FactoryVersion.h"
#include "Remote_FactorySave.h"   // ★2026-11-04 按存档持久化 hook：必须在 StartupModule 早装（见下方）
// ★1.0.26 专用服务器禁用（用户 2026-10-25 定案）：IsRunningDedicatedServer() 的
// 一手声明处（CoreMisc.h:152 FORCEINLINE 无参；FactoryServer 二进制下经
// FPlatformProperties::IsServerOnly() 恒 true）。显式 include 不依赖传递包含。
#include "Misc/CoreMisc.h"
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
    // ★1.0.26 专用服务器禁用闸门（用户 2026-10-25 定案）：本 mod 为单机向，禁止在
    // dedicated server 上生效。FactoryServer 二进制下 IsRunningDedicatedServer() 恒 true，
    // 此处直接短路：不打加载横幅、不注册任何东西。配方/研究树/建筑/传送/面板的注册
    // 全在 GameWorldModule::DispatchLifecycleEvent，那里有同款闸门；4 个 wt.* 调试命令
    // 是静态初始化注册的（运行时拦不住），已用 #if !UE_SERVER 在编译期剔除。
    if (IsRunningDedicatedServer())
    {
        UE_LOG(LogRemote_Factory, Warning,
            TEXT("Remote Factory %s: dedicated server detected - this mod is DISABLED on dedicated "
                 "servers (single-player mod). Installing it on a dedicated server has no effect."),
            REMOTE_FACTORY_VERSION_TEXT);
        return;
    }

    // ★1.0.5 撤销 1.0.2 的 pak 挂载点补偿（RegisterMountPoint 两条）——实测证明它是**有害**的：
    // 1.0.4 实测日志 vs 手工部署正常期日志对比实锤：
    //  - "Mount point ... not mounted to a valid Root Path"（IPlatformFilePak.cpp:6168）对**所有**
    //    mod（含 SML/AllNodesPure 等正常 mod）都在报，是 pak 层噪音；IoStore 资产走
    //    LogFilePackageStore "Mounting container"（NumPackages=4）挂载，与该警告无关，两局一致。
    //  - 1.0.2/1.0.3 加的 RegisterMountPoint("/Smart_Factory/", ".../Mods/Smart_Factory/Content/")
    //    把磁盘**散装 Content**（uncooked Editor 格式 uasset）映射成了 /Smart_Factory/ 的包供给者，
    //    LoadObject 命中散装版而非 pak 内 cooked 版 ⇒ "Missing shader resource ... M_ScreenLogo"
    //    （shader 库无此 hash，Shipping 又不能现场编译）⇒ 默认材质 ⇒ 屏幕米黄。**补偿本身制造了病**。
    // 正确行为：什么都不注册。IoStore 容器由引擎扫描 Mods/<目录>/Content/Paks/ 挂载（与目录名
    // 无关），PackageStore 固化了 cook 时的包名 /Smart_Factory/...，LoadObject 直达 cooked 资产。
    // 若未来用新插件名重烤 pak（包名 /SmartFactory/），需同步改 C++ 加载路径候选，而不是恢复
    // 这两条注册。

    UE_LOG(LogRemote_Factory, Log,
        TEXT("Remote Factory %s loaded: whole-map machine console with live output, native UI, "
             "output pull and teleport; plus a full-map alien-energy browser (from collectibles.json) "
             "with teleport."),
        REMOTE_FACTORY_VERSION_TEXT);

    LogBuildProfile();   // ★0.2.36g 档位指纹，紧跟加载横幅（见下方函数注释）

    // ★2026-11-04 修「每次进存档两个开关都回到关」：
    //   按存档持久化靠 hook `AFGGameState::Serialize`（存档写魔数、读档还原）。该 hook 必须在
    //   【任何世界/存档加载之前】装上，否则读档发生在安装前 → 读档被漏掉、开关回落默认关。
    //   原实现挂在 GameWorldModule 的 POST_INITIALIZATION（随世界初始化才跑）——实测「继续游戏」
    //   直接进存档时，世界 05:03:41 就先加载了、hook 05:04:05 才装（晚 24s），读档全程没被拦到。
    //   这里提前到模块 StartupModule（DLL 加载即装，早于所有世界）；GameWorldModule 里那次保留作
    //   幂等兜底（InstallPerSaveHooks 内有 static bInstalled 守卫，重复调用直接返回）。
    Remote_FactorySave::InstallPerSaveHooks();
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
