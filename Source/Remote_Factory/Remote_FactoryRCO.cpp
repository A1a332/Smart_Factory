#include "Remote_FactoryRCO.h"

#include "Remote_FactoryLoc.h"
#include "Remote_FactoryCollectibles.h"
#include "Remote_FactoryLog.h"
#include "Remote_FactoryCrashTrace.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGBuildableResourceExtractor.h"
#include "Buildables/FGBuildableResourceExtractorBase.h"
#include "Buildables/FGBuildableGenerator.h"
#include "Buildables/FGBuildableResourceSink.h"
#include "Buildables/FGBuildableResourceSinkShop.h"   // ★0.2.146 AWESOME 商店
#include "Buildables/FGBuildableMAM.h"                // ★0.2.146 MAM 稳定原生类（取代纯 widget 兜底）
#include "Buildables/FGBuildableSpaceElevator.h"
#include "Buildables/FGBuildableAutomatedWorkBench.h"
#include "Buildables/FGBuildableHubTerminal.h"
#include "Buildables/FGBuildableTradingPost.h"             // ★0.2.153 枢纽相关：AFGBuildableTradingPost + UFGTradingPostIntegratedBuildable
#include "FGBuildableSubsystem.h"                          // ★0.2.153 AFGBuildableSubsystem::Get / 模板 GetTypedBuildable
#include "FGItemPickup.h"                                  // AFGItemPickup（收集品扫描 / 传送落点判定）
#include "FGDropPod.h"                                     // AFGDropPod（空投舱扫描）
#include "Buildables/FGBuildableCircuitSwitch.h"           // AFGBuildableCircuitSwitch（远程开关）
#include "Buildables/FGBuildablePriorityPowerSwitch.h"      // AFGBuildablePriorityPowerSwitch（优先级开关）
#include "Remote_FactorySharedSession.h"                   // Remote_FactoryShared::IsSessionOwner / SetViewer
#include "Components/CapsuleComponent.h"                   // ★0.2.153 重建：UCapsuleComponent（枢纽/落点胶囊体）
#include "FGPowerInfoComponent.h"                          // ★0.2.153b 电力总览：建筑上的电力信息组件
#include "FGPowerCircuit.h"                                // ★0.2.153b UFGPowerCircuit::GetStats / FPowerCircuitStats
#include "Buildables/FGBuildablePowerStorage.h"            // ★0.2.153b 电池储能 GetPowerStore/GetPowerStoreCapacity
#include "Buildables/FGBuildableResourceSinkShop.h"        // ★0.2.153b AWESOME 商店分类（此前落 Other 被丢）
#include "Resources/FGItemDescriptor.h"                    // ★0.2.153m UFGItemDescriptor::GetBigIcon（列表行图标）
#include "Internationalization/Internationalization.h"      // ★0.2.153t FInternationalization::Get().GetCurrentCulture()（按游戏语言决定要不要用中文表）
#include "Internationalization/Culture.h"                   // ★0.2.153t FCulture::GetTwoLetterISOLanguageName()（Internationalization.h 只前向声明 FCulture，不解引用）
#include "FGVehicle.h"                                      // ★0.2.153m AFGVehicle（运输工具扫描：轮式载具/火车/无人机）
#include "FGRecipe.h"                                       // ★0.2.153p UFGRecipe::GetProducts（生产机器产出物图标）
#include "Resources/FGResourceNode.h"                       // ★0.2.153p AFGResourceNode（AFGBuildableResourceExtractorBase::GetResourceNode 的返回类型，需完整定义才能调 GetResourceClass）
#include "ModLoading/ModLoadingLibrary.h"                   // ★0.2.153p UModLoadingLibrary::GetLoadedModInfo（MOD建筑友好名）
#include "Buildables/FGBuildablePipeReservoir.h"            // ★0.2.153m AFGBuildablePipeReservoir（液体容器）
#include "Buildables/FGBuildablePortalBase.h"               // ★0.2.153m AFGBuildablePortalBase（外星传送门/传送门卫星）
#include "Buildables/FGBuildablePortal.h"                   // ★0.2.153r AFGBuildablePortal::GetFuelInventory（传送门消耗物图标）
#include "Buildables/FGBuildableGeneratorFuel.h"            // ★0.2.153r AFGBuildableGeneratorFuel::GetCurrentFuelClass / GetFuelInventory（发电机消耗物图标）
#include "WheeledVehicles/FGWheeledVehicle.h"               // ★0.2.153r AFGWheeledVehicle::GetStorageInventory（运输工具货物图标）
#include "EngineUtils.h"                                  // ★0.2.153 重建：TActorIterator 模板完整定义
#include "NavigationSystem.h"                             // ★0.2.153c: UNavigationSystemV1::ProjectPointToNavigation（枢纽落点 navmesh 兜底）
#include "Net/UnrealNetwork.h"                             // DOREPLIFETIME（GetLifetimeReplicatedProps）
#include "FGPlayerController.h"                            // AFGPlayerController::GetRemoteCallObjectOfClass
#include "FGCharacterPlayer.h"                            // AFGCharacterPlayer::GetInventory
#include "Buildables/FGBuildableFactory.h"                 // AFGBuildableFactory::MoveOrDropInventory / GetOutputInventory
#include "FGInventoryComponent.h"                          // UFGInventoryComponent
#include "Misc/FileHelper.h"                               // ★0.2.153e 落点表持久化
#include "Misc/Paths.h"                                    // ★0.2.153e FPaths::ProjectSavedDir
#include "Serialization/JsonReader.h"                      // ★0.2.153e TJsonReaderFactory
#include "Serialization/JsonSerializer.h"                  // ★0.2.153e FJsonSerializer
#include "Dom/JsonObject.h"                                // ★0.2.153e FJsonObject
#include "Dom/JsonValue.h"                                 // ★0.2.153e FJsonValue
#include "TimerManager.h"                                  // ★0.2.153e 传后地面校正定时器
#include "Engine/OverlapResult.h"                          // ★0.2.153n FOverlapResult（落点被挡时打印挡路组件名）

// ----------------------------------------------------------------------------
// ★0.2.153 重建：WT_HasAuthority
//
// 原定义所在的 include 在上轮编辑事故（_rf_edit_0153.py 误删 RCO.cpp 顶部若干
// #include + 函数头）中一并丢失，全工程检索（含 F:/ E:/ 全盘）无任何残留定义。
// 此处按原语义重建：RCO 的 Server RPC 实现必须运行在服务端权威侧
// （"server owns the character transform"），用「世界存在 AuthGameMode」判定 ——
// 监听 / 专用服务器均返回 true，纯客户端返回 false。等价原
// AFGPlayerController::HasAuthority() 的判权威意图，且不依赖已丢失的玩家控制器句柄。
// ----------------------------------------------------------------------------
static inline bool WT_HasAuthority(const UObject* Obj)
{
    if (!Obj)
    {
        return false;
    }
    UWorld* World = Obj->GetWorld();
    return World && (World->GetAuthGameMode() != nullptr);
}

// ----------------------------------------------------------------------------
// ★0.2.153 重建：下列 8 个成员函数体被上轮编辑事故（_rf_edit_0153.py）整段误删，
//   头文件声明仍在、全工程无残留（F:/ D:/ E:/ 全盘 / git / 回收站均无备份）。按声明 + SDK 现有
//   API 重建：Get / GetLifetimeReplicatedProps / 三个 Client 广播 / Server_RequestOpenUI 为确定性
//   重建；Server_ScanMachines / Server_PullOutput 按 SDK API 重建，目标"能编过 + 不崩溃 + 进游戏可验"。
//   注：全图电力统计 / 收集品 / 共享会话字段暂置默认，待对原始源码或实测再补。
// ----------------------------------------------------------------------------
URemote_FactoryRCO* URemote_FactoryRCO::Get(AFGPlayerController* PC)
{
    if (!PC)
    {
        return nullptr;
    }
    return PC->GetRemoteCallObjectOfClass<URemote_FactoryRCO>();
}

void URemote_FactoryRCO::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(URemote_FactoryRCO, DummyReplicatedField);
}

// ----------------------------------------------------------------------------
// ★0.2.153p MOD 建筑识别（用户实测：「MOD建筑，我的列表中读取不到」）。
// 根因：0.2.153 重建扫描时把 0.2.146 的 Modded 分支漏掉了 ⇒ 第三方 MOD 建筑（父类是裸
// AFGBuildable / AFGBuildableFactory，不命中 Manufacturer/Storage/Generator… 任何一条 Cast）
// 一律落进 Other，而 Other 不在面板任何分组里（= 直接看不见）。
// 判据取**包路径**而不是类名黑名单：原版 C++ 类在 /Script/FactoryGame，原版蓝图/资产在
// /Game/FactoryGame；SML 挂载的 MOD 内容一律在 /<ModReference>/... 下，故「两段前缀之外」即 MOD。
// ----------------------------------------------------------------------------
static bool WT_IsVanillaClass(const UClass* Cls)
{
    if (!Cls || !Cls->GetOutermost())
    {
        return true;   // 判不出来就当原版，绝不把原版建筑误塞进 MOD 组
    }
    const FString Pkg = Cls->GetOutermost()->GetName();
    return Pkg.StartsWith(TEXT("/Script/FactoryGame"))
        || Pkg.StartsWith(TEXT("/Game/FactoryGame"))
        || Pkg.StartsWith(TEXT("/Engine/"));
}

/** ★0.2.153t 当前 UI 语言是否为中文（zh*）。英文游戏下不强塞中文，交给原生/SML 名字。 */
static bool WT_IsChineseUI()
{
    // FInternationalization::GetCurrentCulture() → FCultureRef（Internationalization.h:63），
    // GetTwoLetterISOLanguageName()（Culture.h:79）：zh-Hans / zh-Hant / zh-CN 都是 "zh"。
    return FInternationalization::Get().GetCurrentCulture()->GetTwoLetterISOLanguageName() == TEXT("zh");
}

/**
 * ★0.2.153t 建筑显示名走**原生翻译**（用户：「你就不能走原生游戏翻译吗」）。
 *   ① AFGBuildable::mDisplayName —— FText，引擎按当前语言求值（zh-Hans 下就是中文）；
 *   ② 建造它所用描述符的名字 UFGItemDescriptor::GetItemName()（FGItemDescriptor.h:191，同为 FText）
 *      —— 这就是**建造菜单/HUD 里显示的那一行**，与游戏原生显示完全同源。很多 MOD 建筑压根不填
 *      mDisplayName，旧代码此时退化成 C++ 类名（日志实锤过 "WirelessPowerRelay" 这种）；
 *   ③ 类名（最后兜底）。
 * ※ 若某 MOD 连描述符名也是英文，那是**该 MOD 没提供 locres**（引擎日志里能看到
 *   "LocRes .../Mods/<mod>/Content/Localization/.../xx.locres could not be opened"），
 *   游戏自己的建造菜单里同样是英文——这种我们补不了，也不该硬编一个名字。
 */
static FText WT_BuildableDisplayName(const AFGBuildable* B)
{
    if (!B)
    {
        return FText::GetEmpty();
    }
    const FText Own = B->mDisplayName;
    if (!Own.IsEmpty())
    {
        return Own;
    }
    if (const TSubclassOf<UFGItemDescriptor> Desc = B->GetBuiltWithDescriptor())
    {
        const FText ItemName = UFGItemDescriptor::GetItemName(Desc);
        if (!ItemName.IsEmpty())
        {
            return ItemName;
        }
    }
    return FText::FromString(B->GetClass()->GetName());
}

/** ★0.2.153t 载具版：同一条原生取值链（AFGVehicle::GetBuiltWithDescriptor，FGVehicle.h:242）。 */
static FText WT_VehicleDisplayName(const AFGVehicle* V)
{
    if (!V)
    {
        return FText::GetEmpty();
    }
    const FText Own = V->mDisplayName;
    if (!Own.IsEmpty())
    {
        return Own;
    }
    if (const TSubclassOf<UFGItemDescriptor> Desc = V->GetBuiltWithDescriptor())
    {
        const FText ItemName = UFGItemDescriptor::GetItemName(Desc);
        if (!ItemName.IsEmpty())
        {
            return ItemName;
        }
    }
    return FText::FromString(V->GetClass()->GetName());
}

// ----------------------------------------------------------------------------
// ★0.2.153r MOD 名中文映射（用户要求：MOD 建筑列表显示中文）
// SML 只给 FModInfo::FriendlyName（多数 MOD 是英文）。取值优先级：
//   ① RF_mod_names.json（Saved/Remote_Factory/，用户可编辑，键 = mod reference）→ 最优先，自己改的算数；
//   ② 内置中文表（只收录能准确意译的 mod，绝不臆造）；
//   ③ SML FriendlyName；
//   ④ mod reference 原文。
// 文件**首次运行时自动生成模板**：把本机已加载的全部 mod 写进去（值默认 FriendlyName），
// 用户把它改成中文即可生效——未收录的 mod 不会凭空编名字。
// ----------------------------------------------------------------------------
static FString WT_ModNameFilePath()
{
    // 与落点表同目录（Saved/Remote_Factory 子目录刻意不随 mod 改名）
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Remote_Factory"), TEXT("RF_mod_names.json"));
}

static TMap<FString, FString> WT_LoadModNameOverrides()
{
    TMap<FString, FString> Out;
    FString Raw;
    if (!FFileHelper::LoadFileToString(Raw, *WT_ModNameFilePath()))
    {
        return Out; // 文件不存在：空表（随后由 WT_EnsureModNameTemplate 生成模板）
    }
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        WT_WARNF(TEXT("[mod-name] file corrupt, starting empty: %s"), *WT_ModNameFilePath());
        return Out;
    }
    for (const auto& Pair : Root->Values)
    {
        FString V;
        if (Pair.Value.IsValid() && Pair.Value->TryGetString(V) && !V.IsEmpty())
        {
            Out.Add(Pair.Key, V);
        }
    }
    return Out;
}

// 进程内读一次就够（改文件需重开游戏生效，避免每个 MOD 建筑都去读盘）。
static const TMap<FString, FString>& WT_CachedModNameOverrides()
{
    static TMap<FString, FString> Cache;
    static bool bLoaded = false;
    if (!bLoaded)
    {
        Cache = WT_LoadModNameOverrides();
        bLoaded = true;
    }
    return Cache;
}

