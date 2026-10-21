#include "Remote_FactoryEquipment.h"

#include "Remote_FactoryLoc.h"
#include "Remote_FactoryLog.h"
#include "Remote_FactoryCrashTrace.h"
#include "Remote_FactoryPanelWidget.h"

#include "Resources/FGEquipmentDescriptor.h"   // declared under Resources/, not Equipment/
#include "FGCharacterPlayer.h"
#include "FGAnimPlayer.h"          // ★0.2.87 3P 举持姿势诊断：UFGAnimPlayer::mArmSlotType
#include "FGPlayerController.h"
#include "GameFramework/PlayerController.h"   // ★v0.2.75b GetPlayerViewPoint（显式包含，不依赖 FGPlayerController.h 传递）
#include "Equipment/FGBuildGun.h"          // ★v0.2.48 建造模式守卫：AFGBuildGun::IsActive()
#include "ItemAmount.h"
#include "Resources/FGItemDescriptor.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"          // ★0.2.48 TObjectIterator（不靠 EngineUtils.h 间接包含）
#include "Engine/Texture2D.h"
#include "Misc/Paths.h"                       // ★0.2.142 FPaths::ProjectModsDir / Combine（运行时定位 mod 目录 logo 文件）
#include "Materials/MaterialInstanceDynamic.h"   // ★0.2.142 终端屏幕 logo：克隆屏幕材质为 MID
#include "Materials/MaterialInstance.h"           // ★0.2.142 UMaterialInstance::TextureParameterValues（public，运行时枚举屏幕材质真实纹理参数名）
#include "Materials/MaterialInterface.h"          // ★0.2.144 GetAllTextureParameterInfo/GetAllVectorParameterInfo（runtime 枚举，UMaterial/MIC 通吃）+ FMaterialParameterInfo
#include "Materials/Material.h"                     // ★0.2.145 自烤白底屏材质 M_ScreenLogo：LoadObject<UMaterial>
#include "Kismet/KismetRenderingLibrary.h"        // ★0.2.142 ImportFileAsTexture2D（运行时从 mod 目录加载 logo 贴图，无需 cook）
#include "Engine/GameViewportClient.h"   // ★v0.2.50：EMouseLockMode / FInputModeGameAndUI::SetLockMouseToViewportBehavior
#include "TimerManager.h"          // ★0.2.48 面板绑定延时重试（FTimerHandle / SetTimer）
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"   // ★0.2.59 第一人称 viewmodel
#include "GameFramework/SpringArmComponent.h"   // ★0.2.108 头变大修复：快照/恢复 TargetArmLength
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "ReferenceSkeleton.h"
#include "Animation/AnimInstance.h"    // ★0.2.89 3P 举持：PlaySlotAnimationAsDynamicMontage / StopSlotAnimation
#include "Animation/AnimMontage.h"     // ★0.2.89 UAnimMontage::SlotAnimTracks / FSlotAnimationTrack::SlotName
#include "Animation/AnimSequence.h"    // ★0.2.89 UAnimSequence::AdditiveAnimType 诊断
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"        // ★0.2.115 rf.3pArmSlot 运行时开关（FAutoConsoleVariableRef）
#include "UI/FGGameUI.h"

// ★v0.2.12 右键开面板的 Enhanced Input 三件套
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputActionValue.h"

/**
 * One line of the handheld's crafting cost. This file's own copy: Remote_FactoryRegistry.cpp has
 * an identical struct with file-local linkage, and sharing it would mean exporting a helper for a
 * three-field POD.
 */
struct FWTHandheldCostLine
{
    const TCHAR* ItemPath;
    int32 Amount;
};

// ★v0.2.11 原版扫描仪装备的挂接 socket（ApplyHandheldHandMesh 反射读 mAttachSocket 后写入；Equip 优先用它）。
static FName GWT_HandAttachSocket;

// ★v0.2.19 实例级兜底：CDO 模板修改对生成实例不生效时（默认子对象复制时机问题），挂接时直接重设。
static UObject* GWT_HandMeshAsset = nullptr;   // USkeletalMesh*
static UObject* GWT_HandAnimClass = nullptr;   // UClass*（BPA_ObjectScanner_C）
static UClass* GWT_TempAnimClass = nullptr;     // ★0.2.48 G：来自另一骨骼件的 AnimClass 回落值

// ★0.2.41 原版扫描仪装备类（钉死）：贴图对齐要在**挂接时**再抄一遍材质 override，
//   所以把类缓存住（CDO 及其组件随类存活）。成功才锁定 + AddToRoot（0.2.38 纪律）。
static UClass* GWT_HandVanillaEquipClass = nullptr;

// ★0.2.89 原版角色蒙太奇使用的 Slot 节点名（从原版 CDO mEquipMontage.Montage_3P 资产反射读出）。
//   角色 3P 动画图里必有同名 Slot 节点（原版蒙太奇要能播），我方 3P 举持动态蒙太奇必须落进同一
//   Slot 才会被图采样；无值时回退 UE 标准 DefaultSlot。
static FName GWT_3pSlotName;

// ★0.2.93 原版扫描仪的装备（拿出）动画蒙太奇（同一反射读取点保存：mEquipMontage.Montage_3P）。
//   Equip 时播在角色 3P 网格 AnimInstance 上（UpperBody slot），播完 slot 空闲，
//   EnforceArmPoseCache 的 IsSlotActive 门控自动接上 idle 循环。
static UAnimMontage* GWT_EquipMontage3p = nullptr;

// ★0.2.109 「3P 切装备瞬间头变大」二期：0.2.108 实测无效且日志实锤 Equip 内臂长零变化
//   （"spring arm" 日志 0 条 → 触发点不在 Equip，群友同判）。改走群友新思路：BeginPlay 早赋值
//   + 两处判 >0 + 每帧低水位守卫（触发时机未知 → 守卫不依赖触发点）。GWT_GoodArmLen=「好臂长」
//   基线（BeginPlay/Equip 快照处捕获，>1 才记）；GWT_LastArmLen=上一帧臂长，用于抓「3P 下
//   一帧内暴跌 >100」的异常（native 平滑过渡是每帧小步长，不会误伤）。
static float GWT_GoodArmLen = 0.f;
static float GWT_LastArmLen = 0.f;
// ★0.2.111 骨骼 scale 扫描一次性去重（Equip 时清空，每次装备重新记录新命中）。
static TArray<int32> GWT_ScaleHit3P;
static TArray<int32> GWT_ScaleHitEquip;
static TArray<int32> GWT_ScaleHit1P;   // ★0.2.113 群友 leader-pose 假设验证通道
// ★0.2.116 群友仪表（"别猜了，打 log"）：每 1s 采样一行臂长+相机世界坐标+FOV+3P 骨骼最大 scale；
//   装备事件后 3s 内加密为每 0.1s 一行——「那一瞬数值掉下去了就查谁动的，没掉就是画面骗你」。
//   maxScale 不走去重（bonescale 数组只记首次，看不出恢复/持续），每行都打当前真实值。
static float GWT_CamWatchAcc       = 0.f;   // 1Hz 采样累加器（秒）
static float GWT_CamWatchBurstAcc  = 0.f;   // 突发采样累加器（秒）
static float GWT_CamWatchBurstLeft = 0.f;   // 突发窗口剩余秒数（Equip 置 3.0）

// ★0.2.114 实验开关：停用 3P 动态举持蒙太奇（直驱角色身体网格 GetMesh3P，蒙太奇 scale 沿骨骼层级
//   累乘=「头变大/左臂锥形」真凶；0.2.113 只停了 3P 装备蒙太奇，漏了 3P 举持/待机蒙太奇）。
//   ★0.2.114 实测推翻：停用后 char3P 骨骼 scale 仍逐帧累乘（1.06→1.30→1024×）→ 动态蒙太奇非真凶。
// ★0.2.115 真凶定位（三轮排除+一手资料）：泵 = AnimBP 的 **AE_Generic1Hand 分支消费非空
//   mIdlePoseAnimation3p** 的未验证组合。证据链：
//   ① 0.2.90~0.2.104 强写 3P 臂槽=AE_ObjectScanner(10) + 注入 idle3p=ObjectScannerIdle_01
//      → 十几轮实测：抬臂正常、零变形报告；
//   ② 0.2.105 把强写改为 AE_Generic1Hand(15) 后，0.2.107 即报「头变大」；
//   ③ 0.2.113（停装备蒙太奇）/0.2.114（停 3P 动态举持蒙太奇）两轮排除后 char3P 仍累乘
//      → 剩余驱动只有 native AnimBP 路径（臂槽分支 + idle3p 消费）；
//   ④ 一手资料 DumpedObjects/Equip_ObjectScanner_C：原版扫描仪 mArmAnimation=AE_None、
//      mIdlePoseAnimation(3p) 均空 → 原版没有任何设备同时具备「Generic1Hand 分支 + 非空 idle3p」，
//      该组合从无原版验证；而「scanner 分支 + 同一资产」= 原版扫描仪同款消费路径，长期干净。
//   修法：3P 臂槽每帧强写回 AE_ObjectScanner(10)（下方 EnforceArmPoseCache 处）。
//   运行时开关 rf.3pArmSlot（控制台，改完重装卸装备即生效，本写每帧读 CVar 实时值）：
//   -1=不强写交还 native；0=AE_None；10=AE_ObjectScanner（默认，原版扫描仪精确复刻）；
//   15=AE_Generic1Hand（=0.2.105~0.2.114 的已知变形态，留作 A/B 复现）。
static int32 GWT_3pArmSlotOverride = 10;   // ★0.2.120 默认恢复常开(10)：0.2.119 本局日志实锤，每次 Equip 时 native
                                           //   会在装备蒙太奇窗口把 3P 缓存瞬态直写为 15（[pose] 3P armSlot now 15 与
                                           //   equip 同帧），叠加注入的 mIdlePoseAnimation3p → AnimBP Generic1Hand 分支
                                           //   逐帧累乘骨骼 scale（×1.82/层级，Head 20×、手指 400×，本局 2667 条命中、
                                           //   全部紧跟 Equip 时间戳）——「头变大/左臂锥形」在 3P 每次切装备都复发。
                                           //   0.2.117 判「强写无效」的前提（源头=15、native 每帧从源头重缓存冲掉强写）
                                           //   已不成立：现在源头=10，native 自身重缓存也是 10，本强写只纠正装备瞬态。
                                           //   时序实锤：瞬态 15 写在我方 Tick 之前（同 tick 观察日志已见 15），
                                           //   动画求值在组件 tick（晚于 actor Tick）→ 我方保底写先于求值生效，泵无评估机会。
                                           //   稳态下 inst=cache=10，本写为同值 no-op，零回归。
static FAutoConsoleVariableRef CVarRF3pArmSlot(
    TEXT("rf.3pArmSlot"),
    GWT_3pArmSlotOverride,
    TEXT("Remote_Factory 3P AnimInstance arm-slot override: 10=AE_ObjectScanner (default, corrects native equip-transient 15 that pumps bone scale), -1=leave native, 0=AE_None, 15=AE_Generic1Hand (known-deform A/B state)."),
    ECVF_Default);

// ★0.2.117 「只修 1P」：3P 侧整体回退到 0.2.104（用户长期实测 3P 正常的版本）的 mArmAnimation 取值
//   AE_ObjectScanner(10)；1P 侧 0.2.107 的全部修复（1P 蒙太奇 + 1P idle 姿势注入）原样保留。
//   关键认知修正（0.2.115 失效根因）：0.2.115 强写的是 **AnimInstance 缓存** Player->mArmSlotType，
//   而 UFGAnimPlayer::CacheArmSlotData（FGAnimPlayer.h:24）每帧从 **装备实例** 的 mArmAnimation
//   （0.2.105 起为 15）重新缓存 → 我方强写被原生每帧冲掉，实测等价于从未改成 10。
//   故本轮换改 **源头**：ctor 写入 + EnforceArmPoseCache 每帧保底写（防其它代码改回）。
//   -1=不写（保留 ctor 值）；0=AE_None；10=AE_ObjectScanner（默认，0.2.104 的 3P 正常态）；15=复现变形态。
static int32 GWT_ArmAnimOverride = 10;
static FAutoConsoleVariableRef CVarRFArmAnim(
    TEXT("rf.armAnim"),
    GWT_ArmAnimOverride,
    TEXT("Remote_Factory equipment mArmAnimation source: -1=leave as-is, 0=AE_None, 10=AE_ObjectScanner (default=0.2.104 clean 3P state), 15=AE_Generic1Hand (known-deform A/B state)."),
    ECVF_Default);
// 1P 视角专用覆盖（mArmAnimation 是单属性、1P/3P 共用；>=0 时在 1P 视角按此值写）。
// ★0.2.118 默认改 15：0.2.117 实测（日志+截图）证明 **1P 设备的卡手 socket 模式依赖
//   armSlot=15 的 Generic1Hand 抬臂姿势**——armSlot=15 时 [1p] dot=0.87/mode=socket（贴手），
//   armSlot=10 时 dot≈0/mode=camera-fallback（设备掉到相机下方 u=-112，即用户截图
//   「有切换动画但挂在这」：只有切装备蒙太奇播放的 0.7s 内 armSlot=15，播完即垂）。
//   1P 期源头=15 对 3P 无害（0.2.116 仪表实测：1P 期 char3P maxScale=1.000，3P 身体
//   动画仅在 camMode=2 评估，而切到 3P 首帧我方即写回 10）。
static int32 GWT_ArmAnim1POverride = 15;
static FAutoConsoleVariableRef CVarRFArmAnim1P(
    TEXT("rf.armAnim1P"),
    GWT_ArmAnim1POverride,
    TEXT("Remote_Factory 1P per-view mArmAnimation override: 15=AE_Generic1Hand (default, required for 1P socket-hold arm pose), -1=use global rf.armAnim, 0/10=other enums."),
    ECVF_Default);
// ★0.2.115 臂槽留痕去重（变化+2s 限频，见 EnforceArmPoseCache）。
static int32 GWT_LastLogged3pSlot = -999;
static double GWT_LastSlotLogSec = 0.0;
// ★0.2.119【修复】1P 手臂网格 **自身** AnimInstance 的臂槽缓存直写。
//   0.2.118 失效根因（日志实证，非推测）：源头写块的条件是 `Player && SlotEquip == this`，
//   而 Player = Cast<UFGAnimPlayer>(**Character->GetMesh3P()**->GetAnimInstance())——
//   1P 视角下 3P 身体网格被停更、该 AnimInstance 为 null ⇒ 整块跳过 ⇒ mArmAnimation 恒为
//   ctor 的 10 ⇒ native 缓存 armSlot=10 ⇒ 1P 手臂垂 ⇒ 设备走 camera-fallback 飘相机下方。
//   佐证：0.2.118 局 `[pose] mArmAnimation now` 留痕 **0 条**（值从未被写过）。
//   ⇒ 本块改为直写 **1P 手臂网格的缓存**（[1p] 判定读的正是它），完全不依赖 3P 侧。
//   15=AE_Generic1Hand（单手握持通路，群友确认站立走它没问题）；-1=不写（观察 native 原值）。
static int32 GWT_1pArmSlotOverride = 15;
static FAutoConsoleVariableRef CVarRF1pArmSlot(
    TEXT("rf.1pArmSlot"),
    GWT_1pArmSlotOverride,
    TEXT("Remote_Factory 1P arm-mesh AnimInstance mArmSlotType override: 15=AE_Generic1Hand (default, 1P socket-hold arm pose), -1=leave native value, 0/10=other enums."),
    ECVF_Default);
// ★0.2.119 一发仪表：见证「源头值 / CDO 值 / 1P 缓存值 / 3P AnimInstance 是否为 null」。
static int32 GWT_LastLogged1pSlot = -999;
static double GWT_Last1pSlotLogSec = 0.0;
static int32 GWT_LastCdoSyncArmAnim = -999;
static int32 GWT_ArmTraceInst = -999;
static int32 GWT_ArmTraceC1P = -999;
static double GWT_ArmTraceSec = 0.0;
// ★0.2.117 默认**恢复** 3P 动态举持蒙太奇：0.2.114 曾因误判真凶而停用，但 0.2.115 实测证明
//   停用后 char3P 仍逐帧累乘 → 它不是变形源。且它就是 0.2.104（3P 正常版本）的 3P 抬臂驱动器
//   ——0.2.105 的注释实证「0.2.96~0.2.104 六轮实测 armSlot=10 而臂永不抬」，即臂槽 10 本身
//   不抬臂，抬臂全靠本蒙太奇（0.2.89 引入 / 0.2.93 改 slot 状态驱动）。故 0.2.117 组合=
//   mArmAnimation=10（不撑爆）+ 本蒙太奇（抬臂）= 精确复刻 0.2.104 的 3P 行为。
//   1=启用（默认）；0=停用（若实测仍有变形可控制台一键关，无需重编译）。
// ★0.2.122 默认回 1（0.2.121 的 A/B 实测证伪了「native 属性通路自己出 pose」假设：
//   rf.3pDynamicIdle=0 时 3P cache3P=10 但直接垂臂——0.2.104 的老实测仍是最终事实：
//   臂槽 10 本身不抬臂，本动态蒙太奇就是唯一抬臂驱动器）。保留 0.2.121 的源头边沿写
//   （edge-write 全程仅 4 条，equip 节奏肉眼正常）；乘法叠加的 6/84 轻微爆发用户已接受。
//   历史注记（0.2.121）：蒙太奇每重建一次 slot 混合把骨骼 scale 乘一遍（手指链 key 细
//   → thumb/ring/pinky 全中）——这个机制描述与日志吻合，但代价可接受、抬臂不可丢。
static int32 GWT_Use3pDynamicIdle = 1;
static FAutoConsoleVariableRef CVarRF3pDynamicIdle(
    TEXT("rf.3pDynamicIdle"),
    GWT_Use3pDynamicIdle,
    TEXT("Remote_Factory 3P dynamic idle montage (the ONLY 3P hold-arm driver, 0.2.104 verified; setting 0 drops the arm): 1=on (default), 0=off."),
    ECVF_Default);
static bool GWT_3pDynamicIdleLog = false;
// ★0.2.120 扫描仪网格连续压制的留痕限频（5s 一条，防 native 每次装备重新点亮时刷屏）。
static double GWT_LastScanMeshRehideSec = 0.0;
// ★0.2.121 源头写边沿门控（群友 2026-10-19 处方①）：mArmAnimation 只在「视角切换/本装备
//   重新获得槽位」的沿上写一次，不再每帧保底。机制（群友模型，与本局日志吻合）：
//   native 装备蒙太奇窗口会把源头翻 15 → 我方下一帧写回 10 → 这个「写回」本身戳 native
//   重跑装备链路（[equip] path/unequip 一秒两三次地抖）→ 蒙太奇跟着重建 → scale 乘法叠加。
//   边沿写让 native 稳定在自己写的值上，装备链路只跑一次；视角没变我方绝不插手。
static int32 GWT_LastSrcWriteB1P = -1;      // 上次写源头时的视角（-1=从未写过 ⇒ 首帧必写）
static bool  GWT_LastSlotHadThis = false;   // 上次边沿判定时臂槽是否为本装备（重装备沿）
static int32 GWT_LastAppliedWant = -999;    // 上次沿上应用的目标枚举值（CVar 变化沿：
                                            //   0.2.121 边沿化的代价是 CVar 改了不再每帧生效，
                                            //   把「应用值变化」也并入沿判定保住调试即时性）

// ★0.2.123 DeepSeek 处方③（放大二分开关）：mIdlePoseAnimation3p 属性注入门。
//   泵配方 =「Generic1Hand(15) + 注入非空 mIdlePoseAnimation3p」（0.2.105~0.2.116 六轮实锤）。
//   置 0 跑一局：仍放大 ⇒ 两套 arm pose 在乘；不放大 ⇒ ObjectScannerIdle_01 自带 scale track。
//   只门控**属性注入**两处（ctor 实例写 + 加载期 CDO 写），GWT_Idle3pAsset 静态保留 ——
//   蒙太奇通路（rf.3pDynamicIdle）与手持建模完全不受影响，A/B 一次只动这一个变量。
static int32 GWT_InjectIdle3p = 1;
static FAutoConsoleVariableRef CVarRFInjectIdle3p(
    TEXT("rf.injectIdle3p"),
    GWT_InjectIdle3p,
    TEXT("Remote_Factory mIdlePoseAnimation3p property-injection gate: 1=inject (default), 0=skip for scale bisect (persistent enlargement at 0 => two arm poses multiplying; gone => ObjectScannerIdle_01 carries its own scale track)."),
    ECVF_Default);

// ★0.2.107 原版 1P 拿出动画蒙太奇（同点保存：mEquipMontage.Montage_1P，FGEquipment.h:115
//   "Montage to play on the character in 1P" = 播在角色 1P 手臂网格 GetMesh1P()==GetMesh()）。
//   群友实锤（2026-10-19）：切装备就是先 Unequip 再 Equip、各播各的 montage；1P 手臂 AnimBP
//   不换状态 —— 蒙太奇是 1P 唯一插播通路（与 0.2.106「1P 退出 idle 蒙太奇、抬臂交状态机」不冲突：
//   本蒙太奇是 Equip 瞬间一次性插播，播完即止，不与状态机长期争 slot）。收起方向无需动作：
//   原版 FEquipmentUnEquipMontage 只含音效（FGEquipment.h:87-102），收起=停蒙太奇，天然对齐。
static UAnimMontage* GWT_EquipMontage1p = nullptr;

// ★0.2.107 1P 蒙太奇资产自带的 Slot 节点名（同 0.2.89 的 GWT_3pSlotName 逻辑，规避
//   「slot 没接上」风险：用资产真实值，不猜）。
static FName GWT_1pSlotName;

/**
 * ★0.2.41 把原版扫描仪**组件**上的材质 override 逐槽抄到我们的 HandMesh。
 * 为什么：白模修复（0.2.36）实证过 —— 原版的材质经常不挂在网格资产上，而是蓝图在组件上做的
 * override；只抄网格不抄组件材质，手里那台的贴图就可能不对（用户实测「无线终端贴图没改」）。
 * 槽位上原版没 override 时 GetMaterial 返回网格资产自带材质，抄过去 = 与原版完全同图，无副作用。
 */
static void WT_CopyVanillaHandMaterials(USkeletalMeshComponent* Dst)
{
    if (!Dst || !GWT_HandVanillaEquipClass)
    {
        return;
    }
    AFGEquipment* VanillaCDO = Cast<AFGEquipment>(GWT_HandVanillaEquipClass->GetDefaultObject());
    if (!VanillaCDO)
    {
        return;
    }
    USkeletalMeshComponent* Src = Cast<USkeletalMeshComponent>(
        VanillaCDO->GetComponentByClass(USkeletalMeshComponent::StaticClass()));
    if (!Src)
    {
        return;
    }
    int32 Copied = 0;
    const int32 NumSlots = Dst->GetNumMaterials();
    for (int32 i = 0; i < NumSlots; ++i)
    {
        if (UMaterialInterface* M = Src->GetMaterial(i))
        {
            Dst->SetMaterial(i, M);
            ++Copied;
        }
    }
    WT_WARNF(TEXT("[equip] copied %d/%d material slot(s) from vanilla scanner component."),
             Copied, Src->GetNumMaterials());
}

// ★v0.2.21 从原版组件拷来的相对变换。挂接后**显式回写**，不再依赖 SnapToTarget 的隐式保留
// （v0.2.10-0.2.20 只依赖隐式保留；一旦引擎版本/挂接规则有差异就会歪掉且看不出来）。
static FVector GWT_HandRelLoc = FVector::ZeroVector;
static FRotator GWT_HandRelRot = FRotator::ZeroRotator;
static FVector GWT_HandRelScale = FVector::OneVector;

// ★修复 ①（接手调查，2026-10-18）：当时结论"hand_rSocket 在 1P 下距相机 ~94m"系**误诊**
// （证据取自 01:03 UTC 旧包日志的 detach-at-origin 旧 bug 现场）；15:39 现行日志实锤
// CharacterMesh0 的 hand_rSocket 就在相机前 45.8cm 且 rendered=1。当日改挂 ViewModelAnchor
// 作为过渡（Tick 每帧摆位），存在落后一帧的固有小滞后（奔跑时可见模型拖尾漂移）。
// ★v0.2.75d（2026-10-18）：回归原版一手链路。DumpedObjects/Equip_ObjectScanner_C 实锤
// mAttachSocket=hand_rSocket；AFGCharacterPlayer::GetMesh1P() 为 public UFUNCTION
// （FGCharacterPlayer.gen.cpp:5315 实锤）。原版 1P 持握=装备挂 1P 手臂网格（Mesh1P）的
// hand_rSocket——手臂动画/奔跑摆动全在 Mesh1P 上，挂它=与原版同帧同步零滞后；
// 相机锚点每帧 Tick 摆位落后一帧，奔跑时模型拖尾漂到右下角变小（15:43 截图实测）。
// v0.2.50 注释「挂 1P 网格 socket 失败」用的是错误 socket 名（hand_r 系别名），
// 正名 hand_rSocket 来自本次转储。GWT_HandRel* 语义本就是"相对手部插槽"，插槽链路下原样套用。
// 相机锚点降级为兜底（Mesh1P/socket 缺失时），保证建模在任何情况下不消失。
// ★v0.2.75c（用户定案「原版扫描仪的尺寸就可以了」）：缩放=GWT_HandRelScale（从原版扫描仪
// 组件拷来的握持缩放，用户实测认可该尺寸）。
// ★0.2.90 相机前方锚点常量（GWT_HandViewModelLoc/Rot）退役：1P 下把全身大设备摆在相机前 36cm
// 处糊满整个屏幕（0.2.89 实测截图）。1P/3P 统一走「手部 socket 跟随」（见 UpdateViewModelTransform），
// 与原版扫描仪同机制。原址改放 0.2.90 新增的待机姿势资产静态：
// ★0.2.90 实例姿势传导用：加载期从原版 CDO 反射读出的待机姿势序列。ctor 用它直写实例属性——
// 运行时改 CDO 不会自动传导到 SpawnActor 出来的实例（0.2.84 HandMesh 无网格同机制），0.2.89
// 实测实例侧 GetIdlePoseAnimation3p()==null → 3P 播放块静默跳过 → 手臂不举。
static UAnimSequence* GWT_Idle1pAsset = nullptr;
static UAnimSequence* GWT_Idle3pAsset = nullptr;

