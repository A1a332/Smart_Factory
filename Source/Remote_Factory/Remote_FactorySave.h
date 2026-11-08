// 按存档持久化（per-save persistence）
// 把「智能仓储」「里程碑货舱即刻返回」两个开关状态随存档保存 / 载入，
// 实现「新存档默认关、开过的存档记住」。
// 机制：hook AFGGameState::Serialize，用魔数 + 版本标记读写；旧档无此标记段则回退默认（关），不破坏存档。
#pragma once

#include "CoreMinimal.h"

namespace Remote_FactorySave
{
    // 安装按存档持久化 hook（全局一次，内部 static bInstalled 守卫幂等）。
    // ★2026-11-04：主安装在 FRemote_FactoryModule::StartupModule（DLL 加载即装，早于任何世界/存档）
    //   ——必须早于读档，否则读档发生在安装前会被漏掉（实测「继续游戏」直接进存档时晚 24s）。
    //   GameWorldModule POST_INITIALIZATION 里那次调用保留作兜底（幂等 no-op）。
    void InstallPerSaveHooks();
}