static const TMap<FString, FString>& WT_BuiltinModNames()
{
    // 键 = mod reference **或** C++ 模块名（两者常同名，但不必）。只放能准确意译的；
// 拿不准的一律不收，留给 json / FriendlyName。
    static TMap<FString, FString> M = {
        // ★自家：UE 模块名刻意保留 Remote_Factory（插件已改名 Smart_Factory），两个键都收。
        {TEXT("Remote_Factory"),            TEXT("智能工厂")},
        {TEXT("Smart_Factory"),             TEXT("智能工厂")},
        {TEXT("SmartFactoryLib"),           TEXT("智能工厂库")},
        {TEXT("FasterManualCraftingRedux"), TEXT("更快手工制作（重制版）")},
        {TEXT("RecipeCopier"),              TEXT("配方复制器")},
        {TEXT("ThirdPersonView"),           TEXT("第三人称视角")},
        // ★0.2.153u WirelessPower 的 C++ 模块名与 mod reference 不同名（包路径首段= WirelessPower，
        // SML FModInfo.Name = WirelessPower_Remake），两个键都收，分组标题才命中中文。
        {TEXT("WirelessPower_Remake"),      TEXT("无线电力（重制版）")},
        {TEXT("WirelessPower"),             TEXT("无线电力（重制版）")},
        {TEXT("Additional_300_Inventory_Slots"), TEXT("额外 300 格背包")},
        {TEXT("MarcioCommonLibs"),          TEXT("Marcio 公共库")},
        {TEXT("MinoDabsCommonLib"),         TEXT("MinoDabs 公共库")},
    };
    return M;
}

/**
 * 首次运行把本机已加载 mod 全写进 json 当翻译模板（★0.2.153s 值优先用**内置中文表**，
 * 没有中文才退回 FriendlyName）——否则模板里的英文会因为「json 最优先」把内置中文挡住
 * （本局实锤：模板写了 "RecipeCopier":"Settings Copier"，中文表名永远轮不到）。
 * 文件**已存在**时只做「升级」：仅当该项仍是默认值（= FriendlyName/Name，说明用户没改过）
 * 且内置表有中文，才替换为中文；用户手改过的值一律保留。
 */
static void WT_EnsureModNameTemplate(UWorld* World)
{
    // 一次会话只试一次（否则每个 MOD 建筑都读一遍盘）
    static bool bChecked = false;
    if (bChecked)
    {
        return;
    }
    bChecked = true;
    UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    UModLoadingLibrary* Lib = GI ? GI->GetSubsystem<UModLoadingLibrary>() : nullptr;
    if (!Lib)
    {
        return;
    }
    const TMap<FString, FString>& Builtin = WT_BuiltinModNames();

    FString Raw;
    const bool bHaveFile = FFileHelper::LoadFileToString(Raw, *WT_ModNameFilePath()) && !Raw.IsEmpty();
    TSharedPtr<FJsonObject> Root;
    if (bHaveFile)
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            return; // 坏文件：绝不动，免得毁掉用户内容
        }
    }
    else
    {
        Root = MakeShared<FJsonObject>();
    }

    bool bChanged = false;
    for (const FModInfo& Info : Lib->GetLoadedMods())
    {
        if (Info.Name.IsEmpty())
        {
            continue;
        }
        const FString DefaultName = Info.FriendlyName.IsEmpty() ? Info.Name : Info.FriendlyName;
        FString Cur;
        const bool bHas = Root->TryGetStringField(Info.Name, Cur);
        // ★0.2.153t 模板同样跟随语言：中文游戏下才把默认英文替换成内置中文
        const FString* Zh = WT_IsChineseUI() ? Builtin.Find(Info.Name) : nullptr;
        if (Zh)
        {
            // 内置中文优先：模板/未改过的项直接写中文
            if (!bHas || Cur.IsEmpty() || Cur == DefaultName)
            {
                Root->SetStringField(Info.Name, *Zh);
                bChanged = true;
            }
        }
        else if (!bHas)
        {
            // 没中文译名：只补缺失项（值用 FriendlyName，用户可自己改）
            Root->SetStringField(Info.Name, DefaultName);
            bChanged = true;
        }
    }
    if (!bChanged)
    {
        return; // 无需写盘（用户已自行改过 或 模板已是最新）
    }
    FString OutString;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
    if (FJsonSerializer::Serialize(Root.ToSharedRef(), Writer)
        && FFileHelper::SaveStringToFile(OutString, *WT_ModNameFilePath(),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        WT_WARNF(TEXT("[mod-name] template %s: %s"),
                 bHaveFile ? TEXT("upgraded") : TEXT("written"), *WT_ModNameFilePath());
    }
}

/**
 * ★0.2.153s 包路径 → **候选** mod reference 列表（按把握排序）。
 * 教训（本局日志实锤 `[scan-cat] modded ... mod='Script'`）：**包路径首段不是 mod reference**！
 *   - C++ 类的包 = "/Script/<模块名>"          （如 /Script/WirelessPower_Remake → 首段 "Script" ❌）
 *   - BP 类的包   = "/Game/<ModRef>/..."        （SML 把内容挂到 /Game 下）
 *   - 内容 mod    = "/<ModRef>/..."             （SML 默认挂载根，首段即 mod reference ✓）
 * ⇒ 首段是引擎固定根（Script/Game/Engine）时取**下一节**，并把首段留作兜底候选。
 */
static void WT_ModRefCandidates(const UClass* Cls, TArray<FString>& Out)
{
    Out.Reset();
    if (!Cls || !Cls->GetOutermost())
    {
        return;
    }
    FString Pkg = Cls->GetOutermost()->GetName();
    if (Pkg.StartsWith(TEXT("/")))
    {
        Pkg = Pkg.Mid(1);
    }
    TArray<FString> Parts;
    Pkg.ParseIntoArray(Parts, TEXT("/"), /*bCullEmpty=*/true);
    if (Parts.Num() == 0)
    {
        return;
    }
    if (Parts[0] == TEXT("Script") || Parts[0] == TEXT("Game") || Parts[0] == TEXT("Engine"))
    {
        if (Parts.Num() > 1)
        {
            Out.Add(Parts[1]);      // 真正的模块名 / mod reference
        }
        Out.Add(Parts[0]);          // 兜底候选（基本查不到，保留只为可解释）
    }
    else
    {
        Out.Add(Parts[0]);          // SML 挂载根 = mod reference
    }
}