// ★v0.2.21 诊断探针：和 HandMesh 挂同一 socket 的静态网格实体，用来二分"手上无建模"的病灶。
// ★0.2.37（用户定案「把终端做成原版扫描仪吧，这样不用去改来改去的了」）：探针**永久关闭** ——
// 手里的立方体是诊断工具不是产品外观；终版外观 = 原版扫描仪网格 + 原版动画 + 原版相对变换。
// ★★0.2.42 临时**重新打开**：用户 0.2.41 实测「手持无终端建模」，而挂接诊断三条全绿
//   （mesh/socket/vis/reg/mats 全对）—— 需要一次二分：手里有立方体 = 挂接链没问题（问题在网格
//   资产/材质/动画），手里连立方体都没有 = 挂接/可见性链断了。
//   ★★0.2.48（用户定案「无线终端的建模和贴图统一采用原版扫描仪建模和贴图」）：**永久关闭**。
//   现场日志（0.2.46 实测）三条全绿 —— vanilla scanner equip class loaded / mesh=SK_ObjectScanner_01
//   anim=BPA_ObjectScanner / copied 3/3 material slot(s) —— 而 hand-marker probe mesh loaded:
//   /Engine/BasicShapes/Cube.Cube 说明玩家手里**同时**看得见扫描仪和一个立方体 ⇒ 与「统一采用
//   原版扫描仪建模/贴图」直接冲突。关掉后手里 = 纯原版扫描仪（网格 + 动画 + 3/3 原版材质）。
//   保留代码路径（不创建组件即零成本），仅作为将来再次二分问题时的开关。
static constexpr bool kDebugHandMarker = false;
static UObject* GWT_MarkerMeshAsset = nullptr;   // UStaticMesh*

// ★v0.2.12 右键开面板：运行时自建 InputAction + InputMappingContext（mod 不带 .uasset，只能 new 出来）。
// AddToRoot 钉死防 GC；跨装备实例共享（全局一次创建，挂接按实例做）。
static UObject* GWT_ToggleAction = nullptr;   // UInputAction*
static UObject* GWT_ToggleIMC = nullptr;      // UInputMappingContext*

// ----------------------------------------------------------------------------
// Equipment actor
// ----------------------------------------------------------------------------
ARemote_FactoryEquipment::ARemote_FactoryEquipment()
{
    // Everything the base ctor sets up (slot, socket, animation, mapping context) comes from the
    // class defaults; we only override the fields that have no sensible default for a hand item
    // that is pure UI.
    //
    // ES_ARMS puts it in a hand slot so holding it is mutually exclusive with the scanner /
    // chainsaw, which is what a player expects from "the thing in my hand".
    mEquipmentSlot = EEquipmentSlot::ES_ARMS;

    // ★v0.2.80 走原生 Equip 链路的关键前置：mAttachSocket（FGEquipment.h:554
    // "If this equipment should attach to a socket, this is the socket"）是 protected
    // UPROPERTY，原生 Super::Equip 用它把整 actor 挂到角色手 socket（原版两级挂接结构
    // 第一级）。我们此前从未赋值——代码里只反射"读原版扫描仪 CDO 的值"存静态
    // GWT_HandAttachSocket 供手写挂接用，本实例字段一直是空 FName。空 socket 名正是
    // 0.2.76 调 Super::Equip 崩溃 0x170（FGEquipment.cpp:347）的头号根因：原生挂接拿空
    // socket 名找挂点 → null 解引用。此处按原版 Equip_ObjectScanner_C 一手转储实测值
    // （=hand_rSocket，与 ApplyHandheldHandMesh 拷到的 GWT_HandAttachSocket 同值）反射写入
    // （protected 走 FNameProperty 反射写，同 mPlayerCharacter 的 FObjectProperty 手法）。
    if (FNameProperty* P = FindFProperty<FNameProperty>(
            AFGEquipment::StaticClass(), TEXT("mAttachSocket")))
    {
        P->SetPropertyValue(P->ContainerPtrToValuePtr<void>(this), FName(TEXT("hand_rSocket")));
    }

    // ★0.2.105 修 1P 抬臂：改走 AE_Generic1Hand 通路。0.2.85 曾定 AE_ObjectScanner——但一手 dump
    // 证明原版扫描仪 BP 的 mArmAnimation 本就是 AE_None（scanner-accurate），且 0.2.96~0.2.104
    // 六轮实测 armSlot=10 而臂永不抬；唯一被实测证明能原生抬臂的是 armSlot=15（AE_Generic1Hand，
    // 喷气飞行时原版 native 自己强写，dot=0.88/mode=socket/设备贴手，用户图3确认）。站立态改用
    // 同一状态分支=单手握持，正是手持终端的天然姿势。生效链路（SDK 头一手）：FGEquipment.h:286
    // GetArmsAnimation() → UFGAnimPlayer::CacheArmSlotData()（1P/3P 网格各一份 anim 实例）→
    // mArmSlotType → 动画图选手臂姿势（CharacterAnimationTypes.h:27）；生效前提
    // mEquipmentSlot==ES_ARMS（ctor 上方已设）。AE_None=空手垂臂（3P 设备跟随垂手掉到胯部）。
    //
    // ★0.2.117 「只修 1P」回退：本值改回 AE_ObjectScanner(10) = 0.2.104（3P 正常版本）的取值。
    //   理由（0.2.115 实测反证）：0.2.115 每帧强写 AnimInstance 缓存 armSlot=10 后，3P 骨骼 scale
    //   仍 1 秒内飙到 1024 —— 因 CacheArmSlotData 每帧从**本实例字段**重缓存，缓存强写被冲掉，
    //   真正生效的一直是这里的 15。故本轮直接改源头，3P 回到长期实测干净的组合
    //   （armSlot=10 + 注入 ObjectScannerIdle_01）。1P 侧不受影响：0.2.107 的 1P 蒙太奇播在
    //   GetMesh1P() 手臂网格上，与臂槽状态机互不干扰；若实测 1P 需要抬臂，控制台 rf.armAnim1P 15。
    //   mArmAnimation 是 protected（FGEquipment.h:578），派生类可直写；protected 段=546 起。
    //   钳制到 EArmEquipment 有效域：上界直接取枚举末位 AE_BoomBox（CharacterAnimationTypes.h，
    //   共 18 个成员 = 0..17），越界的 CVar 值一律忽略、绝不写入非法枚举（static_cast<uint8>
    //   会把 999 变成 231，AnimBP 将读到未定义枚举值——★深度自检①穷举抓出的真缺陷，已修）。
    mArmAnimation = (GWT_ArmAnimOverride >= 0 &&
                     GWT_ArmAnimOverride <= static_cast<int32>(EArmEquipment::AE_BoomBox))
        ? static_cast<EArmEquipment>(static_cast<uint8>(GWT_ArmAnimOverride))
        : EArmEquipment::AE_ObjectScanner;

    // Nothing is consumed and nothing is held down: this item has no primary/secondary fire
    // behaviour at all, so no default equipment action is enabled.
    mDefaultEquipmentActions = 0;

    // The panel is opened by Equip()/UnEquip(), not by an Enhanced Input action, so the default
    // equipment mapping context is not needed. Leaving it off also avoids the base class trying to
    // apply a null mapping context.
    mNeedsDefaultEquipmentMappingContext = false;

    // Cost-free to use. The item is already consumed by the crafting recipe; charging on every
    // equip would be a second, invisible tax.
    mCostToUse.Empty();

    PrimaryActorTick.bCanEverTick = true;   // ★修复 ①：每帧把相机跟随锚点摆到玩家相机前
    // v0.2.9: 不再调用 SetReplicates（对未初始化 actor 调它会有 Warning；直接写成员与原版 ctor 同款）。
    // 装备按设计是 client-local（见头文件网络边界注释），不参与复制。
    bReplicates = false;

    // ★v0.2.10 手持网格组件。★v0.2.21 结构改为与原版装备完全一致：
    //   原版 Equip_ObjectScanner_C 的组件树（游戏日志 LogMarcioCommonLibs dump）=
    //     根 = 纯 SceneComponent (loc 0,0,0)
    //     ├─ SkeletalMeshComponent  (rel rot Y=-90, scale 1, Anim=BPA_ObjectScanner)
    //     └─ PointLightComponent
    //   v0.2.10-0.2.20 我们把网格**直接当根**；游戏若在装备 actor 的根上做可见性/挂接
    //   操作就会连带影响我们。改成"纯根 + 子网格"后与原版同构。
    // 资产在 RegisterHandheld 里运行时写入 CDO 模板，实例在 Equip() 时挂到角色右手 socket。
    // 网格加载失败只是手上没模型，绝不影响面板功能。
    USceneComponent* EquipRoot = CreateDefaultSubobject<USceneComponent>(TEXT("WTEquipRoot"));
    SetRootComponent(EquipRoot);

    // ★0.2.86 结构修正（修复「3P 设备掉胯部」根因）：先建 ViewModelAnchor（手持件定位句柄），
    // 再让 HandMesh 挂它之下。HandMesh 必须作为 ViewModelAnchor 的子组件，UpdateViewModelTransform
    // 每帧用世界变换把 ViewModelAnchor 摆到「1P 相机前 / 3P 角色手部 socket」，HandMesh 才随之正确就位。
    // 此前 HandMesh 误挂 EquipRoot（与 ViewModelAnchor 成兄弟），导致 UpdateViewModelTransform 开头
    // 守卫 HandMesh->GetAttachParent()!=ViewModelAnchor 永远为真 → 整段函数从不执行；3P 下锚点停在
    // Super::Equip 把 actor 挂到的 socket（常落在胯部），设备随之掉胯部。现修正为 HandMesh⊂ViewModelAnchor，
    // 函数守卫通过、每帧世界变换定位生效，彻底绕开原生把 actor 挂到哪个 socket 的不确定性。
    ViewModelAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("WTViewModelAnchor"));
    ViewModelAnchor->SetupAttachment(EquipRoot);
    ViewModelAnchor->SetVisibility(true);

    HandMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WTHandMesh"));
    HandMesh->SetupAttachment(ViewModelAnchor);   // ★0.2.86 改挂 ViewModelAnchor（原 EquipRoot）
    HandMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HandMesh->SetCastShadow(false);          // 一人称手持件不投影（原版同款）
    HandMesh->SetOnlyOwnerSee(false);        // ★v0.2.21 不依赖 Owner 链：装备 actor 的 Owner 没接好时 OnlyOwnerSee 会谁都看不见
    HandMesh->SetBoundsScale(10.f);          // ★v0.2.21 防"包围盒被视锥裁掉"这一整类不可见（代价可忽略）
    HandMesh->SetVisibility(false);

    // ★v0.2.21 诊断探针（kDebugHandMarker 关闭时不创建）。
    if (kDebugHandMarker)
    {
        HandMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WTHandMarker"));
        HandMarker->SetupAttachment(EquipRoot);
        HandMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        HandMarker->SetCastShadow(false);
        HandMarker->SetOnlyOwnerSee(false);
        HandMarker->SetBoundsScale(10.f);
        HandMarker->SetVisibility(false);
    }

    // ★0.2.90 实例姿势传导：加载期 ApplyHandheldHandMesh 只把 mIdlePoseAnimation(3p) 反射写进 CDO，
    // 但 SpawnActor 出的实例不会拿到「运行时改过的 CDO 值」（0.2.84 HandMesh 无网格同机制，彼例已用
    // 实例兜底修复）——0.2.89 实测实例侧 GetIdlePoseAnimation3p()==null → 3P 播放块静默跳过 → 手臂不举。
    // 故 ctor 里从静态资产反射直写实例（★深度自检②抓错：FGEquipment.h:630/636 两属性是 **private**，
    // 派生类不可直写，必须同加载期一样走 FObjectProperty）。CDO 构造时静态为空（跳过），实例构造时静态已填。
    // ★0.2.123 挂 rf.injectIdle3p 门（处方③二分）：0 时不写实例属性，AnimBP 的
    //   mIdlePoseAnimation3p 分支拿 null → 该通路静默，用于区分放大来源。GWT_Idle3pAsset
    //   静态不动（蒙太奇通路仍可用），1P 属性注入与本开关无关。
    if (GWT_Idle3pAsset && GWT_InjectIdle3p != 0)
    {
        if (FObjectProperty* P = FindFProperty<FObjectProperty>(
                AFGEquipment::StaticClass(), TEXT("mIdlePoseAnimation3p")))
        {
            P->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(this), GWT_Idle3pAsset);
        }
    }
    if (GWT_Idle1pAsset)
    {
        if (FObjectProperty* P = FindFProperty<FObjectProperty>(
                AFGEquipment::StaticClass(), TEXT("mIdlePoseAnimation")))
        {
            P->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(this), GWT_Idle1pAsset);
        }
    }
}

FText ARemote_FactoryEquipment::GetEquipmentDisplayName()
{
    return WT_LOCTEXT("Equip_PortableTerminal", "智能工厂");
}

// ----------------------------------------------------------------------------
// ★0.2.75 「第一人称手臂网格怎么找」（v0.2.26）与「按视角选挂接目标」（v0.2.72）已整体废止。
// 历史结论"挂到 vis=0 的组件上是白挂"被 0.2.74 日志证伪：组件可见性不随挂接父级传递，
// 挂在 vis=0 的 CharacterMesh3P 上 @0.5s 仍 rendered=1。原版扫描仪机制（DumpedObjects/
// Equip_ObjectScanner_C 实据）= 整只 actor 一次性挂手 socket，从不按视角换挂、不反复重挂。
// 详细定案见 WT_ChooseHandheldTarget 处注释。
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// ★0.2.75 手持网格挂接目标选择器 —— 回归原版机制（0.2.74 日志 + 原版转储双铁证定案）。
//
// 原版扫描仪（DumpedObjects/Equip_ObjectScanner_C）只有 mAttachSocket=hand_rSocket 一个字段，
// 没有任何"按 1P/3P 换挂"逻辑：整只装备 actor 一次性挂上，两种视角通用。组件可见性不随
// 挂接父级传递（0.2.74 日志 17.21.16 段实锤：挂在 vis=0 的 CharacterMesh3P 上，@0.5s 仍
// rendered=1）。0.2.72 引入的"按视角选网格"与 0.2.74 引入的"相机前方过滤"都是对该机制的
// 误判：0.2.74 日志实测挂载目标在 CharacterMesh0/CharacterMesh3P 间狂跳、场景代理反复
// 销毁重建（proxy 0↔1、vis 0↔1、mats 0↔3），这才是"手持无建模"的真凶。
// v0.2.50 时代固定挂 GetMesh() 稳定可见，与原版机制一致。
// ⇒ 定案：永远挂 GetMesh()（0.2.26 枚举实锤 = CharacterMesh0，hand_rSocket 就在其骨架上，
//   0.2.74 日志 socketExists=1）；切视角不重挂（相机看门狗与 OnPostCameraModeChanged 重挂已删）。
// ----------------------------------------------------------------------------
static USkeletalMeshComponent* WT_ChooseHandheldTarget(AFGCharacterPlayer* Character, FName& OutSocket, bool& bOutGuessed)
{
    bOutGuessed = false;
    if (!Character)
    {
        return nullptr;
    }
    OutSocket = GWT_HandAttachSocket.IsNone() ? FName(TEXT("hand_rSocket")) : GWT_HandAttachSocket;

    USkeletalMeshComponent* Body = Character->GetMesh();
    if (Body && Body->DoesSocketExist(OutSocket))
    {
        return Body;
    }

    // 别名 socket 兜底（某些骨架把右手 socket 命名成别名）
    static const FName CandidateSockets[] = {
        TEXT("hand_r"), TEXT("Hand_r"), TEXT("handR"), TEXT("FP_Hand_R"), TEXT("hand_r_01"),
    };
    if (Body)
    {
        for (const FName S : CandidateSockets)
        {
            if (Body->DoesSocketExist(S))
            {
                OutSocket = S;
                return Body;
            }
        }
    }

    // 最后兜底：3P 网格上再找一次原版 socket（两副骨架同源，总有其一存在）。
    if (USkeletalMeshComponent* Mesh3P = Character->GetMesh3P())
    {
        if (Mesh3P != Body && Mesh3P->DoesSocketExist(OutSocket))
        {
            return Mesh3P;
        }
        for (const FName S : CandidateSockets)
        {
            if (Mesh3P != Body && Mesh3P->DoesSocketExist(S))
            {
                OutSocket = S;
                return Mesh3P;
            }
        }
    }

    // 连 socket 都没有：仍挂 GetMesh() 本身（宁可见错位也不消失），socket 名保持原样。
    bOutGuessed = true;
    OutSocket = GWT_HandAttachSocket.IsNone() ? FName(TEXT("hand_rSocket")) : GWT_HandAttachSocket;
    return Body ? Body : Character->GetMesh3P();
}

