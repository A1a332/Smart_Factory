#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Primary game module for the Remote Factory.
 *
 * This exists for one reason and it is not optional: a plugin module must export a concrete
 * IMPLEMENT_MODULE target. Without it the engine can load the DLL but has nothing to construct,
 * and the plugin manager reports
 *
 *     LogPluginManager: Error: '<Mod>' 插件加载失败，因为其被加载后模块 '<Mod>' 无法被成功初始化
 *
 * followed by an access violation while it unwinds -- the whole editor/cook run dies with it.
 * SML's own modules all have this; so does WirelessPower's WirelessPowerModule.cpp.
 *
 * StartupModule deliberately logs exactly one banner line. That banner is the only way to tell
 * from FactoryGame.log which build the game actually loaded, so it is the one message in the mod
 * that is never gated behind WT_LOG_VERBOSE.
 */
class FRemote_FactoryModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /**
     * ★0.2.36g 档位指纹：测试档与正式档**各有一条专属 LOG 串**（互斥 ⇒ 一份 dll 里恰好一条）。
     * 构建脚本 / `_verify_0236.py` / `make_release_terminal.py` 三方据此**证实**包的档位，
     * 而不是靠"搜不到 [wt-trace] 串"这种缺证推断。完整说明见 .cpp。
     */
    void LogBuildProfile();
};
