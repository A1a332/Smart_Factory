using UnrealBuildTool;

public class Remote_Factory : ModuleRules {
    public Remote_Factory(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // 本项目源码以 UTF-8 保存，且源码里直接出现中文字面量（面板标签、分类名）。
        // 不需要在这里手动加 /utf-8：UE5 的 VCToolChain.cs 对 MSVC **无条件**加 /utf-8
        // （见 Engine/Source/Programs/UnrealBuildTool/Platform/Windows/VCToolChain.cs:650），
        // 同时加 /wd4819 抑制非英文代码页的警告。曾经写过的 AddCompilerFlagIfSupported
        // 在这个 UBT 版本里并不存在（CS0103），已移除。
        PrivateDefinitions.Add("_CRT_SECURE_NO_WARNINGS");

        // ★0.2.36g 追踪设施档位开关（用户定案：「崩溃/卡顿链快速定位」只在**测试版**保留，
        //   正式版移除。原话：「这个在正式版移除，只针对测试版时进行快速查询漏洞」）。
        //
        //   WT_TRACE 未设 / 非 "0"  ⇒ WT_ENABLE_TRACE=1 = **测试档**：带 [wt-trace] 面包屑 +
        //                              CHAIN DUMP + hitch watchdog（即 0.2.36d 那套）。
        //   WT_TRACE=0                ⇒ WT_ENABLE_TRACE=0 = **正式档**：整套追踪被预处理器剔除。
        //     注意这不是"运行时关掉"：宏参数不会展开进产物，所以 49 处 zone 名字面量与
        //     CHAIN DUMP 的格式串**都不会进 .rdata**（这才是"移除"的真义）。
        //
        //   用法：出正式版前 `WT_TRACE=0 python build_wt_now.py` 重建，再跑 make_release_terminal.py
        //   （后者会**拒绝**打包测试档 dll，见该脚本的档位闸门）。
        //   档位会被编成一条 [wt-profile] 指纹串（见 Remote_FactoryModule.cpp），
        //   构建脚本 / _verify_0236.py / make_release_terminal.py 三方都据此核对。
        //   ★用 `global::System.Environment` 而不是裸 `System.Environment`：本文件有
        //   `using UnrealBuildTool;`，若该命名空间下存在同名类型 `System`，裸写会被解析成它
        //   而编译失败（C# 的"外层命名空间优先"）。写全 `global::` 前缀彻底规避。
        string WtTraceEnv = global::System.Environment.GetEnvironmentVariable("WT_TRACE");
        bool bWtTraceEnabled = (WtTraceEnv != "0");
        PrivateDefinitions.Add("WT_ENABLE_TRACE=" + (bWtTraceEnabled ? "1" : "0"));

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "FactoryGame",   // AFGBuildable / AFGBuildableFactory / UFGInteractWidget / AFGPlayerController
            "SML",           // Satisfactory Mod Loader
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Projects",
            "Json",          // ★0.2.69: collectibles.json read/write (FJsonObject / FJsonSerializer)
            "JsonUtilities", // JSON value helpers used by the collectible registry
            "UMG",          // C++-built terminal panel (UFGInteractWidget subclass) + calculator window
            "Slate",
            "SlateCore",
            "EnhancedInput", // handheld keybinding
            "NavigationSystem" // ★0.2.153c: ProjectPointToNavigation fallback for hub landing-spot search
        });

        // Editor-only: used by the WTMakeNode commandlet to author the research-tree node
        // Blueprint asset (GetNodeSchematic -> return mNodeSchematic). Guarded so the
        // Shipping/Server game builds never link the editor.
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] {
                "UnrealEd",
                "BlueprintGraph",
                "KismetCompiler",
                "Kismet"
            });
        }
    }
}