void ARemote_FactoryEquipment::Equip(AFGCharacterPlayer* Character)
{
    // ★v0.2.80 回归原生 Equip 链路（用户决策「走原版的 Equip 链路」，2026-10-18）。
    // 0.2.9 起绕开 Super::Equip 的理由（裸 C++ actor 崩溃 0x170）已定位根因 = mAttachSocket
    // 空 FName（ctor 现已反射写入 hand_rSocket）；RecipeCopier（同构纯 C++ AFGEquipment 子类、
    // 装备只调 Super::Equip）长期稳定实证该链路对纯 C++ 可用。原生链路接管全部装备职责：
    //   ① mPlayerCharacter/Instigator 绑定；② 整 actor 挂 hand_rSocket（两级挂接结构）；
    //   ③ 装备可见性（SetEquipmentVisibility + 角色 UpdateEquipmentVisibility 自管，
    //      行走/飞行/1P/3P 行为与原版扫描仪完全一致）；④ 相机模式材质切换。
    // 0.2.78-0.2.79 的间歇隐身与地上孤儿，正是手写挂接/看门狗与原生可见性管理互相踩踏的
    // 产物——手写逻辑全部退役，不再与原生打架。
    WT_ZONE_COARSE(TEXT("Equip.Equip"));
    // ★0.2.111 每次装备重新收集骨骼 scale 异常命中（去重数组清空）。
    GWT_ScaleHit3P.Reset();
    GWT_ScaleHitEquip.Reset();
    GWT_ScaleHit1P.Reset();
    // ★0.2.116 装备瞬间开 3 秒高频突发采样窗（0.1s/行），抓切装备那一帧的臂长/相机/scale 瞬变。
    GWT_CamWatchBurstLeft = 3.f;
    GWT_CamWatchBurstAcc  = 0.f;

    // ★0.2.123 DeepSeek 处方②（可行版）：把「写 mArmSlotType」挪到动画求值正前方。
    //   无法真写进 AnimInstance::UpdateAnimation（3P 用的是原版 BP 类 Anim_3p_WIP_C，不可子类化；
    //   且 CacheArmSlotData 在求值内部从装备实例重缓存，先写也会被它盖掉）。等价手段：把本装备
    //   actor 设为 3P 网格的 tick 前置（AddTickPrerequisiteActor，ActorComponent.h:1282）——同帧内
    //   我方 Tick（含 rf.3pArmSlot 每帧保底写）必然先于网格动画 tick 执行，native 在装备事件里
    //   抢写的 cache=15 会在求值前被最后一次覆盖。0.2.120 强写只压掉 78/84 簇，剩余 6 簇即
    //   「我方写之后、求值之前」的时序窗口，本改动正对该窗口。
    if (Character)
    {
        if (USkeletalMeshComponent* B3P = Character->GetMesh3P())
        {
            B3P->AddTickPrerequisiteActor(this);
        }
    }

    // ★0.2.108 修复「切装备瞬间头变大（3P）」：群友定位=相机 spring arm 被瞬时缩短（镜头冲向头部，
    //   非角色变大），且"Super 一进去 native 就把相机推了一帧，后面再调整补不回那一帧"。
    //   对照 dump（Equip_ObjectScanner_C）装备侧无臂长/相机距离属性（群友说的"父类那个 0"在属性表
    //   对不上号），故采用与他给的备选方案「干脆手动赋一次臂长」等价但更稳的做法：Super 前快照
    //   臂长 + 当前/目标相机模式，Super 返回后**同帧**核对——仅当相机模式没有真实切换（即臂长变化
    //   属于 native 的误推）才恢复。同帧恢复先于 spring arm 本帧更新与渲染，坏值一帧都不会渲染出来。
    float ArmLenBefore = 0.f;
    ECameraMode CamModeBefore = ECameraMode::ECM_None;
    ECameraMode CamTargetBefore = ECameraMode::ECM_None;
    // ★0.2.109b 群友三样排查（臂长默认 300 本身没问题 → 相机被换/挂点动了/CameraLag）：
    //   ①ViewTarget（Equip 里有没有 SetViewTargetWithBlend 换相机）；②CameraLag 标志有没有被开；
    //   ③boom 的 SocketOffset/TargetOffset/挂接父与 socket 有没有变。全量同帧对比。
    FVector SockOffBefore = FVector::ZeroVector;
    FVector TgtOffBefore = FVector::ZeroVector;
    bool LagBefore = false;
    bool RotLagBefore = false;
    FName BoomSockBefore;
    UObject* ViewTargetBefore = nullptr;
    // ★0.2.110 群友定案：FOV 是唯一能「头变大又自己恢复」的量。角色侧实锤（FGCharacterPlayer.h）：
    //   mTargetCameraDistance(:1926)/mTargetCameraOffset(:1930)/mTargetCameraFOV(:1934) 是 protected
    //   BlueprintReadWrite 的「相机插值目标」，CameraTick 注释明说 "interpolates ... into and out of
    //   the head of the player" —— native 改目标值 → 相机平滑插值过去（头变大）→ 被纠正（自己恢复），
    //   与症状机制完全吻合。protected 直访问不了 → 反射读写（mEquipmentIdlePose 注入同一成熟模式）。
    float TgtDistBefore = 0.f;
    float TgtFovBefore = 0.f;
    FVector TgtCamOffBefore = FVector::ZeroVector;
    float CamFovBefore = 0.f;
    FFloatProperty* PDist = nullptr;
    FFloatProperty* PFov = nullptr;
    FStructProperty* POff = nullptr;
    if (Character)
    {
        // ★0.2.110b 群友提醒：「缓存一下指针，别每帧重新找」——FindFProperty 是类属性表遍历。
        //   角色类运行期不变（三属性在 AFGCharacterPlayer 基类上，子类同样解析到）→ 查一次挂
        //   静态复用；首查留痕，反射解析失败（属性改名）一眼可见。
        static FFloatProperty* GWT_CamDistProp = nullptr;
        static FFloatProperty* GWT_CamFovProp = nullptr;
        static FStructProperty* GWT_CamOffProp = nullptr;
        if (!GWT_CamDistProp && !GWT_CamFovProp && !GWT_CamOffProp)
        {
            GWT_CamDistProp = FindFProperty<FFloatProperty>(Character->GetClass(), TEXT("mTargetCameraDistance"));
            GWT_CamFovProp = FindFProperty<FFloatProperty>(Character->GetClass(), TEXT("mTargetCameraFOV"));
            GWT_CamOffProp = FindFProperty<FStructProperty>(Character->GetClass(), TEXT("mTargetCameraOffset"));
            WT_WARNF(TEXT("[equip] cached camera target props: dist=%d fov=%d off=%d"),
                     GWT_CamDistProp ? 1 : 0, GWT_CamFovProp ? 1 : 0, GWT_CamOffProp ? 1 : 0);
        }
        PDist = GWT_CamDistProp;
        PFov = GWT_CamFovProp;
        POff = GWT_CamOffProp;
        if (PDist) { TgtDistBefore = PDist->GetPropertyValue_InContainer(Character); }
        if (PFov) { TgtFovBefore = PFov->GetPropertyValue_InContainer(Character); }
        if (POff) { TgtCamOffBefore = *POff->ContainerPtrToValuePtr<FVector>(Character); }
        if (UCameraComponent* Cam = Character->FindComponentByClass<UCameraComponent>())
        {
            CamFovBefore = Cam->FieldOfView;
        }
    }
    if (USpringArmComponent* Spring = Character ? Character->GetSpringArmComponent() : nullptr)
    {
        ArmLenBefore = Spring->TargetArmLength;
        // ★0.2.109 好值顺带刷新基线（每帧守卫用）。
        if (ArmLenBefore > 1.f)
        {
            GWT_GoodArmLen = ArmLenBefore;
        }
        CamModeBefore = Character->GetCameraMode();
        CamTargetBefore = Character->GetTargetCameraMode();
        SockOffBefore = Spring->SocketOffset;
        TgtOffBefore = Spring->TargetOffset;
        LagBefore = Spring->bEnableCameraLag;
        RotLagBefore = Spring->bEnableCameraRotationLag;
        BoomSockBefore = Spring->GetAttachSocketName();
        if (const APlayerController* PC = Character->GetController<APlayerController>())
        {
            ViewTargetBefore = PC->GetViewTarget();
        }
    }

    // ★0.2.104：父类已是 AFGObjectScanner → Super::Equip 会执行扫描仪装备逻辑（循环扫描物/
    // 屏幕初始化等）。显式调 AFGEquipment::Equip，保持与 0.2.103（父类还是 AFGEquipment 时调
    // Super）完全相同的挂接/可见性行为。
    AFGEquipment::Equip(Character);

    // ★0.2.129 装备同帧直写手臂姿势源（大肥鱼处方）：native 在装备瞬态把 mArmAnimation 当 ctor/装备默认处理，
    //   首帧 native 滞后一帧读到非 1P 目标 → 1P armSlot 偏 → 手持建模短暂走 camera-fallback（飘）。
    //   这里在 Super::Equip 返回后同帧把目标值铺到位，并重置边沿状态，让下方 Tick 首帧也立即写（Tick 按真实
    //   视角兜底纠正本处可能的视角滞后判定）。
    {
        const int32 EquipWantRaw = (Character && Character->IsFirstPerson() && GWT_ArmAnim1POverride >= 0)
                                       ? GWT_ArmAnim1POverride : GWT_ArmAnimOverride;
        if (EquipWantRaw >= 0 && EquipWantRaw <= static_cast<int32>(EArmEquipment::AE_BoomBox))
        {
            mArmAnimation = static_cast<EArmEquipment>(static_cast<uint8>(EquipWantRaw));
            if (ARemote_FactoryEquipment* CDO = Cast<ARemote_FactoryEquipment>(
                    ARemote_FactoryEquipment::StaticClass()->GetDefaultObject()))
            {
                if (CDO != this)
                {
                    CDO->mArmAnimation = static_cast<EArmEquipment>(static_cast<uint8>(EquipWantRaw));
                }
            }
            if (USkeletalMeshComponent* B1P = Character ? Character->GetMesh() : nullptr)
            {
                if (UFGAnimPlayer* AI1P = Cast<UFGAnimPlayer>(B1P->GetAnimInstance()))
                {
                    AI1P->mArmSlotType = static_cast<EArmEquipment>(static_cast<uint8>(EquipWantRaw));
                }
            }
        }
        // 重置边沿状态：确保 Tick 首帧（无论视角）都把目标值写一次，不依赖上一局残留。
        GWT_LastSlotHadThis = false;
        GWT_LastAppliedWant = -999;
        GWT_LastSrcWriteB1P = -1;
        GWT_LastCdoSyncArmAnim = -999;
    }

    // ★0.2.108 同帧臂长核对恢复（接上方快照；判定反例自证见注释末）。
    if (USpringArmComponent* Spring = Character ? Character->GetSpringArmComponent() : nullptr)
    {
        const float Pushed = Spring->TargetArmLength;
        const bool bCamUnchanged = Character->GetCameraMode() == CamModeBefore
                                && Character->GetTargetCameraMode() == CamTargetBefore;
        if (bCamUnchanged && !FMath::IsNearlyEqual(Pushed, ArmLenBefore))
        {
            Spring->TargetArmLength = ArmLenBefore;
            WT_WARNF(TEXT("[equip] spring arm pushed by native (%.1f) restored to %.1f (camMode unchanged)"),
                     Pushed, ArmLenBefore);
        }
        else if (!bCamUnchanged)
        {
            // 相机模式真切换（1P↔3P）→ 臂长变化是合法过渡，绝不动它（否则会锁死视角切换动画）。
            WT_WARNF(TEXT("[equip] spring arm %.1f->%.1f camMode %d->%d: legit transition, untouched"),
                     ArmLenBefore, Pushed, (int32)CamModeBefore, (int32)Character->GetCameraMode());
        }

        // ★0.2.109b 群友三样对比（Super 后同帧）：偏移变了且相机模式没真切 → 恢复；lag 被开 →
        //   恢复并告警；ViewTarget 被换 → 只记日志（恢复 view target 可能打断合法混合，先观察）。
        const FVector SockOffAfter = Spring->SocketOffset;
        const FVector TgtOffAfter = Spring->TargetOffset;
        if (bCamUnchanged)
        {
            if (!SockOffAfter.Equals(SockOffBefore) || !TgtOffAfter.Equals(TgtOffBefore))
            {
                Spring->SocketOffset = SockOffBefore;
                Spring->TargetOffset = TgtOffBefore;
                WT_WARNF(TEXT("[equip] boom offset pushed (%s,%s)->(%s,%s) restored"),
                         *SockOffBefore.ToCompactString(), *TgtOffBefore.ToCompactString(),
                         *SockOffAfter.ToCompactString(), *TgtOffAfter.ToCompactString());
            }
            if (Spring->bEnableCameraLag != LagBefore || Spring->bEnableCameraRotationLag != RotLagBefore)
            {
                Spring->bEnableCameraLag = LagBefore;
                Spring->bEnableCameraRotationLag = RotLagBefore;
                WT_WARNF(TEXT("[equip] camera lag flags flipped by native (%d,%d)-> restored"),
                         LagBefore ? 1 : 0, RotLagBefore ? 1 : 0);
            }

            // ★0.2.110 群友定案修复：FOV 是唯一能「头变大又自己恢复」的量；角色侧插值目标
            //   （mTargetCameraDistance/FOV/Offset）被 native 推动且相机模式未真切 → 反射写回。
            //   相机模式真切（3P↔1P）时目标值变更是合法过渡，绝不拦（bCamUnchanged 已挡）。
            if (PDist && !FMath::IsNearlyEqual(PDist->GetPropertyValue_InContainer(Character), TgtDistBefore))
            {
                PDist->SetPropertyValue_InContainer(Character, TgtDistBefore);
                WT_WARNF(TEXT("[equip] mTargetCameraDistance pushed by native -> restored to %.1f"),
                         TgtDistBefore);
            }
            if (PFov && !FMath::IsNearlyEqual(PFov->GetPropertyValue_InContainer(Character), TgtFovBefore))
            {
                PFov->SetPropertyValue_InContainer(Character, TgtFovBefore);
                WT_WARNF(TEXT("[equip] mTargetCameraFOV pushed by native -> restored to %.1f"), TgtFovBefore);
            }
            if (POff)
            {
                const FVector CurTgtOff = *POff->ContainerPtrToValuePtr<FVector>(Character);
                if (!CurTgtOff.Equals(TgtCamOffBefore))
                {
                    *POff->ContainerPtrToValuePtr<FVector>(Character) = TgtCamOffBefore;
                    WT_WARNF(TEXT("[equip] mTargetCameraOffset pushed by native -> restored to %s"),
                             *TgtCamOffBefore.ToCompactString());
                }
            }
            // 相机实际 FOV 同帧被改 → 写回（群友定案量，先取被推值再写回，日志两端都要对）。
            if (UCameraComponent* Cam = Character->FindComponentByClass<UCameraComponent>())
            {
                const float PushedFov = Cam->FieldOfView;
                if (!FMath::IsNearlyEqual(PushedFov, CamFovBefore))
                {
                    Cam->FieldOfView = CamFovBefore;
                    WT_WARNF(TEXT("[equip] camera FOV pushed by native (%.1f) -> restored to %.1f"),
                             PushedFov, CamFovBefore);
                }
            }
        }
        if (Spring->GetAttachSocketName() != BoomSockBefore)
        {
            WT_WARNF(TEXT("[equip] boom attach socket changed: %s -> %s"),
                     *BoomSockBefore.ToString(), *Spring->GetAttachSocketName().ToString());
        }
        if (const APlayerController* PC2 = Character->GetController<APlayerController>())
        {
            UObject* VTAfter = PC2->GetViewTarget();
            if (VTAfter != ViewTargetBefore)
            {
                WT_WARNF(TEXT("[equip] VIEW TARGET changed: %s -> %s  (SetViewTargetWithBlend suspect!)"),
                         ViewTargetBefore ? *ViewTargetBefore->GetName() : TEXT("<null>"),
                         VTAfter ? *VTAfter->GetName() : TEXT("<null>"));
            }
        }
    }
    // 反例自证：若角色处于 1P（臂长本来就 ~0/很小），native 推 0 不产生差值 → IsNearlyEqual 拦住，
    // 不做无谓写回；若用户在 3P 切装备且 native 误推短值 → 差值触发恢复。两条路径都不误伤。

    // ★0.2.98 孤儿实例清扫 + 世界实例计数诊断：根治「平台上立着的游离设备」。
    //   遍历世界所有 Remote_FactoryEquipment 实例，销毁同使用者、未装备状态的残留 actor（除 this）。
    //   同时打印实例计数 + 各实例 equipped 状态，下次实测一眼定位「世界里有几份设备」。
    //   先收集再批量 Destroy（不在 TObjectIterator 迭代中销毁，安全）。宁可漏杀（sameUser 为空）不误杀。
    {
        TArray<ARemote_FactoryEquipment*> Orphans;
        int32 Total = 0;
        AFGCharacterPlayer* MyChar = Character;
        for (TObjectIterator<ARemote_FactoryEquipment> It; It; ++It)
        {
            ARemote_FactoryEquipment* E = *It;
            if (E == this)
            {
                continue;   // 本实例刚被 Super::Equip 装备，排除
            }
            ++Total;
            const bool bSameUser = MyChar && E->GetInstigatorCharacter() == MyChar;
            const bool bOrphan = !E->IsEquipped() && bSameUser;
            WT_WARNF(TEXT("[equip-sweep] inst=%s equipped=%d sameUser=%d orphan=%d"),
                     *E->GetName(), E->IsEquipped() ? 1 : 0, bSameUser ? 1 : 0, bOrphan ? 1 : 0);
            if (bOrphan)
            {
                Orphans.Add(E);
            }
        }
        int32 Killed = 0;
        for (ARemote_FactoryEquipment* E : Orphans)
        {
            E->Destroy();
            ++Killed;
        }
        WT_WARNF(TEXT("[equip-sweep] total=%d orphans=%d killed=%d (this excluded)"), Total, Orphans.Num(), Killed);
    }

    // ★v0.2.82 修复「建模彻底没了」：本版 FGEquipment.cpp 是编译空桩，运行时走游戏本体真实
    // AFGEquipment::Equip——它把整 actor 挂到 mAttachSocket(hand_rSocket) 并把可见性交角色
    // UpdateEquipmentVisibility（actor 级 SetActorHiddenInGame，与原版扫描仪同机制）。但我们的
    // HandMesh 是**自定义子组件**，ctor 默认 SetVisibility(false)，且 0.2.80 删掉了所有手动
    // SetVisibility(true) ⇒ 无任何代码点亮它 ⇒ 模型永远停在不可见（不崩，因 mAttachSocket 已填对、
    // actor 已正确挂到手 socket，只是网格组件自己停在不可见）。对照 RecipeCopier（蓝图网格组件
    // **默认可见**、走 Super::Equip 稳定显示）确认原生用 actor 级可见性、不遍历/不认识我们的自定义
    // 组件。故此处显式点亮 HandMesh；actor 级隐藏仍由原生在 UnEquip/切视角时统一掌管（不再手写对抗，
    // 那正是 0.2.77-0.2.79 看门狗互踩的元凶）。
    if (HandMesh)
    {
        HandMesh->SetVisibility(true, true);
        HandMesh->SetOnlyOwnerSee(false);

        // ★v0.2.84 建模修复：实现 0.2.19 即声明、却从未落地的「实例级兜底重设」。
        // ApplyHandheldHandMesh 只把网格写进 CDO 模板；子对象复制时机问题可能导致生成实例
        // 未继承运行时网格 → 实例 HandMesh 停在"可见但无网格" = 手上空无一物（无建模，1P/3P 皆然）。
        // 故此处显式检查：实例确实缺网格才重设，避免无谓脏标记。静态量在 ApplyHandheldHandMesh 已就绪。
        if (!HandMesh->GetSkeletalMeshAsset() && GWT_HandMeshAsset)
        {
            HandMesh->SetSkeletalMesh(Cast<USkeletalMesh>(GWT_HandMeshAsset));
            if (GWT_HandAnimClass)
            {
                HandMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
                HandMesh->SetAnimInstanceClass(static_cast<UClass*>(GWT_HandAnimClass));
            }
            else
            {
                HandMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
                HandMesh->SetAnimInstanceClass(nullptr);
            }
            HandMesh->SetRelativeLocation(GWT_HandRelLoc);
            HandMesh->SetRelativeRotation(GWT_HandRelRot);
            HandMesh->SetRelativeScale3D(GWT_HandRelScale);
            WT_CopyVanillaHandMaterials(HandMesh);
            HandMesh->MarkRenderStateDirty();
            WT_WARNF(TEXT("[equip] instance HandMesh had no mesh; reapplied from static (anim=%s)."),
                     GWT_HandAnimClass ? *static_cast<UClass*>(GWT_HandAnimClass)->GetName() : TEXT("<bindpose>"));
        }

        // ★0.2.142 终端屏幕 logo：此处 HandMesh 网格必已就绪（上方兜底可能刚赋）。
        ApplyHandheldScreenLogo();
    }

    // ★0.2.113 【停用】0.2.93 拿出装备动画（3P 蒙太奇播角色身体）——「头变大/左臂锥形」根因实锤：
    //   0.2.111 骨骼扫描抓到 char3P 身体骨骼全体 scale 爆炸（每次往子骨走一级翻倍：
    //   spine_01=2 → Head=32 → hand_l=128 → 指尖=1024，头变大与左臂锥形是同一根因的两个症状，
    //   与相机无关——bDoCollisionTest=0 也被日志排除）。唯一直接驱动身体的动作就是这个
    //   Montage_Play(GetMesh3P())：蒙太奇里的 scale 顺着身体骨骼层级累乘。原版语义存疑
    //   （SDK PlayEquipmentMontage 为空桩，无法确认 Montage_3P 该播身体还是播装备设备网格），
    //   先二分停用：本版若变形消失 → 实锤此块；若仍在 → 按群友提示查 leader pose 传导
    //   （1P 蒙太奇 → char1P 扫描留痕已加）。3P 拿出动画后续再以干净通路重做。
    // if (GWT_EquipMontage3p && Character)
    // {
    //     if (USkeletalMeshComponent* B3P = Character->GetMesh3P())
    //     {
    //         if (UAnimInstance* AI = B3P->GetAnimInstance())
    //         {
    //             const FName SlotName = GWT_3pSlotName.IsNone() ? FName(TEXT("DefaultSlot")) : GWT_3pSlotName;
    //             AI->StopSlotAnimation(0.2f, SlotName);
    //             UAnimMontage* Started = AI->Montage_Play(GWT_EquipMontage3p) > 0.f
    //                                        ? GWT_EquipMontage3p : nullptr;
    //             WT_WARNF(TEXT("[equip] equip montage %s (slot=%s) started=%d"),
    //                      *GWT_EquipMontage3p->GetName(), *SlotName.ToString(), Started ? 1 : 0);
    //             WT_WARNF(TEXT("[equip] 3P mesh rootmotion playing=%d"),
    //                      B3P->IsPlayingRootMotion() ? 1 : 0);
    //         }
    //     }
    // }

    // ★0.2.107 B 类：1P 拿出装备动画 —— 镜像上方 3P 块，播原版 mEquipMontage.Montage_1P 到
    //   角色 1P 手臂网格（GetMesh1P()==GetMesh()，FGCharacterPlayer.h:617）的 AnimInstance 上。
    //   1P 手臂 AnimBP 不换状态（群友实锤），蒙太奇为一次性插播、播完即止，不碰状态机；
    //   0.2.106 的 1P idle 蒙太奇退出守卫不受影响（那里禁的是长期循环重播，这里是单次插播）。
    //   3P 视角下 1P 手臂网格隐藏，播了也无观感副作用，故不按视角门控（与 3P 块对称）。
    if (GWT_EquipMontage1p && Character)
    {
        if (USkeletalMeshComponent* B1P = Character->GetMesh1P())
        {
            if (UAnimInstance* AI = B1P->GetAnimInstance())
            {
                const FName SlotName1P = GWT_1pSlotName.IsNone() ? FName(TEXT("DefaultSlot")) : GWT_1pSlotName;
                AI->StopSlotAnimation(0.2f, SlotName1P);
                UAnimMontage* Started1P = AI->Montage_Play(GWT_EquipMontage1p) > 0.f
                                             ? GWT_EquipMontage1p : nullptr;
                WT_WARNF(TEXT("[equip] 1P equip montage %s (slot=%s) started=%d"),
                         *GWT_EquipMontage1p->GetName(), *SlotName1P.ToString(), Started1P ? 1 : 0);
            }
        }
    }

    const bool bLocal = Character && Character->GetController() && Character->GetController()->IsLocalController();
    {
        AFGCharacterPlayer* Chk = GetInstigatorCharacter();
        // ★0.2.86 诊断：打印本实例的装备槽位与手臂动画枚举，确认动画系统会读到 AE_ObjectScanner(=10)/
        // ES_ARMS(=1)。若 armAnim/ slot 非预期值，说明装备未被识别为"手持举持"，3P 手臂会垂落、设备错位。
        WT_WARNF(TEXT("[equip] native path: char=%s local=%d equipped=%d attachParent=%s handVis=%d armAnim=%d slot=%d 1P=%d"),
                 Character ? *Character->GetName() : TEXT("<null>"),
                 bLocal ? 1 : 0,
                 IsEquipped() ? 1 : 0,
                 GetAttachParentActor() ? *GetAttachParentActor()->GetName() : TEXT("<none>"),
                 (HandMesh && Chk) ? (HandMesh->IsVisible() ? 1 : 0) : -1,
                 (int32)GetArmsAnimation(),
                 (int32)mEquipmentSlot,
                 Character->IsFirstPerson() ? 1 : 0);
    }
    if (!bLocal)
    {
        return;   // 远端/服务器实例：原生挂接已由 Super::Equip 处理，本地 UI 不开
    }

    // ★v0.2.83 静默可见性守护（本地实例）：原生只认 actor 级 bHidden + 专属 mScannerSkeletalMesh，
    //  不认识我们的自定义 HandMesh，会把它盖成不可见——守护每 0.3s 静默拉回。卸装/销毁即停。
    StartVisibilityWatchdog();

    // ★v0.2.80 手写挂接/兜底/诊断大块（v0.2.50~v0.2.79 的 actor 挂 Mesh1P、实例网格兜底、
    // 视角选择器、charMeshChild 转储、hand-mesh[0/5]~[4/4] 与 @0.5s/@2s 探针）整体退役删除：
    // 挂接与可见性已由 Super::Equip 原生接管，这段逻辑不再执行且与原生管理
    // 互相踩踏（0.2.78-0.2.79 间歇隐身/地上孤儿根因）。历史实现见 0.2.79 及更早版本包。

    WT_LOGF(TEXT("[equip] portable terminal equipped by %s"), Character ? *Character->GetName() : TEXT("<null>"));

    // ★v0.2.12 右键开面板的输入挂接（IMC 应用 + EnhancedInput 绑定），随装备生效。
    ApplyToggleInput(Character);

    // ★v0.2.80 看门狗退役：可见性已由原生链路（角色 UpdateEquipmentVisibility 自管），
    // 巡检/自愈与原生管理重复且互相踩踏（本局 [vigil] 刷屏 1092 条实证）。不再启动。

    // ★v0.2.17 不再装备即弹面板：交互定案为右键 toggle（用户确认"切到面板就自动打开"是遗留行为）。
}

void ARemote_FactoryEquipment::OnPostCameraModeChanged_Implementation(ECameraMode newCameraMode)
{
    // ★v0.2.80：切视角全部交给原生——可见性由角色 UpdateEquipmentVisibility 自管、挂接随
    // 角色网格走（原版扫描仪同款），这里不再手写任何可见性翻转/摆位。0.2.77-0.2.79 的手动
    // 逻辑与原生管理互相踩踏（间歇隐身元凶），全部退役。
    AFGEquipment::OnPostCameraModeChanged_Implementation(newCameraMode);

    // ★0.2.125 大肥鱼诊断（0.2.124 日志实锤）：1P→3P 切过去那一下，3P 姿势第一次求值用的还是
    //   原生 mArmSlotType=15 —— 相机模式标志滞后一帧，导致本帧 IsFirstPerson() 仍为真 →
    //   EnforceArmPoseCache 的 3P 直写（!b1P 分支）被跳过 → 首帧用 15 求值得 1024x 雪崩，
    //   下一帧才被我方 10 覆盖（burst 头条与 edge-write 同帧、峰值算在前写在后，差这一口气）。
    //   这里在视角回调里【同步】强写 3P 臂槽，抢在下一帧 3P 姿势求值之前把 15 按回 10，掐掉首帧闪。
    //   3P 网格在 1P 下不求值，故 1P 期间写 10 无害（幂等）。另：切到 3P 时清空骨骼 scale 扫描
    //   去重数组，使本次视角切换重新捕获瞬态（求值前/后一对），避免反复装备堆积全量 bonescale 行。
    if (newCameraMode == ECameraMode::ECM_ThirdPerson)
    {
        if (AFGCharacterPlayer* Character = GetInstigatorCharacter())
        {
            if (USkeletalMeshComponent* B3P = Character->GetMesh3P())
            {
                if (UFGAnimPlayer* P3P = Cast<UFGAnimPlayer>(B3P->GetAnimInstance()))
                {
                    const int32 MaxEnum = static_cast<int32>(EArmEquipment::AE_BoomBox);
                    if (GWT_3pArmSlotOverride >= 0 && GWT_3pArmSlotOverride <= MaxEnum)
                    {
                        const EArmEquipment WantSlot =
                            static_cast<EArmEquipment>(static_cast<uint8>(GWT_3pArmSlotOverride));
                        if (P3P->mArmSlotType != WantSlot)
                        {
                            P3P->mArmSlotType = WantSlot;
                            WT_WARNF(TEXT("[pose] 3P armSlot force-write %d on cam-mode->3P (pre-3P-eval, kills first-frame 15 spike)."),
                                     static_cast<int32>(WantSlot));
                        }
                    }
                }
            }
        }
        GWT_ScaleHit3P.Reset();
        GWT_ScaleHitEquip.Reset();
        GWT_ScaleHit1P.Reset();
    }
}

void ARemote_FactoryEquipment::ReattachHandheldToView()
{
    // ★修复 ①（2026-10-18 接手）：原逻辑把整只装备 actor 重挂到角色网格 hand socket，但 1P 下该
    // socket 被重定位到距相机 ~94m 处 → "手持无建模"。现手持件改挂相机跟随锚点（Tick 每帧摆位），
    // 故"重挂"只需刷新一次锚点世界摆位即可，不再依赖任何 socket/网格。看门狗在 1P/3P 切换时仍调它。
    AFGCharacterPlayer* Character = GetInstigatorCharacter();
    if (!Character || !HandMesh || !IsEquipped())
    {
        return;
    }
    UpdateViewModelTransform();
    bLastAttachedFirstPerson = Character->IsFirstPerson();
    WT_WARNF(TEXT("[camera] handheld anchor refreshed (1P=%d)."),
             bLastAttachedFirstPerson ? 1 : 0);
}

void ARemote_FactoryEquipment::UpdateViewModelTransform()
{
    // 把相机跟随锚点摆到正确位置：1P=相机前（hand_rSocket 在 1P 下被重定位→锚点兜底）；
    // 3P=角色手部 socket（手臂举持时手随姿势抬起→设备到胸前，与原版扫描仪一致）。
    // 用**公开** API 取相机/骨骼 socket，绝不碰 protected 成员。
    if (!ViewModelAnchor || !HandMesh || HandMesh->GetAttachParent() != ViewModelAnchor)
    {
        return;
    }
    AFGCharacterPlayer* Character = GetInstigatorCharacter();
    if (!Character || !IsEquipped())
    {
        return;
    }
    if (Character->IsFirstPerson())
    {
        // ★0.2.95 推翻 0.2.94 的「相机前方锚点 + 相对清零」：0.2.94 实测设备可见了但**朝向歪**——
        // 清零把原版握持标定 R(Y=-90) 也清掉了，且相机锚点没有手部动画跟随感。原版扫描仪 1P 的
        // 真实链路是：**1P 手臂网格播举持动画 → 手抬到面前 → hand_rSocket 随之到相机前方**，
        // 设备跟 socket 走 + 套原版标定相对变换（GWT_HandRel*），朝向/跟随手感与原版一致。
        // 举持动画由 EnforceArmPoseCache 的 1P 分支驱动（0.2.95 新增，与 3P 同款 slot 状态门控）。
        // 本函数只在 socket 失真（缺失/在相机后方/距离异常）时回落相机前方锚点兜底。
        const FName Sock = GWT_HandAttachSocket.IsNone() ? FName(TEXT("hand_rSocket")) : GWT_HandAttachSocket;
        FVector CamLoc = FVector::ZeroVector;
        FRotator CamRot = FRotator::ZeroRotator;
        if (APlayerController* PC = Cast<APlayerController>(Character->GetController()))
        {
            PC->GetPlayerViewPoint(CamLoc, CamRot);
        }
        else
        {
            Character->GetActorEyesViewPoint(CamLoc, CamRot);
        }
        // 1P 手臂网格（GetMesh() 即 GetMesh1P() 返回值，FGCharacterPlayer.h:617 实锤）。
        USkeletalMeshComponent* Body = Character->GetMesh();
        const bool bSockOk = Body && Body->DoesSocketExist(Sock);
        FVector SockLoc = FVector::ZeroVector;
        bool bUseSocket = false;
        float SockFrontDot = 0.f;
        if (bSockOk)
        {
            SockLoc = Body->GetSocketTransform(Sock).GetLocation();
            const FVector ToSock = SockLoc - CamLoc;
            SockFrontDot = ToSock.SizeSquared() > 1e-4f
                ? FVector::DotProduct(ToSock.GetSafeNormal(), CamRot.Vector().GetSafeNormal())
                : 0.f;
            // 守卫（0.2.91 口径）：socket 在相机后方（举持动画没生效、手还在休息位）或距离异常
            // （>3m，手不可能那么远）→ 锚点失真，回落相机前方。
            bUseSocket = SockFrontDot > 0.f && ToSock.Size() <= 300.f;
        }
        if (bUseSocket)
        {
            ViewModelAnchor->SetWorldTransform(Body->GetSocketTransform(Sock));
        }
        else
        {
            // ★0.2.96 兜底：相机前方持握位。原 30/12/-14 在普通 1P 站立（手臂休息位、socket 在相机后
            // → 必走此处）时距相机仅 30cm，约 40cm 的设备糊满整屏（用户 0.2.95 截图「巨型挡屏」）。
            // 改为更靠后+更低右的持握位（前 45 / 右 22 / 下 24 cm），尺寸收敛到「被握在右下前方」观感，
            // 与飞行态 socket 跟随（同为中号、手持位）一致。相对变换仍用原版标定 GWT_HandRel*
            // （0.2.94 清零导致朝向歪的教训——任何分支都不清零）。
            const FRotator HoldRot(CamRot.Pitch, CamRot.Yaw, 0.f);
            const FVector Hold(45.f, 22.f, -24.f);
            ViewModelAnchor->SetWorldTransform(
                FTransform(HoldRot, CamLoc + HoldRot.RotateVector(Hold), FVector::OneVector));
        }
        // ★0.2.95：相对变换统一用原版握持标定（1P/3P 同款，不再按视角清零/恢复——两边一致无切换残留）。
        HandMesh->SetRelativeLocation(GWT_HandRelLoc);
        HandMesh->SetRelativeRotation(GWT_HandRelRot);
        HandMesh->SetRelativeScale3D(GWT_HandRelScale);
        // ★0.2.96 诊断：mode=socket（举持动画生效、原生跟随）/camera-fallback（动画未生效）。
        // 改「首帧 + 模式翻转时」都打——0.2.95 仅首帧打一次，运行时从 socket 切 fallback（普通站立）
        // 不重打 → 巨型挡屏全靠猜。现在翻转即打，可直接看 1P 是否走了 fallback。
        const int8 Cur1pMode = bUseSocket ? (int8)0 : (int8)1;
        if (HandMesh->GetSkeletalMeshAsset() &&
            (!bFirstPersonDiagLogged || Cur1pMode != bLast1pMode))
        {
            bFirstPersonDiagLogged = true;
            bLast1pMode = Cur1pMode;
            // ★0.2.99 一发定位：补打 1P AnimInstance 类名 + UFGAnimPlayer::mArmSlotType /
            // mIsFirstPerson。原版 1P 举持 = AnimPlayer1p 状态机按 mArmSlotType 选臂姿势
            // （FGAnimPlayer.h:65 UFGAnimPlayer1p + :43 枚举缓存；:70 起全 protected，成员
            // mEquipmentIdlePose 外部不可访问——故只读基类 public 的 mArmSlotType/mIsFirstPerson）。
            // 判读：armSlot=10(AE_ObjectScanner) 而臂不抬 → 状态机/蒙太奇互踩（嫌疑：我们播的
            // UpperBody 蒙太奇在 1P AnimBP 无 slot 节点）；armSlot=0 → 缓存链路未通。
            const UAnimInstance* Anim1P = Character->GetMesh()->GetAnimInstance();
            const UFGAnimPlayer* AnimP = Cast<UFGAnimPlayer>(Anim1P);
            // ★0.2.100 一发定死两个黑箱：
            // ① AnimInstance 缓存的 mEquipmentIdlePose（UFGAnimPlayer1p::mEquipmentIdlePose，protected，
            //    反射读，与加载期写法一致）——非空=状态机有姿势可播；空=缓存链路断。
            // ② socket 在相机坐标系的 (前/右/上) 分量——直接看手在相机哪一侧（前>0 在前、<0 在后）。
            FString AiposeStr = TEXT("<null>");
            if (Anim1P)
            {
                if (FObjectProperty* PP = FindFProperty<FObjectProperty>(Anim1P->GetClass(), TEXT("mEquipmentIdlePose")))
                {
                    if (UObject* V = PP->GetObjectPropertyValue(PP->ContainerPtrToValuePtr<void>(Anim1P)))
                    {
                        AiposeStr = V->GetName();
                    }
                }
            }
            const FVector SockCam = (bSockOk && CamRot.Vector().SizeSquared() > KINDA_SMALL_NUMBER)
                ? CamRot.UnrotateVector(SockLoc - CamLoc)
                : FVector::ZeroVector;
            WT_WARNF(TEXT("[1p] mode=%s sock1P=%d dot=%.2f dist=%.0f hmVis=%d proxy=%d mesh=%s | anim=%s armSlot=%d is1P=%d aipose=%s sockCam(f,r,u)=%.0f,%.0f,%.0f"),
                     bUseSocket ? TEXT("socket") : TEXT("camera-fallback"),
                     bSockOk ? 1 : 0,
                     SockFrontDot,
                     bSockOk ? FVector::Dist(SockLoc, CamLoc) : -1.f,
                     HandMesh->IsVisible() ? 1 : 0,
                     HandMesh->GetSceneProxy() ? 1 : 0,
                     *HandMesh->GetSkeletalMeshAsset()->GetName(),
                     Anim1P ? *Anim1P->GetClass()->GetName() : TEXT("<null>"),
                     AnimP ? (int32)AnimP->mArmSlotType : -1,
                     AnimP ? (AnimP->mIsFirstPerson ? 1 : 0) : -1,
                     *AiposeStr,
                     SockCam.X, SockCam.Y, SockCam.Z);
        }
    }
    else
    {
        // 3P：手持件跟随角色手部 socket。手臂举持姿势（mIdlePoseAnimation3p）抬起时，hand socket
        // 世界变换随之上移，设备自然到胸前，与原版扫描仪一致。GWT_HandRel* 是原版扫描仪相对手
        // socket 的握持变换，已由 HandMesh 自身相对变换承担（见下）。
        // ★0.2.87 修正：3P 下玩家看到的是 GetMesh3P()（FGCharacterPlayer.h:620 公开 virtual）；
        // GetMesh() 是 1P 手臂网格（GetMesh1P() 返回 GetMesh()，FGCharacterPlayer.h:617），3P 下
        // 隐藏且骨架动画与 3P 身体不重合 → 0.2.86 以 GetMesh() 优先导致设备落在 1P 网格 ref-pose
        // 的手部位置（胯部）。现改为 GetMesh3P() 优先（带 socket 存在性校验），缺失才回落 GetMesh()。
        const FName Sock = GWT_HandAttachSocket.IsNone() ? FName(TEXT("hand_rSocket")) : GWT_HandAttachSocket;
        USkeletalMeshComponent* Body = Character->GetMesh3P();
        if (!Body || !Body->DoesSocketExist(Sock))
        {
            USkeletalMeshComponent* Mesh1P = Character->GetMesh();
            if (Mesh1P && Mesh1P->DoesSocketExist(Sock))
            {
                Body = Mesh1P;
            }
        }
        if (!Body)
        {
            return;
        }
        const FTransform HandSocketWorld = Body->GetSocketTransform(Sock);
        // ★0.2.86：ViewModelAnchor 直接落到手部 socket 世界变换；「握持相对变换」(GWT_HandRel*)
        // 已由 HandMesh 自身的相对变换承担（Equip/ApplyHandheldHandMesh 写入），此处若再叠加会把
        // 握持偏移应用两次。HandMesh 作为 ViewModelAnchor 子组件随之到手，持握姿态与原版一致。
        ViewModelAnchor->SetWorldTransform(HandSocketWorld);
        // ★0.2.94：切回 3P 时显式恢复握持相对变换（1P 分支已清零，须在此复位，否则设备摆歪）。
        HandMesh->SetRelativeLocation(GWT_HandRelLoc);
        HandMesh->SetRelativeRotation(GWT_HandRelRot);
        HandMesh->SetRelativeScale3D(GWT_HandRelScale);
    }
    // ★0.2.97 移除每帧 MarkRenderStateDirty()：0.2.96 实测它让锚点/HandMesh 的渲染状态每帧重建，
    // SceneProxy 在 0/1 间闪（日志 proxy=0/1 交替）、设备在「卡手上(socket帧)/飘(fallback帧)」间跳
    // （用户三张截图交替表现）。SetWorldTransform 的位置更新走组件变换同步，不需要重建渲染状态；
    // 挂接关系变化时引擎自会经 AttachToComponent 重建。
}