/** 单个候选取名：① 用户 json → ② 内置中文表 → ③ SML FriendlyName；都不中返回 false。 */
static bool WT_TryResolveModNameOne(const FString& Ref, UWorld* World, FString& OutName)
{
    if (Ref.IsEmpty())
    {
        return false;
    }
    if (const FString* Zh = WT_CachedModNameOverrides().Find(Ref))
    {
        OutName = *Zh;
        return true;
    }
    // ★0.2.153t 内置中文表只在**中文游戏**下启用：英文环境交给原生/SML 名字，不强塞中文。
    if (WT_IsChineseUI())
    {
        if (const FString* Zh2 = WT_BuiltinModNames().Find(Ref))
        {
            OutName = *Zh2;
            return true;
        }
    }
    if (World)
    {
        if (UGameInstance* GI = World->GetGameInstance())
        {
            if (UModLoadingLibrary* ModLib = GI->GetSubsystem<UModLoadingLibrary>())
            {
                FModInfo Info;
                if (ModLib->GetLoadedModInfo(Ref, Info) && !Info.FriendlyName.IsEmpty())
                {
                    OutName = Info.FriendlyName;
                    return true;
                }
                // ★0.2.153u 兜底：C++ 模块名与 mod reference 不同名时（如 WirelessPower vs
                // WirelessPower_Remake），按「互为包含」匹配已加载 mod。限较短串长度 >= 5，
                // 防止 "SML" 这类超短名被任意候选误吞。取第一个命中的。
                for (const FModInfo& M : ModLib->GetLoadedMods())
                {
                    if (M.Name.Len() < 5 || Ref.Len() < 5)
                    {
                        continue;
                    }
                    if ((Ref.Contains(M.Name) || M.Name.Contains(Ref)) && !M.FriendlyName.IsEmpty())
                    {
                        OutName = M.FriendlyName;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/**
 * MOD 友好名（分组标题用）。逐个试候选 mod reference，第一个取到名字的即返回；
 * 全不中就退回首个候选（模块名），**绝不返回 "Script"/"Game" 这种引擎根名**。
 */
static FString WT_ResolveModName(UWorld* World, const UClass* Cls)
{
    TArray<FString> Cands;
    WT_ModRefCandidates(Cls, Cands);
    if (Cands.Num() == 0)
    {
        return FString();
    }
    WT_EnsureModNameTemplate(World);   // 首次运行生成翻译模板（内部只做一次）
    for (const FString& Ref : Cands)
    {
        FString Name;
        if (WT_TryResolveModNameOne(Ref, World, Name))
        {
            return Name;
        }
    }
    return Cands[0];
}

// ----------------------------------------------------------------------------
// ★0.2.153r 图标辅助（用户要求：从【容器】开始往下的类目都要有图标——产出物用产出物图标，
// 消耗物用消耗物图标）。统一走 UFGItemDescriptor::GetBigIcon（与建造菜单同一张图，不是自绘素材）。
// ----------------------------------------------------------------------------
static UTexture2D* WT_ItemIcon(const TSubclassOf<UFGItemDescriptor>& Desc)
{
    return Desc ? UFGItemDescriptor::GetBigIcon(Desc) : nullptr;
}

/**
 * 取库存里第一个非空格子的物品图标：容器存货 / 发电机燃料 / 传送门电池 / 商店货架 /
 * 太空电梯交付物 / 回收站票券 都走这一条（避免为每种建筑写一遍遍历）。
 * UFGInventoryComponent::GetInventoryStacks 是 const 成员（FGInventoryComponent.h:451），
 * FInventoryStack::HasItems() / FInventoryItem::GetItemClass()（:103 / :45）同为 const。
 */
static UTexture2D* WT_FirstStackIcon(const UFGInventoryComponent* Inv)
{
    if (!Inv)
    {
        return nullptr;
    }
    TArray<FInventoryStack> Stacks;
    Inv->GetInventoryStacks(Stacks, /*getEmptyStacks=*/false);
    for (const FInventoryStack& S : Stacks)
    {
        if (S.HasItems())
        {
            if (UTexture2D* T = WT_ItemIcon(S.Item.GetItemClass()))
            {
                return T;
            }
        }
    }
    return nullptr;
}

void URemote_FactoryRCO::Server_ScanMachines_Implementation(FVector Origin, float MaxRangeMeters)
{
    FWT_MachineListReply Reply;
    Reply.CategoryCounts.SetNumZeroed(static_cast<int32>(EWT_MachineCategory::Count));

    if (UWorld* World = GetWorld())
    {
        if (AFGBuildableSubsystem* BS = AFGBuildableSubsystem::Get(World))
        {
            const TArray<AFGBuildable*>& All = BS->GetAllBuildablesRef();
            for (AFGBuildable* B : All)
            {
                if (!B)
                {
                    continue;
                }
                const FVector Loc = B->GetActorLocation();
                const float DistM = (Loc - Origin).Size() / 100.f;
                if (MaxRangeMeters > 0.f && DistM > MaxRangeMeters)
                {
                    continue;
                }

                FWT_MachineEntry E;
                E.Target = B;
                // ★0.2.153b 修复：用建筑自带本地化名（此前误填 C++ 类名 → 面板显示 "Build_MinerMk1_C"）。
                // ★0.2.153t 再补一层：mDisplayName 为空时取**建造描述符的原生名**（与建造菜单同源），
                // 不再直接退化成类名 —— MOD 建筑多半不填 mDisplayName。
                E.DisplayName = WT_BuildableDisplayName(B);
                E.DistanceMeters = DistM;
                // ★0.2.153e 坐标直传：扫描时把坐标与水平半宽随回复复制到客户端，远端 Target 为 null
                // 时 UI 退化为按坐标传（Server_TeleportToCoord），不受网络 relevancy 限制。
                E.Location = Loc;
                {
                    const FBox EB = B->GetComponentsBoundingBox(/*bNonColliding=*/true);
                    E.BaseRadius = (EB.IsValid != 0)
                        ? FMath::Max(EB.GetExtent().X, EB.GetExtent().Y) : 0.f;
                }
                // 注：AFGBuildable::GetInteractWidgetClass() 为 protected，模块外不可直接调；
                // 重建版保守置 true —— 客户端按 Target 尝试开界面，无交互界面时为空操作，不致命。
                E.bHasInteractUI = true;

                uint8 Cat = static_cast<uint8>(EWT_MachineCategory::Other);
                // ★0.2.153l 制作台/装备工坊必须**先于** Manufacturer 判定：二者是纯 BP 建筑，一旦
                // 父类是 AFGBuildableManufacturer 就会被 Production 抢走 ⇒ 0.2.146 加的这两个类目
                // 一直读不到任何条目。判据取两条独立证据的「或」，互不依赖：
                // ① BP 包名（GetOutermost 的 FName，零字符串分配，9 万+ 传送带也走得起）；
                // ② 类名前缀（防 cooked 包名与源码期不一致）。命中时打诊断日志，下一轮日志即可核实。
                {
                    const UClass* Cls = B->GetClass();
                    const FName BPPkg = Cls->GetOutermost()->GetFName();
                    const FString ClsName = Cls->GetName();
                    const bool bBench = (BPPkg == FName(TEXT("Build_WorkBench"))) || ClsName.StartsWith(TEXT("Build_WorkBench"));
                    const bool bShop  = (BPPkg == FName(TEXT("Build_Workshop")))  || ClsName.StartsWith(TEXT("Build_Workshop"));
                    // ★0.2.153r 用户定案：制作台 / 装备工坊并入【枢纽】类目
                    // （与 MAM / 商店 / 回收站 / 太空电梯同组；仍必须前置判定，否则会被 Producer 抢走）
                    if (bBench)     { Cat = static_cast<uint8>(EWT_MachineCategory::Hub); }
                    else if (bShop) { Cat = static_cast<uint8>(EWT_MachineCategory::Hub); }
                    if (bBench || bShop)
                    {
                        WT_WARNF(TEXT("[scan-cat] '%s' pkg='%s' cls='%s' cat=%d"),
                                 *B->GetName(), *BPPkg.ToString(), *ClsName, static_cast<int32>(Cat));
                    }
                }
                if (Cat == static_cast<uint8>(EWT_MachineCategory::Other))
                {
                    // ★0.2.153m 液体容器 / 固体容器 / 外星传送门：三个新建类目接在 Manufacturer 之后
                    // （父类均为 AFGBuildableFactory，与 Manufacturer 互不命中，保留既有优先级不动）。
                    // 原「容器」按用户要求拆成液体（PipeReservoir）+ 固体（Storage）两类。
                    if      (Cast<AFGBuildableManufacturer>(B))          Cat = static_cast<uint8>(EWT_MachineCategory::Producer);
                    else if (Cast<AFGBuildablePipeReservoir>(B))         Cat = static_cast<uint8>(EWT_MachineCategory::LiquidContainer);
                    else if (Cast<AFGBuildableStorage>(B))               Cat = static_cast<uint8>(EWT_MachineCategory::SolidContainer);
                    else if (Cast<AFGBuildablePortalBase>(B))            Cat = static_cast<uint8>(EWT_MachineCategory::Portal);
                    else if (Cast<AFGBuildableResourceExtractorBase>(B)) Cat = static_cast<uint8>(EWT_MachineCategory::Miner);
                    else if (Cast<AFGBuildableGenerator>(B))             Cat = static_cast<uint8>(EWT_MachineCategory::Generator);
                    else if (Cast<AFGBuildableCircuitSwitch>(B))         Cat = static_cast<uint8>(EWT_MachineCategory::PowerSwitch);
                    // ★0.2.153r 用户定案：回收站 / 枢纽终端 / 太空电梯 / 分子分析机 / AWESOME 商店
                    // 全部并入【枢纽】类目（连同上面的制作台、装备工坊）。枚举值保留不动，只是不再落入
                    // 各自的桶——面板的 BuildingCats 已同步移除这几项，避免显示空类目。
                    else if (Cast<AFGBuildableResourceSink>(B))          Cat = static_cast<uint8>(EWT_MachineCategory::Hub);
                    else if (Cast<AFGBuildableHubTerminal>(B))           Cat = static_cast<uint8>(EWT_MachineCategory::Hub);
                    else if (Cast<AFGBuildableSpaceElevator>(B))         Cat = static_cast<uint8>(EWT_MachineCategory::Hub);
                    // ★0.2.153l 自动工作台类目按用户要求下架：AFGBuildableAutomatedWorkBench 派生自
                    // AFGBuildableManufacturer（SDK 实锤），上面 Producer 一行已收编它，无需单独桶。
                    else if (Cast<AFGBuildableMAM>(B))                   Cat = static_cast<uint8>(EWT_MachineCategory::Hub);
                    else if (Cast<AFGDropPod>(B))                        Cat = static_cast<uint8>(EWT_MachineCategory::DropPod);
                    else if (Cast<AFGBuildableResourceSinkShop>(B))      Cat = static_cast<uint8>(EWT_MachineCategory::Hub);
                }
                // ★0.2.153p MOD 建筑（上面原版类目一条都没命中 ⇒ 父类是裸 AFGBuildable / 自定义基类）：
                // 包路径不在 /Script/FactoryGame 与 /Game/FactoryGame 之下即 MOD 内容 ⇒ 进 MOD建筑桶
                // （否则落 Other，而 Other 不在面板任何分组里 = 用户看不见，即本轮报的「读不到」）。
                if (Cat == static_cast<uint8>(EWT_MachineCategory::Other)
                    && !WT_IsVanillaClass(B->GetClass()))
                {
                    Cat = static_cast<uint8>(EWT_MachineCategory::Modded);
                    E.ModName = WT_ResolveModName(World, B->GetClass());
                    // ★0.2.153s 打完整包路径：下次再出现怪分组名，一眼看出该取哪一段。
                    // ★0.2.153t 再打最终显示名：验证「原生翻译」取到的是什么（英文=该 MOD 没 locres）。
                    WT_WARNF(TEXT("[scan-cat] modded '%s' cls='%s' pkg='%s' mod='%s' name='%s'"),
                             *B->GetName(), *B->GetClass()->GetName(),
                             *B->GetClass()->GetOutermost()->GetName(), *E.ModName,
                             *E.DisplayName.ToString());
                }
                E.Category = Cat;

                // ★0.2.153m 列表行图标（用户要求：生产机器 / 采矿设备显示对应物品图标）。
                // 图标取自建造该建筑所用描述符（AFGBuildable::GetBuiltWithDescriptor →
                // UFGItemDescriptor::GetBigIcon），与建造菜单里显示的是同一张图，不是自己画的素材。
                // 只给这两个类目填：其余类目保持空路径，避免给每条回复加上万条路径字符串。
                if (Cat == static_cast<uint8>(EWT_MachineCategory::Producer) ||
                    Cat == static_cast<uint8>(EWT_MachineCategory::Miner))
                {
                    if (const TSubclassOf<UFGItemDescriptor> Desc = B->GetBuiltWithDescriptor())
                    {
                        if (UTexture2D* IconTex = UFGItemDescriptor::GetBigIcon(Desc))
                        {
                            E.Icon = TSoftObjectPtr<UTexture2D>(IconTex);
                        }
                    }
                    // ★0.2.153p 产出物图标（同一支里顺手取，避免再来一次 Cast 链）：
                    //  生产机器 = 当前配方 UFGRecipe::GetProducts 的第 0 项（FGRecipe.h:63 静态函数，
                    //  与 GetCurrentRecipe(FGBuildableManufacturer.h:165) 配套）；
                    //  采矿设备 = 所连资源节点的资源类（AFGResourceNodeBase::GetResourceClass，
                    //  FGResourceNodeBase.h:144，经 GetResourceNode() 取到节点）。
                    TSubclassOf<UFGItemDescriptor> OutDesc = nullptr;
                    if (const AFGBuildableManufacturer* M = Cast<AFGBuildableManufacturer>(B))
                    {
                        const TSubclassOf<UFGRecipe> Recipe = M->GetCurrentRecipe();
                        if (Recipe)
                        {
                            const TArray<FItemAmount> Prods = UFGRecipe::GetProducts(Recipe);
                            if (Prods.Num() > 0)
                            {
                                OutDesc = Prods[0].ItemClass;
                            }
                        }
                    }
                    else if (const AFGBuildableResourceExtractorBase* Ex = Cast<AFGBuildableResourceExtractorBase>(B))
                    {
                        if (const AFGResourceNode* Node = Ex->GetResourceNode())
                        {
                            OutDesc = Node->GetResourceClass();
                        }
                    }
                    if (OutDesc)
                    {
                        if (UTexture2D* OutTex = UFGItemDescriptor::GetBigIcon(OutDesc))
                        {
                            E.ProductIcon = TSoftObjectPtr<UTexture2D>(OutTex);
                        }
                    }
                }
                else
                {
                    // ★0.2.153r 其余类目的产出物 / 消耗物图标（用户要求「从【容器】开始往下的类目
                    // 都要加个图标」）。语义按建筑实际角色取，取不到就留空（面板自动不显示该图）：
                    UTexture2D* ProdTex = nullptr;
                    if (Cat == static_cast<uint8>(EWT_MachineCategory::LiquidContainer))
                    {
                        // 液体容器 = 罐内液体（GetFluidDescriptor，FGBuildablePipeReservoir.h:87）
                        if (const AFGBuildablePipeReservoir* R = Cast<AFGBuildablePipeReservoir>(B))
                        {
                            ProdTex = WT_ItemIcon(R->GetFluidDescriptor());
                        }
                    }
                    else if (Cat == static_cast<uint8>(EWT_MachineCategory::SolidContainer))
                    {
                        // 固体容器 = 存货第一格（GetStorageInventory，FGBuildableStorage.h:30）
                        if (AFGBuildableStorage* S = Cast<AFGBuildableStorage>(B))
                        {
                            ProdTex = WT_FirstStackIcon(S->GetStorageInventory());
                        }
                    }
                    else if (Cat == static_cast<uint8>(EWT_MachineCategory::Generator))
                    {
                        // 发电机 = 消耗物（燃料）：优先当前燃料类，回退燃料库存第一格
                        // （FGBuildableGeneratorFuel.h:103 / :54）
                        if (const AFGBuildableGeneratorFuel* G = Cast<AFGBuildableGeneratorFuel>(B))
                        {
                            ProdTex = WT_ItemIcon(G->GetCurrentFuelClass());
                            if (!ProdTex)
                            {
                                ProdTex = WT_FirstStackIcon(G->GetFuelInventory());
                            }
                        }
                    }
                    else if (Cat == static_cast<uint8>(EWT_MachineCategory::Portal))
                    {
                        // 外星传送门 = 消耗物（电池/燃料）：GetFuelInventory（FGBuildablePortal.h:56）
                        if (const AFGBuildablePortal* P = Cast<AFGBuildablePortal>(B))
                        {
                            ProdTex = WT_FirstStackIcon(P->GetFuelInventory());
                        }
                    }
                    else if (Cat == static_cast<uint8>(EWT_MachineCategory::Hub))
                    {
                        // ★0.2.153r 合并后的枢纽组：制作台 / 装备工坊（Manufacturer 派生）显示配方产物；
                        // 商店显示货架、回收站显示票券（产出）、太空电梯显示交付物（消耗）。
                        if (AFGBuildableResourceSinkShop* SH = Cast<AFGBuildableResourceSinkShop>(B))
                        {
                            ProdTex = WT_FirstStackIcon(SH->GetShopInventory());       // :27
                        }
                        else if (const AFGBuildableResourceSink* K = Cast<AFGBuildableResourceSink>(B))
                        {
                            ProdTex = WT_FirstStackIcon(K->GetCouponInventory());      // :34
                        }
                        else if (AFGBuildableSpaceElevator* SE = Cast<AFGBuildableSpaceElevator>(B))
                        {
                            ProdTex = WT_FirstStackIcon(SE->GetInputInventory());      // :140
                        }
                        else if (const AFGBuildableManufacturer* M = Cast<AFGBuildableManufacturer>(B))
                        {
                            const TSubclassOf<UFGRecipe> Recipe = M->GetCurrentRecipe();
                            if (Recipe)
                            {
                                const TArray<FItemAmount> Prods = UFGRecipe::GetProducts(Recipe);
                                if (Prods.Num() > 0)
                                {
                                    ProdTex = WT_ItemIcon(Prods[0].ItemClass);
                                }
                            }
                        }
                        // 枢纽终端 / MAM 无输入产出库存 ⇒ 留空
                    }
                    if (ProdTex)
                    {
                        E.ProductIcon = TSoftObjectPtr<UTexture2D>(ProdTex);
                    }
                }

                if (Cat < static_cast<uint8>(EWT_MachineCategory::Count))
                {
                    Reply.CategoryCounts[Cat]++;
                }

                if (AFGBuildableCircuitSwitch* SW = Cast<AFGBuildableCircuitSwitch>(B))
                {
                    E.bSwitchOn = SW->IsSwitchOn();
                    E.bSwitchConnected = SW->IsSwitchConnected();
                    if (AFGBuildablePriorityPowerSwitch* P = Cast<AFGBuildablePriorityPowerSwitch>(B))
                    {
                        E.SwitchPriority = P->GetPriority();
                    }
                }

                Reply.Entries.Add(E);
            }

            // ★0.2.153b 重建：全厂电力总览（误删后未还原 → 面板恒显 发电0/耗电0、电池0/0）。
            // 电路经 UFGPowerInfoComponent::GetPowerCircuit() 去重后累加 GetStats()；电池从储能建筑直加。
            TSet<const UFGPowerCircuit*> SeenCircuits;
            for (AFGBuildable* B : All)
            {
                if (!B) { continue; }
                if (const AFGBuildablePowerStorage* PS = Cast<AFGBuildablePowerStorage>(B))
                {
                    Reply.BatteryStoredMWh   += PS->GetPowerStore();
                    Reply.BatteryCapacityMWh += PS->GetPowerStoreCapacity();
                }
                if (const UFGPowerInfoComponent* PIC = B->FindComponentByClass<UFGPowerInfoComponent>())
                {
                    if (UFGPowerCircuit* C = PIC->GetPowerCircuit())
                    {
                        bool bAlready = false;
                        SeenCircuits.Add(C, &bAlready);
                        if (!bAlready)
                        {
                            FPowerCircuitStats S;
                            C->GetStats(S);
                            Reply.PowerProducedMW       += S.PowerProduced;
                            Reply.PowerConsumedMW       += S.PowerConsumed;
                            Reply.PowerCapacityMW       += S.PowerProductionCapacity;
                            Reply.PowerMaxConsumptionMW += S.MaximumPowerConsumption;
                        }
                    }
                }
            }
            Reply.CircuitCount = SeenCircuits.Num();
        }

        // ★0.2.153m 运输工具扫描（用户要求：运输车/卡车/火车/无人机进列表）。
        // 载具**不是** AFGBuildable（AFGVehicle : AFGDriveablePawn），不在 BuildableSubsystem 的
        // 建筑表里 ⇒ 必须单独扫一遍世界 actor：TActorIterator<AFGVehicle> 按 IsA 过滤，天然覆盖
        // 轮式载具（拖拉机/卡车/探险车）、AFGRailroadVehicle（机车/货车厢）、AFGDroneVehicle（无人机）。
        {
            const uint8 CatVehicle = static_cast<uint8>(EWT_MachineCategory::Vehicle);
            for (TActorIterator<AFGVehicle> It(World); It; ++It)
            {
                AFGVehicle* V = *It;
                if (!V)
                {
                    continue;
                }
                const FVector VLoc = V->GetActorLocation();
                const float VDistM = (VLoc - Origin).Size() / 100.f;
                if (MaxRangeMeters > 0.f && VDistM > MaxRangeMeters)
                {
                    continue;
                }

                FWT_MachineEntry E;
                E.Target = V;
                // AFGVehicle::mDisplayName 是 public UPROPERTY，与建筑一样读本地化名；
                // ★0.2.153t 空则退到**建造描述符的原生名**（AFGVehicle::GetBuiltWithDescriptor，
                // FGVehicle.h:242）——与建造菜单同源，最后才回退类名。
                E.DisplayName = WT_VehicleDisplayName(V);
                E.DistanceMeters = VDistM;
                E.Location = VLoc;
                {
                    const FBox EB = V->GetComponentsBoundingBox(/*bNonColliding=*/true);
                    E.BaseRadius = (EB.IsValid != 0)
                        ? FMath::Max(EB.GetExtent().X, EB.GetExtent().Y) : 0.f;
                }
                // 载具没有可远程打开的交互面板（Server_RequestOpenUI 只收 AFGBuildable）⇒ 只给[传送]。
                E.bHasInteractUI = false;
                E.Category = CatVehicle;
                // ★0.2.153r 运输工具图标 = 车上装的第一格货物（轮式载具有货箱：
                // WheeledVehicles/FGWheeledVehicle.h:91 GetStorageInventory）。
                // 机车 / 货车厢 / 无人机未暴露库存 API（SDK 实锤 FGRailroadVehicle.h 无 inventory）
                // ⇒ 取不到就留空，面板自动不显示该图，不臆造。
                if (const AFGWheeledVehicle* WV = Cast<AFGWheeledVehicle>(V))
                {
                    if (UTexture2D* CargoTex = WT_FirstStackIcon(WV->GetStorageInventory()))
                    {
                        E.ProductIcon = TSoftObjectPtr<UTexture2D>(CargoTex);
                    }
                }

                if (CatVehicle < static_cast<uint8>(EWT_MachineCategory::Count))
                {
                    Reply.CategoryCounts[CatVehicle]++;
                }
                Reply.Entries.Add(E);
            }
        }
    }

    Client_ReceiveMachineList(Reply);
}

void URemote_FactoryRCO::Server_RequestOpenUI_Implementation(AFGBuildable* Target)
{
    if (!Target)
    {
        return;
    }
    // 服务端确认建筑存在后，把"打开原生交互界面"指令下发给请求方客户端；
    // 真正的 widget 由客户端按 mInteractObject 指向 Target 生成（同按 E 的交互，只是免去走过去）。
    Client_OpenInteractUI(Target);
}

void URemote_FactoryRCO::Server_PullOutput_Implementation(AFGBuildable* Target)
{
    int32 ItemsMoved = 0;
    bool  bSuccess   = false;

    if (Target && WT_HasAuthority(this))
    {
        UWorld* World = GetWorld();
        AFGPlayerController* PC = Cast<AFGPlayerController>(GetOuter());
        if (!PC && World)
        {
            PC = Cast<AFGPlayerController>(World->GetFirstPlayerController());
        }
        AFGCharacterPlayer* Char = (PC ? Cast<AFGCharacterPlayer>(PC->GetCharacter()) : nullptr);
        UFGInventoryComponent* PlayerInv = (Char ? Char->GetInventory() : nullptr);

        if (AFGBuildableManufacturer* M = Cast<AFGBuildableManufacturer>(Target))
        {
            if (UFGInventoryComponent* OutInv = M->GetOutputInventory())
            {
                if (PlayerInv && Char)
                {
                    // 把输出库存整体搬入玩家背包，放不下的落到脚边（与原版取料行为一致）。
                    AFGBuildableFactory::MoveOrDropInventory(OutInv, Char);
                    bSuccess = true;
                }
            }
        }
    }

    Client_NotifyPullResult(Target, ItemsMoved, bSuccess);
}

void URemote_FactoryRCO::Client_ReceiveMachineList_Implementation(const FWT_MachineListReply& Reply)
{
    OnMachineListReceived.Broadcast(Reply);
}

void URemote_FactoryRCO::Client_OpenInteractUI_Implementation(AFGBuildable* Target)
{
    OnInteractUIRequested.Broadcast(Target);
}

void URemote_FactoryRCO::Client_NotifyPullResult_Implementation(AFGBuildable* Target, int32 ItemsMoved, bool bSuccess)
{
    OnPullResult.Broadcast(Target, ItemsMoved, bSuccess);
}

// ----------------------------------------------------------------------------
// ★0.2.153 重建：落点搜索辅助函数。
//
// 原定义随上轮编辑事故（_rf_edit_0153.py 误删顶部 include + 函数头）一并丢失，
// 全盘检索（F:/ D:/ SMLStarterProject）无残留。按调用点注释的 0.2.135 落点语义重建：
//   WT_FindCollectibleLandingSpot —— 收集品(外星 slug/球/果实 / 空投舱 / 文件坐标)落点：
//       先试「物品坐标 2m(200cm) 外」环搜 → 失败退「通用环搜」加宽 → 仍失败且为实体
//       收集品(bHasActor)时「洞穴兜底：直接站在物品上方」；坐标模式(bHasActor=false)
//       搜不到则交由调用方取消传送（不硬落虚空）。
//   WT_FindSafeLandingSpot —— 机器/建筑通用落点(0.2.88；0.2.153e 第八轮大肥鱼定案改候选序)：
//       再 8 向环搜，最后加宽兜底；搜不到交由调用方取消（0.2.135 不再硬落）。
// 所有候选点均做「上方200下探-400找地面 + ECC_Pawn 胶囊占位检测」，落点 = 地面 + 胶囊半高 + 2。
// ----------------------------------------------------------------------------
namespace
{
    // 单个候选 XY：上方 200 下探 -400 找地面；命中则做胶囊占位检测，通过返回安全落点。
    static bool WT_TryGroundAt(UWorld* World, const FCollisionQueryParams& Q,
                               const FVector& Xy, float CapRadius, float CapHalfH, FVector& OutDest)
    {
        FHitResult Hit;
        const FVector Start = Xy + FVector(0.f, 0.f, 200.f);
        const FVector End   = Xy - FVector(0.f, 0.f, 400.f);
        if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Q))
        {
            return false;
        }
        const FVector Dest = Hit.ImpactPoint + FVector(0.f, 0.f, CapHalfH + 2.f);
        const FCollisionShape Shape = FCollisionShape::MakeCapsule(CapRadius, FMath::Max(CapHalfH - 2.f, 10.f));
        if (World->OverlapBlockingTestByChannel(Dest, FQuat::Identity, ECC_Pawn, Shape, Q))
        {
            return false;
        }
        OutDest = Dest;
        return true;
    }

    // 以 Center 为中心、半径 Radius 的环上 N 向搜索，返回首个安全落点。
    static bool WT_TryRing(UWorld* World, const FCollisionQueryParams& Q,
                           const FVector& Center, float Radius, float CapRadius, float CapHalfH,
                           FVector& OutDest)
    {
        static const int32 N = 12;
        for (int32 i = 0; i < N; ++i)
        {
            const FVector Dir = FRotator(0.f, (360.f / static_cast<float>(N)) * static_cast<float>(i), 0.f).Vector();
            if (WT_TryGroundAt(World, Q, Center + Dir * Radius, CapRadius, CapHalfH, OutDest))
            {
                return true;
            }
        }
        return false;
    }
}

static bool WT_FindCollectibleLandingSpot(UWorld* World, AFGCharacterPlayer* Character,
                                          FVector Center, float ItemTopZ, bool bHasActor, FVector& OutDest)
{
    if (!World || !Character)
    {
        return false;
    }
    const UCapsuleComponent* Cap = Character->GetCapsuleComponent();
    const float CapRadius = Cap ? Cap->GetScaledCapsuleRadius() : 60.f;
    const float CapHalfH  = Cap ? Cap->GetScaledCapsuleHalfHeight() : 90.f;

    FCollisionQueryParams Q(SCENE_QUERY_STAT(WT_CollectibleLanding), /*bTraceComplex=*/false);
    Q.AddIgnoredActor(Character);

    // 1) 专用落点：物品坐标 2m(200cm) 外环搜
    if (WT_TryRing(World, Q, Center, 200.f, CapRadius, CapHalfH, OutDest))
    {
        return true;
    }
    // 2) 通用环搜兜底：加宽到 400cm（2m 贴墙/夹缝时外扩再试）
    if (WT_TryRing(World, Q, Center, 400.f, CapRadius, CapHalfH, OutDest))
    {
        return true;
    }
    // 3) 洞穴兜底：实体收集品直接站在物品上方（bHasActor=false 坐标模式无实体可踩 → 交给调用方取消）
    if (bHasActor)
    {
        const FVector Dest = FVector(Center.X, Center.Y, ItemTopZ + CapHalfH + 2.f);
        const FCollisionShape Shape = FCollisionShape::MakeCapsule(CapRadius, FMath::Max(CapHalfH - 2.f, 10.f));
        if (!World->OverlapBlockingTestByChannel(Dest, FQuat::Identity, ECC_Pawn, Shape, Q))
        {
            OutDest = Dest;
            return true;
        }
    }
    return false;
}

static bool WT_FindSafeLandingSpot(UWorld* World, AFGCharacterPlayer* Character,
                                   FVector Center, float BaseRadius, FVector ToPlayer,
                                   const FVector& MachineFwd, FVector& OutDest)
{
    if (!World || !Character)
    {
        return false;
    }
    const UCapsuleComponent* Cap = Character->GetCapsuleComponent();
    const float CapRadius = Cap ? Cap->GetScaledCapsuleRadius() : 60.f;
    const float CapHalfH  = Cap ? Cap->GetScaledCapsuleHalfHeight() : 90.f;

    FCollisionQueryParams Q(SCENE_QUERY_STAT(WT_SafeLanding), /*bTraceComplex=*/false);
    Q.AddIgnoredActor(Character);

    const float RingR = BaseRadius + 100.f;
    // ★0.2.153e 第八轮（大肥鱼定案）：机器 forward 就是出料面，ToPlayer/世界 8 向都可能在出口处。
    // 候选序以机器朝向为基准：先 -forward（出料面对侧）、再 ±90（两翼）、最后 +forward（出料面），
    // 再补 4 条 45° 斜向。每个候选 WT_TryGroundAt 内部做地面 trace + overlap（撞地基/机器自动跳下一个）；
    // navmesh 不参与（地基上不一定有 navmesh），Z 一律 trace 地面顶面兜底——都是现状已实现的行为。
    {
        FVector Fwd = MachineFwd;
        Fwd.Z = 0.f;
        if (Fwd.SizeSquared() < KINDA_SMALL_NUMBER)
        {
            Fwd = FVector(1.f, 0.f, 0.f); // forward 全竖直（理论上不存在）时的无害兜底
        }
        Fwd.Normalize();
        const FVector Side = FVector(Fwd.Y, -Fwd.X, 0.f); // Fwd 左转 90°（水平）
        const FVector DiagA = (Fwd + Side).GetSafeNormal();
        const FVector DiagB = (Fwd - Side).GetSafeNormal();
        const FVector FwdDirs[7] = {
            -Fwd,          // 出料面对侧：最优先
             Side, -Side,  // 两翼
             Fwd,          // 出料面：最末
             DiagA, DiagB  // 斜向补位
        };
        for (const FVector& Dir : FwdDirs)
        {
            if (WT_TryGroundAt(World, Q, Center + Dir * RingR, CapRadius, CapHalfH, OutDest))
            {
                return true;
            }
        }
    }
    // 8 向环搜（世界坐标兜底）
    static const int32 N = 8;
    for (int32 i = 0; i < N; ++i)
    {
        const FVector Dir = FRotator(0.f, (360.f / static_cast<float>(N)) * static_cast<float>(i), 0.f).Vector();
        if (WT_TryGroundAt(World, Q, Center + Dir * RingR, CapRadius, CapHalfH, OutDest))
        {
            return true;
        }
    }
    // 加宽兜底：半径 +200cm
    const float RingR2 = RingR + 200.f;
    for (int32 i = 0; i < N; ++i)
    {
        const FVector Dir = FRotator(0.f, (360.f / static_cast<float>(N)) * static_cast<float>(i), 0.f).Vector();
        if (WT_TryGroundAt(World, Q, Center + Dir * RingR2, CapRadius, CapHalfH, OutDest))
        {
            return true;
        }
    }
    return false;
}

// ---- ★0.2.153e 远程传送落点表（大肥鱼第七轮定案；★0.2.153h 键改稳定身份）----
// 远距离传送时目的地周围地形/碰撞尚未流送，trace/navmesh 都拿不到真地面，传送前根本算不出落点。
// 改为表驱动：近距离成功落地过一次就把落点存盘；远距离直接查表传到存档点——落地后目的地区块
// 才开始流送，那时地面 Z 才算得出来，所以传完等 0.6s 再补一次地面校正（绝不在传之前算）。
// ★0.2.153h 键 = Hub|对象路径名（大肥鱼定案「按 FGuid 别拿坐标当键」；SDK 实锤 FActorSaveHeader
// 无实例 FGuid，FG 存档里等价稳定 ID = 对象路径名，跨存档持久）。拆了重建 = 新名字 = 新键，
// 旧表项成无害孤儿（永不命中、不动别人记录）；旧版坐标键条目一并留作孤儿，不做迁移不清表。
static FString WT_HubSpotFilePath()
{
    // 沿用收集品持久化位置（Saved/Remote_Factory 子目录刻意不随 mod 改名，见 RF_collected.json 约定）
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Remote_Factory"), TEXT("RF_teleport_spots.json"));
}

static FString WT_HubSpotKey(const AActor* Target)
{
    // ★0.2.153h 坐标键的坑实锤：拿量化坐标当键，拆了重建/缓存污染后旧落点会被一直端出来
    // （「每次落同一个错点」根因）。改按稳定身份（对象路径名，存档跨会话持久）。
    return FString::Printf(TEXT("Hub|%s"), *Target->GetPathName());
}

static TMap<FString, FVector> WT_LoadHubSpots()
{
    TMap<FString, FVector> Out;
    FString Raw;
    if (!FFileHelper::LoadFileToString(Raw, *WT_HubSpotFilePath()))
    {
        return Out; // 首次使用/文件不存在：空表
    }
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        WT_WARNF(TEXT("[teleport-hub] spot file corrupt, starting empty: %s"), *WT_HubSpotFilePath());
        return Out;
    }
    for (const auto& Pair : Root->Values)
    {
        const TSharedPtr<FJsonObject> Obj = Pair.Value->AsObject();
        if (!Obj.IsValid() || !Obj->HasField(TEXT("x")) || !Obj->HasField(TEXT("y")) || !Obj->HasField(TEXT("z")))
        {
            continue;
        }
        Out.Add(Pair.Key, FVector(
            Obj->GetNumberField(TEXT("x")),
            Obj->GetNumberField(TEXT("y")),
            Obj->GetNumberField(TEXT("z"))));
    }
    return Out;
}

static void WT_SaveHubSpot(const FString& Key, const FVector& Dest)
{
    TMap<FString, FVector> Spots = WT_LoadHubSpots();
    Spots.Add(Key, Dest);
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    for (const auto& Pair : Spots)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetNumberField(TEXT("x"), Pair.Value.X);
        Obj->SetNumberField(TEXT("y"), Pair.Value.Y);
        Obj->SetNumberField(TEXT("z"), Pair.Value.Z);
        Root->SetObjectField(Pair.Key, Obj);
    }
    FString OutString;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
    if (FJsonSerializer::Serialize(Root.ToSharedRef(), Writer)
        && FFileHelper::SaveStringToFile(OutString, *WT_HubSpotFilePath(),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        WT_WARNF(TEXT("[teleport-hub] spot-saved key='%s' dest=%s"), *Key, *Dest.ToString());
    }
    else
    {
        WT_WARNF(TEXT("[teleport-hub] spot-save FAILED path=%s"), *WT_HubSpotFilePath());
    }
}

// ★0.2.153f 传送 ground-fix 公共助手（原 Server_TeleportToCoord 内联 lambda 提取共用；
// ★0.2.153h 上移到 Server_TeleportTo 之前供 hub 分支复用，并扩展：可忽略指定 actor + 空键不回写表）：
// 0.6s 后把角色校正到落点正下真地面（远距落地时目的地区块才刚开始流送）；命中且 Key 非空即写回
// 落点表（首次到那再补写真落点，大肥鱼第十轮定案）。校正本身是几厘米级竖直贴地，保持 sweep=true
// 正确（sweep 只在长距瞬移时才是误判源）。
static void WT_ScheduleTeleportGroundFix(UWorld* World, AFGCharacterPlayer* Character,
                                         const FString& Key, AActor* IgnoreActor = nullptr)
{
    if (!World || !Character)
    {
        return;
    }
    TWeakObjectPtr<AFGCharacterPlayer> CharW(Character);
    TWeakObjectPtr<UWorld> WorldW(World);
    TWeakObjectPtr<AActor> IgnoreW(IgnoreActor);
    FTimerHandle FixHandle;
    World->GetTimerManager().SetTimer(FixHandle, FTimerDelegate::CreateWeakLambda(Character,
        [CharW, WorldW, Key, IgnoreW]()
    {
        AFGCharacterPlayer* FixChar = CharW.Get();
        UWorld* FixWorld = WorldW.Get();
        if (!FixChar || !FixWorld)
        {
            return;
        }
        FCollisionQueryParams FixQ(SCENE_QUERY_STAT(WT_CoordFix), /*bTraceComplex=*/false);
        FixQ.AddIgnoredActor(FixChar);
        if (AActor* FixIgnore = IgnoreW.Get())
        {
            // ★0.2.153h hub 用：忽略目标建筑本体（终端就贴着落点），别把人校正到终端顶上
            FixQ.AddIgnoredActor(FixIgnore);
        }
        FHitResult FixHit;
        const FVector CurLoc = FixChar->GetActorLocation();
        const UCapsuleComponent* FixCap = FixChar->GetCapsuleComponent();
        const float FixHalfH = FixCap ? FixCap->GetScaledCapsuleHalfHeight() : 90.f;
        if (FixWorld->LineTraceSingleByChannel(FixHit,
                CurLoc + FVector(0.f, 0.f, 200.f),
                CurLoc - FVector(0.f, 0.f, 600.f), ECC_Pawn, FixQ))
        {
            // ★0.2.153n 贴地判据修正（bug①「卡在枢纽背面的墙里」真凶之一）：
            // 旧逻辑「命中第一个面就贴」会把人**钉到枢纽结构顶上**——本局实测：人被兜底砸到终端
            // 正上方（枢纽结构内部）后，射线起点 -2353.9 本身就在实体里，命中 -2354（≈起点），
            // 于是「贴地」贴了个寂寞，人继续卡在墙里。
            // 真地面只可能在**脚下**（≤ 胶囊底 + 容差）；命中面高于脚底 = 头顶的结构/天花板 ⇒ 不贴，
            // 让玩家靠重力自然落到外面（兜底位置本身已由宽松阶段尽量选在结构之外）。
            const float FixBottom = CurLoc.Z - FixHalfH;
            if (FixHit.ImpactPoint.Z > FixBottom + 30.f)
            {
                WT_WARNF(TEXT("[teleport-fix] ground-fix-above-skip hitZ=%.0f bottomZ=%.0f key='%s'"),
                         FixHit.ImpactPoint.Z, FixBottom, *Key);
                return;
            }
            const FVector Fixed(FixHit.ImpactPoint.X, FixHit.ImpactPoint.Y,
                                FixHit.ImpactPoint.Z + FixHalfH + 2.f);
            FixChar->SetActorLocation(Fixed, /*bSweep=*/true, nullptr, ETeleportType::TeleportPhysics);
            if (!Key.IsEmpty())
            {
                WT_SaveHubSpot(Key, Fixed); // ★首次到那再补写真落点（空键=未经验证，只贴地不入表）
            }
            WT_WARNF(TEXT("[teleport-fix] ground-fix Z=%.0f key='%s'"),
                     FixHit.ImpactPoint.Z, *Key);
        }
        else
        {
            WT_WARNF(TEXT("[teleport-fix] ground-fix-skip (terrain not streamed yet) key='%s'"), *Key);
        }
    }), 0.6f, /*bLoop=*/false);
}

// ★0.2.153o 落地面朝目标（用户定案：「传送到终端**面前**」）：落点算法只能保证脚下有地、身上不卡，
// 但镜头朝向仍沿用传送前的旧朝向 ⇒ 人站在终端旁边却对着墙/荒野，体感就是「没传送到终端面前」。
// 落地后把控制器与角色 yaw 对准目标（只转水平朝向，不改 pitch/roll，避免仰天/低头）。
// SetControlRotation 为 AController 的 public 虚函数（Controller.h:163），蓝图可调、服务端调安全。
static void WT_FaceTarget(AFGCharacterPlayer* Character, const FVector& TargetLoc)
{
    if (!Character)
    {
        return;
    }
    AController* Ctrl = Character->GetController();
    if (!Ctrl)
    {
        return;
    }
    FVector Dir = TargetLoc - Character->GetActorLocation();
    Dir.Z = 0.f;
    if (Dir.SizeSquared() < KINDA_SMALL_NUMBER)
    {
        return;
    }
    const FRotator Look = Dir.Rotation();      // 水平朝向（Z 已清零 ⇒ pitch=0）
    Ctrl->SetControlRotation(Look);
    Character->SetActorRotation(FRotator(0.f, Look.Yaw, 0.f));
}

// ★0.2.153q 统一落点搜索器（用户定案：「所有建筑传送逻辑也按枢纽传送走」）
// 0.2.153g~0.2.153o2 为枢纽终端迭代出来的那一套，原先只有枢纽在用；普通建筑/机器/载具仍走
// 0.2.88 的 WT_FindSafeLandingSpot 环搜（搜不到就砸建筑上方 300 —— 与「卡在枢纽背面的墙里」
// 同源的老毛病：建筑 pivot 常在结构内部或底座之下，砸正上方 = 把人塞进结构里）。
// 现在抽成公共函数，两条路径共用同一套：
//   ① 起跳距现场推导 = 目标包围盒沿主方向的水平半径 + 胶囊半径 + 10cm（不留死数字）；
//   ② 高度基准 = 锚点正下方自然地面 Z（pivot 埋地时拿 actor Z 当基准必错）；
//   ③ 8 方向 × 14 圈候选，按代价排序：近场(≤3m)优先 → 正面优先 → 宽松阶段排在最后；
//   ④ 落差哨兵（地面比候选低 >350cm = 缝/坑）、高度哨兵（高于自然地面 +450 严格 / +250 宽松）；
//   ⑤ 严格阶段用真实碰撞体 overlap 查「会不会卡进结构」，宽松阶段退化为「头顶净空」射线；
//   ⑥ 严格阶段全灭先查落点表（远距离直传），命中即返回 bFromCache（调用方补一次 ground-fix）；
//   ⑦ 命中即移动并写表 ⇒ 调用方拿到 FWT_LandSpot 后只做朝向/通知，不再二次移动。
struct FWT_LandSpot
{
    FVector Dest;
    bool    bFound;
    bool    bFromCache;   // true: 点来自存档落点表（函数内已移动），调用方需补一次 ground-fix
};

static FWT_LandSpot WT_SearchLandingSpot(UWorld* World, AFGCharacterPlayer* Character,
                                         const AActor* Target, const FVector& LandBase,
                                         const TCHAR* DirSrc,
                                         const FCollisionQueryParams& GroundTrace,
                                         const FCollisionQueryParams& LandOverlapQ,
                                         const FString& SpotKey, const TCHAR* LogTag)
{
    FWT_LandSpot Out;
    Out.Dest = FVector::ZeroVector;
    Out.bFound = false;
    Out.bFromCache = false;
    if (!World || !Character || !Target)
    {
        return Out;
    }

    const FVector Anchor = Target->GetActorLocation();
    const UCapsuleComponent* Cap = Character->GetCapsuleComponent();
    const float CapH = Cap ? Cap->GetScaledCapsuleHalfHeight() : 90.f;
    const float CapR = Cap ? Cap->GetScaledCapsuleRadius() : 60.f;
    const FCollisionShape CapShape = FCollisionShape::MakeCapsule(CapR, FMath::Max(CapH - 2.f, 10.f));

    // ① 起跳距现场推导（★0.2.153i：取代 0.2.153c 的「hub bbox 半宽 +100」——那个死数字正是
    // 「落在终端 10 米外」的根因；★0.2.153q 推广到所有建筑：建筑多大多宽就从多远起跳）。
    float StandoffBase = 150.f; // 目标无有效包围盒时的兜底（≈旧「交互范围内」语义）
    const FBox TBox = Target->GetComponentsBoundingBox();
    if (TBox.IsValid != 0)
    {
        const FVector TExt = TBox.GetExtent();
        const float Radius = FMath::Abs(TExt.X * LandBase.X) + FMath::Abs(TExt.Y * LandBase.Y);
        StandoffBase = Radius + CapR + 10.f;
    }

    // ② 高度基准（第五轮定案）：锚点正下方的自然地面 Z。
    float GroundRefZ = Anchor.Z;
    {
        FHitResult GHit;
        if (World->LineTraceSingleByChannel(GHit,
                FVector(Anchor.X, Anchor.Y, Anchor.Z + 300.f),
                FVector(Anchor.X, Anchor.Y, Anchor.Z - 600.f), ECC_Pawn, GroundTrace))
        {
            GroundRefZ = GHit.ImpactPoint.Z;
        }
        else
        {
            UNavigationSystemV1* Nav0 = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
            FNavLocation GNav;
            if ((Nav0 != nullptr)
                && Nav0->ProjectPointToNavigation(FVector(Anchor.X, Anchor.Y, Anchor.Z + 88.f),
                                                  GNav, FVector(200.f, 200.f, 400.f)))
            {
                GroundRefZ = GNav.Location.Z;
            }
        }
    }
    WT_WARNF(TEXT("[%s] anchor=%s groundRefZ=%.0f dirSrc=%s standoff=%.0f"),
             LogTag, *Anchor.ToString(), GroundRefZ, DirSrc, StandoffBase);

    // ③ 方向扇 8 条（45° 间隔，自 LandBase 起）
    FVector LandDirs[8];
    for (int32 k = 0; k < 8; ++k)
    {
        LandDirs[k] = LandBase.RotateAngleAxis(45.f * k, FVector::UpVector);
    }
    // ★0.2.153o 前向优先重排：0°, ±45°, ±90°, ±135°, 180°（原 0..7 顺序会让两侧/背面抢先命中）
    static const int32 DirOrder[8] = {0, 1, 7, 2, 6, 3, 5, 4};

    // ④ 候选代价排序
    struct FHubCandidate { int32 Ring; int32 DirIdx; int32 Cost; bool bBack; };
    const float NearMax = 300.f;   // ★0.2.153o2 用户定案：近场半径 3 米
    TArray<FHubCandidate> Plan;
    Plan.Reserve(8 * 14);
    for (int32 Ring = 0; Ring < 14; ++Ring)
    {
        const bool bNear = (StandoffBase + 100.f * Ring) <= NearMax;
        for (int32 Oi = 0; Oi < 8; ++Oi)
        {
            FHubCandidate C;
            C.Ring   = Ring;
            C.DirIdx = DirOrder[Oi];
            C.bBack  = (Oi >= 5);                       // DirOrder[5..7] = ±135° / 180°
            C.Cost   = (bNear ? 0 : 100)                // 近场优先
                     + Ring + (C.bBack ? 2 : 0) + (Ring >= 10 ? 20 : 0);
            Plan.Add(C);
        }
    }
    Plan.Sort([](const FHubCandidate& A, const FHubCandidate& B)
    {
        if (A.Cost != B.Cost) { return A.Cost < B.Cost; }
        if (A.bBack != B.bBack) { return !A.bBack; }    // 同代价先正面
        return A.Ring < B.Ring;
    });

    bool bCacheTried = false;
    for (int32 Ci = 0; (Ci < Plan.Num()) && !Out.bFound; ++Ci)
    {
        const bool bRelaxed = (Plan[Ci].Ring >= 10);
        // ⑤ 严格阶段全灭时先试落点表（此前验证过的落点），再进宽松阶段——
        // 免得「明明存档里有好点、却把人扔到 10 米外」。
        if (!bCacheTried && bRelaxed && !SpotKey.IsEmpty())
        {
            bCacheTried = true;
            const TMap<FString, FVector> Spots = WT_LoadHubSpots();
            if (const FVector* Cached = Spots.Find(SpotKey))
            {
                // ★0.2.153f sweep=false：sweep 查路径不查终点，会把存档好的落点也挡掉。
                if (Character->SetActorLocation(*Cached, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics))
                {
                    Out.Dest = *Cached;
                    Out.bFound = true;
                    Out.bFromCache = true;
                    WT_WARNF(TEXT("[%s] cache-landed dest=%s"), LogTag, *Cached->ToString());
                    break;
                }
                WT_WARNF(TEXT("[%s] cache-move-failed at %s"), LogTag, *Cached->ToString());
            }
            else
            {
                WT_WARNF(TEXT("[%s] cache-miss key='%s' (进入宽松落点阶段)"), LogTag, *SpotKey);
            }
        }
        const int32 Di = Plan[Ci].DirIdx;
        const float Standoff = StandoffBase + 100.f * Plan[Ci].Ring;
        const FVector Candidate = Anchor + LandDirs[Di] * Standoff;

        // 下探 1200 找地面（★0.2.153n：-400 打不到悬空底座下的缝底，看不出那里是个缝）
        FHitResult FloorHit;
        FVector FloorPoint = FVector::ZeroVector;
        const TCHAR* FloorSrc = TEXT("trace");
        if (World->LineTraceSingleByChannel(FloorHit,
                Candidate + FVector(0.f, 0.f, 200.f),
                Candidate - FVector(0.f, 0.f, 1200.f), ECC_Pawn, GroundTrace))
        {
            FloorPoint = FloorHit.ImpactPoint;
        }
        else
        {
            // navmesh 投影兜底（★0.2.153c）：竖直 trace 落空时按 extent(200,200,400) 投影，
            // 再空则沿该方向外推 100 投影一次，还空则放弃该候选。
            UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
            FNavLocation NavLoc;
            const FVector ProjBase = Candidate + FVector(0.f, 0.f, 88.f);
            const bool bNav1 = (NavSys != nullptr)
                && NavSys->ProjectPointToNavigation(ProjBase, NavLoc, FVector(200.f, 200.f, 400.f));
            if (!bNav1)
            {
                const FVector PushBase = ProjBase + LandDirs[Di] * 100.f;
                const bool bNav2 = (NavSys != nullptr)
                    && NavSys->ProjectPointToNavigation(PushBase, NavLoc, FVector(200.f, 200.f, 400.f));
                if (!bNav2)
                {
                    WT_WARNF(TEXT("[%s] no-floor+no-navmesh dir=%d standoff=%.0f at %s"),
                             LogTag, Di, Standoff, *Candidate.ToString());
                    continue;
                }
            }
            FloorPoint = NavLoc.Location;
            FloorSrc = TEXT("navmesh");
        }
        // 落差哨兵（bug②「传送过去掉进空隙里」）：地面比候选点低 >350cm = 打到缝底/坑底
        if (FloorPoint.Z < Candidate.Z - 350.f)
        {
            WT_WARNF(TEXT("[%s] gap-reject dir=%d standoff=%.0f floorZ=%.0f candZ=%.0f at %s"),
                     LogTag, Di, Standoff, FloorPoint.Z, Candidate.Z, *Candidate.ToString());
            continue;
        }
        // 落点离地余量 +20cm（★0.2.153n：+2 会被起伏地形擦边误杀，是「48 候选全灭」元凶）
        const FVector Dest = FloorPoint + FVector(0.f, 0.f, CapH + 20.f);
        // 高度哨兵：严格 +450（枢纽抬高平台 ≈+3m 可用，有 overlap 兜底）/ 宽松 +250（防上房顶）
        const float MaxAboveGround = bRelaxed ? 250.f : 450.f;
        if (Dest.Z > GroundRefZ + MaxAboveGround)
        {
            WT_WARNF(TEXT("[%s] height-reject dir=%d destZ=%.0f groundRefZ=%.0f cap=%.0f at %s"),
                     LogTag, Di, Dest.Z, GroundRefZ, MaxAboveGround, *Dest.ToString());
            continue;
        }
        // ⑤ 严格阶段：真实碰撞体 overlap（LandOverlapQ 只忽略玩家与目标本体，保留其它一切）
        if (!bRelaxed && World->OverlapBlockingTestByChannel(Dest, FQuat::Identity, ECC_Pawn, CapShape, LandOverlapQ))
        {
            // ★0.2.153n 诊断：再跑一次 Multi 版把第一个 blocking 组件名打出来（下轮日志可实锤）
            TArray<FOverlapResult> Blockers;
            FString Blocker(TEXT("unknown"));
            if (World->OverlapMultiByChannel(Blockers, Dest, FQuat::Identity, ECC_Pawn, CapShape, LandOverlapQ))
            {
                for (const FOverlapResult& R : Blockers)
                {
                    if (!R.bBlockingHit)
                    {
                        continue; // 只报真正挡路（blocking）的那一项
                    }
                    Blocker = FString::Printf(TEXT("%s/%s"),
                        R.GetActor() ? *R.GetActor()->GetName() : TEXT("none"),
                        R.GetComponent() ? *R.GetComponent()->GetName() : TEXT("none"));
                    break;
                }
            }
            WT_WARNF(TEXT("[%s] overlap-reject dir=%d standoff=%.0f at %s blocker='%s'"),
                     LogTag, Di, Standoff, *Dest.ToString(), *Blocker);
            continue;
        }
        if (bRelaxed)
        {
            // ★0.2.153n 宽松阶段的替代保险「头顶净空」：不放 overlap（怕地形擦边误杀），
            // 但从落点头顶向上打 400 ⇒ 命中 = 落点在某结构内部/挑檐下/缝隙里，弃用。
            FHitResult RoofHit;
            if (World->LineTraceSingleByChannel(RoofHit, Dest, Dest + FVector(0.f, 0.f, 400.f),
                                                ECC_Pawn, LandOverlapQ))
            {
                WT_WARNF(TEXT("[%s] relaxed-roof-reject dir=%d standoff=%.0f at %s roof='%s'"),
                         LogTag, Di, Standoff, *Dest.ToString(),
                         RoofHit.GetActor() ? *RoofHit.GetActor()->GetName() : TEXT("none"));
                continue;
            }
        }
        // ★0.2.153f sweep=false：sweep 查「当前位置→落点」整条路径，远距必被沿途地形误挡。
        if (!Character->SetActorLocation(Dest, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics))
        {
            WT_WARNF(TEXT("[%s] move-reject dir=%d standoff=%.0f at %s"),
                     LogTag, Di, Standoff, *Dest.ToString());
            continue;
        }
        WT_WARNF(TEXT("[%s] landed dir=%d standoff=%.0f relaxed=%d dirSrc=%s floor='%s'(%s) floorZ=%.0f groundRefZ=%.0f dest=%s"),
                 LogTag, Di, Standoff, bRelaxed ? 1 : 0, DirSrc,
                 FloorHit.GetActor() ? *FloorHit.GetActor()->GetName() : TEXT("none"),
                 FloorSrc, FloorPoint.Z, GroundRefZ, *Dest.ToString());
        if (!SpotKey.IsEmpty())
        {
            WT_SaveHubSpot(SpotKey, Dest);   // ★0.2.153e 落点入表，供远距离查表直传
        }
        Out.Dest = Dest;
        Out.bFound = true;
        break;
    }
    return Out;
}

void URemote_FactoryRCO::Server_TeleportTo_Implementation(AActor* Target)
{
    // 传送会写角色 Transform（可能带着载具/梯子/传送带上的附着状态），是本 mod 里最"物理"的一步。
    WT_ZONE_COARSE(TEXT("RCO.TeleportTo"));

    // AUTHORITY GATE: the server owns the character transform, so this must run here for the move
    // to stick (and for a host to see it).
    if (!WT_HasAuthority(this))
    {
        WT_WARNF(TEXT("Server_TeleportTo called without authority — ignored."));
        return;
    }

    AFGCharacterPlayer* Character = GetOwnerPlayerCharacter();
    if (!Character)
    {
        Client_NotifyTeleport(Target, false, WTTeleportFail::TargetGone);
        return;
    }

    // The actor reference arrives by NetGUID and resolves to null if it is no longer relevant to
    // this client (demolished, collected, or outside its network relevancy set). Bail cleanly.
    if (!IsValid(Target))
    {
        WT_WARNF(TEXT("Server_TeleportTo: target did not resolve on the server."));
        Client_NotifyTeleport(Target, false, WTTeleportFail::TargetGone);
        return;
    }

    UWorld* World = GetWorld();
    const FVector Center = Target->GetActorLocation();
    const FBox Box = Target->GetComponentsBoundingBox(/*bNonColliding=*/true);
    const bool bHaveBox = (Box.IsValid != 0);   // IsValid 是 uint8 位域，直转 bool 报 C4800

    FVector ToPlayer = Character->GetActorLocation() - Center;
    ToPlayer.Z = 0.f;
    if (ToPlayer.SizeSquared() < KINDA_SMALL_NUMBER)
    {
        ToPlayer = FVector(0.f, 1.f, 0.f); // arbitrary fallback direction
    }
    ToPlayer.Normalize();

    // ★0.2.135 外星收集品（slug / 球 / 果实）走专用落点：坐标点 2m 外；洞穴内强制站在物品上方。
    // 机器/建筑仍走 0.2.88 的通用环搜（它们体积大，本来就该落在建筑外沿）。
    const bool bIsPickup = (Cast<AFGItemPickup>(Target) != nullptr);

    FVector Destination;
    // ★0.2.153f：本函数内所有 SetActorLocation 一律 sweep=false（sweep 查「当前位置→落点」路径
    // 而非终点，是 hub encroach-reject / 兜底砸落 bMoved=false 拒传的误判源）；落点合法性由
    // 上游 trace/overlap/高度哨兵保证，砸落由 ground-fix / 重力着陆兜底。
    // ★0.2.153 传送枢纽(终端/集成制作台等枢纽子建筑)：按用户建议简化为"落到终端交互范围内正前方"。
    // 不再做包围盒粗判 / 8 方向环搜 / 屋顶检测。终端屏幕法线正对玩家，
    // 故“终端朝向” = 玩家所在方向 = 函数上文已算好的 ToPlayer(已归一化、压平)。
    // 落点 = 终端位置 + ToPlayer × 150(≈1.5m，交互范围内)；下探找地面(忽略终端/枢纽组件/玩家自身)，
    // 带 sweep 的 SetActorLocation 落位并验证不嵌物体；主方向失败仅做同距(≤1.5m)小幅旋转兜底，绝不扩距离、不爬房顶。
    AFGBuildableTradingPost* Hub = Target->GetTypedOuter<AFGBuildableTradingPost>();
    const bool bIsHubRelated = (Cast<AFGBuildableHubTerminal>(Target) != nullptr)
                            || (Hub != nullptr)
                            || Target->Implements<UFGTradingPostIntegratedBuildable>();
    if (bIsHubRelated)
    {
        // ★0.2.153g/h 坐标来源（SDK 一手实锤）：终端在枢纽上的位置是蓝图资源摆的，cpp 无偏移
        // 常量；引擎 spawn 终端就落在 TradingPost::mHubTerminalLocation（protected，外部拿不到）
        // 上 ⇒ 终端 actor 世界 pivot 与 BP 摆放点等价。★0.2.153h 简化（用户拍板「找枢纽终端的
        // 坐标不就好了」）：面板扫描只把 AFGBuildableHubTerminal 收进枢纽类目，Target 就是终端
        // 本尊，0.2.153g 的 TermAnchor/mHubTerminal 间接层是死代码，砍掉；传送时服务端直读
        // Target 活坐标，面板每开一次扫描回包 E.Location 也带最新坐标（客户端 Target 失效时按
        // 坐标直传用的就是它）——坐标每次打开面板都是动态刷新的，不落盘不过夜。
        // Hub 正本（bbox/忽略集用）：GetTradingPost()（SDK 公开函数）> GetTypedOuter > Subsystem 枚举兜底。
        if (!Hub)
        {
            // 注意：GetTradingPost() 是非 const 成员函数（SDK 21 行），Term 不能加 const。
            if (AFGBuildableHubTerminal* Term = Cast<AFGBuildableHubTerminal>(Target))
            {
                Hub = Term->GetTradingPost();
            }
        }
        if (!Hub)
        {
            if (AFGBuildableSubsystem* BS = AFGBuildableSubsystem::Get(World))
            {
                TArray<AFGBuildableTradingPost*> Hubs;
                BS->GetTypedBuildable<AFGBuildableTradingPost>(Hubs);
                if (Hubs.Num() > 0)
                {
                    Hub = Hubs[0];
                }
            }
        }

        // ★0.2.153h 坐标正本 = 终端活坐标（Target 本尊，见上注释）。
        const FVector TLoc = Target->GetActorLocation();

        // 地面 trace 忽略终端/枢纽组件/玩家自身，避免把人落在枢纽屋顶或结构上。
        FCollisionQueryParams GroundTrace(SCENE_QUERY_STAT(WT_HubLanding), /*bTraceComplex=*/false);
        GroundTrace.AddIgnoredActor(Target);
        GroundTrace.AddIgnoredActor(Character);
        if (Hub)
        {
            GroundTrace.AddIgnoredActor(Hub);
            TArray<AActor*> Attached;
            Hub->GetAttachedActors(Attached, /*bResetArray=*/true, /*bRecursivelyIncludeAttachedActors=*/true);
            for (AActor* A : Attached)
            {
                if (A)
                {
                    GroundTrace.AddIgnoredActor(A);
                }
            }
        }

        // ★0.2.153i 落点 embed 检验专用查询：只忽略玩家与终端本体，保留枢纽真实碰撞体
        // （GroundTrace 连枢纽一起忽略，是给「找地面」用的——它要穿过枢纽看下方地形；
        //  查「人卡不卡进结构」必须反过来把枢纽算进去）。
        FCollisionQueryParams LandOverlapQ(SCENE_QUERY_STAT(WT_HubLandOverlap), /*bTraceComplex=*/false);
        LandOverlapQ.AddIgnoredActor(Target);
        LandOverlapQ.AddIgnoredActor(Character);


        // ★0.2.153c 落点方向定稿（大肥鱼第三轮修正，取代前两版）：
        // ① 方向原点用枢纽 bbox 的 GetCenter 压平算「中心→终端」水平方向——枢纽 actor 的 pivot
        //    不在几何中心（底座/顶端偏斜），拿 pivot 当原点方向会算歪；落点高度不靠算，由 trace/navmesh 定；
        // ② 主距不再拍脑袋 250：终端是嵌入式（本来就贴结构表面），起跳距 = hub bbox 沿该方向
        //    的水平半宽 + 100；
        // ③ 挖到落点后再验一眼在不在 bbox 框内（嵌入式结构有挑檐/凸檐，框内=落檐下），
        //    框内沿该方向径向外推 100，最多推两次，还在框内就放弃该方向，不硬塞；
        // ④ 兜底扇 0/±30 三条（第五轮：±60 两条也剔掉）；
        // ⑤ 方向水平长度 <200 就弃用：改取终端自身 forward 水平分量，再不行用 bbox 短轴外推；
        // ⑥ 竖直 trace 先撞挑檐顶（命中点水平在框内）的命中不可信，弃用换方向；
        // ⑦ 高度哨兵跟「地面 Z」比（第五轮：终端 pivot 埋地 94，anchor 当高度基准是错的）。
        const FBox HubBox = Hub ? Hub->GetComponentsBoundingBox() : FBox(EForceInit::ForceInitToZero);
        const bool bHaveHubBox = (Hub != nullptr) && (HubBox.IsValid != 0);
        const FVector HubCenter = bHaveHubBox ? HubBox.GetCenter() : FVector::ZeroVector;
        FVector LandBase = FVector::ZeroVector;
        const TCHAR* DirSrc = TEXT("hub-center");
        if (bHaveHubBox)
        {
            LandBase = TLoc - HubCenter;
            LandBase.Z = 0.f;
        }
        // 第三轮补①：方向水平长度 <200 别用（终端嵌入式、太贴 bbox 中心，方向会乱跳甚至掉回结构里），
        // 改取终端自身 forward 的水平分量；再不行用 bbox 短轴水平方向外推。
        if (LandBase.Size() < 200.f)
        {
            LandBase = Target->GetActorForwardVector(); // 终端朝向（Target 即终端本尊）
            LandBase.Z = 0.f;
            DirSrc = TEXT("terminal-fwd");
            // ★0.2.153o 用户实测定案：**位置是对的，方向要转 180°**。
            // 终端自身 forward 的水平分量实测指进枢纽**内部**（本局 dirSrc=terminal-fwd 三条方向
            // 全被枢纽碰撞体判死 ⇒ 兜底砸终端本体 ⇒ 人卡进护栏与外墙之间，即「卡墙里」那一版）。
            // 落点算法本身没有问题，错的只是这一支的朝向基准 ⇒ 取反，正面 = 玩家正常站位那一侧。
            // （hub-center 那支 = 「枢纽中心→终端」= 天然朝外，不参与翻转，否则会转进枢纽里。）
            LandBase = -LandBase;
            DirSrc = TEXT("terminal-fwd-flipped");
            // ★0.2.153o2 用户第二轮实测定案：翻转后**再向右转 90°**（+yaw = 前(X)→右(Y)，UE 约定）。
            // 终端的 authored forward 与「玩家站位侧」差 180°+90°：直接翻 180° 会把人放到枢纽另一面
            // （本局日志 landed standoff=916 实锤落在 9 米外那侧）→ 再右转 90° 才是正对终端的那一面。
            LandBase = LandBase.RotateAngleAxis(90.f, FVector::UpVector);
            DirSrc = TEXT("terminal-fwd-flipped+right90");
            if (LandBase.SizeSquared() < KINDA_SMALL_NUMBER)
            {
                const FVector AxisExt = bHaveHubBox ? HubBox.GetExtent() : FVector(1.f, 0.f, 0.f);
                LandBase = (AxisExt.X <= AxisExt.Y) ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f);
                DirSrc = TEXT("bbox-short-axis");
            }
        }
        if (LandBase.SizeSquared() < KINDA_SMALL_NUMBER)
        {
            LandBase = ToPlayer; // 终极兜底：以上全不可用时退回玩家方向
            DirSrc = TEXT("to-player");
        }
        LandBase.Normalize();
        // ★0.2.153q 落点搜索统一走 WT_SearchLandingSpot（与普通建筑完全同一套，见函数注释）。
        // 本分支只保留枢纽专属准备：忽略集（终端 + 枢纽本体及其挂件）、方向基准（终端朝向
        // 翻转 180° 再右转 90°，0.2.153o/o2 用户实测定案）、以及落点表键 Hub|<对象路径名>。
        const FString HubKey = WT_HubSpotKey(Target);
        const FWT_LandSpot Spot = WT_SearchLandingSpot(World, Character, Target, LandBase, DirSrc,
                                                       GroundTrace, LandOverlapQ,
                                                       HubKey, TEXT("teleport-hub"));
        if (Spot.bFound)
        {
            WT_FaceTarget(Character, TLoc);   // ★0.2.153o 落地转身面向终端
            if (Spot.bFromCache)
            {
                // 查表直传：目的地区块可能刚开始流送，0.6s 后补一次地面校正
                WT_ScheduleTeleportGroundFix(World, Character, HubKey, Target);
            }
            WT_WARNF(TEXT("[teleport-result] '%s' -> dest=%s (hub landing path)"),
                     *Target->GetName(), *Spot.Dest.ToString());
            Client_NotifyTeleport(Target, true, WTTeleportFail::None);
            return;
        }
        // ★0.2.153n 最后手段（严格 8×10 + 查表 + 宽松 8×4 全灭，多为目的地区块未流送）：
        // 砸终端正上方 300。空键=不入表（未经验证的点可能是檐顶，防「每次落同一个错点」复发）。
        Destination = TLoc + FVector(0.f, 0.f, 300.f);
        WT_WARNF(TEXT("[teleport-hub] last-resort drop onto terminal at %s"), *Destination.ToString());
        WT_ScheduleTeleportGroundFix(World, Character, FString(), Target);
        // 不 return：落到函数末尾公共 SetActorLocation（sweep=false）完成移动与通知。
    }
    else if (bIsPickup)
    {
        const float ItemTopZ = bHaveBox ? Box.Max.Z : Center.Z;
        if (!WT_FindCollectibleLandingSpot(World, Character, Center, ItemTopZ, /*bHasActor=*/true, Destination))
        {
            WT_WARNF(TEXT("Teleport refused: no safe landing near pickup '%s' at %s"),
                     *Target->GetName(), *Center.ToString());
            Client_NotifyTeleport(Target, false, WTTeleportFail::NoSafeLanding);
            return;
        }
    }
    else
    {
        // ★0.2.153q 建筑/机器/载具统一走枢纽那套落点搜索（用户定案「所有建筑传送逻辑也按枢纽
        // 传送走」）。旧路径（WT_FindSafeLandingSpot 环搜 + 搜不到就砸建筑上方 300）与「卡在枢纽
        // 背面的墙里」同源：建筑 pivot 常在结构内部/底座之下，砸正上方 = 把人塞进结构里。
        // 主方向 = 玩家所在侧（ToPlayer），方向扇 8 条全试，近场 ≤3m 优先。
        FCollisionQueryParams BldGround(SCENE_QUERY_STAT(WT_BldLanding), /*bTraceComplex=*/false);
        BldGround.AddIgnoredActor(Target);
        BldGround.AddIgnoredActor(Character);
        {
            TArray<AActor*> Attached;
            Target->GetAttachedActors(Attached, /*bResetArray=*/true,
                                      /*bRecursivelyIncludeAttachedActors=*/true);
            for (AActor* A : Attached)
            {
                if (A)
                {
                    BldGround.AddIgnoredActor(A);
                }
            }
        }
        // 查卡不卡用另一套查询：只忽略玩家与目标本体，保留其它一切真实碰撞体。
        FCollisionQueryParams BldOverlap(SCENE_QUERY_STAT(WT_BldLandOverlap), /*bTraceComplex=*/false);
        BldOverlap.AddIgnoredActor(Target);
        BldOverlap.AddIgnoredActor(Character);

        const FString BldKey = FString::Printf(TEXT("Bld|%s"), *Target->GetPathName());
        const FWT_LandSpot Spot = WT_SearchLandingSpot(World, Character, Target, ToPlayer,
                                                       TEXT("to-player"), BldGround, BldOverlap,
                                                       BldKey, TEXT("teleport-bld"));
        if (Spot.bFound)
        {
            WT_FaceTarget(Character, Target->GetActorLocation());  // 落地转身面向建筑
            if (Spot.bFromCache)
            {
                WT_ScheduleTeleportGroundFix(World, Character, BldKey, Target);
            }
            WT_WARNF(TEXT("[teleport-result] '%s' -> dest=%s (bld landing path)"),
                     *Target->GetName(), *Spot.Dest.ToString());
            Client_NotifyTeleport(Target, true, WTTeleportFail::None);
            return;
        }
        // 最后手段：建筑正上方 300 砸落 + ground-fix 贴地（与枢纽兜底同策略）
        Destination = Center + FVector(0.f, 0.f, 300.f);
        WT_WARNF(TEXT("[teleport-bld] last-resort drop onto building at %s"), *Destination.ToString());
        WT_ScheduleTeleportGroundFix(World, Character, FString(), Target);
    }

    const bool bMoved = Character->SetActorLocation(Destination, /*bSweep=*/false, nullptr,
                                                    ETeleportType::TeleportPhysics);
    // ★0.2.153o 通用路径也转身面向目标（机器/收集品/枢纽兜底都走这里）：落点算法只管脚下与卡不卡，
    // 朝向沿用传送前旧 yaw 会让玩家落地后对着空气。只改水平 yaw，不动 pitch/roll。
    if (bMoved)
    {
        WT_FaceTarget(Character, Target->GetActorLocation());
    }

    // ★0.2.149 诊断（强制 Warning）：所有传送的最终落点与是否真的移动，便于对比理论锚点定位偏差。
    WT_WARNF(TEXT("[teleport-result] '%s' -> dest=%s bMoved=%s"),
             *Target->GetName(), *Destination.ToString(), bMoved ? TEXT("true") : TEXT("false"));

    WT_LOGF(TEXT("Teleport: %s -> '%s' at %s (ok=%s)"),
            *Character->GetName(), *Target->GetName(), *Destination.ToString(),
            bMoved ? TEXT("true") : TEXT("false"));

    Client_NotifyTeleport(Target, bMoved, bMoved ? WTTeleportFail::None : WTTeleportFail::TargetGone);
}

void URemote_FactoryRCO::Client_NotifyTeleport_Implementation(AActor* Target, bool bSuccess, int32 Reason)
{
    OnTeleportResult.Broadcast(Target, bSuccess, Reason);
}

void URemote_FactoryRCO::Server_TeleportToLocation_Implementation(FVector Location)
{
    // 传送到文件坐标：没有 actor 可引用（对应单元格未流送进内存），只有 collectibles.json 里的坐标。
    WT_ZONE_COARSE(TEXT("RCO.TeleportToLocation"));

    if (!WT_HasAuthority(this))
    {
        WT_WARNF(TEXT("Server_TeleportToLocation called without authority — ignored."));
        return;
    }

    AFGCharacterPlayer* Character = GetOwnerPlayerCharacter();
    if (!Character)
    {
        Client_NotifyTeleport(nullptr, false, WTTeleportFail::TargetGone);
        return;
    }

    UWorld* World = GetWorld();

    // ★0.2.153f 收集品坐标直传三档落点（与 Server_TeleportToCoord 同构；日志实锤：旧版只做
    // 2m/4m 环搜、搜不到直接拒——远处收集品 actor 未复制到客户端全走这里，面板内收集品
    // 超 relevancy 距离全军覆没 "TeleportToLocation refused: no safe landing"）。
    // 废除 0.2.135 的「搜不到即拒」（那是为「玩家没进洞硬落=掉虚空」设计的）；现在砸落由
    // ground-fix 贴地兜住，与机器坐标直传同策略：
    //   ① 查表（RF_teleport_spots.json，键前缀 Col| 区分收集品）→ sweep=false 直传 + ground-fix；
    //   ② 环搜（区域已加载时走这条）→ 成功即写表；
    //   ③ 兜底：坐标上方 3m 砸落（sweep=false）+ ground-fix 补写真落点。
    const FString Key = FString::Printf(TEXT("Col|%d,%d,%d"),
        FMath::RoundToInt(Location.X), FMath::RoundToInt(Location.Y), FMath::RoundToInt(Location.Z));

    // ① 查表直传：手里有坐标就不需要那片区域已加载
    const TMap<FString, FVector> Spots = WT_LoadHubSpots();
    if (const FVector* Cached = Spots.Find(Key))
    {
        const bool bCacheMoved = Character->SetActorLocation(*Cached, /*bSweep=*/false, nullptr,
                                                             ETeleportType::TeleportPhysics);
        WT_WARNF(TEXT("[teleport-col] cache-landed key='%s' dest=%s ok=%s"),
                 *Key, *Cached->ToString(), bCacheMoved ? TEXT("true") : TEXT("false"));
        if (bCacheMoved)
        {
            WT_ScheduleTeleportGroundFix(World, Character, Key);
            Client_NotifyTeleport(nullptr, true, WTTeleportFail::None);
            return;
        }
        WT_WARNF(TEXT("[teleport-col] cache-move-failed key='%s' — re-resolving"), *Key);
    }

    // ② 环搜（沿用 0.2.135 的收集品专用落点：坐标点 2m 外、洞穴内强制站物品上方）
    FVector Destination;
    if (WT_FindCollectibleLandingSpot(World, Character, Location, Location.Z,
                                      /*bHasActor=*/false, Destination))
    {
        Character->SetActorLocation(Destination, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
        WT_SaveHubSpot(Key, Destination); // 近距首传即入表
        WT_WARNF(TEXT("[teleport-col] resolved dest=%s spot-saved key='%s'"),
                 *Destination.ToString(), *Key);
        Client_NotifyTeleport(nullptr, true, WTTeleportFail::None);
        return;
    }

    // ③ 兜底：收集品坐标上方 3m 砸落（sweep=false：sweep 查的是「当前位置→落点」的路径，
    // 远距必被沿途地形误挡；终点合法性交给落地后 ground-fix），fix 成功补写落点表。
    Destination = Location + FVector(0.f, 0.f, 300.f);
    const bool bMoved = Character->SetActorLocation(Destination, /*bSweep=*/false, nullptr,
                                                    ETeleportType::TeleportPhysics);
    WT_WARNF(TEXT("[teleport-col] fallback-drop dest=%s bMoved=%s key='%s'"),
             *Destination.ToString(), bMoved ? TEXT("true") : TEXT("false"), *Key);
    if (bMoved)
    {
        WT_ScheduleTeleportGroundFix(World, Character, Key);
    }
    Client_NotifyTeleport(nullptr, bMoved, bMoved ? WTTeleportFail::None : WTTeleportFail::TargetGone);
}

/**
 * ★0.2.153e 坐标直传（第十轮定案，1.0.0 前完工）：不依赖 actor 引用的机器传送。
 * 客户端未复制远端机器（网络 relevancy）时 Entry->Target 为 null，Server_TeleportTo 发不出来；
 * 本 RPC 只收坐标+半宽（扫描时随 FWT_MachineEntry 复制，坐标永远有）。
 * 落点策略（与 hub 落点表同一张 RF_teleport_spots.json，键前缀 Coord| 区分）：
 *   ① 查表命中 → sweep=false 直传 + 0.6s ground-fix（fix 后写回刷新表）；
 *   ② 未命中且区域已加载 → 环搜落点，成功即写表（近距首传即入表）；
 *   ③ 环搜也失败（远端未流送）→ 兜底 Z300 砸落 + ground-fix，fix 成功把真落点补写进表
 *      （大肥鱼：「存档留个字段，归档进来是空表，首次到那再补写」）。
 * ★0.2.153f 全部长距瞬移 sweep=false（日志实锤）：sweep 查的是「当前位置→落点」路径而非终点，
 * 站高处时扫掠线穿山被地形挡 → 表里存好的落点也 cache-sweep-blocked 拒传。ground-fix 已提取为
 * 共享助手 WT_ScheduleTeleportGroundFix（与收集品坐标直传共用）。
 */
void URemote_FactoryRCO::Server_TeleportToCoord_Implementation(FVector Location, float BaseRadius)
{
    WT_ZONE_COARSE(TEXT("RCO.TeleportToCoord"));

    if (!WT_HasAuthority(this))
    {
        WT_WARNF(TEXT("Server_TeleportToCoord called without authority — ignored."));
        return;
    }

    AFGCharacterPlayer* Character = GetOwnerPlayerCharacter();
    if (!Character)
    {
        Client_NotifyTeleport(nullptr, false, WTTeleportFail::TargetGone);
        return;
    }

    UWorld* World = GetWorld();
    const FVector Center = Location;
    const FString Key = FString::Printf(TEXT("Coord|%d,%d,%d"),
        FMath::RoundToInt(Center.X), FMath::RoundToInt(Center.Y), FMath::RoundToInt(Center.Z));

    // ① 查表直传：坐标在手，不需要那片区域已加载（大肥鱼：手里有坐标就不用等那块加载）
    const TMap<FString, FVector> Spots = WT_LoadHubSpots();
    if (const FVector* Cached = Spots.Find(Key))
    {
        // ★0.2.153f sweep=false：sweep 查路径不查终点，表里验证过的落点也被沿途地形挡掉
        if (Character->SetActorLocation(*Cached, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics))
        {
            WT_WARNF(TEXT("[teleport-coord] cache-landed key='%s' dest=%s"), *Key, *Cached->ToString());
            WT_ScheduleTeleportGroundFix(World, Character, Key);
            Client_NotifyTeleport(nullptr, true, WTTeleportFail::None);
            return;
        }
        WT_WARNF(TEXT("[teleport-coord] cache-move-failed key='%s' at %s — re-resolving"),
                 *Key, *Cached->ToString());
        // 极罕见（sweep=false 基本不会失败）→ 落到下面重新算，算出的新点会覆盖表项
    }

    // ② 环搜（区域已加载时走这条；坐标模式没有机器 forward，用 ToPlayer 为基准：
    //    候选序 -ToPlayer 优先=机器远离玩家侧，玩家落地后面向机器，与普通机器行为同风格）
    FVector ToPlayer = Character->GetActorLocation() - Center;
    ToPlayer.Z = 0.f;
    if (ToPlayer.SizeSquared() < KINDA_SMALL_NUMBER)
    {
        ToPlayer = FVector(0.f, 1.f, 0.f);
    }
    ToPlayer.Normalize();

    FVector Destination;
    if (WT_FindSafeLandingSpot(World, Character, Center, FMath::Max(BaseRadius, 0.f),
                               ToPlayer, ToPlayer, Destination))
    {
        Character->SetActorLocation(Destination, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
        WT_SaveHubSpot(Key, Destination); // 近距首传即入表
        WT_WARNF(TEXT("[teleport-coord] resolved dest=%s spot-saved key='%s'"),
                 *Destination.ToString(), *Key);
        Client_NotifyTeleport(nullptr, true, WTTeleportFail::None);
        return;
    }

    // ③ 兜底：机器坐标上方 3m 砸落（★0.2.153f sweep=false：sweep=true 时远距扫掠线被沿途
    // 地形挡 → bMoved=false 拒传，正是「超距传不过去」的真凶之一），落地 ground-fix 后补写真落点
    Destination = Center + FVector(0.f, 0.f, 300.f);
    const bool bMoved = Character->SetActorLocation(Destination, /*bSweep=*/false, nullptr,
                                                    ETeleportType::TeleportPhysics);
    WT_WARNF(TEXT("[teleport-coord] fallback-drop dest=%s bMoved=%s key='%s'"),
             *Destination.ToString(), bMoved ? TEXT("true") : TEXT("false"), *Key);
    if (bMoved)
    {
        WT_ScheduleTeleportGroundFix(World, Character, Key);
    }
    Client_NotifyTeleport(nullptr, bMoved, bMoved ? WTTeleportFail::None : WTTeleportFail::TargetGone);
}

namespace
{
    /**
     * ★0.2.69: one-shot whole-map dump. Scans the currently-streamed collectibles (slugs, spheres,
     * loops) and drop pods, and merges their spawn points into collectibles.json at the mod root via
     * FWTCollectibles::MergeSpawns. Because World Partition only keeps nearby cells resident, the
     * caller should run this repeatedly (or from a fully-loaded editor) to accumulate the entire map.
     */
    void WT_DumpCollectiblesToJson(UWorld* World)
    {
        if (!World)
        {
            WT_WARNF(TEXT("wt.DumpCollectibles: no world available."));
            return;
        }

        TArray<FWT_CollectibleSpawn> Spawns;
        Spawns.Reserve(2048);

        for (TActorIterator<AFGItemPickup> It(World); It; ++It)
        {
            AFGItemPickup* Pickup = *It;
            if (!IsValid(Pickup))
            {
                continue;
            }
            FText Label;
            const uint8 Kind = FWTCollectibles::ClassifyCollectibleItem(
                Pickup->GetPickupItemClass().Get(), Label);
            if (Kind == 0xFF)
            {
                continue;
            }
            FWT_CollectibleSpawn S;
            S.Kind = Kind;
            S.Location = Pickup->GetActorLocation();
            Spawns.Add(S);
        }

        for (TActorIterator<AFGDropPod> It(World); It; ++It)
        {
            AFGDropPod* Pod = *It;
            if (!IsValid(Pod))
            {
                continue;
            }
            FWT_CollectibleSpawn S;
            S.Kind = static_cast<uint8>(EWT_CollectibleKind::DropPod);
            S.Location = Pod->GetActorLocation();
            Spawns.Add(S);
        }

        const bool bOk = FWTCollectibles::MergeSpawns(Spawns);
        if (bOk)
        {
            WT_LOGF(TEXT("wt.DumpCollectibles: merged %d spawns into %s"),
                    Spawns.Num(), *FWTCollectibles::GetCollectiblesFilePath());
        }
        else
        {
            WT_WARNF(TEXT("wt.DumpCollectibles: failed to write collectibles.json (check path / permissions)"));
        }
    }
}

// ★0.2.69: regenerate collectibles.json from the currently-loaded map. See WT_DumpCollectiblesToJson.
static FAutoConsoleCommand GDumpCollectiblesCmd(
    TEXT("wt.DumpCollectibles"),
    TEXT("Remote_Factory: scan the currently-loaded map for alien-energy collectibles and merge "
         "their spawn points into collectibles.json at the mod root."),
    FConsoleCommandDelegate::CreateLambda([]()
    {
        UWorld* World = nullptr;
        if (GEngine)
        {
            for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
            {
                if (UWorld* W = Ctx.World())
                {
                    if (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE)
                    {
                        World = W;
                        break;
                    }
                }
            }
            // Fallback: any non-null world (e.g. editor) if no game/PIE world was found.
            if (!World)
            {
                for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
                {
                    if (UWorld* W = Ctx.World())
                    {
                        World = W;
                        break;
                    }
                }
            }
        }
        WT_DumpCollectiblesToJson(World);
    }));

// ----------------------------------------------------------------------------
// Form 3 "A": remote switch control
//
// Both entry points funnel through the same three checks, in this order:
//   1. authority        -- switches are server-owned; a client write would be overwritten
//   2. valid + is-a     -- the NetGUID may have resolved to null, or to the wrong kind of building
//   3. wired            -- an unwired bridge does literally nothing when toggled, so say so
// Permission (Form 3 "D") sits between 2 and 3: only the shared-session owner may write.
// ----------------------------------------------------------------------------

namespace
{
    /**
     * Form 3 "D" permission gate.
     *
     * "Weak sharing" means every player has their own panel but they all see the same factory, so
     * the interesting question is not "who owns the widget" but "who may CHANGE things". The answer
     * this mod ships is: the first player to open the terminal (the session owner) writes, everyone
     * else reads. That keeps item destinations and switch flips unambiguous without inventing a
     * vote or a lock.
     *
     * The owner is tracked on the game state rather than in this RCO because an RCO is per-player:
     * it has no way to see the other players' RCOs. See FWTSharedSessionState in
     * Remote_FactoryRCO.h's companion file for the replicated store.
     */
    bool WT_MayWrite(const URemote_FactoryRCO* RCO, FText& OutReason)
    {
        const AFGPlayerController* PC = RCO ? RCO->GetOwnerPlayerController() : nullptr;
        if (!PC)
        {
            OutReason = WT_LOCTEXT("Reason_NoPlayerController", "Could not find the player controller.");
            return false;
        }
        // Single-player and listen-host always pass: the session owner is the only player, and on a
        // dedicated server this check falls through to the shared-state lookup below.
        if (PC->HasAuthority())
        {
            return true;
        }

        if (!Remote_FactoryShared::IsSessionOwner(PC->GetWorld(), PC))
        {
            OutReason = WT_LOCTEXT("Reason_ViewerReadOnly",
                "Only the console host can make changes (you are in view-only mode).");
            return false;
        }
        return true;
    }
}

void URemote_FactoryRCO::Server_SetSwitchOn_Implementation(AFGBuildable* Target, bool bOn)
{
    WT_ZONE_COARSE(TEXT("RCO.SetSwitchOn"));

    if (!WT_HasAuthority(this))
    {
        WT_WARNF(TEXT("Server_SetSwitchOn called without authority — ignored."));
        return;
    }

    AFGBuildableCircuitSwitch* Switch = Cast<AFGBuildableCircuitSwitch>(Target);
    if (!Switch)
    {
        WT_WARNF(TEXT("Server_SetSwitchOn: target is not a circuit switch."));
        Client_NotifySwitchResult(Target, false, WT_LOCTEXT("Msg_NotASwitch", "Target is not a power switch."));
        return;
    }

    FText Reason;
    if (!WT_MayWrite(this, Reason))
    {
        Client_NotifySwitchResult(Target, false, Reason);
        return;
    }

    // Unwired switches accept the state change but the circuits never actually merge, so the player
    // would see the button flip and nothing happen. Report it as a failure with the real reason
    // instead of a silent success.
    if (!Switch->IsSwitchConnected())
    {
        Client_NotifySwitchResult(Target, false,
            WT_LOCTEXT("Msg_SwitchNotWired", "The switch is not wired on both ends, so this has no effect."));
        return;
    }

    Switch->SetSwitchOn(bOn);

    WT_LOGF(TEXT("Switch '%s' -> %s"), *Switch->GetName(), bOn ? TEXT("ON") : TEXT("OFF"));
    Client_NotifySwitchResult(Target, true,
        bOn ? WT_LOCTEXT("Msg_SwitchedOn", "Switched on") : WT_LOCTEXT("Msg_SwitchedOff", "Switched off"));
}

void URemote_FactoryRCO::Server_SetSwitchPriority_Implementation(AFGBuildable* Target, int32 Priority)
{
    WT_ZONE_COARSE(TEXT("RCO.SetSwitchPriority"));

    if (!WT_HasAuthority(this))
    {
        WT_WARNF(TEXT("Server_SetSwitchPriority called without authority — ignored."));
        return;
    }

    AFGBuildablePriorityPowerSwitch* PrioritySwitch = Cast<AFGBuildablePriorityPowerSwitch>(Target);
    if (!PrioritySwitch)
    {
        WT_WARNF(TEXT("Server_SetSwitchPriority: target is not a priority power switch."));
        Client_NotifySwitchResult(Target, false,
            WT_LOCTEXT("Msg_NoPrioritySupport", "This switch has no power-limit priority."));
        return;
    }

    FText Reason;
    if (!WT_MayWrite(this, Reason))
    {
        Client_NotifySwitchResult(Target, false, Reason);
        return;
    }

    PrioritySwitch->SetPriority(Priority);

    WT_LOGF(TEXT("Switch '%s' priority -> %d"), *PrioritySwitch->GetName(), Priority);
    // 0 or less has a specific in-game meaning worth surfacing: the switch is exempt from the
    // automatic load shedding that a power shortage triggers.
    Client_NotifySwitchResult(Target, true, Priority > 0
        ? FText::Format(WT_LOCTEXT("Msg_PrioritySet", "Priority set to {0}"), FText::AsNumber(Priority))
        : WT_LOCTEXT("Msg_NoAutoLimit", "Set to never auto-limit"));
}

void URemote_FactoryRCO::Server_AnnounceViewer_Implementation(bool bViewing, AFGBuildable* Terminal)
{
    WT_ZONE_COARSE(TEXT("RCO.AnnounceViewer"));

    if (!WT_HasAuthority(this))
    {
        return;
    }

    AFGPlayerController* PC = GetOwnerPlayerController();
    if (!PC)
    {
        return;
    }

    // The shared-session store lives on the game state so every player's RCO can read it; the
    // server is the only writer. bViewing=false is the panel closing (NativeDestruct), which is why
    // this is a single "announce" call rather than separate open/close RPCs.
    Remote_FactoryShared::SetViewer(GetWorld(), PC, bViewing, Terminal);

    WT_LOGF(TEXT("Viewer announce: %s viewing=%s"), *PC->GetName(), bViewing ? TEXT("yes") : TEXT("no"));
}

void URemote_FactoryRCO::Client_NotifySwitchResult_Implementation(AFGBuildable* Target, bool bSuccess, const FText& Message)
{
    OnSwitchResult.Broadcast(Target, bSuccess, Message);
}