void ARemote_FactoryEquipment::EnforceArmPoseCache(AFGCharacterPlayer* Character)
{
    // ★0.2.87 3P 举持姿势诊断+兜底。原版链路：AFGEquipment 的 mArmAnimation（已写 AE_ObjectScanner=10）
    // 应由 UFGAnimPlayer::CacheArmSlotData 缓存到 mArmSlotType，3P 动画蓝图据此播放双手举持。
    // 0.2.86 实测：配置全对（armAnim=10 slot=1 idle3p=ObjectScannerIdle_01）但 3P 手臂仍下垂 →
    // 在此把「槽位注册」与「动画缓存」两层实况打出来定位断点，并做直写兜底。
    // ★0.2.95：不再对 1P 提前直返——1P 手臂网格（GetMesh()）同样要播举持蒙太奇（原版同款链路：
    // 1P 手抬到面前，hand_rSocket 随之到相机前方，设备跟 socket 走朝向才正）。数据层诊断/直写仍只做 3P。
    if (!Character)
    {
        return;
    }
    // ★0.2.124 rf.injectIdle3p 门控改「每帧重应用」：0.2.123 只门 ctor/加载期两处写入，
    //   已 Spawn 的实例属性早已写死 —— 运行时敲 rf.injectIdle3p 0 不会清掉已写的实例属性，
    //   控制台 A/B 会得假阴性（「没变化」≠ 二分结论）。这里按当前 CVar 把实例
    //   mIdlePoseAnimation3p 对齐到目标值（1=GWT_Idle3pAsset / 0=null），仅与现值不同时
    //   反射写一次（属性 private → FObjectProperty 同 ctor 成熟模式；getter 公有可直读）。
    //   稳态同值零写入，不会复现 0.2.121 的「每帧写戳 native」问题。
    {
        UAnimSequence* Want3p = (GWT_InjectIdle3p != 0) ? GWT_Idle3pAsset : nullptr;
        UAnimSequence* Cur3p = GetIdlePoseAnimation3p();
        if (Cur3p != Want3p)
        {
            if (FObjectProperty* P = FindFProperty<FObjectProperty>(
                    AFGEquipment::StaticClass(), TEXT("mIdlePoseAnimation3p")))
            {
                P->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(this), Want3p);
                WT_WARNF(TEXT("[inject3p] %s -> %s (rf.injectIdle3p=%d)."),
                         Cur3p ? *Cur3p->GetName() : TEXT("<null>"),
                         Want3p ? *Want3p->GetName() : TEXT("<null>"),
                         GWT_InjectIdle3p);
            }
        }
    }
    // ★0.2.120 连续压制继承的 mScannerSkeletalMesh：BeginPlay（cpp [scan-off] 处）只隐藏一次，
    //   而 native 的可见性管理（0.2.83 一手注释实锤：UpdateEquipmentVisibility 只认 actor 级
    //   bHidden + 本组件）在**每次 Equip**时都会把它重新点亮 → 重复设备 mesh 出现在 native
    //   挂接点（actor 预挂 GetMesh() 的 hand_rSocket，3P 模式下 1P 网格臂槽=10 不抬臂 →
    //   socket 停在休息位大腿侧 → 用户截图「腿边橙色小件垂到脚踝」的第二个设备）。
    //   这里每帧确认隐藏：点亮即压回（等值 no-op 零成本），5s 限频留痕便于实测确认。
    if (USkeletalMeshComponent* ScanMesh = mScannerSkeletalMesh)
    {
        if (ScanMesh->IsVisible())
        {
            ScanMesh->SetVisibility(false);
            const double NowScanSec = FPlatformTime::Seconds();
            if (NowScanSec - GWT_LastScanMeshRehideSec > 5.0)
            {
                GWT_LastScanMeshRehideSec = NowScanSec;
                WT_WARNF(TEXT("[scan-off] native re-showed mScannerSkeletalMesh; re-hidden (per-frame guard)."));
            }
        }
    }
    const bool b1P = Character->IsFirstPerson();
    USkeletalMeshComponent* Body3P = Character->GetMesh3P();
    UFGAnimPlayer* Player = Body3P ? Cast<UFGAnimPlayer>(Body3P->GetAnimInstance()) : nullptr;
    AFGEquipment* SlotEquip = Character->GetEquipmentInSlot(EEquipmentSlot::ES_ARMS);
    if (!b1P && !bArmPoseDiagLogged)
    {
        bArmPoseDiagLogged = true;
        WT_WARNF(TEXT("[pose] 3P animInst=%s armSlot=%d slotEquip=%s isSelf=%d mesh3PVis=%d"),
                 Player ? *Player->GetClass()->GetName() : TEXT("<null>"),
                 Player ? static_cast<int32>(Player->mArmSlotType) : -1,
                 SlotEquip ? *SlotEquip->GetName() : TEXT("<null>"),
                 SlotEquip == this ? 1 : 0,
                 Body3P ? (Body3P->IsVisible() ? 1 : 0) : -1);
    }
    // ★0.2.117 源头每帧保底写：mArmAnimation 是 CacheArmSlotData 的**输入源**且 1P/3P 共用单属性。
    //   1P 视角按 rf.armAnim1P（>=0 时优先），否则按 rf.armAnim（>=0 时）；两者都 <0 则交还不动。
    //   写源头而非 0.2.115 那样写 AnimInstance 缓存——后者每帧被 native 重缓存冲掉（0.2.115 失效根因）。
    // ★0.2.119：条件由 `Player && SlotEquip == this` 放宽为 `SlotEquip == this`
    //   —— Player 取自 3P 身体网格的 AnimInstance，1P 视角下其为 null，旧条件导致整块跳过。
    // ★0.2.121 边沿门控（群友处方①）：每帧写回会与 native 抢源头——native 装备窗口把源头翻 15、
    //   我方下一帧写回 10，这个写回本身戳 native 重跑装备链路（equip/unequip 一秒两三次地抖）。
    //   改为只在「视角切换 / 本装备重新获得槽位 / 目标值变化(CVar 调试) / 首帧」的沿上写一次。
    //   写块与下方 CDO 同步块共用同一沿与同一 WantRaw。
    const int32 EdgeWantRaw = (b1P && GWT_ArmAnim1POverride >= 0) ? GWT_ArmAnim1POverride
                                                                  : GWT_ArmAnimOverride;
    const bool bSrcEdge = (SlotEquip == this) &&
                          (((b1P ? 1 : 0) != GWT_LastSrcWriteB1P) || !GWT_LastSlotHadThis
                           || (EdgeWantRaw != GWT_LastAppliedWant));
    // ★0.2.129 1P 每帧保底写手臂姿势源：1P 的 armSlot 由 native 从 mArmAnimation 每帧重缓存
    //   （UFGAnimPlayer1p::NativeUpdateAnimation 直读装备实例字段），边沿写在首帧会因 native 滞后一帧
    //   读到 ctor 默认(10=AE_ObjectScanner)而 armSlot 偏 10 → 1P 手持建模走 camera-fallback（飘）。
    //   1P 目标(15=Generic1Hand)本就与 native 期望一致，每帧写不会与 native 抢值触发重跑抖动，故 1P 放开为每帧写；
    //   3P 维持边沿写（3P 目标 10 与 native 翻的 15 不同值，每帧写会抖，0.2.121 已证）。
    const bool bSrcWrite = b1P ? (SlotEquip == this) : bSrcEdge;
    if (SlotEquip == this && bSrcWrite)
    {
        const int32 WantRaw = EdgeWantRaw;
        // 钳制到 [0, AE_BoomBox=17]：越界 CVar 值静默忽略（同 ctor 处，防写入非法枚举）。
        if (WantRaw >= 0 && WantRaw <= static_cast<int32>(EArmEquipment::AE_BoomBox))
        {
            const EArmEquipment Want = static_cast<EArmEquipment>(static_cast<uint8>(WantRaw));
            if (mArmAnimation != Want)
            {
                mArmAnimation = Want;
                WT_WARNF(TEXT("[pose] mArmAnimation edge-write %d (1P=%d)."),
                         static_cast<int32>(Want), b1P ? 1 : 0);
            }
        }
    }

    // ★0.2.119 1P 手臂臂槽直写兜底 —— **不依赖 3P 侧 AnimInstance**：
    //   [1p] 判定读的就是「1P 手臂网格 AnimInstance 的 mArmSlotType」（见 [1p] 日志块）。
    //   实证：armSlot=15 ⇒ 状态机走单手握持 ⇒ socket 到相机前 ⇒ dot≈0.9 / mode=socket（贴手）；
    //         armSlot=10 ⇒ 垂臂 ⇒ dot≈0 / mode=camera-fallback（设备飘相机下方）。
    //   故这里每帧校验并直写该缓存，绕开源头链路上 native 重缓存带来的一切不确定性。
    if (b1P && SlotEquip == this && GWT_1pArmSlotOverride >= 0 &&
        GWT_1pArmSlotOverride <= static_cast<int32>(EArmEquipment::AE_BoomBox))
    {
        if (USkeletalMeshComponent* B1P = Character->GetMesh1P())
        {
            if (UFGAnimPlayer* AI1P = Cast<UFGAnimPlayer>(B1P->GetAnimInstance()))
            {
                const EArmEquipment Want1P =
                    static_cast<EArmEquipment>(static_cast<uint8>(GWT_1pArmSlotOverride));
                if (AI1P->mArmSlotType != Want1P)
                {
                    AI1P->mArmSlotType = Want1P;
                }
            }
        }
    }

    // ★0.2.119 CDO 同步兜底：若 native CacheArmSlotData 从**类默认对象**而非实例读该字段
    //   （本机日志出现过 "hand model applied to CDO"，说明 native 确有走 CDO 的路径），
    //   只写实例不够 —— 这里在值变化时把 CDO 同字段同步，消除实例/CDO 分歧。
    // ★0.2.121 挂同一沿（bSrcEdge）：CDO 也只在边沿写，理由同源头写块——防与 native 抢值。
    if (SlotEquip == this && bSrcWrite)
    {
        const int32 MaxEnum = static_cast<int32>(EArmEquipment::AE_BoomBox);
        const int32 WantRaw = EdgeWantRaw;
        if (WantRaw >= 0 && WantRaw <= MaxEnum)
        {
            if (ARemote_FactoryEquipment* CDO = Cast<ARemote_FactoryEquipment>(
                    ARemote_FactoryEquipment::StaticClass()->GetDefaultObject()))
            {
                if (CDO != this && static_cast<int32>(CDO->mArmAnimation) != WantRaw)
                {
                    CDO->mArmAnimation = static_cast<EArmEquipment>(static_cast<uint8>(WantRaw));
                    if (WantRaw != GWT_LastCdoSyncArmAnim)
                    {
                        GWT_LastCdoSyncArmAnim = WantRaw;
                        WT_WARNF(TEXT("[pose] CDO mArmAnimation synced -> %d (1P=%d)."),
                                 WantRaw, b1P ? 1 : 0);
                    }
                }
            }
        }
    }
    // ★0.2.121 边沿状态更新：写沿消费完毕即登记本帧视角/占槽状态；本装备不在槽时清标记，
    //   下次重新装备时沿再次触发（保证 unequip→equip 后源头一定被重新写一次）。
    GWT_LastSlotHadThis = (SlotEquip == this);
    if (SlotEquip == this)
    {
        GWT_LastSrcWriteB1P = b1P ? 1 : 0;
        // 只登记有效值：被钳制拒绝的越界 CVar 值不算"已应用"（否则改回有效值时沿不触发）。
        if (EdgeWantRaw >= 0 && EdgeWantRaw <= static_cast<int32>(EArmEquipment::AE_BoomBox))
        {
            GWT_LastAppliedWant = EdgeWantRaw;
        }
    }

    // ★0.2.119 一发仪表：见证「装备实例值 / CDO 值 / 1P 手臂缓存值 / 3P 身体缓存值」四处是否一致。
    //   判读：inst=cdo=cache1P=15 ⇒ 通路已通（设备应贴手）；
    //        cache1P=10 而 inst=15 ⇒ native 从别处（CDO）读并覆盖了 1P 缓存；
    //        playerNull=1 ⇒ 印证 3P 身体 AnimInstance 在 1P 下确实停更（0.2.118 失效根因）。
    {
        int32 CdoV = -999;
        if (const ARemote_FactoryEquipment* CDO = Cast<ARemote_FactoryEquipment>(
                ARemote_FactoryEquipment::StaticClass()->GetDefaultObject()))
        {
            CdoV = static_cast<int32>(CDO->mArmAnimation);
        }
        int32 C1P = -999;
        if (const USkeletalMeshComponent* B1P = Character->GetMesh1P())
        {
            if (const UFGAnimPlayer* AI1P = Cast<UFGAnimPlayer>(B1P->GetAnimInstance()))
            {
                C1P = static_cast<int32>(AI1P->mArmSlotType);
            }
        }
        const int32 C3P = Player ? static_cast<int32>(Player->mArmSlotType) : -999;
        const int32 InstV = static_cast<int32>(mArmAnimation);
        const double Now = FPlatformTime::Seconds();
        if ((InstV != GWT_ArmTraceInst || C1P != GWT_ArmTraceC1P) && (Now - GWT_ArmTraceSec) > 2.0)
        {
            GWT_ArmTraceInst = InstV;
            GWT_ArmTraceC1P  = C1P;
            GWT_ArmTraceSec  = Now;
            WT_WARNF(TEXT("[armtrace] 1P=%d inst=%d cdo=%d cache1P=%d cache3P=%d playerNull=%d self=%d"),
                     b1P ? 1 : 0, InstV, CdoV, C1P, C3P, Player ? 0 : 1, SlotEquip == this ? 1 : 0);
        }
    }

    // 兜底：槽位注册确为本装备、而动画缓存没跟上 → 直写（UPROPERTY 公有，C++ 可写）。
    // ★0.2.105 曾强写 AE_Generic1Hand —— 0.2.107 起用户报「头变大」，0.2.113/0.2.114 两轮蒙太奇
    //   排除后定位泵在 AnimBP Generic1Hand 分支 + 非空 idle3p 组合（证据链见 GWT_3pArmSlotOverride 处）。
    // ★0.2.115 默认回退 AE_ObjectScanner（0.2.90~0.2.104 长期实测：抬臂正常且无骨骼 scale 累乘）。
    //   rf.3pArmSlot 可运行时切换；<0 = 不强写（交还 native 缓存，用于观察 native 原值）。
    // ★0.2.120 域钳制并入条件：本块自 0.2.120 起默认常开，越界 CVar 值（如 999）若放行，
    //   static_cast<uint8> 会环绕成 231 之类非法枚举写进动画蓝图——与 ctor/源头写块同款防线。
    if (!b1P && Player && SlotEquip == this && GWT_3pArmSlotOverride >= 0 &&
        GWT_3pArmSlotOverride <= static_cast<int32>(EArmEquipment::AE_BoomBox))
    {
        const EArmEquipment WantSlot =
            static_cast<EArmEquipment>(static_cast<uint8>(GWT_3pArmSlotOverride));
        if (Player->mArmSlotType != WantSlot)
        {
            Player->mArmSlotType = WantSlot;
        }
    }
    // ★0.2.115 臂槽留痕：实测值变化即记一行（2s 限频防 native 每帧翻转时刷屏——若真发生翻转，
    //   这本身就是 CacheArmSlotData 每帧重缓存的决定性证据，限频后仍可从多条留痕看出）。
    if (!b1P && Player)
    {
        const int32 CurSlot = static_cast<int32>(Player->mArmSlotType);
        const double NowSlotSec = FPlatformTime::Seconds();
        if (CurSlot != GWT_LastLogged3pSlot && (NowSlotSec - GWT_LastSlotLogSec) > 2.0)
        {
            GWT_LastLogged3pSlot = CurSlot;
            GWT_LastSlotLogSec = NowSlotSec;
            WT_WARNF(TEXT("[pose] 3P armSlot now %d (rf.3pArmSlot=%d, slotEquip=%s)."),
                     CurSlot, GWT_3pArmSlotOverride,
                     SlotEquip ? *SlotEquip->GetName() : TEXT("<null>"));
        }
    }
    // ★0.2.89 姿势强制播放：0.2.87 实测数据层全对（[pose] armSlot=10 slotEquip=本装备 mesh3PVis=1）
    // 而 3P 手臂仍不举 → 断点在「姿势由谁播」。原版扫描仪 CDO 的 mArmAnimation 本就是 AE_None
    // （0.2.4x 日志 scanner-accurate），即原版 3P 举持不靠 ArmSlot 状态机，而是 mIdlePoseAnimation3p
    // 以蒙太奇方式播在角色 3P 网格的 Slot 节点上。引擎源码实锤（AnimMontage.cpp:3093 起）：
    //   PlaySlotAnimationAsDynamicMontage 的 LoopCount 直通 FAnimSegment::LoopingCount，
    //   GetLength = LoopingCount * 时长 → 0 即蒙太奇长度 0 必然失败，用大值近似无限循环。
    // Slot 名用 GWT_3pSlotName（原版蒙太奇资产里的真实值，见 ApplyHandheldHandMesh），缺省回退
    // UE 标准 DefaultSlot。本装备期只播一次（防每帧重启），UnEquip 复位并停表。
    // ★0.2.93 改「slot 状态驱动」：0.2.91 实测 3P 姿势已修好，但用户飞行落地后姿势又丢回垂手——
    // 飞行/切视角时原生动画系统会清掉 UpperBody slot 上的蒙太奇，而 0.2.89~0.2.91 的
    // bIdle3pMontagePlayed「只播一次」标记还在 → 永不重播 → 姿势永久丢失。现改为每帧检查
    // IsSlotActive(Slot)（引擎契约：AnimInstance.h:601，遍历活跃蒙太奇评估态，动态蒙太奇同样覆盖）：
    // slot 空闲即重播、被占用（含 equip 蒙太奇播放期）就让位——飞行丢姿势自动恢复，
    // 且与 0.2.93 新增的装备动画无缝衔接（equip 播完 slot 空出 → idle 自动接管）。
    // 防每帧重启：IsSlotActive 天然门控；若动态蒙太奇创建失败（Played==null，slot 永远空闲），
    // 置 bIdle3pMontagePlayed=true 永久放弃重试并打警告，避免每帧重建蒙太奇刷日志。
    // ★0.2.95 蒙太奇目标按视角分流：3P=身体网格（GetMesh3P 的动画实例）；1P=1P 手臂网格
    // （GetMesh() 的动画实例）。两网格骨架同源（Character_Skeleton），UpperBody slot 节点均存在。
    // 播放/门控只用 UAnimInstance 基类 API（IsSlotActive/PlaySlotAnimationAsDynamicMontage），
    // 不依赖 UFGAnimPlayer 派生——1P 动画实例即使非 UFGAnimPlayer 也能播。
    USkeletalMeshComponent* PoseMesh = b1P ? Character->GetMesh() : Body3P;
    UAnimInstance* PoseAI = PoseMesh ? PoseMesh->GetAnimInstance() : nullptr;
    if (PoseAI && SlotEquip == this)
    {
        // ★0.2.101 1P 举持姿势反射注入：0.2.100 探针实锤 aipose=<null>（1P AnimInstance 的
        // UFGAnimPlayer1p::mEquipmentIdlePose 为空）而 armSlot=10（基类 CacheArmSlotData 写枚举正常）。
        // C++ 保护规则实锤：mEquipmentIdlePose 在派生类 UFGAnimPlayer1p（protected），基类
        // CacheArmSlotData 摸不到 → 该字段由 UFGAnimPlayer1p::NativeUpdateAnimation（native）写；
        // 它对我们返回 null 的最可能机制是内部 Cast<AFGObjectScanner>（我们非其子类）。
        // 修法：每帧检查该字段，值不对且 GWT_Idle1pAsset 有值即反射写入（反射不受 protected 限制，
        // 与加载期写 mIdlePoseAnimation 同款模式）→ 原版状态机 ObjectScanner 分支有姿势可播，
        // 手臂真正抬起，hand_rSocket 到相机前方 → 设备走真 socket 跟随（不再依赖我们蒙太奇先抬臂）。
        // ★0.2.102 实例姿势补写（时序竞态根治，本轮主修）：0.2.101 日志实锤 AnimInstance 上的
        // mEquipmentIdlePose 被 native 每帧覆写回 null（每次 prev=null）→ 注入治标不治本。
        // native 每帧取数的自然路径是基类公有 getter GetIdlePoseAnimation()（FGEquipment.h:328，任何
        // 装备类都通，Nobelisk/Generic1Hand 等无专属子类也靠它举臂）→ 它读的是**装备实例**的
        // mIdlePoseAnimation。我方该实例值只在 ctor 直写（cpp:269），0.2.91 已实锤首装备 ctor 早于
        // 资产加载 → 跳过 → 永久 null 且从无补写（0.2.91 只给 3P 蒙太奇路径补了静态值，漏了实例）。
        // 这里每帧确保实例值与加载期静态一致（private → 反射写，同 ctor 模式）：写入后 native 自己
        // 每帧读到非空 → 原版状态机 ObjectScanner 分支原生接管（含手 IK），不再依赖我们蒙太奇先抬臂。
        // 仅 1P 分支触碰（3P 已由我方 slot 蒙太奇稳定工作，不引入变量）。一次性日志防刷。
        if (b1P && GWT_Idle1pAsset)
        {
            static FObjectProperty* InstIdleP = FindFProperty<FObjectProperty>(
                AFGEquipment::StaticClass(), TEXT("mIdlePoseAnimation"));
            if (InstIdleP)
            {
                void* InstPtr = InstIdleP->ContainerPtrToValuePtr<void>(this);
                UObject* CurInst = InstIdleP->GetObjectPropertyValue(InstPtr);
                if (CurInst != static_cast<UObject*>(GWT_Idle1pAsset))
                {
                    InstIdleP->SetObjectPropertyValue(InstPtr, GWT_Idle1pAsset);
                    if (!bIdle1pInstPoseEnsured)
                    {
                        bIdle1pInstPoseEnsured = true;
                        WT_WARNF(TEXT("[pose-inst] instance mIdlePoseAnimation ensured=%s (prev=%s) — ctor timing race patched; expect aipose non-null next frames."),
                                 *GWT_Idle1pAsset->GetName(),
                                 CurInst ? *CurInst->GetName() : TEXT("null"));
                    }
                }
            }
        }
        // ★0.2.102 反射注入 mEquipmentIdlePose 保底（0.2.103 保留）：即使 native 每帧清 null，我方
        // 每帧补写 ObjectScannerIdle_01 → AnimInstance 有姿势可播、手 IK 有数据；与下方蒙太奇抬臂并存不冲突。
        if (b1P && GWT_Idle1pAsset && PoseAI->IsA<UFGAnimPlayer1p>())
        {
            static FObjectProperty* IdlePoseP = FindFProperty<FObjectProperty>(
                UFGAnimPlayer1p::StaticClass(), TEXT("mEquipmentIdlePose"));
            if (IdlePoseP)
            {
                void* PosePtr = IdlePoseP->ContainerPtrToValuePtr<void>(PoseAI);
                UObject* CurPose = IdlePoseP->GetObjectPropertyValue(PosePtr);
                if (CurPose != static_cast<UObject*>(GWT_Idle1pAsset))
                {
                    IdlePoseP->SetObjectPropertyValue(PosePtr, GWT_Idle1pAsset);
                    // 限流日志：只按 5s 时间窗打（若 native 每帧清 null，重写每帧发生但不刷屏）。
                    static double LastInjectLogSec = -1e9;
                    const double InjectNowSec = FPlatformTime::Seconds();
                    if ((InjectNowSec - LastInjectLogSec) > 5.0)
                    {
                        LastInjectLogSec = InjectNowSec;
                        WT_WARNF(TEXT("[pose1p-fix] mEquipmentIdlePose injected=%s (prev=%s) — vanilla scanner hold pose armed."),
                                 *GWT_Idle1pAsset->GetName(),
                                 CurPose ? *CurPose->GetName() : TEXT("null"));
                    }
                }
            }
        }
        const FName SlotName = GWT_3pSlotName.IsNone() ? FName(TEXT("DefaultSlot")) : GWT_3pSlotName;
        // ★0.2.97 重播冷却：0.2.96 实测 1P 下 slot 每 ~114ms 被原生侧清一次（日志 [pose1p] re-arm 密集
        // 出现 + [1p] sockFrontDot 在 0.88/-0.03 间闪），纯 IsSlotActive 门控退化为「每帧重建蒙太奇」
        // → BlendIn 反复打断手臂姿势（抖）+ 设备路径 socket/fallback 每帧互换（卡手上/飘交替）。
        // 冷却 0.75s：被清后最多 0.75s 恢复姿势，重建频率降 6 倍以上，视觉连续。
        const double NowSec = FPlatformTime::Seconds();
        const bool bReplayCooldown = (NowSec - LastPoseMontagePlaySec) < 0.75;
        // ★0.2.106 1P 完全退出蒙太奇重播：armSlot 改走 AE_Generic1Hand 后，1P 抬臂由 native 状态机驱动
        // （飞行态实测 dot=0.88/mode=socket 唯一被证实能抬臂的通路）。我方动态蒙太奇挂在 slot 上会把
        // native 抬到位的手臂"僵在姿势里"（群友实测警告：montage slot 空状态会锁死姿势）。注入块照常
        // 保留（喂数据给 AnimInstance，不挂 slot，无僵臂风险）。3P 分支行为不变（蒙太奇已实测可用）。
        // ★0.2.114 实验：3P 动态举持蒙太奇停用（当时误判为真凶）——0.2.115 实测证明停用后
        //   char3P 仍逐帧累乘，真凶其实是 mArmAnimation=15。
        // ★0.2.117 恢复本蒙太奇为默认启用（它就是 0.2.104 的 3P 抬臂驱动器：臂槽 10 本身不抬臂）；
        //   rf.3pDynamicIdle 可运行时开关（0=停用，用于再次二分）。一次性留痕便于实测判定。
        if (!GWT_3pDynamicIdleLog)
        {
            GWT_3pDynamicIdleLog = true;
            WT_WARNF(TEXT("[pose] 3P dynamic idle montage %s (0.2.117 restores 0.2.104 hold-arm driver; rf.3pDynamicIdle=%d)."),
                     GWT_Use3pDynamicIdle ? TEXT("ENABLED") : TEXT("DISABLED"),
                     GWT_Use3pDynamicIdle);
        }
        if (!b1P && GWT_Use3pDynamicIdle && !PoseAI->IsSlotActive(SlotName) && !bReplayCooldown)
        {
            // ★0.2.91 釜底抽薪：播放序列直接用加载期静态 GWT_Idle3pAsset，实例属性降为二级兜底。
            // 五轮悬案根因（0.2.90 日志+时序实锤）：首个装备实例的 ctor 跑在 ApplyHandheldHandMesh
            // 资产加载完成之前（日志 18.52.50:355 先 equip、:659 才打 CDO pose check）——ctor 执行时
            // GWT_Idle3pAsset 尚为空 → 跳过写 → 实例 mIdlePoseAnimation3p 永久 null（加载期 CDO 写对
            // SpawnActor 实例本就不传导，ctor 是唯一传导路，偏偏撞上时序竞态）。静态在加载完成后必然
            // 有值（后续每次 equip 实测 static=ObjectScannerIdle_01），直接用它不再依赖任何传导链。
            UAnimSequence* Idle3p = GWT_Idle3pAsset ? GWT_Idle3pAsset : GetIdlePoseAnimation3p();
            if (Idle3p)
            {
                UAnimMontage* Played = PoseAI->PlaySlotAnimationAsDynamicMontage(
                    Idle3p, SlotName,
                    /*BlendInTime=*/0.2f, /*BlendOutTime=*/0.2f,
                    /*InPlayRate=*/1.f, /*LoopCount=*/1000000);
                if (Played)
                {
                    LastPoseMontagePlaySec = NowSec;   // ★0.2.97 冷却起点
                    WT_WARNF(TEXT("[pose%s] slot=%s idle3p=%s played=1 (slot was idle; re-arm after view/flight reset)"),
                             b1P ? TEXT("1p") : TEXT("3p"),
                             *SlotName.ToString(), *Idle3p->GetName());
                }
                else
                {
                    // 创建失败：slot 永远不会变活跃，置位放弃以免每帧重建刷日志（1P/3P 各自独立标记）。
                    if (b1P)
                    {
                        bIdle1pMontageFailed = true;
                    }
                    else
                    {
                        bIdle3pMontagePlayed = true;
                    }
                    WT_WARNF(TEXT("[pose%s] slot=%s idle3p=%s played=0 — dynamic montage creation failed; giving up re-arm."),
                             b1P ? TEXT("1p") : TEXT("3p"),
                             *SlotName.ToString(), *Idle3p->GetName());
                }
            }
            else if (!bIdle3pNullLogged)
            {
                bIdle3pNullLogged = true;
                WT_WARNF(TEXT("[pose3p] idle pose asset missing on BOTH static and instance (static=%s instance=%s)."),
                         GWT_Idle3pAsset ? *GWT_Idle3pAsset->GetName() : TEXT("<null>"),
                         GetIdlePoseAnimation3p() ? *GetIdlePoseAnimation3p()->GetName() : TEXT("<null>"));
            }
        }
    }
}

void ARemote_FactoryEquipment::BeginPlay()
{
    // ★0.2.104 父类切 AFGObjectScanner 后：**跳过扫描仪的 BeginPlay**（重建屏材质渲染目标/
    // 扫描计时器/EquipDefaultScannableResource 默认扫描物）——全是扫描行为副作用，与举持无关
    // （1P 抬臂由角色动画实例按类 Cast 驱动，不依赖扫描仪 BeginPlay）。只调 AFGEquipment 初始化。
    // 顺带压掉继承来的 mScannerSkeletalMesh（native ctor 可能带原版扫描仪网格 → 与 HandMesh 重复）。
    AFGEquipment::BeginPlay();
    if (mScannerSkeletalMesh)
    {
        mScannerSkeletalMesh->SetVisibility(false);
        // ★0.2.112 群友定位「头变大」真根因：spring arm 的碰撞测试（bDoCollisionTest）——装备
        //   spawn 那帧模型恰挡在相机前 → boom 被顶近一瞬（头变大）→ 挂到手 socket 模型挪开 →
        //   弹回（自己恢复）。HandMesh ctor 已 NoCollision，但这里继承的扫描仪网格此前**只隐藏
        //   不关碰撞**，正是那个「挡在相机前的模型」。直接关碰撞 + 对 Camera 通道双保险 Ignore。
        mScannerSkeletalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        mScannerSkeletalMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
        WT_WARNF(TEXT("[scan-off] inherited mScannerSkeletalMesh hidden (name=%s)"),
                 *mScannerSkeletalMesh->GetName());
    }

    // ★0.2.112 群友方案②：装备网格对相机碰撞通道 Ignore——spawn 位置挡相机也不再触发 boom
    //   碰撞测试顶近。HandMesh ctor 已 NoCollision（再设 Ignore 属双保险，NoCollision 本就不参与）。
    if (HandMesh)
    {
        HandMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    }

    // ★0.2.108 修复「切装备瞬间头变大（3P）」：群友定位=相机 spring arm 那一瞬间被缩短，非角色变大；
    //   根因是挂接时序——装备 spawn 时停在 spawn 位置（角色身上），下一帧才被 native Equip 挂到
    //   hand_rSocket，中间隔一帧 → 相机弹簧臂在这一帧被打到/被顶短。修复（群友方案）：BeginPlay 时
    //   角色已知就**直接预挂手 socket**（mAttachSocket=hand_rSocket 与 native 同值，0.2.26 枚举实锤
    //   挂接目标=GetMesh()），native Equip 的挂接退化为同 socket 同父重挂（no-op），中间帧消失。
    //   三个守卫：角色有效、socket 存在、尚未挂在该角色上（防重复挂接）。
    if (AFGCharacterPlayer* Ch = GetInstigatorCharacter())
    {
        if (USkeletalMeshComponent* Body = Ch->GetMesh())
        {
            const FName Sock = GWT_HandAttachSocket.IsNone() ? FName(TEXT("hand_rSocket")) : GWT_HandAttachSocket;
            if (GetAttachParentActor() != Ch && Body->DoesSocketExist(Sock))
            {
                AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Sock);
                WT_WARNF(TEXT("[spawn] pre-attached actor to %s on %s (gap-frame fix)"),
                         *Sock.ToString(), *Body->GetName());
            }
        }

        // ★0.2.109 群友思路「BeginPlay 里赋的更早」：spawn→BeginPlay→Equip 时序下此刻早于 native
        //   Equip。先记「好臂长」基线（>1 才记，防把坏值记成基线）；若此刻已被推坏（≤1）且基线在，
        //   立即拉回——「两处判 >0」的第一处（第二处=Tick 每帧守卫）。
        if (USpringArmComponent* Spring = Ch->GetSpringArmComponent())
        {
            // ★0.2.112 群友方案①验证数据：boom 碰撞测试开关状态留痕（true=本 mod 的通道 Ignore
            //   修复生效的前提；若 false 则碰撞测试本来就没开，头变大另有其因）。
            WT_WARNF(TEXT("[armguard] spring arm bDoCollisionTest=%d at BeginPlay"),
                     Spring->bDoCollisionTest ? 1 : 0);
            const float Cur = Spring->TargetArmLength;
            if (Cur > 1.f)
            {
                GWT_GoodArmLen = Cur;
                GWT_LastArmLen = Cur;
                WT_WARNF(TEXT("[armguard] baseline %.1f captured at BeginPlay"), Cur);
            }
            else if (GWT_GoodArmLen > 1.f)
            {
                Spring->TargetArmLength = GWT_GoodArmLen;
                GWT_LastArmLen = GWT_GoodArmLen;
                WT_WARNF(TEXT("[armguard] BeginPlay found arm length %.2f <= 1, restored to %.1f"),
                         Cur, GWT_GoodArmLen);
            }
        }
    }

    // ★0.2.142 终端屏幕 logo 不在 BeginPlay 应用：此帧实例 HandMesh 还没网格
    //   （GetNumMaterials()==0，0.2.142 首版实测 [screen] num=0 即此因）——网格在 Equip 时
    //   才赋上，改由 Equip（网格就绪后）与 vigil 材质重置后调用 ApplyHandheldScreenLogo()。
}

// ★0.2.142 终端屏幕显示 Smart Factory logo（白底）。
//   ★0.2.144 重写（大肥鱼处方 A+B，槽位认知已按一手 DumpedObjects 修正）：
//   ★0.2.145 换底方案（用户拍板"自烤白底材质"）：
//   SK_ObjectScanner_01 网格 3 材质槽【槽名与实际内容错位】：
//     [0] ObjectScanner -> MI_ObjectScanner_01       （机身，跳过）
//     [1] ScreenMat     -> MI_GlassBuilding_Inst     （名"屏幕"实为玻璃 MIC，跳过）
//     [2] GlassMat      -> Material_UI_ObjectScanner （名"玻璃"实为 UI 材质 = 真屏面）
//   0.2.144 用"参数化清白"贴 [1]+[2]，但 [2] 的琥珀色是 Material_UI_ObjectScanner 的
//   【烘焙常数】（vec=0，参数层物理不可达）⇒ 屏幕照旧琥珀。故本版改为：
//   ★只对 [2] 真屏面（底材质名含 "Material_UI_ObjectScanner"）整体替换为我们自烤的
//     无光照材质 M_ScreenLogo（/Smart_Factory/Materials/M_ScreenLogo，由编辑器 Python 烘焙、
//     Alpakit 重烤进 pak）；M_ScreenLogo 仅一个纹理参数 "ScreenTex" → 自发光，
//     白色物理可达（白底靠 ScreenLogo.png 自身，不再依赖材质常数）。
//     [0]/[1] 保持原版外观，不再误贴（0.2.144 把玻璃槽贴白是副作用）。
//   ★回退：若 M_ScreenLogo 加载失败（部署漏带烘焙材质），退回 0.2.144 参数化兜底
//     （克隆底材质→纹理全指 logo、向量清白），至少不比原版差。
//   ★不调用 AFGObjectScanner::BeginPlay（扫描副作用），零扫描副作用。
//   ★调用时机：HandMesh 网格就绪之后（Equip 赋网格后 / vigil 材质重置后）。
void ARemote_FactoryEquipment::ApplyHandheldScreenLogo()
{
    if (!HandMesh || HandMesh->GetNumMaterials() == 0)
    {
        WT_WARNF(TEXT("[screen] skip: HandMesh not ready (num=%d)"),
                 HandMesh ? HandMesh->GetNumMaterials() : -1);
        return;
    }

    // ★散装白底 logo（512x512 白底 PNG），随 Content 镜像部署，运行时载入
    const FString LogoPath = FPaths::Combine(FPaths::ProjectModsDir(),
                                             TEXT("Smart_Factory/Content/ScreenLogo.png"));
    UTexture2D* LogoTex = UKismetRenderingLibrary::ImportFileAsTexture2D(this, LogoPath);
    if (!LogoTex)
    {
        WT_WARNF(TEXT("[screen] logo texture load FAILED (file missing?): %s"), *LogoPath);
        return;
    }

    // ★0.2.145 自烤白底材质（编辑器 Python 烘焙，包名 /Smart_Factory/Materials）
    UMaterial* ScreenBaseMat = LoadObject<UMaterial>(
        nullptr, TEXT("/Smart_Factory/Materials/M_ScreenLogo.M_ScreenLogo"));
    if (!ScreenBaseMat)
    {
        WT_WARNF(TEXT("[screen] M_ScreenLogo load FAILED (baked material missing?) -> fallback to 0.2.144 param logo"));
    }

    const TArray<FName> SlotNames = HandMesh->GetMaterialSlotNames();
    int32 Applied = 0;
    for (int32 i = 0; i < HandMesh->GetNumMaterials(); ++i)
    {
        UMaterialInterface* Base = HandMesh->GetMaterial(i);
        if (!Base)
        {
            continue;
        }

        // ★复用：已是我们放的 M_ScreenLogo MID 则直接复用并刷新纹理（避免反复 Create 链式累积）
        if (UMaterialInstanceDynamic* Existing = Cast<UMaterialInstanceDynamic>(Base))
        {
            if (ScreenBaseMat && Existing->Parent == ScreenBaseMat)
            {
                Existing->SetTextureParameterValue(FName(TEXT("ScreenTex")), LogoTex);
                HandMesh->SetMaterial(i, Existing);
                ++Applied;
                continue;
            }
        }

        // ★只命中真屏面：底材质是 Material_UI_ObjectScanner（琥珀烘焙常数，参数不可达）
        const bool bTrueScreen = Base->GetPathName().Contains(TEXT("Material_UI_ObjectScanner"));
        if (!bTrueScreen)
        {
            continue;  // [0] 机身 / [1] 玻璃 MIC：保持原版外观，不再误贴
        }

        if (ScreenBaseMat)
        {
            // ★真屏面 → 整体替换为自烤白底 M_ScreenLogo（Unlit + "ScreenTex"→自发光）
            UMaterialInstanceDynamic* NewMID = UMaterialInstanceDynamic::Create(ScreenBaseMat, this);
            NewMID->SetTextureParameterValue(FName(TEXT("ScreenTex")), LogoTex);
            HandMesh->SetMaterial(i, NewMID);
            ++Applied;
            WT_WARNF(TEXT("[screen] 0.2.153 white M_ScreenLogo applied: slot=%d(%s) base=%s path=%s"),
                     i, *(SlotNames.IsValidIndex(i) ? SlotNames[i].ToString() : FString()),
                     *Base->GetName(), *LogoPath);
        }
        else
        {
            // ★回退：M_ScreenLogo 缺失，用 0.2.144 参数化兜底（克隆底材质→纹理全指 logo、向量清白）
            UMaterialInstanceDynamic* FallMID = UMaterialInstanceDynamic::Create(Base, this);
            TArray<FMaterialParameterInfo> TexInfos, VecInfos, DVecInfos, ScalarInfos;
            TArray<FGuid> Ids;
            Base->GetAllTextureParameterInfo(TexInfos, Ids);
            Base->GetAllVectorParameterInfo(VecInfos, Ids);
            Base->GetAllDoubleVectorParameterInfo(DVecInfos, Ids);
            Base->GetAllScalarParameterInfo(ScalarInfos, Ids);
            for (const FMaterialParameterInfo& TI : TexInfos)
            {
                FallMID->SetTextureParameterValue(TI.Name, LogoTex);
            }
            if (TexInfos.Num() == 0)
            {
                FallMID->SetTextureParameterValue(FName(TEXT("ScreenTexture")), LogoTex);
                FallMID->SetTextureParameterValue(FName(TEXT("Texture")), LogoTex);
            }
            for (const FMaterialParameterInfo& VI : VecInfos)
            {
                FallMID->SetVectorParameterValue(VI.Name, FLinearColor::White);
            }
            for (const FMaterialParameterInfo& DI : DVecInfos)
            {
                FallMID->SetDoubleVectorParameterValue(DI.Name, FVector4(1.0, 1.0, 1.0, 1.0));
            }
            HandMesh->SetMaterial(i, FallMID);
            ++Applied;
            WT_WARNF(TEXT("[screen] fallback param-logo applied: slot=%d base=%s tex=%d vec=%d"),
                     i, *Base->GetName(), TexInfos.Num(), VecInfos.Num());
        }
    }

    if (Applied == 0)
    {
        WT_WARNF(TEXT("[screen] no TRUE screen slot (Material_UI_ObjectScanner) found (num=%d)"),
                 HandMesh->GetNumMaterials());
    }
}

void ARemote_FactoryEquipment::AddEquipmentActionBindings()
{
    // ★0.2.104：跳过 AFGObjectScanner 的绑定（Input_OpenMenu 扫描菜单 / Input_Cycle 左右循环）
    // ——那会抢玩家输入弹原版扫描仪 UI。只保留 AFGEquipment 基类默认绑定；我们的右键开面板
    // 走自有 ToggleInputComponent（ApplyToggleInput），与本次无关。
    AFGEquipment::AddEquipmentActionBindings();
}

void ARemote_FactoryEquipment::Tick(float DeltaSeconds)
{
    // ★0.2.104：父类已是 AFGObjectScanner，Super::Tick 会跑扫描仪每帧逻辑（UpdateClosestObject/
    // 距离/哔哔节拍）→ 显式调 AActor::Tick 跳过（AFGEquipment 未覆写 Tick，旧 Super::Tick 本就
    // 解析到 AActor::Tick，行为与 0.2.103 完全一致）。
    AActor::Tick(DeltaSeconds);
    // ★0.2.86：每帧把手持件锚点摆到正确位置（1P=相机前 / 3P=角色手部 socket）。
    // 此前只在 1P 调用，3P 下锚点永不刷新 → 设备停在陈旧位置（胯部）。角色手臂摆动时
    // 3P 也需每帧跟随 socket，故两种视角都更新（UpdateViewModelTransform 内部按视角分支）。
    AFGCharacterPlayer* Character = GetInstigatorCharacter();
    if (Character && IsEquipped())
    {
        // ★0.2.110 群友排查「相机中途是不是切到 1P 那套」：装备期间相机模式翻转必留痕（只记不拦）。
        static ECameraMode GWT_LastCamModeLog = ECameraMode::ECM_None;
        const ECameraMode CurCamMode = Character->GetCameraMode();
        if (CurCamMode != GWT_LastCamModeLog)
        {
            WT_WARNF(TEXT("[armguard] camMode flip: %d -> %d (equip active)"),
                     (int32)GWT_LastCamModeLog, (int32)CurCamMode);
            GWT_LastCamModeLog = CurCamMode;
        }
        // ★0.2.111 群友排查「装备 mesh 的 AnimBP 有没有 scale 曲线」：判据不猜资产——骨骼 scale
        //   无论源头（AnimBP 曲线/montage 曲线/代码 SetBoneScale）效果必经骨骼变换数组，直接每帧
        //   扫 GetComponentSpaceTransforms()（SkinnedMeshComponent.h:1625）的 Scale3D，任何骨骼
        //   偏离 1 超 5% 都留痕（GetBoneName:1083 取名，Mesh 名作 Tag，去重数组 Equip 时清空）。
        //   头变大的直接观测=char3P 头部链骨骼 scale != 1；equip 网格命中则坐实 AnimBP scale 曲线。
        auto SweepBoneScale = [](USkeletalMeshComponent* Mesh, TArray<int32>& Hits, const TCHAR* Tag) -> float
        {
            if (!Mesh || !Mesh->IsRegistered())
            {
                return 0.f;
            }
            const TArray<FTransform>& CS = Mesh->GetComponentSpaceTransforms();
            // ★0.2.123 处方④（comp/local 两组数）+ ★0.2.124 返回本帧 CS 峰值给爆发断头用：
            //   comp=组件自身 scale（!=1 ⇒ 组件级被放大）；local=父骨相对 scale（Parent⁻¹·Child
            //   反解）；comp=1 而 local!=1 ⇒ 纯继承链；同帧各骨 local 同值 ⇒ 整副骨架逐层同乘。
            const FVector CompScale = Mesh->GetComponentScale();
            const USkeletalMesh* SM = Mesh->GetSkeletalMeshAsset();
            const FReferenceSkeleton* RS = SM ? &SM->GetRefSkeleton() : nullptr;
            float MaxCS = 0.f;
            for (int32 i = 0; i < CS.Num(); ++i)
            {
                const FVector S = CS[i].GetScale3D();
                MaxCS = FMath::Max(MaxCS, S.X);
                if (!FMath::IsNearlyEqual(S.X, 1.f, 0.05f) || !FMath::IsNearlyEqual(S.Y, 1.f, 0.05f)
                    || !FMath::IsNearlyEqual(S.Z, 1.f, 0.05f))
                {
                    const bool bNew = !Hits.Contains(i);
                    if (bNew)
                    {
                        FVector LocalS = S;
                        if (RS && i > 0)
                        {
                            const int32 ParentIdx = RS->GetParentIndex(i);
                            if (CS.IsValidIndex(ParentIdx))
                            {
                                LocalS = (CS[ParentIdx].Inverse() * CS[i]).GetScale3D();
                            }
                        }
                        Hits.Add(i);
                        WT_WARNF(TEXT("[bonescale] %s bone %d (%s): scale=%s local=%s comp=%s rootmotion=%d"),
                                 Tag, i, *Mesh->GetBoneName(i).ToString(), *S.ToCompactString(),
                                 *LocalS.ToCompactString(), *CompScale.ToCompactString(),
                                 Mesh->IsPlayingRootMotion() ? 1 : 0);
                    }
                }
            }
            return MaxCS;
        };
        if (USkeletalMeshComponent* B3P = Character->GetMesh3P())
        {
            const float BurstCS = SweepBoneScale(B3P, GWT_ScaleHit3P, TEXT("char3P"));
            // ★0.2.124 DeepSeek「爆发帧归零得再看日志」：CS>4 的帧一次性断头 —— 同帧打出
            //   3P AnimInstance 的 mArmSlotType 实况、当前活动蒙太奇名、slot 权重，
            //   一行日志直接回答「是哪两套 arm pose 在乘 / 是不是 ObjectScannerIdle_01 自带 track」。
            // ★0.2.124 DeepSeek 追加三条落码：
            //  a)「新头条记得带冷却，不然又变成上次那种一秒三条」——事件沿武装：CS 回落
            //     <=1.05 才重新武装，上升沿只打一条；另加 3s 硬冷却兜底（防快速连爆刷屏）。
            //  b)「每帧重应用注入门可以，但别顺手把 mArmAnimation 也带进去」——重应用块只写
            //     mIdlePoseAnimation3p（见 EnforceArmPoseCache 头部），mArmAnimation 仍只在
            //     bSrcEdge 沿上写（结构断言已固化此隔离，此处纯声明）。
            //  c)「CS 峰值和爆发帧的先后要打出来，峰值在前才算因」——peakAge=CS 峰值已持续
            //     时长（跨 1.05 上升沿起算），montAge=距上次 3P 动态蒙太奇重建时长：
            //     peakAge < montAge ⇒ 蒙太奇重建在前（蒙太奇是因，乘法叠加模型成立）；
            //     peakAge > montAge ⇒ 峰值先于最近一次蒙太奇重建（峰值在前，另有源头）。
            //     帧号用日志行自带前缀即可关联其它行，不重复打。
            static double GWT_LastBurstHeaderSec = 0.0;
            static bool   GWT_BurstHeaderArmed = true;
            static double GWT_PeakOnsetSec = -1.0;
            static bool   GWT_PeakOngoing = false;
            const double NowBurst = FPlatformTime::Seconds();
            if (BurstCS > 1.05f && !GWT_PeakOngoing)
            {
                GWT_PeakOngoing = true;
                GWT_PeakOnsetSec = NowBurst;
            }
            else if (BurstCS <= 1.05f)
            {
                GWT_PeakOngoing = false;
                GWT_BurstHeaderArmed = true;   // episode 结束，重新武装上升沿
            }
            if (BurstCS > 4.f && GWT_BurstHeaderArmed
                && (NowBurst - GWT_LastBurstHeaderSec) > 3.0)
            {
                GWT_BurstHeaderArmed = false;
                GWT_LastBurstHeaderSec = NowBurst;
                UAnimInstance* AI3P = B3P->GetAnimInstance();
                UFGAnimPlayer* Player3P = Cast<UFGAnimPlayer>(AI3P);
                const int32 Cache3P = Player3P ? static_cast<int32>(Player3P->mArmSlotType) : -999;
                const FName BurstSlot = GWT_3pSlotName.IsNone() ? FName(TEXT("DefaultSlot")) : GWT_3pSlotName;
                UAnimMontage* ActMont = AI3P ? AI3P->GetCurrentActiveMontage() : nullptr;
                const double PeakAge = (GWT_PeakOnsetSec > 0.0) ? (NowBurst - GWT_PeakOnsetSec) : -1.0;
                const double MontAge = (LastPoseMontagePlaySec > -1e8) ? (NowBurst - LastPoseMontagePlaySec) : -1.0;
                WT_WARNF(TEXT("[burst] CS>4: peak=%.0f peakAge=%.3fs montAge=%.3fs cache3P=%d mont=%s slotW=%.2f"),
                         BurstCS, PeakAge, MontAge, Cache3P,
                         ActMont ? *ActMont->GetName() : TEXT("<none>"),
                         AI3P ? AI3P->GetSlotMontageLocalWeight(BurstSlot) : 0.f);
            }
        }
        // ★0.2.113 群友 leader-pose 假设验证：若 3P 身体 scale 是跟着 1P 动画传导的
        //   （身体 SetLeaderPoseComponent 到 1P 手臂网格，LeaderPoseComponent 为 protected
        //   非 UPROPERTY 无法直读留痕），则 0.2.107 的 1P 蒙太奇播放期间 char1P 自身必先
        //   出现同款 scale 异常 —— 本通道命中即坐实传导链，1P 蒙太奇就是根因。
        if (USkeletalMeshComponent* B1P = Character->GetMesh1P())
        {
            SweepBoneScale(B1P, GWT_ScaleHit1P, TEXT("char1P"));
        }
        SweepBoneScale(HandMesh, GWT_ScaleHitEquip, TEXT("equip"));
        // ★0.2.116 群友仪表（"别猜了，打 log：每秒打一下臂长和相机世界坐标…那一瞬数值掉下去了
        //   就查谁动的，没掉就是画面骗你"）：1Hz 常规采样 + 装备后 3s 内 0.1s 突发采样。
        //   行内容：相机模式 / spring arm 臂长 / 相机世界坐标 / FOV / char3P 全骨骼最大 scale
        //   （不走去重，恢复到 1 还是持续 1024 一眼可判）。bDrawDebugCamera 引擎 Runtime 全源
        //   不存在该属性（群友记岔），等效=本仪表行 + 控制台 toggledebugcamera。
        // ★0.2.125 camwatch 收敛：稳态全 1.000 整段无信息量（大肥鱼），改为仅 3P 进入时打印首帧
        //   一次（及 burst 高分辨率采样期照常），其余帧不刷。1P 期重新武装，下次进 3P 再打首帧。
        static bool GWT_CamWatch3pArmed = true;
        if (CurCamMode == ECameraMode::ECM_FirstPerson)
        {
            GWT_CamWatch3pArmed = true;
        }
        GWT_CamWatchAcc += DeltaSeconds;
        bool bSample = false;
        bool bIsBurst = false;
        if (GWT_CamWatchBurstLeft > 0.f)
        {
            // 触发后用 Fmod 保留累加余量（float 连加 1/60 有精度边缘，清零会漂移采样间隔）。
            GWT_CamWatchBurstLeft -= DeltaSeconds;
            GWT_CamWatchBurstAcc  += DeltaSeconds;
            if (GWT_CamWatchBurstAcc >= 0.1f)
            {
                bSample = true;
                bIsBurst = true;
                GWT_CamWatchBurstAcc = FMath::Fmod(GWT_CamWatchBurstAcc, 0.1f);   // 保留余量，不丢帧
                GWT_CamWatchAcc = 0.f;   // 突发行替代当秒常规行，避免同帧双行
            }
        }
        if (!bSample && GWT_CamWatchAcc >= 1.f)
        {
            bSample = true;
            GWT_CamWatchAcc = 0.f;
        }
        if (bSample && CurCamMode == ECameraMode::ECM_ThirdPerson
            && (GWT_CamWatch3pArmed || bIsBurst))
        {
            float MaxScale = 1.f;
            if (USkeletalMeshComponent* B3P = Character->GetMesh3P())
            {
                const TArray<FTransform>& CS = B3P->GetComponentSpaceTransforms();
                for (int32 i = 0; i < CS.Num(); ++i)
                {
                    MaxScale = FMath::Max(MaxScale, CS[i].GetScale3D().GetMax());
                }
            }
            float ArmLenNow = -1.f;
            if (USpringArmComponent* SpringNow = Character->GetSpringArmComponent())
            {
                ArmLenNow = SpringNow->TargetArmLength;
            }
            FVector CamLoc = FVector::ZeroVector;
            float FovNow = -1.f;
            if (UCameraComponent* Cam = Character->FindComponentByClass<UCameraComponent>())
            {
                CamLoc = Cam->GetComponentLocation();
                FovNow = Cam->FieldOfView;
            }
            WT_WARNF(TEXT("[camwatch] mode=%d armLen=%.2f camLoc=(%.1f, %.1f, %.1f) fov=%.1f maxScale=%.3f burst=%d"),
                     (int32)CurCamMode, ArmLenNow, CamLoc.X, CamLoc.Y, CamLoc.Z, FovNow, MaxScale, bIsBurst ? 1 : 0);
            if (GWT_CamWatch3pArmed)   // 首帧消费武装；burst 采样不消耗（瞬态窗口需连续采样）
            {
                GWT_CamWatch3pArmed = false;
            }
        }
        // ★0.2.109 每帧低水位守卫（「两处判 >0」第二处）：触发点不在 Equip（0.2.108 日志实锤
        //   Equip 内臂长零变化）→ 触发时机未知，守卫必须每帧兜底。仅 3P 相机模式下生效——1P 臂长
        //   本来就近 0，判 >0 会误伤视角切换。拦截两条异常路径：①臂长被打到 ≤1（群友模型：
        //   「父类那个 0」被推进来）；②3P 下一帧内暴跌 >100（native 平滑过渡是每帧小步长，
        //   TransitionToCameraMode 插值数秒，不会误伤；真切的 3P→1P 相机模式已变，守卫根本不进）。
        //   两个坏值条件都不满足时只记录 LastArmLen，绝不写 spring arm——不与 native 打架。
        if (USpringArmComponent* Spring = Character->GetSpringArmComponent())
        {
            const float Cur = Spring->TargetArmLength;
            const bool bZeroPush = (Cur <= 1.f);
            const bool bSuddenDrop = (GWT_LastArmLen > 1.f) && (Cur < GWT_LastArmLen - 100.f);
            if (Character->GetCameraMode() == ECameraMode::ECM_ThirdPerson && (bZeroPush || bSuddenDrop)
                && GWT_GoodArmLen > 1.f)
            {
                Spring->TargetArmLength = GWT_GoodArmLen;
                GWT_LastArmLen = GWT_GoodArmLen;
                WT_WARNF(TEXT("[armguard] per-frame rescue: arm len %.2f (%s), restored to %.1f (3P)"),
                         Cur, bZeroPush ? TEXT("<=1") : TEXT("sudden drop"), GWT_GoodArmLen);
            }
            else
            {
                // ★0.2.109 诊断档：3P 下一帧跌 30~100（不足以判定异常、拦截可能误伤）→ 只记日志
                //   不写 spring arm。若实测"头变大"仍出现而 rescue 行没触发，这里必然留下痕迹，
                //   下一版按真实数值收紧阈值。
                if (Character->GetCameraMode() == ECameraMode::ECM_ThirdPerson
                    && GWT_LastArmLen > 1.f && Cur < GWT_LastArmLen - 30.f)
                {
                    WT_WARNF(TEXT("[armguard] diag: arm len %.1f -> %.1f in one frame (3P, no rescue)"),
                             GWT_LastArmLen, Cur);
                }
                GWT_LastArmLen = Cur;
                // 好值顺带刷新基线（角色设置变更/加载存档后基线自动跟上）。
                if (Cur > 1.f)
                {
                    GWT_GoodArmLen = Cur;
                }
            }
        }
        UpdateViewModelTransform();
        EnforceArmPoseCache(Character);   // ★0.2.87 举持姿势诊断+兜底（★0.2.95 起 1P 也驱动手臂网格蒙太奇）
    }
}

void ARemote_FactoryEquipment::StartCameraWatchdog()
{
    StopCameraWatchdog();
    if (AFGCharacterPlayer* Character = GetInstigatorCharacter())
    {
        bLastAttachedFirstPerson = Character->IsFirstPerson();
    }
    TWeakObjectPtr<ARemote_FactoryEquipment> WeakSelf(this);
    CameraWatchdogHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [WeakSelf](float /*DeltaTime*/) -> bool
        {
            if (!WeakSelf.IsValid())
            {
                return false;   // 自身已销毁，停表
            }
            ARemote_FactoryEquipment* Self = WeakSelf.Get();
            AFGCharacterPlayer* Character = Self->GetInstigatorCharacter();
            // 未装备（mPlayerCharacter 已清空）即停表，等下次 Equip 再起。
            if (!Character || !Self->IsEquipped())
            {
                return false;
            }
            const bool bNow1P = Character->IsFirstPerson();
            if (bNow1P != Self->bLastAttachedFirstPerson)
            {
                Self->ReattachHandheldToView();   // 内部会刷新 bLastAttachedFirstPerson
            }
            // ★v0.2.77 可见性守护 + 取证：角色会主动管理挂在其网格下的装备可见性
            // （FGCharacterPlayer.h:1105 UpdateEquipmentVisibility → :1315 ShouldShowEquipment，
            //  真实现=角色蓝图；0.2.76 日志实锤 Hoverpack_1P vis 随悬浮背包飞行翻转）。
            //  我们的 actor 也挂在 CharacterMesh0 下，疑似被同一套规则"行走藏起/飞行放行"
            //  （用户实测：正常行走无模型、悬浮飞行才显示）。每 0.2s 巡检：一旦偏离期望状态，
            //  先打全套现场（组件 vis/OwnerNoSee/OnlyOwnerSee/actorHidden/根可见性/父链/
            //  身体网格状态/身体下扫描仪副本数），再自愈恢复——下次日志即可锁定"谁在藏模型"。
            if (USkeletalMeshComponent* HM = Self->HandMesh)
            {
                // ★v0.2.79：手持件一旦装备即常显（bWant=true），不再依赖 IsFirstPerson() 误报。
                //   游戏在装备后把 HM 设 vis=0（proxy 随之被引擎销毁）→ 看门狗每 0.2s 强制拉回可见。
                //   1P/3P 都随身体网格可见，与原版扫描仪（手持件在两种视角都显示在手上）一致。
                const bool bWant = true;
                USkeletalMeshComponent* Body = Character->GetMesh();
                USceneComponent* Root = Self->GetRootComponent();
                UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(Root);
                int32 ScannerCopies = 0;
                {
                    TArray<USceneComponent*> Attached;
                    Body->GetChildrenComponents(true, Attached);
                    for (USceneComponent* C : Attached)
                    {
                        const USkeletalMeshComponent* S = Cast<USkeletalMeshComponent>(C);
                        if (S && S->GetSkeletalMeshAsset() &&
                            S->GetSkeletalMeshAsset()->GetName().Contains(TEXT("ObjectScanner")))
                        {
                            ++ScannerCopies;
                        }
                    }
                }
                const bool bParentOk = (HM->GetAttachParent() == Self->ViewModelAnchor);
                const bool bDev = (HM->IsVisible() != bWant) ||
                                  (Root && Root->IsVisible() != bWant) ||
                                  Self->IsHidden() ||
                                  HM->bOwnerNoSee ||
                                  (RootPrim && RootPrim->bOwnerNoSee) ||
                                  !bParentOk ||
                                  !Body->IsVisible();   // ★v0.2.78 补盲区：父级(身体网格)被游戏切隐藏会让子模型跟随隐（HM->IsVisible() 仍 true，旧守护漏检）。
                // ★v0.2.78 关键修复：proxy=0（GetSceneProxy()==null）→ 渲染器未建代理 → 模型画不出。
                //   日志实锤装备后数秒 proxy 空（原版 CDO 构造即带网格注册，代理从建好；我们运行时赋网格→代理迟建/反复建）。
                //   每帧巡检：代理空就强制脏标记，直到网格就绪即建成代理后停（仅在缺失时脏一次，低成本）。
                // ★0.2.92 升级为 RecreateRenderState_Concurrent：0.2.91 实测 [1p] proxy=0 持续存在——
                //   MarkRenderStateDirty() 只在渲染状态**已创建**时才重建；视角切换/原生隐藏曾销毁渲染状态时
                //   （bRenderStateCreated=false），它什么都不做，代理永远建不起来 = 1P 画不出而 3P 正常。
                //   RecreateRenderState_Concurrent() 对未创建态也会走 CreateRenderState，是真重建。
                if (!HM->GetSceneProxy())
                {
                    HM->RecreateRenderState_Concurrent();
                    if (!Self->b1pProxyFixLogged)
                    {
                        Self->b1pProxyFixLogged = true;
                        WT_WARNF(TEXT("[1p-fix] scene proxy missing; render state recreated (mesh=%s vis=%d ownerNoSee=%d actorHidden=%d)."),
                                 HM->GetSkeletalMeshAsset() ? *HM->GetSkeletalMeshAsset()->GetName() : TEXT("<null>"),
                                 HM->IsVisible() ? 1 : 0,
                                 HM->bOwnerNoSee ? 1 : 0,
                                 Self->IsHidden() ? 1 : 0);
                    }
                }
                if (bDev)
                {
                    WT_WARNF(TEXT("[vigil] vis=%d want=%d actorHidden=%d ownerNoSee=%d onlyOwnerSee=%d rootVis=%d rootOwnerNoSee=%d parent=%s | bodyVis=%d bodyOwnerNoSee=%d | scannerCopies=%d"),
                             HM->IsVisible() ? 1 : 0, bWant ? 1 : 0,
                             Self->IsHidden() ? 1 : 0,
                             HM->bOwnerNoSee ? 1 : 0,
                             HM->bOnlyOwnerSee ? 1 : 0,
                             (Root && Root->IsVisible()) ? 1 : 0,
                             (RootPrim && RootPrim->bOwnerNoSee) ? 1 : 0,
                             HM->GetAttachParent() ? *HM->GetAttachParent()->GetName() : TEXT("<none>"),
                             Body->IsVisible() ? 1 : 0,
                             Body->bOwnerNoSee ? 1 : 0,
                             ScannerCopies);
                    // 自愈：只恢复我们自己的显示链（不动角色网格——那是游戏自己管理的）。
                    Self->SetActorHiddenInGame(false);
                    HM->SetOwnerNoSee(false);
                    HM->SetOnlyOwnerSee(false);
                    HM->SetVisibility(bWant, true);
                    if (RootPrim)
                    {
                        RootPrim->SetOwnerNoSee(false);
                    }
                    if (Root)
                    {
                        Root->SetVisibility(bWant, true);
                    }
                    if (!bParentOk)
                    {
                        HM->AttachToComponent(Self->ViewModelAnchor, FAttachmentTransformRules::KeepRelativeTransform);
                        // ★0.2.95：重挂统一用原版握持标定（1P 已改回 socket 跟随 + 同款标定，无视角差异）。
                        HM->SetRelativeLocation(GWT_HandRelLoc);
                        HM->SetRelativeRotation(GWT_HandRelRot);
                        HM->SetRelativeScale3D(GWT_HandRelScale);
                    }
                    HM->MarkRenderStateDirty();
                }
            }
            return true;
        }), 0.2f);
}

void ARemote_FactoryEquipment::StopCameraWatchdog()
{
    if (CameraWatchdogHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(CameraWatchdogHandle);
        CameraWatchdogHandle.Reset();
    }
}

void ARemote_FactoryEquipment::StartVisibilityWatchdog()
{
    // ★v0.2.83 静默可见性守护。原生 UpdateEquipmentVisibility 只认 actor 级 bHidden + 专属
    // mScannerSkeletalMesh 组件（DumpedObjects/Equip_ObjectScanner_C 实锤：actor bHidden=true，
    // 可见网格是 mScannerSkeletalMesh 而非 actor 通用网格）。我们的自定义 WTHandMesh 原生完全
    // 不认识，会被原生通过 actor 级 bHidden 盖成不可见——这是 0.2.80~0.2.82「建模彻底没了」的根因
    // （0.2.82 只点了 HandMesh 可见，没顶住 actor 级隐藏）。故每 0.3s 静默把 actor + HandMesh
    // 拉回可见（0.2.79 同款可见性口径，用户已认可；本版去掉 [vigil] 刷屏，且孤儿已由原生
    // Super::UnEquip 根除，不再需要自愈重挂父链）。无日志，不打扰玩家；actor 销毁或卸装即自停。
    StopVisibilityWatchdog();
    TWeakObjectPtr<ARemote_FactoryEquipment> WeakSelf(this);
    VisibilityWatchdogHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [WeakSelf](float /*DeltaTime*/) -> bool
        {
            if (!WeakSelf.IsValid())
            {
                return false;   // 自身已销毁，停表
            }
            ARemote_FactoryEquipment* Self = WeakSelf.Get();
            AFGCharacterPlayer* Character = Self->GetInstigatorCharacter();
            if (!Character || !Self->IsEquipped())
            {
                return false;   // 未装备即停表
            }
            if (USkeletalMeshComponent* HM = Self->HandMesh)
            {
                // 代理空 → 强制重建（0.2.78 实证：运行时赋网格→代理迟建/反复建，必须脏一次）。
                if (!HM->GetSceneProxy())
                {
                    HM->MarkRenderStateDirty();
                }
                // ★v0.2.84 建模兜底：实例 HandMesh 若丢失网格（CDO 模板未继承），直接重设。
                // 仅当确实缺失才重设，平时不触发（不刷屏）。命中即记一条，便于确认根因已消除。
                if (!HM->GetSkeletalMeshAsset() && GWT_HandMeshAsset)
                {
                    HM->SetSkeletalMesh(Cast<USkeletalMesh>(GWT_HandMeshAsset));
                    if (GWT_HandAnimClass)
                    {
                        HM->SetAnimationMode(EAnimationMode::AnimationBlueprint);
                        HM->SetAnimInstanceClass(static_cast<UClass*>(GWT_HandAnimClass));
                    }
                    else
                    {
                        HM->SetAnimationMode(EAnimationMode::AnimationSingleNode);
                        HM->SetAnimInstanceClass(nullptr);
                    }
                    HM->SetRelativeLocation(GWT_HandRelLoc);
                    HM->SetRelativeRotation(GWT_HandRelRot);
                    HM->SetRelativeScale3D(GWT_HandRelScale);
                    WT_CopyVanillaHandMaterials(HM);
                    // ★0.2.142 材质被重置后补回屏幕 logo（否则网格丢失恢复一次，logo 就被冲掉）。
                    Self->ApplyHandheldScreenLogo();
                    HM->MarkRenderStateDirty();
                    WT_WARNF(TEXT("[vigil] instance HandMesh lost mesh; reapplied from static (anim=%s)."),
                             GWT_HandAnimClass ? *static_cast<UClass*>(GWT_HandAnimClass)->GetName() : TEXT("<bindpose>"));
                }
                // actor 级隐藏 / HandMesh 被原生或父级隐藏 → 静默拉回（不记日志）。
                if (Self->IsHidden() || !HM->IsVisible() || HM->bOwnerNoSee || HM->bOnlyOwnerSee)
                {
                    Self->SetActorHiddenInGame(false);
                    HM->SetOwnerNoSee(false);
                    HM->SetOnlyOwnerSee(false);
                    HM->SetVisibility(true, true);
                    HM->MarkRenderStateDirty();
                }
            }
            return true;
        }), 0.3f);
}

void ARemote_FactoryEquipment::StopVisibilityWatchdog()
{
    if (VisibilityWatchdogHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(VisibilityWatchdogHandle);
        VisibilityWatchdogHandle.Reset();
    }
}

void ARemote_FactoryEquipment::StopPanelWatchdog()
{
    if (PanelWatchdogHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(PanelWatchdogHandle);
        PanelWatchdogHandle.Reset();
    }
}

void ARemote_FactoryEquipment::UnEquip()
{
    WT_ZONE_COARSE(TEXT("Equip.UnEquip"));
    ClosePanel();
    // ★v0.2.12 摘除右键输入（IMC 移除 + 解绑）。
    RemoveToggleInput();
    // ★v0.2.80 停相机模式看门狗（0.2.80 起看门狗不再启动，此调用对无效 handle 是 no-op，保留对称）。
    StopCameraWatchdog();
    // ★v0.2.83 停静默可见性守护（对无效 handle 是 no-op，保留对称）。
    StopVisibilityWatchdog();
    // ★0.2.89 停 3P/1P 举持动态蒙太奇（本装备期播过的话）并复位播放标记。必须在 Super::UnEquip 之前：
    // Super 会清 mPlayerCharacter，之后 GetInstigatorCharacter 返回空。
    if (AFGCharacterPlayer* Character = GetInstigatorCharacter())
    {
        const FName PoseSlot = GWT_3pSlotName.IsNone() ? FName(TEXT("DefaultSlot")) : GWT_3pSlotName;
        if (USkeletalMeshComponent* Mesh3P = Character->GetMesh3P())
        {
            if (UAnimInstance* Anim3P = Mesh3P->GetAnimInstance())
            {
                Anim3P->StopSlotAnimation(0.2f, PoseSlot);
            }
            // ★0.2.123 与 Equip 的 tick 前置配对移除（AddTickPrerequisiteActor 的反向）。
            Mesh3P->RemoveTickPrerequisiteActor(this);
        }
        // ★0.2.95 1P 手臂网格同款停表（与播放目标配对）。
        if (USkeletalMeshComponent* Mesh1P = Character->GetMesh())
        {
            if (UAnimInstance* Anim1P = Mesh1P->GetAnimInstance())
            {
                Anim1P->StopSlotAnimation(0.2f, PoseSlot);
            }
        }
    }
    bIdle3pMontagePlayed = false;
    bIdle1pMontageFailed = false;   // ★0.2.95 1P 放弃标记复位，重装可重试
    bIdle3pNullLogged = false;   // ★0.2.90 null 诊断一次性标记随之复位，重装可重试/重报
    LastPoseMontagePlaySec = -1e9;   // ★0.2.97 重播冷却复位：重装装备立即起姿势，不等冷却窗
    // ★0.2.121 源头写边沿复位：EnforceArmPoseCache 只在装备期间跑（Tick 里 IsEquipped() 门控），
    //   卸装后 GWT_LastSlotHadThis 停在 true 会漏掉下次重装备沿 —— 这里显式清，
    //   保证每次重新装备（native 重置源头之后）我方边沿必再写一次。
    GWT_LastSlotHadThis = false;
    // ★v0.2.80 回归原生卸装（与 Equip 的 Super::Equip 配对）：Super::UnEquip 负责整 actor
    // 从手 socket 脱离 + 切装备可见性（SetEquipmentVisibility(false)）+ 清 mPlayerCharacter。
    // 旧手写路径的两个实锤问题一并根除：
    //   ① 手动 DetachFromActor(KeepWorldTransform) 把 actor 留在卸装点=地上孤儿
    //      （17:31 局 6 次 Equip/UnEquip 仅 1 次 EndPlay，0.2.79 常显后孤儿现形，截图实锤）；
    //   ② 反射清 mPlayerCharacter 绕过原生状态机（原生卸装自会清理，且重建重装流程一致）。
    // ★0.2.104：同 Equip——显式 AFGEquipment::UnEquip，绕开扫描仪卸装逻辑，行为同 0.2.103。
    AFGEquipment::UnEquip();
    WT_WARNF(TEXT("[equip] native unequip path: attachParent=%s equipped=%d"),
             GetAttachParentActor() ? *GetAttachParentActor()->GetName() : TEXT("<none>"),
             IsEquipped() ? 1 : 0);
}

void ARemote_FactoryEquipment::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    WT_ZONE_COARSE(TEXT("Equip.EndPlay"));   // 换档/销毁期：输入上下文与面板必须在这里干净卸掉
    // Covers the cases UnEquip does not: the player dies, the level unloads, the item is dropped
    // while the panel is still up. Without this the widget would stay on screen over a dead pawn.
    ClosePanel();
    RemoveToggleInput();
    // ★v0.2.72 停相机模式看门狗。
    StopCameraWatchdog();
    // ★v0.2.83 停静默可见性守护（核心 ticker，actor 销毁/卸装前必须摘，否则访问悬空 UObject）。
    StopVisibilityWatchdog();
    // ★v0.2.81 修复「游戏退不掉」：面板看门狗是 FTSTicker 核心 ticker（不随 World 销毁），
    //   若面板开着时本 actor 走 EndPlay（退出/换档/角色死亡），ticker 仍挂在核心 ticker 上，
    //   下一跳会访问已销毁的 PanelWidget/PlayerController/GameUI → 访问无效 UObject → 崩溃/卡死。
    //   这里在 actor 销毁前强制摘掉，从源头消除退出期崩溃通道（ClosePanel 内的自停只覆盖面板关闭路径，
    //   覆盖不到「面板开着 + actor 直接被销毁」这条 EndPlay 路径）。
    StopPanelWatchdog();
    // ★0.2.123 防御性摘 tick 前置：死亡/换档/丢弃等不走 UnEquip 的销毁路径也要解除
    //   Equip 里挂上的 prerequisite（weak 引用本身不悬空，但显式配对保持状态干净）。
    if (AFGCharacterPlayer* DyingChar = GetInstigatorCharacter())
    {
        if (USkeletalMeshComponent* Dying3P = DyingChar->GetMesh3P())
        {
            Dying3P->RemoveTickPrerequisiteActor(this);
        }
    }
    Super::EndPlay(EndPlayReason);
}

void ARemote_FactoryEquipment::OpenPanel()
{
    WT_ZONE_COARSE(TEXT("Equip.OpenPanel"));
    // ★v0.2.12 ESC 关闭路径（FGGameUI PopWidget → RemoveFromParent）不会通知我们清指针：
    // 检测"指针还在但已不在视口"先重置，否则这次 OpenPanel 会被旧指针挡成 no-op。
    // ★0.2.74 兜底还原：走到这里说明面板是被外部摘掉的（机器 UI 交接/游戏栈清理），
    // 旧面板的 UIOnly 可能还压在输入栈上——重开前先还原 GameOnly+隐光标，杜绝
    // 「UIOnly 残留 + 新面板再设 UIOnly」叠加成永久卡鼠标（0.2.73 日志 :19→:26→:34 连开实锤）。
    if (PanelWidget && !PanelWidget->IsInViewport())
    {
        if (APlayerController* StalePC = PanelWidget->GetOwningPlayer())
        {
            StalePC->SetInputMode(FInputModeGameOnly());
            StalePC->bShowMouseCursor = false;
            WT_WARNF(TEXT("[equip] stale panel pointer reset; input re-grounded to GameOnly before reopen."));
        }
        PanelWidget = nullptr;
    }
    // Already open: Equip can run twice (a hotbar re-select, a save reload). Never stack panels.
    if (PanelWidget)
    {
        return;
    }

    AFGCharacterPlayer* Character = GetInstigatorCharacter();
    if (!Character)
    {
        WT_WARNF(TEXT("[equip] no instigator character; cannot open the panel."));
        return;
    }

    // ★v0.2.48 建造模式守卫（bug N：建造时约半数几率开不出面板且卡鼠标）：
    // 建造枪处于「放置/拆解/刷漆」激活态时，右键会被建造枪与我们这个 toggle 同时捕获，
    // 若强行开面板会把输入切到 GameAndUI+显示光标，与建造输入打架 → 卡鼠标。
    // 处于这些态时不打开面板（已开的仍可正常关闭），退出对应态后右键正常开。
    // 注：IsActive() 是 UFGBuildGunState 的方法（FGBuildGun.h:87），不在 AFGBuildGun 上；
    // 用 AFGBuildGun::IsInState(EBuildGunState)（FGBuildGun.h:313）判定当前态，已一手核实。
    if (AFGBuildGun* BG = Character->GetBuildGun())
    {
        if (BG->IsInState(EBuildGunState::BGS_BUILD) ||
            BG->IsInState(EBuildGunState::BGS_DISMANTLE) ||
            BG->IsInState(EBuildGunState::BGS_PAINT))
        {
            WT_WARNF(TEXT("[equip] build gun in build/dismantle/paint state; skipping panel open to avoid input-mode conflict (mouse lock)."));
            return;
        }
    }

    APlayerController* PC = Cast<APlayerController>(Character->GetController());
    if (!PC)
    {
        WT_WARNF(TEXT("[equip] character has no player controller; cannot open the panel."));
        return;
    }

    // Same widget the fixed terminal spawns, created the same way. The panel reads its data from
    // URemote_FactoryRCO on the server, so it needs no buildable to be bound to.
    PanelWidget = CreateWidget<URemote_FactoryPanelWidget>(
        PC, URemote_FactoryPanelWidget::StaticClass());
    if (!PanelWidget)
    {
        WT_WARNF(TEXT("[equip] CreateWidget returned null for the panel."));
        return;
    }

    // ★v0.2.15 面板 ✕ 关闭按钮 → 完整收尾（摘 GameUI 栈 + 清指针），不只是 RemoveFromParent。
    PanelWidget->PanelCloseRequested.AddDynamic(
        this, &ARemote_FactoryEquipment::HandlePanelCloseRequested);

    // Z-order 10 matches what the fixed terminal uses. SetInputMode is a BlueprintNativeEvent on
    // UFGInteractWidget that hands the widget keyboard focus and sets the game input mode -- the
    // crafting panel does the same, and without it the chat box would not accept typing.
    // ★0.2.130 修复「ESC 关不掉面板」根因（大肥鱼诊断核实）：widget 必须可聚焦，否则下方
    // FInputModeUIOnly::SetWidgetToFocus 静默无效、ESC 永远落到 viewport 进不了面板。SObjectWidget::
    // SupportsKeyboardFocus 动态读 UUserWidget::bIsFocusable，故在 AddToViewport 之前置为可聚焦即可。
    PanelWidget->SetIsFocusable(true);
    PanelWidget->AddToViewport(10);
    PanelWidget->SetInputMode();

    // ★v0.2.14 注册进 FGGameUI 交互栈（成功后 ESC = 原版关面板）；★v0.2.15 看门狗兜底
    // （暂停菜单开着立即收面板 + GameUI 晚就绪时补注册）。不注册的后果（0.2.13 实测）：
    // ESC 直接弹暂停菜单，把面板压在底下卡死。
    TryRegisterWithGameUI();
    StartPanelWatchdog();

    // ★v0.2.53 显式接管输入模式（修复「关面板后卡鼠标+键盘」）：构造里已把 mUseMouse/mUseKeyboard 设
    // false 退出游戏自动屏蔽机制，这里由我们自己对称驱动——开面板设 UIOnly + 显光标（面板可点按），
    // 关面板（ClosePanel / NativeDestruct）设 GameOnly + 隐光标。两路同一对标准 UE API，无"施加缺还原"缝隙。
    {
        // ★修复 ④「卡鼠标 / 光标不显示或被捕获」：FInputModeUIOnly 在捕获鼠标进视口时会隐藏并锁定光标
        // （导致面板开着看不见鼠标、也点不到 UI）。这里用 SetLockMouseToViewportBehavior(DoNotLock)
        // 禁止捕获/锁定，并显式 bShowMouseCursor=true 强制光标可见 —— 鼠标自由可见且 UI 可点按；
        // 游戏输入依旧被 UIOnly 抑制（WASD / 视角不动），与 0.2.62 的"开面板时屏蔽游戏输入"初衷一致。
        // 注意：SetHideCursorDuringCapture 仅 FInputModeGameAndUI 有，UIOnly 上不存在（上一版编译报错的根因）。
        FInputModeUIOnly PanelInputMode;
        PanelInputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        if (TSharedPtr<SWidget> PanelSlate = PanelWidget->TakeWidget())
        {
            if (PanelSlate.IsValid())
            {
                PanelInputMode.SetWidgetToFocus(PanelSlate.ToSharedRef());
            }
        }
        PC->SetInputMode(PanelInputMode);
        PC->bShowMouseCursor = true;
        WT_WARNF(TEXT("[equip] panel opened; UIOnly + free visible cursor + focus (game input suppressed)."));
    }

    WT_LOGF(TEXT("[equip] panel opened on %s"), *PC->GetName());
}

void ARemote_FactoryEquipment::TryRegisterWithGameUI()
{
    // ★v0.2.20 停用 GameUI 栈注册：0.2.18 会话出现 FasterManualCraftingRedux 的 UMG 文本绑定
    // 空引用崩溃（execGetProducedCount 读 0x2e1，崩栈无我们 DLL），时间点与注册上栈吻合。
    // 注册动了游戏 UI 栈内部状态，是最可疑的外部交互 → 先下线保稳（ESC 替补关随之暂停）。
    // 如需恢复：把 kRegisterWithGameUI 改回 true 重打包。
    constexpr bool kRegisterWithGameUI = false;
    if (!kRegisterWithGameUI || bRegisteredWithGameUI || !PanelWidget || !PanelWidget->IsInViewport())
    {
        return;
    }
    if (AFGPlayerController* FGP = Cast<AFGPlayerController>(PanelWidget->GetOwningPlayer()))
    {
        if (UFGGameUI* GameUI = FGP->GetGameUI())
        {
            if (!GameUI->IsPauseMenuOpen())
            {
                GameUI->AddInteractWidget(PanelWidget);
                bRegisteredWithGameUI = true;
                WT_WARNF(TEXT("[equip] panel registered with FGGameUI — ESC now closes it."));
            }
        }
    }
}

void ARemote_FactoryEquipment::StartPanelWatchdog()
{
    if (PanelWatchdogHandle.IsValid())
    {
        return;   // 已在跑（同一面板重复 OpenPanel 的守卫）
    }

    TWeakObjectPtr<ARemote_FactoryEquipment> WeakThis(this);
    PanelWatchdogHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateLambda([WeakThis](float) -> bool
        {
            if (!WeakThis.IsValid())
            {
                return false;   // 装备实例已亡，停表
            }
            ARemote_FactoryEquipment* Self = WeakThis.Get();
            if (!Self->PanelWidget || !Self->PanelWidget->IsInViewport())
            {
                Self->PanelWatchdogHandle.Reset();
                return false;   // 面板已关，停表
            }

            AFGPlayerController* FGP = Cast<AFGPlayerController>(Self->PanelWidget->GetOwningPlayer());
            UFGGameUI* GameUI = FGP ? FGP->GetGameUI() : nullptr;
            if (!GameUI)
            {
                return true;   // GameUI 还没就绪，下一跳再看
            }

            // ① 防卡死铁律：暂停菜单一旦出现，无条件收面板——暂停菜单永远可点。
            if (GameUI->IsPauseMenuOpen())
            {
                WT_WARNF(TEXT("[equip] watchdog: pause menu is up; closing the panel so it stays clickable."));
                Self->ClosePanel();
                return false;
            }

            // ② GameUI 就绪即补注册（ESC 替补关）。★v0.2.20 注册已停用（见 TryRegisterWithGameUI 内注释）。
            if (!Self->bRegisteredWithGameUI)
            {
                Self->TryRegisterWithGameUI();
            }
            return true;
        }), 0.5f);
}

void ARemote_FactoryEquipment::ClosePanel()
{
    WT_ZONE_COARSE(TEXT("Equip.ClosePanel"));
    if (!PanelWidget)
    {
        return;
    }

    // ★v0.2.48 输入模式还原修复（bug M：关闭后卡输入）：
    // OpenPanel 经 PanelWidget->SetInputMode()（UFGInteractWidget 的 BP 原生事件）把输入切到
    // GameAndUI 并显示鼠标光标；旧 ClosePanel 只 RemoveFromParent，输入模式没还原 → 关闭后鼠标仍
    // 被捕获。这里收尾时显式还原输入模式。
    // ★v0.2.52 修正：还原目标从 FInputModeGameAndUI 改为 FInputModeGameOnly（游戏默认模式）。
    //   之前的 GameAndUI 还原（无焦点控件）会把鼠标锁在 UI 模式（无法转视角=卡鼠标）、键盘路由到
    //   无焦点 Slate（=卡键盘）——这正是 0.2.51 用户实测「关闭后卡鼠标+键盘」的根因。
    //   v0.2.50 注释里"GameOnly 切键盘"是绑定风暴时期的误判，现右键绑定累积已修，GameOnly 正常
    //   （鼠标自由转视角 + 键盘全归游戏）。
    // 例外：若暂停菜单正开着（看门狗因暂停而收面板），不要动输入模式——暂停菜单自己管光标，
    // 否则会把暂停菜单的光标也干掉。
    AFGPlayerController* ClosePC = Cast<AFGPlayerController>(PanelWidget->GetOwningPlayer());
    const bool bPauseOpen = ClosePC && ClosePC->GetGameUI() && ClosePC->GetGameUI()->IsPauseMenuOpen();

    if (PanelWidget->IsInViewport())
    {
        // ★v0.2.14 若已注册进交互栈，先摘掉（ESC 关闭时 GameUI 已自己摘过，再调是安全 no-op）。
        if (bRegisteredWithGameUI)
        {
            if (AFGPlayerController* FGP = Cast<AFGPlayerController>(PanelWidget->GetOwningPlayer()))
            {
                if (UFGGameUI* GameUI = FGP->GetGameUI())
                {
                    GameUI->RemoveInteractWidget(PanelWidget);
                }
            }
        }
        // RemoveFromParent is the UMG-side teardown; clearing the pointer lets the widget be
        // collected and makes the next OpenPanel() a fresh open rather than a no-op.
        PanelWidget->RemoveFromParent();
    }
    PanelWidget = nullptr;
    bRegisteredWithGameUI = false;
    // ★v0.2.15 看门狗主动停表（ticker 下一跳自检面板已关也会自停，这里双保险）。
    if (PanelWatchdogHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(PanelWatchdogHandle);
        PanelWatchdogHandle.Reset();
    }

    // ★v0.2.52 还原输入模式（修复「关闭面板后卡鼠标+键盘」）：暂停菜单没开时才还原
    // （避免覆盖暂停菜单光标）。还原用 FInputModeGameOnly（游戏默认模式）：鼠标自由转视角 +
    // 键盘全归游戏。此前误还原成 FInputModeGameAndUI（无焦点控件），会把鼠标锁在 UI 模式
    // （无法转视角=卡鼠标）、键盘路由到无焦点 Slate（=卡键盘）。
    if (ClosePC && !bPauseOpen)
    {
        FInputModeGameOnly InputMode;
        ClosePC->SetInputMode(InputMode);
        ClosePC->bShowMouseCursor = false;
        WT_WARNF(TEXT("[equip] panel closed; input restored to GameOnly (mouse + keyboard back to game)."));
    }

    WT_LOGF(TEXT("[equip] panel closed."));
}

void ARemote_FactoryEquipment::HandlePanelCloseRequested()
{
    // ★v0.2.15 ✕ 按钮：与右键 toggle 的"关"分支同一出口，保证栈/指针状态一致。
    ClosePanel();
}

// ----------------------------------------------------------------------------
// ★v0.2.12 右键开面板（Enhanced Input，运行时自建 IMC/Action）
// ----------------------------------------------------------------------------

void ARemote_FactoryEquipment::ApplyToggleInput(AFGCharacterPlayer* Character)
{
    WT_ZONE_COARSE(TEXT("Equip.ApplyToggleInput"));
    APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
    if (!PC || !PC->GetLocalPlayer())
    {
        return;
    }

    if (!GWT_ToggleAction)
    {
        UInputAction* Action = NewObject<UInputAction>(
            GetTransientPackage(), TEXT("WT_TogglePanelIA"));
        if (!Action)
        {
            // ★自检加固（2026-10-07）：原来直接 AddToRoot()，分配失败即崩；而且后面还会把
            // nullptr 交给 MapKey/AddMappingContext/BindAction，半配置状态比早退更危险。
            WT_WARNF(TEXT("[equip] NewObject<UInputAction> returned null; 右键开关面板不可用。"));
            return;
        }
        Action->AddToRoot();   // 运行时资产没有引用链，钉根防 GC（与手持图标同款双保险）
        GWT_ToggleAction = Action;
    }
    if (!GWT_ToggleIMC)
    {
        UInputMappingContext* IMC = NewObject<UInputMappingContext>(
            GetTransientPackage(), TEXT("WT_TogglePanelIMC"));
        if (!IMC)
        {
            WT_WARNF(TEXT("[equip] NewObject<UInputMappingContext> returned null; 右键开关面板不可用。"));
            return;
        }
        IMC->MapKey(static_cast<UInputAction*>(GWT_ToggleAction), EKeys::RightMouseButton);
        IMC->AddToRoot();
        GWT_ToggleIMC = IMC;
    }

    ULocalPlayer* LP = PC->GetLocalPlayer();
    if (UEnhancedInputLocalPlayerSubsystem* Sub =
            LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
    {
        Sub->AddMappingContext(static_cast<UInputMappingContext*>(GWT_ToggleIMC), 10000);
    }

    // ★v0.2.51 绑定到「自有 EIC」并 Push 上玩家控制器输入栈（替代 0.2.48 的
    // "绑 PC->InputComponent + 失败延时重试" 方案）。PC->InputComponent 是随装备/建造枪
    // 推栈漂移的临时组件：绑上去的委托会随栈弹出而死（0.2.50 日志实锤：换装后重新绑定
    // 也零触发）；且旧绑定从不解除 → 一次按键触发全部历史绑定（6 次装备后一按 6 连
    // toggle，面板闪烁后净「关」，用户看到的就是"打不开面板"）。自有 EIC + Push/Pop
    // 让绑定随组件生死，两类病灶一起根除；绑定动作不再依赖 EIC 就绪时机，重试机制删除。
    if (ToggleBindHandle != 0 && ToggleInputComponent)
    {
        // 同一装备周期已绑定（防 ApplyToggleInput 被调两次）；引擎 Push 内部 RemoveSingle 去重，重复 Push 安全。
        PC->PushInputComponent(ToggleInputComponent);
        return;
    }

    UEnhancedInputComponent* WTEIC = NewObject<UEnhancedInputComponent>(this, TEXT("WTToggleEIC"));
    if (!WTEIC)
    {
        // ★自检加固：NewObject 失败时绝不带着半配置状态往下走（与 Action/IMC 同款纪律）。
        WT_WARNF(TEXT("[equip] NewObject<UEnhancedInputComponent> failed; 右键开关面板不可用。"));
        return;
    }
    ToggleBindHandle = WTEIC->BindAction(
        static_cast<UInputAction*>(GWT_ToggleAction),
        ETriggerEvent::Started, this, &ARemote_FactoryEquipment::HandleToggleInput
    ).GetHandle();
    ToggleInputComponent = WTEIC;
    ToggleBindPC = PC;
    PC->PushInputComponent(WTEIC);
    WT_WARNF(TEXT("[equip] right-click toggle bound (handle=%llu) on own EIC, pushed to PC input stack."),
             static_cast<uint64>(ToggleBindHandle));
}

void ARemote_FactoryEquipment::RemoveToggleInput()
{
    // ★v0.2.51 真正卸载：把自有 EIC 从玩家控制器输入栈 Pop（引擎侧 ClearBindingValues），
    // 随后置空强引用让旧组件可被 GC（输入栈存的是弱引用）。0.2.48 及之前的实现只把
    // ToggleBindHandle 清零、从不解除真实绑定 → 绑定跨装备累积（0.2.50 实测：一按右键
    // 触发 6 连 toggle）。本函数必须幂等：UnEquip 与 EndPlay 都会调用，且可能连调多次。
    APlayerController* PC = ToggleBindPC.Get();
    if (!PC)
    {
        if (AFGCharacterPlayer* Character = GetInstigatorCharacter())
        {
            PC = Cast<APlayerController>(Character->GetController());
        }
    }
    if (PC && ToggleInputComponent)
    {
        PC->PopInputComponent(ToggleInputComponent);
    }
    // ★0.2.125：此处【不再清零】ToggleInputComponent / ToggleBindHandle / ToggleBindPC。
    //   根因（大肥鱼诊断 + 本机日志实锤）：UnEquip 每次卸装都调本函数，旧实现把它仨清零，
    //   致下次 Equip 的 ApplyToggleInput 守卫（ToggleBindHandle!=0）失效 → 又 NewObject 一个新
    //   EIC 并 Push，输入栈堆积 N 个孤儿 EIC（本机 0.2.124 日志 26 次 "right-click toggle bound"，
    //   handle 126→1258 单调递增，右键一次触发 N 次 toggle 的雪球「越跑越快、破千」）。
    //   保留句柄使重装备只 re-push 同一个 EIC，不再 NewObject —— EIC 仅创建一次，随 actor
    //   生命周期由 GC 回收（EndPlay 调本函数仍只 Pop，actor 即将销毁，残留强引用随 GC 清理，无泄漏）。
    //   注意：下方 IMC 摘除仍执行，重装备时由 ApplyToggleInput 的 AddMappingContext（位于守卫之前）
    //   重建，不受本处跳过影响，右击开关面板在装备期内始终可用、卸装后自动失效（UX 不变）。

    AFGCharacterPlayer* Character = GetInstigatorCharacter();
    APlayerController* IMCPC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
    if (!IMCPC || !IMCPC->GetLocalPlayer() || !GWT_ToggleIMC)
    {
        return;
    }
    if (UEnhancedInputLocalPlayerSubsystem* Sub =
            IMCPC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
    {
        Sub->RemoveMappingContext(static_cast<UInputMappingContext*>(GWT_ToggleIMC));
    }
}

void ARemote_FactoryEquipment::HandleToggleInput(const FInputActionValue& Value)
{
    WT_ZONE_COARSE(TEXT("Equip.HandleToggleInput"));
    // ★v0.2.15 防暂停卡死：暂停菜单开着时忽略右键（否则会把面板 toggle 到暂停菜单底下卡死）。
    if (const AFGCharacterPlayer* Character = GetInstigatorCharacter())
    {
        if (const AFGPlayerController* FGP = Cast<AFGPlayerController>(Character->GetController()))
        {
            if (const UFGGameUI* GameUI = FGP->GetGameUI())
            {
                if (GameUI->IsPauseMenuOpen())
                {
                    return;
                }
            }
        }
    }

    // ★v0.2.13 用户定案：右键 = 开/关切换；★v0.2.14 ESC 为替补关面板键（GameUI 栈注册成功后生效）。
    if (PanelWidget && PanelWidget->IsInViewport())
    {
        ClosePanel();
    }
    else
    {
        OpenPanel();
    }
}

// ----------------------------------------------------------------------------
// Item descriptor
// ----------------------------------------------------------------------------
URemote_FactoryEquipDescriptor::URemote_FactoryEquipDescriptor()
{
    // mEquipmentClass is the one field UFGEquipmentDescriptor adds on top of UFGItemDescriptor:
    // it is how the game knows which actor to spawn when the item is equipped.
    mEquipmentClass = ARemote_FactoryEquipment::StaticClass();

    DisplayName = ARemote_FactoryEquipment::GetEquipmentDisplayName();
    Desc = WT_LOCTEXT("EquipDesc_PortableTerminal",
        "A handheld Smart Factory. Craft it at a workbench and put it in an arm slot -- "
        "drawing it opens the factory console instantly, so you can check output, open a machine's "
        "own panel and pull items while standing at the miner instead of walking back to the HUB.");

    // Stackable items cannot be equipment (the game equips one instance), so the stack size is 1.
    // mStackSize sits in a protected section of UFGItemDescriptor with no C++ setter, so it is
    // written by reflection -- the same route the buildable descriptor uses for its category fields.
    //
    // ★★v0.2.8 真根因（2026-10-06 04:3x 实锤）：工坊"物品栏空间不足"与 mStackSize 无关——
    // UFGItemDescriptor::GetStackSize() 先读 mCachedStackSize（!=INDEX_NONE 即返回），而缓存是
    // PostLoad() 算的；**原生 CDO 不走 PostLoad**（原版物品全是蓝图类、从资产加载），所以我们的
    // 纯 C++ 描述符缓存永远停在零初始化的 0 → GetStackSize() 恒返回 0 → 堆叠上限 0 的物品永远
    // "塞不进背包"。v0.2.6 的 ImportText 加固方向没错但没打到这一层（日志 default=2 final=0 正确）。
    // 修法：直接把 mCachedStackSize 写成 1（SS_ONE 每格 1 个）。
    if (FProperty* StackProp = FindFProperty<FProperty>(
            UFGItemDescriptor::StaticClass(), TEXT("mStackSize")))
    {
        uint8* Raw = StackProp->ContainerPtrToValuePtr<uint8>(this);
        const uint8 Before = *Raw;
        StackProp->ImportText_Direct(TEXT("SS_ONE"), Raw, this, 0, nullptr);
        if (*Raw != static_cast<uint8>(EStackSize::SS_ONE))
        {
            *Raw = static_cast<uint8>(EStackSize::SS_ONE);
        }

        // 关键一行：缓存写成 1（SS_ONE 每格 1 个）。派生类可直接访问 protected 成员。
        mCachedStackSize = 1;

        UE_LOG(LogRemote_Factory, Display,
               TEXT("[equip] mStackSize final=%d (SS_ONE=%d), mCachedStackSize=%d."),
               static_cast<int32>(*Raw), static_cast<int32>(EStackSize::SS_ONE),
               mCachedStackSize);
    }
    else
    {
        WT_WARNF(TEXT("[equip] UFGItemDescriptor::mStackSize not found; the handheld may stack."));
    }
}

FText URemote_FactoryEquipDescriptor::GetItemNameInternal() const { return DisplayName; }
FText URemote_FactoryEquipDescriptor::GetItemDescriptionInternal() const { return Desc; }

// ----------------------------------------------------------------------------
// Crafting recipe (workbench)
// ----------------------------------------------------------------------------
URemote_FactoryEquipRecipe::URemote_FactoryEquipRecipe()
{
    // Matches the object scanner's 40 s hand-craft; the terminal is a comparable convenience item.
    mManufactoringDuration = 40.0f;
    mProduct.Add(FItemAmount(URemote_FactoryEquipDescriptor::StaticClass(), 1));
}

void URemote_FactoryEquipRecipe::ApplyProducedIn(UClass* WorkBenchClass)
{
    if (!WorkBenchClass) return;
    mProducedIn.Empty();
    mProducedIn.Add(TSoftClassPtr<UObject>(WorkBenchClass));
}

void URemote_FactoryEquipRecipe::ApplyIngredients()
{
    // ★0.2.141（用户 2026-10-20 定案）-- 与里程碑成本对调：车间（手持终端）配方改采原里程碑料单，
    // 含 10×原版对象扫描仪（路径 Resource/Equipment/GemstoneScanner 下；T1「Field Research」解锁，
    // 进库存后可由工作台配方消耗）+ 10×铁棒 + 300×螺丝 + 100×铜板。
    // （v0.2.2 起曾用 铁棒/螺丝/铜板 的简化版；0.2.141 前为 20/30/10。）
    static const FWTHandheldCostLine Costs[] = {
        { TEXT("/Game/FactoryGame/Resource/Equipment/GemstoneScanner/BP_EquipmentDescriptorObjectScanner.BP_EquipmentDescriptorObjectScanner_C"), 10 },
        { TEXT("/Game/FactoryGame/Resource/Parts/IronRod/Desc_IronRod.Desc_IronRod_C"), 10 },
        { TEXT("/Game/FactoryGame/Resource/Parts/IronScrew/Desc_IronScrew.Desc_IronScrew_C"), 300 },
        { TEXT("/Game/FactoryGame/Resource/Parts/CopperSheet/Desc_CopperSheet.Desc_CopperSheet_C"), 100 },
    };

    mIngredients.Empty();
    for (const FWTHandheldCostLine& Line : Costs)
    {
        UClass* ItemClass = StaticLoadClass(UFGItemDescriptor::StaticClass(), nullptr, Line.ItemPath);
        if (!ItemClass)
        {
            WT_WARNF(TEXT("[equip] handheld cost could not resolve item '%s'."), Line.ItemPath);
            continue;
        }
        mIngredients.Add(FItemAmount(TSubclassOf<UFGItemDescriptor>(ItemClass), Line.Amount));
    }
    WT_LOGF(TEXT("[equip] handheld craft cost: %d ingredient lines."), mIngredients.Num());
}

// ----------------------------------------------------------------------------
// Handheld hand mesh (★v0.2.11: 整包拷贝原版 Equip_ObjectScanner_C 的模型配置)
// ----------------------------------------------------------------------------
void ARemote_FactoryEquipment::ApplyHandheldHandMesh()
{
    // ★v0.2.11 定案：不再逐项手抄（v0.2.10 只拷了网格，缺 AnimClass/相对变换 → 模型不可见、姿势错）。
    // 游戏日志（LogMarcioCommonLibs）dump 出原版扫描仪装备的完整配置：
    //   类=/Game/FactoryGame/Equipment/ObjectScanner/Equip_ObjectScanner.Equip_ObjectScanner_C
    //   网格=Mesh/SK_ObjectScanner_01  Anim=BPA_ObjectScanner  待机姿势=ObjectScannerIdle_01(1P/3P)
    // 直接 StaticLoadClass 加载它，从其 CDO 的网格组件模板整包拷贝；mAttachSocket/mIdlePoseAnimation
    // 是 protected/private，按本项目惯例走 FProperty 反射读。原版类加载失败则回退 v0.2.10 的裸网格路径。
    UClass* VanillaEquip = StaticLoadClass(AFGEquipment::StaticClass(), nullptr,
        TEXT("/Game/FactoryGame/Equipment/ObjectScanner/Equip_ObjectScanner.Equip_ObjectScanner_C"));
    if (VanillaEquip)
    {
        WT_WARNF(TEXT("[equip] vanilla scanner equip class loaded: %s"), *VanillaEquip->GetPathName());
        // ★0.2.41 钉死缓存：Equip 时还要再抄一遍材质 override（成功才锁定 + AddToRoot，0.2.38 纪律）。
        if (!VanillaEquip->IsRooted())
        {
            VanillaEquip->AddToRoot();
        }
        GWT_HandVanillaEquipClass = VanillaEquip;
    }
    else
    {
        WT_WARNF(TEXT("[equip] Equip_ObjectScanner_C failed to load; falling back to raw mesh path."));
    }

    USkeletalMeshComponent* Src = nullptr;
    AFGEquipment* VanillaCDO = nullptr;
    if (VanillaEquip)
    {
        VanillaCDO = Cast<AFGEquipment>(VanillaEquip->GetDefaultObject());
        if (VanillaCDO)
        {
            // ★0.2.89：原版 3P 举持姿势落点的 Slot 节点名只能从运行时资产反推 —— 原版 CDO 的
            // mEquipMontage.Montage_3P 播在角色 3P 网格上，蒙太奇资产自带真实 SlotNodeName。
            // mEquipMontage 在 AFGEquipment 里是 protected，而原版类不是本类 → FProperty 反射读。
            if (const FStructProperty* MontageProp = CastField<FStructProperty>(
                    AFGEquipment::StaticClass()->FindPropertyByName(TEXT("mEquipMontage"))))
            {
                const FFGWeightedEquipmentMontageArray* Montages =
                    MontageProp->ContainerPtrToValuePtr<FFGWeightedEquipmentMontageArray>(VanillaCDO);
                if (Montages)
                {
                    for (const FFGWeightedEquipmentMontage& Weighted : Montages->Montages)
                    {
                        if (Weighted.Montage_3P && Weighted.Montage_3P->SlotAnimTracks.Num() > 0 &&
                            !Weighted.Montage_3P->SlotAnimTracks[0].SlotName.IsNone() && !GWT_EquipMontage3p)
                        {
                            GWT_3pSlotName = Weighted.Montage_3P->SlotAnimTracks[0].SlotName;
                            // ★0.2.93 同点保存蒙太奇本体：Equip 时播它=原版「拿出设备」动画。
                            GWT_EquipMontage3p = Weighted.Montage_3P;
                            WT_WARNF(TEXT("[pose3p] vanilla montage3P=%s slot=%s"),
                                     *Weighted.Montage_3P->GetName(), *GWT_3pSlotName.ToString());
                        }
                        // ★0.2.107 同点保存 1P 拿出动画：Montage_1P 播在角色 1P 手臂网格上，
                        //   slot 名取资产自带值（B 类：1P 装备栏切换动画）。
                        if (Weighted.Montage_1P && Weighted.Montage_1P->SlotAnimTracks.Num() > 0 &&
                            !Weighted.Montage_1P->SlotAnimTracks[0].SlotName.IsNone() && !GWT_EquipMontage1p)
                        {
                            GWT_1pSlotName = Weighted.Montage_1P->SlotAnimTracks[0].SlotName;
                            GWT_EquipMontage1p = Weighted.Montage_1P;
                            WT_WARNF(TEXT("[pose1p] vanilla montage1P=%s slot=%s"),
                                     *Weighted.Montage_1P->GetName(), *GWT_1pSlotName.ToString());
                        }
                        if (GWT_EquipMontage3p && GWT_EquipMontage1p)
                        {
                            break;
                        }
                    }
                }
            }
            // ★0.2.48 G：原版扫描仪手持网格/动画可能分布在不同的骨骼组件上（手持件网格在一个件、
            //   动画类在另一个件），不能只取第一个带网格的件。分两路扫描：
            //   MeshSrc = 第一个带网格的件（拿 mesh + 相对变换）；AnimSrc = 第一个带 AnimClass 的件
            //   （拿手持动画蓝图）。最终 AnimClass 优先用 MeshSrc 的，缺失时回落 AnimSrc 的，
            //   避免 AnimClass 为空 ⇒ bind pose 静态、没有手持动画（用户实测现象）。
            TArray<USkeletalMeshComponent*> SkelComps;
            VanillaCDO->GetComponents(SkelComps);
            USkeletalMeshComponent* MeshSrc = nullptr;
            USkeletalMeshComponent* AnimSrc = nullptr;
            for (USkeletalMeshComponent* C : SkelComps)
            {
                if (!C) continue;
                if (!MeshSrc && C->GetSkeletalMeshAsset())
                    MeshSrc = C;
                if (!AnimSrc && C->GetAnimClass())
                    AnimSrc = C;
            }
            Src = MeshSrc ? MeshSrc
                          : (SkelComps.Num() ? SkelComps[0] : nullptr);
            if (!Src)
            {
                Src = Cast<USkeletalMeshComponent>(
                    VanillaCDO->GetComponentByClass(USkeletalMeshComponent::StaticClass()));
            }
            // 记录可能来自 AnimSrc 的动画类，下面统一赋值
            GWT_TempAnimClass = (Src && Src->GetAnimClass()) ? Src->GetAnimClass()
                                  : (AnimSrc ? AnimSrc->GetAnimClass() : nullptr);
        }
    }

    USkeletalMesh* Mesh = nullptr;
    UClass* AnimClass = nullptr;      // 只用于日志留档：★v0.2.21 不再套用到我们的组件上，见下。
    FVector RelLoc = FVector::ZeroVector;
    FRotator RelRot = FRotator::ZeroRotator;
    FVector RelScale = FVector::OneVector;

    if (Src && Src->GetSkeletalMeshAsset())
    {
        Mesh = Src->GetSkeletalMeshAsset();
        AnimClass = Src->GetAnimClass();
        if (!AnimClass && GWT_TempAnimClass)
        {
            AnimClass = GWT_TempAnimClass;   // ★0.2.48 G：动画类可能来自另一骨骼件，回落使用
        }
        RelLoc = Src->GetRelativeLocation();
        RelRot = Src->GetRelativeRotation();
        RelScale = Src->GetRelativeScale3D();
        WT_WARNF(TEXT("[equip] copied scanner component config: mesh=%s anim=%s rel=(%s %s %s)"),
                 *Mesh->GetName(),
                 AnimClass ? *AnimClass->GetName() : TEXT("<no anim>"),
                 *RelLoc.ToCompactString(), *RelRot.ToCompactString(), *RelScale.ToCompactString());
    }
    else
    {
        // 回退：v0.2.10 的多候选裸网格路径（无动画、零相对变换，聊胜于无）。
        static const TCHAR* MeshPaths[] = {
            TEXT("/Game/FactoryGame/Equipment/ObjectScanner/Mesh/SK_ObjectScanner_01.SK_ObjectScanner_01"),
            TEXT("/Game/FactoryGame/Equipment/ObjectScanner/SK_ObjectScanner_01.SK_ObjectScanner_01"),
            TEXT("/Game/FactoryGame/Equipment/ScanTool/Mesh/SK_ObjectScanner_01.SK_ObjectScanner_01"),
        };
        for (const TCHAR* Path : MeshPaths)
        {
            Mesh = LoadObject<USkeletalMesh>(nullptr, Path);
            if (Mesh)
            {
                WT_WARNF(TEXT("[equip] fallback raw mesh loaded: %s"), Path);
                break;
            }
        }
    }

    if (!Mesh)
    {
        WT_WARNF(TEXT("[equip] no hand mesh available under any path; the handheld shows no model."));
        return;
    }

    // *0.2.59b fix: force mesh render data PostLoad so the first CreateSceneProxy succeeds (no more proxy=0).
    Mesh->ConditionalPostLoad();

    if (!Mesh->IsRooted())
    {
        Mesh->AddToRoot();
    }
    // ★v0.2.19 存静态供实例级兜底（Equip 时发现实例没网格直接重设）。
    GWT_HandMeshAsset = Mesh;
    // ★v0.2.21 相对变换也存静态：Equip 挂接后显式回写（不再依赖 SnapToTarget 隐式保留）。
    GWT_HandRelLoc = RelLoc;
    GWT_HandRelRot = RelRot;
    GWT_HandRelScale = RelScale;
    // ★0.2.37（用户终案「终端做成原版扫描仪」）：**恢复套用原版扫描仪的动画蓝图**。
    // v0.2.21 的"动画导致隐身"只是假设 —— 0.2.24 已实锤真根因 = 挂错到隐藏的 3P 网格
    // （CharacterMesh3P），挂对 1P 组件后动画是安全的，而且这才是"原版扫描仪"观感的关键：
    // 动画图负责扫描仪的持握/晃动姿态，纯 bind pose 是 T 口字站姿的静态网格，观感是歪的。
    // 兜底：GWT_HandAnimClass 为 null（原版组件/动画没拷到）时自动退回单节点 bind pose，不会更糟。
    GWT_HandAnimClass = AnimClass;

    // ★v0.2.21 诊断探针网格：优先引擎立方体（最直观），退而用原版扫描仪自己的静态展示网格。
    if (kDebugHandMarker)
    {
        static const TCHAR* MarkerPaths[] = {
            TEXT("/Engine/BasicShapes/Cube.Cube"),
            TEXT("/Game/FactoryGame/Equipment/ObjectScanner/Mesh/CrystalScanner.CrystalScanner"),
        };
        for (const TCHAR* Path : MarkerPaths)
        {
            if (UStaticMesh* Marker = LoadObject<UStaticMesh>(nullptr, Path))
            {
                if (!Marker->IsRooted())
                {
                    Marker->AddToRoot();
                }
                GWT_MarkerMeshAsset = Marker;
                WT_WARNF(TEXT("[equip] hand-marker probe mesh loaded: %s"), Path);
                break;
            }
        }
        if (!GWT_MarkerMeshAsset)
        {
            WT_WARNF(TEXT("[equip] hand-marker probe mesh NOT loadable; the probe stays off."));
        }
    }

    if (ARemote_FactoryEquipment* CDO = Cast<ARemote_FactoryEquipment>(
            ARemote_FactoryEquipment::StaticClass()->GetDefaultObject()))
    {
        if (CDO->HandMesh)
        {
            CDO->HandMesh->SetSkeletalMesh(Mesh);
            // ★0.2.37 套原版扫描仪动画蓝图（含兜底：拷不到时退回单节点 bind pose）。
            if (GWT_HandAnimClass)
            {
                CDO->HandMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
                CDO->HandMesh->SetAnimInstanceClass(static_cast<UClass*>(GWT_HandAnimClass));
            }
            else
            {
                CDO->HandMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
                CDO->HandMesh->SetAnimInstanceClass(nullptr);
            }
            CDO->HandMesh->SetRelativeLocation(RelLoc);
            CDO->HandMesh->SetRelativeRotation(RelRot);
            CDO->HandMesh->SetRelativeScale3D(RelScale);
            // ★0.2.41 材质 override 也照抄（贴图对齐，见 WT_CopyVanillaHandMaterials 注释）。
            WT_CopyVanillaHandMaterials(CDO->HandMesh);
        }
        // ★0.2.37 手臂持握姿势也照抄原版：m1PAnimClass = "角色 1P 手臂怎么摆"的动画蓝图
        // （基类保护成员，同基类两边都有此属性，反射拷贝）。原版扫描仪怎么持，我们就怎么持。
        if (VanillaCDO)
        {
            if (FObjectProperty* P = CastField<FObjectProperty>(
                    AFGEquipment::StaticClass()->FindPropertyByName(TEXT("m1PAnimClass"))))
            {
                UObject* Vanilla1PAnim = P->GetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(VanillaCDO));
                if (Vanilla1PAnim)
                {
                    P->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(CDO), Vanilla1PAnim);
                    WT_WARNF(TEXT("[equip] copied vanilla m1PAnimClass=%s (arms hold pose matches scanner)."),
                             *Vanilla1PAnim->GetName());
                }
                else
                {
                    WT_WARNF(TEXT("[equip] vanilla m1PAnimClass is null; arms use AE_None (scanner-accurate)."));
                }
            }
        }
        if (CDO->HandMarker && GWT_MarkerMeshAsset)
        {
            CDO->HandMarker->SetStaticMesh(static_cast<UStaticMesh*>(GWT_MarkerMeshAsset));
        }

        // 挂点：原版 mAttachSocket（protected，反射读）优先；存到静态供 Equip 用。
        if (VanillaCDO)
        {
            if (FNameProperty* P = FindFProperty<FNameProperty>(
                    AFGEquipment::StaticClass(), TEXT("mAttachSocket")))
            {
                const FName Sock = *P->ContainerPtrToValuePtr<FName>(VanillaCDO);
                if (!Sock.IsNone())
                {
                    GWT_HandAttachSocket = Sock;
                }
            }
            // 待机姿势动画（private，反射读写）：让角色手臂摆出持扫描仪的姿势。
            if (FObjectProperty* P = FindFProperty<FObjectProperty>(
                    AFGEquipment::StaticClass(), TEXT("mIdlePoseAnimation")))
            {
                if (UObject* Val = P->GetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(VanillaCDO)))
                {
                    P->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(CDO), Val);
                    // ★0.2.90 同步存静态：实例 ctor 用它直写（运行时改 CDO 不传导到 SpawnActor 实例）。
                    GWT_Idle1pAsset = Cast<UAnimSequence>(Val);
                }
            }
            if (FObjectProperty* P = FindFProperty<FObjectProperty>(
                    AFGEquipment::StaticClass(), TEXT("mIdlePoseAnimation3p")))
            {
                if (UObject* Val = P->GetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(VanillaCDO)))
                {
                    // ★0.2.123 rf.injectIdle3p 门：0 时跳过 CDO 属性写（AnimBP 走 CDO 路径时同样
                    //   拿 null），但 GWT_Idle3pAsset 照常赋值 —— 蒙太奇通路不随二分开关失效。
                    if (GWT_InjectIdle3p != 0)
                    {
                        P->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(CDO), Val);
                    }
                    // ★0.2.90 同步存静态：实例 ctor 用它直写（运行时改 CDO 不传导到 SpawnActor 实例）。
                    GWT_Idle3pAsset = Cast<UAnimSequence>(Val);
                }
            }
            // ★0.2.124 与 ctor 同步修正：0.2.105 这里写 15，0.2.117 ctor 已回退 10（用户定调
            //   「3P 用 0.2.104 干净组合」）但本行漏改 —— CDO 从加载期起就是 15，与 ctor(10)
            //   分歧；「hand model applied to CDO」实证 native 有走 CDO 的路径，CDO 常驻 15
            //   即恒定 15 源（DeepSeek 观察到的 native 抢 15 疑似上游之一）。改为与 ctor 完全
            //   同款（clamp(rf.armAnim)，默认 10），消除实例/CDO 加载期分歧。
            CDO->mArmAnimation = (GWT_ArmAnimOverride >= 0 &&
                                  GWT_ArmAnimOverride <= static_cast<int32>(EArmEquipment::AE_BoomBox))
                ? static_cast<EArmEquipment>(static_cast<uint8>(GWT_ArmAnimOverride))
                : EArmEquipment::AE_ObjectScanner;
            // ★0.2.86 防御性：把 mEquipmentSlot=ES_ARMS 也写进 CDO。mArmAnimation / mIdlePoseAnimation3p
            // 的生效前提都是 mEquipmentSlot==ES_ARMS（FGEquipment.h:576/577 EditCondition），动画系统按
            // 类默认判定槽位；ctor 虽设实例值，但显式写 CDO 杜绝"读 CDO 槽位=ES_NONE→动画被忽略"的可能。
            CDO->mEquipmentSlot = EEquipmentSlot::ES_ARMS;
            // 诊断：确认从 vanilla CDO 拷来的待机姿势动画是否非空（空=复制失败→无举持姿势）。
            auto* Idle3pProp = FindFProperty<FObjectProperty>(AFGEquipment::StaticClass(), TEXT("mIdlePoseAnimation3p"));
            UObject* Idle3p = Idle3pProp ? Idle3pProp->GetObjectPropertyValue(Idle3pProp->ContainerPtrToValuePtr<void>(CDO)) : nullptr;
            auto* Idle1pProp = FindFProperty<FObjectProperty>(AFGEquipment::StaticClass(), TEXT("mIdlePoseAnimation"));
            UObject* Idle1p = Idle1pProp ? Idle1pProp->GetObjectPropertyValue(Idle1pProp->ContainerPtrToValuePtr<void>(CDO)) : nullptr;
            WT_WARNF(TEXT("[equip] CDO pose check: slot=%d armAnim=%d idle1p=%s idle3p=%s"),
                     (int32)CDO->mEquipmentSlot, (int32)CDO->mArmAnimation,
                     Idle1p ? *Idle1p->GetName() : TEXT("<null>"),
                     Idle3p ? *Idle3p->GetName() : TEXT("<null>"));
        }

        WT_WARNF(TEXT("[equip] hand model applied to CDO (mesh=%s, vanillaAnim=%s, socket=%s, armPose=ObjectScanner, ourAnim=%s)."),
                 *Mesh->GetName(),
                 AnimClass ? *AnimClass->GetName() : TEXT("<no anim>"),
                 GWT_HandAttachSocket.IsNone() ? TEXT("<probe>") : *GWT_HandAttachSocket.ToString(),
                 GWT_HandAnimClass ? *static_cast<UClass*>(GWT_HandAnimClass)->GetName() : TEXT("<bindpose>"));
    }
}

// ----------------------------------------------------------------------------
// Registration
// ----------------------------------------------------------------------------
namespace Remote_Factory
{
    // Defined below RegisterHandheld; forward-declared so the call site above the body resolves.
    static void ApplyHandheldIcon();

    void RegisterHandheld()
    {
        // mProducedIn must point at the workbench, which only exists once the game content is
        // mounted. Resolve it the same way the build gun is resolved: an explicit path first, then
        // a fallback scan by class, so a re-pathed workbench does not silently break crafting.
        static UClass* WorkBenchClass = nullptr;
        if (!WorkBenchClass)
        {
            WorkBenchClass = StaticLoadClass(UObject::StaticClass(), nullptr,
                TEXT("/Game/FactoryGame/Buildable/-Shared/WorkBench/BP_WorkshopComponent.BP_WorkshopComponent_C"));
        }

        if (URemote_FactoryEquipRecipe* RecipeCDO = Cast<URemote_FactoryEquipRecipe>(
                URemote_FactoryEquipRecipe::StaticClass()->GetDefaultObject()))
        {
            if (WorkBenchClass)
            {
                RecipeCDO->ApplyProducedIn(WorkBenchClass);
                WT_LOGF(TEXT("[equip] handheld recipe produced in %s"), *WorkBenchClass->GetName());
            }
            else
            {
                WT_WARNF(TEXT("[equip] could not resolve BP_WorkshopComponent — the handheld "
                              "terminal will not be craftable."));
            }

            RecipeCDO->ApplyIngredients();
        }

        ApplyHandheldIcon();
        ARemote_FactoryEquipment::ApplyHandheldHandMesh();   // ★v0.2.10 手持模型（同为运行时资产写入 CDO）
    }

    // ★0.2.42 图标对齐（用户实测「便携终端这个没改过来」）：终端的终版外观 = **原版扫描仪**
    //   （0.2.37 定案），0.2.41 又把其它物品的图标全换成了原版同族官方图标 —— 唯独便携终端
    //   还挂着 v0.2.3 的自绘绿色图标，HUB 奖励列表里就它一个格格不入。
    //   修：优先用**原版扫描仪的官方物品图标**（路径来自官方 Docs JSON，权威：
    //   Resource/Equipment/GemstoneScanner/UI/ObjectScanner_256），取不到再回落自绘图标。
    // ★0.2.48（用户「无线终端的建模和贴图统一采用原版扫描仪建模和贴图」）：图标来源再补一层
    //   **最权威的一条** —— 直接读原版扫描仪物品描述符 CDO 的 mSmallIcon/mPersistentBigIcon
    //   （BP_EquipmentDescriptorObjectScanner_C，Docs JSON 里的 mPersistentBigIcon 就是
    //   ObjectScanner_256）。顺序：描述符 CDO → 纹理路径 → 模组图标（后者只是取不到时的兜底，
    //   实测原版这条路必中，日志 `[equip] handheld item icon <= ...` 每次都能验证）。
    // v0.2.3 备注仍有效：图标必须是包内真资产（运行时 CreateTransient 会被 GC ⇒ CDO 图标
    // 变悬垂 ⇒ Slate OnPaint AV，WP v1.0.43 踩过的坑）；原版资产同样满足，双保险照旧。
    static void ApplyHandheldIcon()
    {
        UTexture2D* Icon = nullptr;
        const TCHAR* Chosen = TEXT("<none>");

        // ① 原版扫描仪描述符 CDO（最权威：原版自己挂什么我们就用什么）
        static const TCHAR* DescPaths[] = {
            TEXT("/Game/FactoryGame/Resource/Equipment/ObjectScanner/BP_EquipmentDescriptorObjectScanner.BP_EquipmentDescriptorObjectScanner_C"),
            TEXT("/Game/FactoryGame/Resource/Equipment/GemstoneScanner/BP_EquipmentDescriptorObjectScanner.BP_EquipmentDescriptorObjectScanner_C"),
        };
        UClass* DescClass = nullptr;
        for (const TCHAR* Path : DescPaths)
        {
            DescClass = StaticLoadClass(UFGItemDescriptor::StaticClass(), nullptr, Path);
            if (DescClass)
            {
                WT_WARNF(TEXT("[equip] vanilla scanner descriptor loaded: %s"), *DescClass->GetPathName());
                break;
            }
        }
        if (!DescClass)
        {
            // 路径没中就走类迭代器扫名字（同 WT_FindVanillaClass 的兜底口径）
            for (TObjectIterator<UClass> It; It; ++It)
            {
                if (It->IsChildOf(UFGItemDescriptor::StaticClass())
                    && It->GetName().Equals(TEXT("BP_EquipmentDescriptorObjectScanner_C")))
                {
                    DescClass = *It;
                    WT_WARNF(TEXT("[equip] vanilla scanner descriptor found by name sweep: %s"),
                             *DescClass->GetPathName());
                    break;
                }
            }
        }
        if (UObject* DescCDO = DescClass ? DescClass->GetDefaultObject() : nullptr)
        {
            for (const TCHAR* PropName : { TEXT("mPersistentBigIcon"), TEXT("mSmallIcon") })
            {
                if (FObjectProperty* P = FindFProperty<FObjectProperty>(DescClass, PropName))
                {
                    UObject* V = P->GetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(DescCDO));
                    if (UTexture2D* Tex = Cast<UTexture2D>(V))
                    {
                        Icon = Tex;
                        Chosen = TEXT("<vanilla scanner descriptor>");
                        break;
                    }
                }
            }
            if (!Icon)
            {
                WT_WARNF(TEXT("[equip] vanilla scanner descriptor has no usable icon prop; trying texture paths."));
            }
        }

        // ② 纹理路径多候选（官方 Docs JSON 给出的权威路径排第一位，去掉 /Game 前缀）。
        //   后两条是同名资产的其他常见落点 —— 一条写错就回落自绘图标的话，用户看到的
        //   还是那个格格不入的绿图标，所以这里多花两行保险。
        static const TCHAR* VanillaIconPaths[] = {
            TEXT("/Game/FactoryGame/Resource/Equipment/GemstoneScanner/UI/ObjectScanner_256.ObjectScanner_256"),
            TEXT("/Game/FactoryGame/Equipment/ObjectScanner/UI/ObjectScanner_256.ObjectScanner_256"),
            TEXT("/Game/FactoryGame/Resource/Equipment/ObjectScanner/UI/ObjectScanner_256.ObjectScanner_256"),
        };
        for (const TCHAR* Path : VanillaIconPaths)
        {
            if (!Icon)
            {
                if (UTexture2D* Loaded = LoadObject<UTexture2D>(nullptr, Path))
                {
                    Icon = Loaded;
                    Chosen = Path;
                }
            }
        }
        if (!Icon)
        {
            WT_WARNF(TEXT("[equip] vanilla scanner icon not loadable -- falling back to mod icon."));
            static const TCHAR* Roots[] =
            {
                // ★0.2.137 改名 Smart_Factory：新根在最前，旧根保留兜底（fallback 铁律）
                TEXT("/Smart_Factory/"),
                TEXT("/Game/Smart_Factory/"),
                TEXT("/smart_factory/"),
                TEXT("/Game/smart_factory/"),
                TEXT("/Remote_Factory/"),
                TEXT("/Game/Remote_Factory/"),
                TEXT("/remote_factory/"),
                TEXT("/Game/remote_factory/"),
                // ★ fallback（铁律：改名后保留旧根兜底，防止 .uasset 内部包名未随文件夹重命名而重映射时 LoadObject 找不到资源）
                TEXT("/WirelessTerminal/"),
                TEXT("/Game/WirelessTerminal/"),
                TEXT("/wirelessterminal/"),
                TEXT("/Game/wirelessterminal/"),
            };
            for (const TCHAR* Root : Roots)
            {
                const FString Full = FString(Root) + TEXT("Icon/icon_handheld_terminal.icon_handheld_terminal");
                Icon = LoadObject<UTexture2D>(nullptr, *Full);
                if (Icon)
                {
                    Chosen = TEXT("<mod>Icon/icon_handheld_terminal");
                    break;
                }
            }
        }
        if (!Icon)
        {
            WT_WARNF(TEXT("[equip] cook-time handheld icon missing (was create_wt_icons.py run?) "
                          "-- workbench tile stays blank."));
            return;
        }
        if (!Icon->IsRooted())
        {
            Icon->AddToRoot();
        }
        WT_WARNF(TEXT("[equip] handheld item icon <= %s"), Chosen);

        static const TCHAR* IconProps[] = { TEXT("mSmallIcon"), TEXT("mPersistentBigIcon") };
        int32 NumSet = 0;
        if (URemote_FactoryEquipDescriptor* DescCDO = Cast<URemote_FactoryEquipDescriptor>(
                URemote_FactoryEquipDescriptor::StaticClass()->GetDefaultObject()))
        {
            for (const TCHAR* Name : IconProps)
            {
                if (FObjectProperty* Prop = FindFProperty<FObjectProperty>(
                        URemote_FactoryEquipDescriptor::StaticClass(), Name))
                {
                    if (Prop->PropertyClass && Prop->PropertyClass->IsChildOf(UTexture2D::StaticClass()))
                    {
                        Prop->SetObjectPropertyValue(Prop->ContainerPtrToValuePtr<void>(DescCDO), Icon);
                        ++NumSet;
                    }
                }
            }
        }
        WT_LOGF(TEXT("[equip] handheld icon applied: %d/2 props (tex=%s)."),
                NumSet, Icon ? *Icon->GetName() : TEXT("NULL"));
    }
}
