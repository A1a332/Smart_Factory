// ★2026-11-04 智能仓储集装箱面板（仿「位面存储站」样式）：背包打开时停靠在【物品栏】左侧
// （与原版【位面】同一条左侧带），列出全局集装箱货物（聚合），每行显示【背包 / 位面 / 集装箱】
// 三类可用量，可一键取出到背包。
// 结构：深色底板 → 标题栏(含关闭) + 搜索框 + 滚动物品列表；**无底部手动上传**（用户定案）。
// 用户总纲：只显集装箱、不进左侧位面；颜色按三源最多者：背包橙/位面紫/集装箱蓝；只管固体、不碰流体。
//
// ─────────────────────────────────────────────────────────────────────────
// 本文件覆盖「这个功能」在游戏内会遇到的【全部情况】。改之前先按编号对一遍：
//
//  A. 出现 / 消失
//     A1 建不出来      —— RegisterWorld 早于 PlayerController ⇒ 0.1s 重试 ticker（上限 60s）。
//     A2 过一会儿消失  —— AddDynamic 委托静默失效 ⇒ TickRefresh 自观测背包 widget 可见性作为真相。
//     A3 按 ESC 消失   —— 暂停菜单把背包顶掉并广播 open=0，不是玩家关背包 ⇒ 暂停期间冻结 + 委托里直接忽略。
//     A4 点了 ×        —— bUserClosed 只压"本次会话"，关背包或重开背包即复位，下次照样出现。
//     A5 单帧抖动      —— 判"关"连续 2 次才真收（CloseTicks），防闪一下就没了。
//     A6 反复开关背包  —— RebuildWidget 有 `if (!WidgetTree->RootWidget)` 守卫，单例复用不重建树。
//     A7 世界切换      —— 单例挂在 PlayerController 上，换世界时弱引用失效，新世界 RegisterWorld 重新建。
//
//  B. 内容
//     B1 开关关着      —— 显示「（智能仓储未开启）」提示，不显示货。
//     B2 没有货        —— 显示「（无集装箱货物）」提示。
//     B3 搜索过滤      —— 按物品名大小写敏感 Contains 过滤，签名变化才重建。
//     B4 刷新不丢滚动  —— 保留 ScrollOffset。
//     B5 刷新不丢点击  —— 内容签名 LastSig 相同则跳过重建（否则 0.5s 拆一次按钮，点击会丢）。
//     B6 三源着色      —— 背包橙 / 位面紫 / 集装箱蓝，**严格大于**才染色（并列不染，避免误导）。
//     B7 液体           —— 天然不出现：集装箱列表只收 AFGBuildableStorage，流体是 PipeReservoir 支系。
//
//  C. 取货（见 RequestTakeFromContainer 与 SmartWarehouse 侧注释）
//     C1 数量档位      —— 左键 1 / Shift 10 / Ctrl 一组（GetStackSize）。
//     C2 超出存量      —— 行里按 NumContainer 夹取，服务端再夹一次。
//     C3 背包满        —— 「先加后删」，一个都放不进就一个都不删，绝不蒸发。
//     C4 客户端/服务端 —— 集装箱是 actor 库存，必须 RPC 到服务端改；面板绝不本地直接扣。
//
//  D. 布局 / 动画（见 LayoutLeftOfInventory 与 TickSlide 注释）
//     D1 顶底与位面齐平 —— 锚点 (0,0)+Alignment(0,0)，读参照面板 RefTop/RefBottom。
//     D2 空白带太窄     —— 收窄到带内（最小 120px），绝不压原版面板。
//     D3 读不到参照     —— 回退「贴左 + 纵向居中」，保证始终可见。
//     D4 分辨率变化     —— 每 0.5s 重算 Layout，自动跟随。
//     D5 滑入动画       —— 每帧 ticker + EaseOutQuint + dt 钳 1/30s + 只读 SlideTargetY。
//
//  E. 只读 / 边界
//     E1 原版上传站     —— 本文件完全不碰，只做「取出/下载」方向。
//     E2 原版背包蓝图   —— 不依赖其任何子节点，纯 C++ 建树。
//     E3 智能仓储本体   —— 只管固体；流体另有「流体仓储」，这里不参与。
// ─────────────────────────────────────────────────────────────────────────
#include "Remote_FactoryWarehousePanel.h"          // 自由函数 Remote_FactoryWarehousePanel_Init 的声明（GameWorldModule 调它）；★UHT 要求本 TU 首个 include 须与文件名匹配
#include "Remote_FactoryWarehousePanelWidget.h"   // UCLASS 定义（含 .generated.h），消除 GameWorldModule 的 C2653 竞态
#include "Remote_FactoryRCO.h"                     // URemote_FactoryRCO::Get + Server_TakeFromContainer（服务端权威）

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"      // ★2026-11-04 左移：UWidgetBlueprintLibrary::GetAllWidgetsOfClass 读原版物品栏几何
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"       // RebuildWidget 里做根画布（照 Remote_FactoryItemSelectPanel 的建法）
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"           // ★2026-11-05【第二十二次修】标题栏箭头按钮固定 24×20 可点面积（Automatic 宽度太窄，鼠标命不中）
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"          // ★2026-11-04 视口自适应：UGameViewportClient::GetViewportSize
#include "Widgets/SWidget.h"                     // TSharedRef<SWidget>（RebuildWidget 返回类型）
#include "Framework/Application/SlateApplication.h"  // ★2026-11-04 单按钮三用：读 Shift/Ctrl 修饰键状态
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Layout/Anchors.h"
#include "Containers/Ticker.h"
#include "GameFramework/PlayerController.h"
#include "FGPlayerController.h"                 // AFGPlayerController::OnToggleInventory(bool) — 动态 multicast 委托
#include "UI/FGGameUI.h"                        // ★2026-11-05【第二十四次修】UFGGameUI::GetInteractWidgetStack() —— 太空电梯/HUB 打开时不显示本面板
#include "UI/FGInteractWidget.h"                // 栈内元素类型完整定义（要调 GetClass()）
#include "UI/FGGameUI.h"                        // ★2026-11-04 ESC 不收面板：UFGGameUI::IsPauseMenuOpen()（FGGameUI.h:165 读 mPauseMenuOpen）
#include "FGCharacterPlayer.h"                 // AFGCharacterPlayer::GetInventory()
#include "Equipment/FGBuildGun.h"              // ★2026-11-05【第二十一次修】AFGBuildGun::IsInState(EBuildGunState::BGS_MENU) —— 判定「建造菜单开着」，那期间不该把面板当前背包面板跟着常驻
#include "FGInventoryComponent.h"              // UFGInventoryComponent::GetNumItems
#include "Resources/FGItemDescriptor.h"         // UFGItemDescriptor / GetSmallIcon / GetItemName
#include "Remote_FactorySmartWarehouse.h"       // GetContainerAvailability / GetCentralStorageRealCount / IsSmartWarehouseEnabled
#include "HAL/IConsoleManager.h"                // ★对照探针运行时开关 wt.PanelCmp（FAutoConsoleCommand）
#include "Remote_FactoryLog.h"

namespace
{
    // 单例（进程内唯一面板）。★weak：不延长 UObject 生命期，靠 PlayerController 的委托强引用保活。
    //   ★勿在 NativeDestruct 里 Reset（背包关闭也会走 NativeDestruct），否则下次开背包面板就没了。
    static TWeakObjectPtr<URemote_FactoryWarehousePanel> GPanel;

    // ★2026-11-04 自愈 ticker 是否还活着（装机标志）。由 InstallSelfHealTicker 置 true，
    //   ticker 自我注销时（成功 / 世界失效）必须置回 false，否则换世界后不会重装 ⇒ 新世界无自愈。
    //   ★不能用"装过就不再装"的 static bool：那会在换世界后彻底失去自愈能力。
    static bool GSelfHealActive = false;

    // 配色（与成本格着色、用户总纲一致）。
    //   三源颜色是**同一套语义**，面板行、成本格背景色、集装箱标识都用这三色，改一处必须同步改其它处，
    //   否则玩家会看到"同一个集装箱在两处是两种颜色"。语义 = 「数量最多的那个来源」。
    static const FLinearColor COL_VALUE     = FLinearColor(0.870f, 0.940f, 0.970f, 1.f);  // 常规文字
    static const FLinearColor COL_DIM       = FLinearColor(0.480f, 0.560f, 0.630f, 1.f);  // 次要文字（"背N/云N"）
    static const FLinearColor COL_BACKPACK  = FLinearColor(1.000f, 0.550f, 0.000f, 1.f);  // 背包 橙
    static const FLinearColor COL_CLOUD     = FLinearColor(0.600f, 0.180f, 0.850f, 1.f);  // 位面 紫（照原版紫）
    static const FLinearColor COL_CONTAINER = FLinearColor(0.120f, 0.450f, 1.000f, 1.f);  // 集装箱 蓝（与成本格染蓝同色系）
    static const FLinearColor COL_TAKE_BG   = FLinearColor(0.106f, 0.353f, 0.243f, 0.96f); // 取出按钮 绿（可操作）
    // ★2026-11-05 存入按钮：取蓝（= 集装箱色系，动作语义是"东西进集装箱"）。
    //   刻意和取出的绿区分开：两颗按钮挨着放，颜色必须一眼能分辨方向，否则玩家总点错。
    static const FLinearColor COL_STORE_BG  = FLinearColor(0.098f, 0.243f, 0.494f, 0.96f); // 存入按钮 蓝
    static const FLinearColor COL_MUTED_BG  = FLinearColor(0.160f, 0.180f, 0.220f, 1.f);   // 灰按钮（> 收起）
    static const FLinearColor COL_ROOT_BG   = FLinearColor(0.045f, 0.060f, 0.085f, 0.96f); // 面板底（半透明深色，不挡后方）
    static const FLinearColor COL_TITLE_BG  = FLinearColor(0.080f, 0.100f, 0.140f, 1.00f); // 标题栏底
    static const FLinearColor COL_ROW_BG    = FLinearColor(0.070f, 0.090f, 0.120f, 0.96f); // 行底

    // 建文本块（强制对齐，避免 UTextBlock 默认居中导致纵向参差）。
    // ★必须显式 SetAutoWrapText(false)：窄面板里长物品名会自动换行，把行高撑成两倍、列表挤爆。
    // ★字号在调用点就按"面板最窄 120px"考虑过（名称 13 / 数量 12 / 副量 9），别再调大。
    static UTextBlock* WH_MakeText(UWidgetTree* Tree, const FText& InText, int32 FontSize,
                                   bool bBold, FLinearColor Color,
                                   ETextJustify::Type Justify = ETextJustify::Left)
    {
        UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
        T->SetText(InText);
        T->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), FontSize));
        T->SetColorAndOpacity(FSlateColor(Color));
        T->SetAutoWrapText(false);
        T->SetJustification(Justify);
        return T;
    }

    // 建按钮（内容槽 HAlign_Fill，文字按自身对齐，避免标签左右漂）。
    static UButton* WH_MakeButton(UWidgetTree* Tree, const FText& InText, FLinearColor BgColor,
                                  UTextBlock** OutLabel = nullptr,
                                  ETextJustify::Type Justify = ETextJustify::Center,
                                  int32 FontSize = 12)
    {
        UButton* B = Tree->ConstructWidget<UButton>();
        B->SetBackgroundColor(BgColor);
        UTextBlock* T = WH_MakeText(Tree, InText, FontSize, false, COL_VALUE, Justify);
        if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(B->AddChild(T)))
        {
            ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
            ButtonSlot->SetVerticalAlignment(VAlign_Center);
            ButtonSlot->SetPadding(FMargin(4.f, 0.f));
        }
        if (OutLabel)
        {
            *OutLabel = T;
        }
        return B;
    }

    static UFGInventoryComponent* GetLocalPlayerInventory(UWorld* W)
    {
        if (!W) return nullptr;
        if (APlayerController* PC = W->GetFirstPlayerController())
            if (AFGCharacterPlayer* Char = Cast<AFGCharacterPlayer>(PC->GetPawn()))
                return Char->GetInventory();
        return nullptr;
    }

    // 建单例面板（不挂视口，只 CreateWidget）。★失败是**正常情况**，不是错误：
    //   本函数在 GameWorldModule::POST_INITIALIZATION 阶段被调用，那时 PlayerController 往往还没创建，
    //   GetFirstPlayerController() 返回 null ⇒ 直接 return，等 RegisterWorld 的重试 ticker 再调。
    //   ⚠ 千万别在这里 CreateWidget(nullptr) —— OwningPlayer 为空会让 CreateWidget 直接返回 null，
    //      且此后所有 WidgetBlueprintLibrary 的界面查询都拿不到这个世界上下文。
    // ⚠ 只创建、不绑定：委托注册与 ticker 都必须等成员就绪后由 RegisterWorld 统一做，
    //   否则 CreateWidget 内部就会跑一次 RebuildWidget → 那时还没有 ticker，动画/布局全靠 NativeConstruct 兜底。
    //
    // ★2026-11-04 加诊断（用户报"进游戏啥也没有"，日志里只有 RegisterWorld 第一条，之后连
    //   "no player controller yet" 都没有 ⇒ 说明流程要么进了 else 分支、要么 CreateWidget 返回 null）。
    //   现在两种失败都打日志，下次一看就知道卡在哪：
    //     · PC 为 null   —— 控制器还没创建（正常，等重试 ticker）
    //     · CreateWidget 返回 null —— 有 PC 但不是可用的本地控制器（PC 不在有效 world / 不是本地 PC）
    //   判据：CreateWidget 需要 OwningPlayer 处于有效 World 且 IsLocalPlayerController()。
    //   这里提前判一次并打日志，比让它静默返回 null 好排查。
    static void EnsurePanelCreated(UWorld* W)
    {
        if (GPanel.IsValid())
        {
            return;
        }
        if (!W)
        {
            return;
        }
        APlayerController* PC = W->GetFirstPlayerController();
        if (!PC)
        {
            return;
        }
        URemote_FactoryWarehousePanel* P = CreateWidget<URemote_FactoryWarehousePanel>(PC);
        if (P)
        {
            GPanel = P;   // 绑定在 RegisterWorld 里统一做（成员函数可调私有成员），这里不重复调
        }
        else
        {
            // 只在第一次失败时打，避免 0.25s 一次刷屏（重试期间这条会反复走到）。
            static bool bLoggedCreateNull = false;
            if (!bLoggedCreateNull)
            {
                bLoggedCreateNull = true;
                WT_WARNF(TEXT("[warehouse-panel] EnsurePanelCreated: CreateWidget NULL (PC=%s local=%d world=%s)."),
                         PC->GetClass() ? *PC->GetClass()->GetName() : TEXT("?"),
                         PC->IsLocalPlayerController() ? 1 : 0,
                         PC->GetWorld() ? TEXT("ok") : TEXT("null"));
            }
        }
    }

    // ★2026-11-04【全局自愈 ticker】用户实测「进游戏啥也没有」，日志里 RegisterWorld 只打了一条、
    //   之后连"no player controller yet"都没有 ⇒ 一次性重试 ticker（见 RegisterWorld）在某些时序下
    //   不可靠。为了彻底避免"永远没有面板"，再挂一条【独立于任何 UObject 生命期】的兜底 ticker：
    //   每 0.25s 问一次"面板存在吗？没有就建；建好顺手注册监听"，成功后自我注销。
    //
    //   为什么它一定跑得起来：AddTicker 走 FTSTicker 核心 ticker，与 UObject/世界模块生命期无关，
    //   只要游戏进程在跑就会被调用（成本是每 0.25s 一次弱引用判空，可忽略）。
    //   为什么不设次数上限：不进存档（主菜单挂机）时永远没有 PlayerController；每 0.25s 一次判空
    //   实在太便宜，且换世界会用新 World 指针重新注册，故只在 World 失效时注销。
    //   ★捕获 WEAK 世界指针，绝不裸指针（读档后旧 World 会悬空，Shipping 下崩溃不留日志）。
    //   ★与 RegisterWorld 的重试 ticker 互补：那条最多试 60s 且依赖 RegisterWorld 被调到；
    //     这条与调用时机无关，只要进程活着就在。两条都会走 EnsurePanelCreated，幂等。
    static void InstallSelfHealTicker(UWorld* W)
    {
        if (!W)
        {
            return;
        }
        // 进程内只存在一条自愈 ticker（重复装会多挂 ticker，虽幂等但浪费）。
        // ★注意：绝不能用"装过一次就永不重装"的布尔量 —— ticker 会在成功/世界失效时自我注销，
        //   那样换世界（读档、进新存档）后就再也不会重装了。所以这里看的是【ticker 是否还活着】。
        if (GSelfHealActive)
        {
            return;
        }
        GSelfHealActive = true;

        TWeakObjectPtr<UWorld> WeakWorld(W);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld](float) -> bool
        {
            UWorld* World = WeakWorld.Get();
            if (!World)
            {
                // 世界没了（读档/回主菜单）。★必须复位 GSelfHealActive，否则新世界启动时
                // InstallSelfHealTicker 会以为"已经装过了"直接返回 ⇒ 新世界没有自愈 ⇒ 面板永不出现。
                GSelfHealActive = false;
                WT_WARNF(TEXT("[warehouse-panel] self-heal ticker: world gone, stopping."));
                return false;
            }
            // ★判"面板是不是本世界的"：换世界后旧面板挂在旧 PlayerController 上，GPanel 可能仍有效，
            //   但它的 GetWorld() 已不是当前世界 ⇒ 必须重建。只看 IsValid() 会漏掉这种情况。
            if (GPanel.IsValid() && GPanel->GetWorld() == World)
            {
                GSelfHealActive = false;
                return false;   // 本世界的面板已就绪，注销
            }
            EnsurePanelCreated(World);
            if (GPanel.IsValid())
            {
                URemote_FactoryWarehousePanel::RegisterToggleListenerForCurrentPanel();
                GSelfHealActive = false;
                WT_WARNF(TEXT("[warehouse-panel] self-heal: panel created + listener registered."));
                return false;
            }
            return true;   // 还没控制器，继续等
        }), 0.25f);
        WT_WARNF(TEXT("[warehouse-panel] self-heal ticker installed (0.25s, stops on success)."));
    }

    // ★2026-11-04 「背包当前是否打开」的自观测判据 —— 这是整套显隐状态机的**唯一真相来源**。
    //   为什么不用 AFGPlayerController::OnToggleInventory：实测开关若干次后委托会静默失效
    //   （widget 被 Slate 拆除后 UObject 可能进 pending kill，AddDynamic 绑定的回调不再触发），
    //   于是"背包关了面板不收" / "背包开了面板不来"。委托只保留为"立即响应"的加速通道。
    //   做法：枚举原版背包 widget（类名含 Widget_Inventory）并检查可见性。
    //   0.1s 节流，避免每帧 ticker 都全量扫 widget（GetAllWidgetsOfClass 是重活）。
    //
    //   ★过滤器细节（改错就会误判）：
    //     · 必须排除含 "Slot" 的类名（Widget_InventorySlot 是每一格物品的控件，它一直可见）。
    //     · 只认原版主背包；本 mod 自己的面板类名不含 Widget_Inventory，天然不冲突。
    //     · 用 Contains 不用 ==：不同版本/蓝图重命名后可能带 _C 后缀或前缀。
    //
    //   ★已知局限（不是 bug，是取舍）：如果玩家装了别的 UI mod 也造了类名带 Widget_Inventory 的控件，
    //     且它常驻可见，本判据会认为背包一直开着 ⇒ 面板常驻。目前未遇到，遇到再加白名单。
    // ★2026-11-04 暂停菜单判据（修「按一下 ESC 面板就没了」）：
    //     实测按 ESC 会加载 Widget_PauseMenu_* 一系列控件，且【原版背包 widget 依然存在、只是不可见】。
    //     旧判据只看 IsVisible() ⇒ 暂停期间误判成「背包关了」⇒ HidePanel；
    //     从暂停菜单返回后背包重新可见需要下一帧，日志里却是「之后再无任何日志」——
    //     说明背包 widget 恢复可见的时机不稳定，靠可见性来回切会让面板消失。
    //     修法：暂停菜单开着时【不改变】背包开关状态（冻结），即「按 ESC 不收面板」，
    //     关了暂停菜单背包若是开着的，面板仍在原处，不会凭空消失。
    //   ★判据优先用官方状态 UFGGameUI::mPauseMenuOpen（FGGameUI.h:165/360，SetPauseMenuOpen 是
    //     BlueprintCallable、由暂停菜单 BP 自己写），读不到时才回退「枚举 Widget_PauseMenu* 控件」。
    //   ⚠ AFGPlayerController::IsPauseMenuOpen() 在 SDK 里是**空桩**（FGPlayerController.cpp:137
    //      `{ return bool(); }`），永远 false，绝不能直接用它。
    static bool WT_IsPauseMenuOpen(UWorld* World)
    {
        if (!World)
        {
            return false;
        }
        if (APlayerController* PC = World->GetFirstPlayerController())
        {
            if (AFGPlayerController* FPC = Cast<AFGPlayerController>(PC))
            {
                if (UFGGameUI* GameUI = FPC->GetGameUI())
                {
                    if (GameUI->IsPauseMenuOpen())
                    {
                        return true;
                    }
                }
            }
        }

        // 回退判据：GameUI 路径失效（GetGameUI 在部分版本是空桩）时，靠控件存在与否兜底。
        //   暂停菜单会在按 ESC 时 new 出 Widget_PauseMenu_* 系列控件；名字里带 PauseMenu 的可见控件即视为暂停中。
        TArray<UUserWidget*> All;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, All, UUserWidget::StaticClass(), /*TopLevelOnly=*/false);
        for (UUserWidget* Wdg : All)
        {
            if (!Wdg || !Wdg->GetClass())
            {
                continue;
            }
            if (!Wdg->IsVisible())
            {
                continue;
            }
            if (Wdg->GetClass()->GetName().Contains(TEXT("PauseMenu")))
            {
                return true;
            }
        }
        return false;
    }

    // ★2026-11-04 「背包当前是否打开」的自观测判据 —— 整套显隐状态机的**唯一真相来源**。
    //   为什么不用 AFGPlayerController::OnToggleInventory：实测开关若干次后委托会静默失效
    //   （widget 被 Slate 拆除后 UObject 可能进 pending kill，AddDynamic 绑定的回调不再触发），
    //   于是出现"背包关了面板不收" / "背包开了面板不来"。委托只保留为"立即响应"的加速通道。
    //   做法：枚举原版背包 widget（类名含 Widget_Inventory）并检查可见性。
    //   0.1s 节流，避免每帧 ticker 都全量扫 widget（GetAllWidgetsOfClass 是重活）。
    //
    //   ★过滤器细节（改错就会误判）：
    //     · 必须排除含 "Slot" 的类名（Widget_InventorySlot 是每一格物品的控件，它一直可见）。
    //     · 只认原版主背包；本 mod 自己的面板类名不含 Widget_Inventory，天然不冲突。
    //     · 用 Contains 不用 ==：不同版本/蓝图重命名后可能带 _C 后缀或前缀。
    //
    //   ★已知局限（是取舍不是 bug）：若玩家另装 UI mod 也造了类名带 Widget_Inventory 且常驻可见的控件，
    //     本判据会认为背包一直开着 ⇒ 面板常驻。目前未遇到，遇到再加白名单。
    //
    // ★★★2026-11-05【第十三次修「轮询把背包误判成关了」—— 这是「连闪 / 随机消失」的第二条链】
    //   【病理】本函数每 0.1s 枚举**全部** UUserWidget 判 IsVisible()。而我们自己的面板也是 UUserWidget，
    //   且 RefreshContents 每 0.5s 会 RemoveChild/AddChild 重建整个行列表 —— Slate 在重建子树的那一帧里，
    //   父链会短暂处于"未布局"状态，原版背包 widget 的 IsVisible() 在该帧返回 false。
    //   轮询刚好落在这一帧 ⇒ 判"背包关了" ⇒ CloseTicks 凑满 2 ⇒ HidePanel ⇒ 背包恢复 ⇒ 又判"开了" ⇒
    //   再 ShowPanel ⇒ **0.6s 一个周期的自激振荡**。日志里 open=0:open=1 恒为 2:1 就是这个节奏。
    //
    //   【日志对照（铁证）】
    //     18:44 那次（正常）：open=0 三次 / open=1 两次 —— 与玩家真实操作次数吻合；
    //     19:01 起（坏）：open=0 二十九次 / open=1 十五次；19:46：五十三 / 二十七。
    //   而 19:01 正是"改成因轮询自观测"的那一版 ⇒ 时间点完全对上。
    //
    //   【修法】轮询结果不可全信，必须用**持续时间**确认：只有"背包持续不可见 ≥ ConfirmCloseSec"
    //   才认为真关。凭据是"噪声是瞬时的、真关是永久的"这条本质差别：
    //     · Slate 重建子树造成的 IsVisible 假 false 只持续几帧到零点几秒，随后必然恢复可见；
    //     · 玩家真关背包后，背包**再也不会**变回可见。
    //   ⇒ 任何短于确认时长的"不可见"都会被自动冲掉（一旦恢复可见就重新计时）。
    //
    //   ★★这里必须记一条**走过弯路的设计史**，免得以后有人再往这个方向改：
    //     我原本写的是"UI 安静期门控"（我们刚动过 UI 的那 0.35s 内不采信判关，用 MarkUiMutated 打时间戳）。
    //     用状态机仿真穷举后它有**两个致命缺陷**，所以被整个拆掉了：
    //       ① **挡不住长噪声**：噪声持续 0.28s 时，"距上次 UI 变更"早已超过 0.35s（因为中间一直在 SHOW），
    //          门控形同虚设 ⇒ 仿真实测 attach=7/hide=6，**振荡照旧**。
    //       ② **会死锁**：若签名持续变化（工厂一直生产 ⇒ 物品数一直变 ⇒ 每次刷新都真重建），
    //          每 0.5s 就打一次时间戳，安静期**永远等不到** ⇒ 玩家关背包后面板**永远收不起来**
    //          （仿真实测 hide=0）。
    //     ⇒ 教训：**别用"我最近干过什么"来判断"对方状态可不可信"**，那种因果链一旦有周期性就会自锁。
    //       要判断对方状态，就用**对方状态本身的持续时间**。这个量不依赖我们做过什么，天然无自锁。
    // ★2026-11-05【第十三次修】bOutFresh：本次返回的值是"刚刚真的枚举出来的"还是"0.1s 节流的缓存"。
    //   ★为什么必须区分：委托（OnToggleInventory）给出的 open=0 是**异步**的，可能在两拍采样之间到达。
    //     若下一拍读到的是**陈旧缓存**（还报 True），就会把委托留下的 bWantClose 当成误报清掉
    //     ⇒ 委托这条零延迟路径彻底失效，只剩 1.0s 的轮询兜底（自检仿真实测：关闭延迟全是 1.0s）。
    //   ⇒ 只有"新鲜采样"报可见，才允许认定委托是误报。
    // ★★★2026-11-05【第二十六次修·把「大界面拦阻」抽成独立判据】
    //   太空电梯（金字塔）界面、HUB、里程碑这些**建筑交互界面**打开时不该显示取货面板。
    //   判据用原版自己的交互栈 UFGGameUI::GetInteractWidgetStack()（UI/FGGameUI.h:84）——
    //   比枚举 widget 类名可靠，且以后原版加新界面也自动覆盖。
    //   ★刻意**不**用 HasActiveInteractWidget()（栈非空即真）：万一原版把背包本身也塞进交互栈，
    //     那样会变成"永远不显示"。这里按栈内**类名**拦已知大界面，并加"栈上出现任何非背包/非本mod的互动控件即拦"通用兜底（#43 收紧，见循环内）。
    //   ★为什么从 WT_IsInventoryWidgetOpen 里抽出来：那个函数马上要降级为"兜底"，
    //     而这条拦阻是**硬规则**，TickRefresh 每一拍都必须独立执行，不能跟着兜底一起被跳过。
    static bool WT_IsBlockedByBigUI(UWorld* World, FString* OutWhy = nullptr)
    {
        if (!World)
        {
            return false;
        }
        APlayerController* PC = World->GetFirstPlayerController();
        AFGPlayerController* FGPC = Cast<AFGPlayerController>(PC);
        if (!FGPC)
        {
            return false;
        }
        UFGGameUI* GameUI = FGPC->GetGameUI();
        if (!GameUI)
        {
            return false;
        }
        TArray<UFGInteractWidget*> Stack = GameUI->GetInteractWidgetStack();
        if (Stack.Num() == 0)
        {
            return false;
        }
        FString Names;
        bool bBlocked = false;
        for (UFGInteractWidget* IW : Stack)
        {
            if (!IW || !IW->GetClass())
            {
                continue;
            }
            const FString N = IW->GetClass()->GetName();
            Names += N + TEXT(",");
            // ★2026-11-07【#43 收紧显隐】原版只按固定类名白名单拦（太空电梯/HUB/里程碑/交易站），
            //   漏掉 MAM 研究树、购物清单、别的 mod 面板等"背包控件在背后仍可见"的大界面 ⇒ 取货面板跟着冒出。
            //   改为"只要栈上除了背包本身还有别的互动控件就拦"：背包控件类名含 Inventory，本 mod 面板不在互动栈上，
            //   其余任何互动界面（MAM 树 / 购物清单 / 机器交互 / 其他 mod 面板）一律视为"正在看别的界面"，面板隐藏。
            //   既不放过未列举的大界面，也不会把"只有背包开着"误判成拦截（背包名含 Inventory）。
            if (N.Contains(TEXT("SpaceElevator")) || N.Contains(TEXT("Elevator")) ||
                N.Contains(TEXT("Milestone")) || N.Contains(TEXT("Hub")) ||
                N.Contains(TEXT("TradingPost")) ||
                N.Contains(TEXT("MAMTree")) || N.Contains(TEXT("MAM")) ||
                N.Contains(TEXT("ShoppingList")) || N.Contains(TEXT("Schematic")) ||
                N.Contains(TEXT("Codex")) || N.Contains(TEXT("Map")))
            {
                bBlocked = true;
            }
            // ★通用兜底：栈上存在"非背包、非本 mod"的互动控件 ⇒ 玩家在跟别的界面交互，面板必须隐藏。
            if (!N.Contains(TEXT("Inventory")) && !N.Contains(TEXT("Remote_Factory")))
            {
                bBlocked = true;
            }
        }
        if (OutWhy)
        {
            *OutWhy = Names;
        }
        return bBlocked;
    }

    // ★★★2026-11-05【第二十六次修·把「原版背包控件」钉成固定身份 —— 根治"面板间歇消失"】
    //
    //   【一手证据】2026-11-05 20:10~20:11 那场日志（用户报"面板又没了"）：
    //     12.10.58.883 `layout WRITE y=287.1 (prev=180.9)`   ← 面板被显示并在移动
    //     12.11.00.853 `[inventory-probe] closed: reason=no-shape-match`
    //     12.11.01.446 `HidePanel: -> Hidden`                 ← 0.6s 后收起
    //     12.11.03.748 `layout WRITE y=287.1 (prev=237.6)`    ← 又被显示
    //     12.11.04.868 `HidePanel: -> Hidden`                 ← 再收起 ……
    //   即：**背包关着的时候，面板被反复唤起又收起，周期 ~2.5 秒**。
    //   而同期探针绝大多数时刻报 `no-shape-match` ⇒ 形状判据在**间歇性误命中**：
    //   屏幕上任何一块 200~700 × 400~1000 的竖长面板（建造菜单、通知条、别的 mod 面板）
    //   只要闪一下，就会把"背包开着"点亮一拍 ⇒ ShowPanel ⇒ 0.6s 后判关 ⇒ HidePanel。
    //
    //   【反向硬伤】同一套形状判据在玩家改了 UI 缩放 / 原版改版时又会**永远命中不了**
    //   ⇒ 面板永远不出现（历史上反复出现的那个症状，探针把它抓了个正着）。
    //
    //   【根治】形状只用来**发现一次**，之后改认**身份**（TWeakObjectPtr）：
    //     原版背包控件是常驻实例（开关只切可见性），钉住它之后"开着没开"退化成一次
    //     `IsVisible()` —— 既不会误命中别人，也不会随 UI 缩放漂移。
    //   ★钉住的时机只认委托 open=1（那一刻背包一定真开着），并开一个 1.5 秒的窗口反复尝试：
    //     委托触发的那一帧控件常常**还没布局**（尺寸 0），只试一帧必然钉不住。
    //   ★窗口内允许**改钉**：万一原版每次打开都新建实例（旧实例留着但不显示），
    //     窗口内以形状命中者为准可以把身份更新到新实例，不会钉死在旧实例上。
    //   ★窗口外不再改钉 —— 身份一旦确认就只信它，否则等于又把形状判据请回来了。
    static TWeakObjectPtr<UUserWidget> G_PinnedSidebar;
    static double G_PinDeadline = -1.0;

    /** 委托报告"背包刚打开"时调用：开一次 1.5 秒的钉住窗口（见上）。 */
    static void WT_ArmPinSidebar(UWorld* World)
    {
        const double NowSec = World ? static_cast<double>(World->GetTimeSeconds()) : 0.0;
        G_PinDeadline = NowSec + 1.5;
    }

    /**
     * 换世界 / 读档时清掉钉住的身份。
     * ★为什么必须清：旧世界（甚至旧关卡）的 UI 实例已随 Slate 拆除，留着它只会让
     *   "身份判据"永远返回关 ⇒ 回到"面板永远不出现"。弱指针本身会自愈，但时间戳
     *   G_PinDeadline 是拿旧世界的时间轴算的，不清会跨世界比较，属于埋雷。
     */
    static void WT_ResetPinnedSidebar()
    {
        G_PinnedSidebar.Reset();
        G_PinDeadline = -1.0;
    }

    /**
     * 按形状找"玩家侧栏"，返回**最靠左**的那个（没有就返回 nullptr）。
     *
     * ★为什么最靠左：本面板要贴在这块侧栏的**左侧**，而 LayoutLeftOfInventory 挑参照用的
     *   也是"最靠左" ⇒ 两处口径必须一致，否则会出现"钉住的是 A、贴边参照的却是 B"的错位。
     * ★判据必须和轮询共用（这里就是唯一实现，轮询直接调本函数）：
     *   历史上因为"两处阈值不一样"，出现过布局一条参照都找不到的坑。
     * ★OutCand 给探针用：把"可见且已布局"的控件清单（最多 8 个）带出去，判 false 时打日志。
     */
    /**
     * ★★★2026-11-10：改用**类名**识别「玩家物品栏」控件（用户指令：只挂到物品栏、其余一律拦截）。
     *   旧实现用形状（最靠左竖长面板）去猜背包，会误命中建造菜单 / 太空电梯子面板等，
     *   或在背包打开瞬间因形状抖动判成「没开」⇒ 面板永远不出现（用户实锤）。
     *   现只看控件类名：原版主背包类名含 "Inventory"（日志实证 Widget_Inventory_C），
     *   排除每格物品控件（含 "Slot"，常驻可见）与本 mod 自己的面板（含 "Remote_Factory"）。
     *   再要求可见且已布局（尺寸>0）跳过幽灵实例。返回最靠左的匹配控件；找不到返回 nullptr。
     *   签名 (World, OutCand) 保持不变，调用方（WT_PinSidebarNow / WT_IsInventoryWidgetOpen /
     *   LayoutLeftOfInventory）无需改动即得「按身份识别」语义。
     */
    static UUserWidget* WT_FindSidebarWidget(UWorld* World, FString* OutCand = nullptr)
    {
        int32 CandCount = 0;
        FString Cand;
        if (OutCand) { *OutCand = FString(); }
        if (!World)
        {
            return nullptr;
        }
        TArray<UUserWidget*> All;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, All, UUserWidget::StaticClass(), /*TopLevelOnly=*/false);

        UUserWidget* Best = nullptr;
        float        BestLeft = 1.0e9f;
        for (UUserWidget* Wdg : All)
        {
            if (!Wdg || !Wdg->GetClass())
            {
                continue;
            }
            const FString CN = Wdg->GetClass()->GetName();
            // 只认原版玩家主背包：类名含 "Inventory"；排除每格物品控件(含 Slot)与本 mod 面板。
            if (!CN.Contains(TEXT("Inventory")))
            {
                continue;
            }
            if (CN.Contains(TEXT("Slot")) || CN.Contains(TEXT("Remote_Factory")))
            {
                continue;
            }
            if (!Wdg->IsVisible())
            {
                continue;
            }
            const FGeometry G = Wdg->GetCachedGeometry();
            const FVector2f Sz = G.GetLocalSize();
            // 幽灵实例过滤：IsVisible() 只读 Visibility 属性，未挂载/未布局的实例同样返回 true。
            if (Sz.X < 2.f || Sz.Y < 2.f)
            {
                continue;
            }
            if (OutCand && CandCount < 8)
            {
                ++CandCount;
                Cand += FString::Printf(TEXT("%s(%.0fx%.0f);"), *CN, Sz.X, Sz.Y);
            }
            const float L = G.GetAbsolutePosition().X;
            if (L < BestLeft)
            {
                BestLeft = L;
                Best = Wdg;
            }
        }
        if (OutCand)
        {
            *OutCand = FString::Printf(TEXT("all=%d inventory=%d | %s"), All.Num(), CandCount, *Cand);
        }
        return Best;
    }

    /**
     * 立刻把身份钉到"当前屏幕上最靠左的那块侧栏"上（找不到就什么都不做）。
     *
     * ★为什么要这个入口：TickRefresh 的自愈会在"轮询说开着、委托说关着"持续 3 秒后
     *   采纳轮询。但**只采纳不钉身份**会留下一个致命尾巴 —— 万一那是一次误命中，
     *   委托已经被改成 true，而轮询此后每次都只会说"关"，`bPollTrustworthy` 又因为
     *   "没有身份"而永远为假 ⇒ 谁也纠正不回来（自检仿真场景2 复现：面板挂上去再也不下来）。
     *   ⇒ 采纳轮询的同时把身份钉上，等于把"举证责任"一起交给它：那个控件一旦不可见，
     *     轮询立刻变假，而此时**有身份** ⇒ 2 秒后就能把权威改回来，自动闭环。
     */
    static void WT_PinSidebarNow(UWorld* World)
    {
        if (UUserWidget* Hit = WT_FindSidebarWidget(World, nullptr))
        {
            G_PinnedSidebar = Hit;
        }
    }

    static bool WT_IsInventoryWidgetOpen(UWorld* World, bool* bOutFresh = nullptr)    {
        static double LastProbe = -1000.0;
        static bool   CachedOpen = false;

        if (bOutFresh)
        {
            *bOutFresh = false;
        }
        if (!World)
        {
            return false;
        }

        // ★★★2026-11-05【第二十五次修·探针】用户报「所有操作都没有面板」，而日志里整个会话
        //   一条 ShowPanel 都没有 ⇒ 本函数（面板显隐的唯一输入）**一直返回 false**。
        //   这个函数有三条提前返回 + 一个形状判定，任何一条都可能把人挡死，光看代码猜不出来。
        //   故加一条**限流 2 秒**的诊断：判 false 时把"被哪一条挡下 + 当时屏幕上有哪些可见控件及尺寸"
        //   打出来。收到这一行就能直接定位，不用再猜。
        //   ★探针只读不写，不影响任何行为；定位完可以整段删掉。
        const double NowT = static_cast<double>(World->GetTimeSeconds());
        static double LastDiag = -1000.0;
        auto Diag = [&](const TCHAR* Why, const FString& Extra)
        {
            if (NowT - LastDiag < 2.0)
            {
                return;
            }
            LastDiag = NowT;
            WT_WARNF(TEXT("[inventory-probe] closed: reason=%s | %s"), Why, *Extra);
        };

        // ★★★2026-11-05【第二十三次修「建造常驻 / 太空电梯常驻关不掉 / 开背包关不掉」】
        //   第二十一次修只排除了 BGS_MENU，**不够**。一手症状与日志（15:04 / 15:08 两场）：
        //     · 按 Q 后要 3~5 秒才关 —— 那是玩家在建造菜单里挑建筑的时长：一进 BGS_BUILD，
        //       BGS_MENU 判据就失效了，而建造界面里那个同名背包控件仍可见 ⇒ 面板继续挂着。
        //     · 太空电梯/里程碑界面里面板常驻关不掉 —— 日志实锤
        //       `[inventory-probe] OPEN hit=Widget_Inventory_C size=414x1794`（被拉长的背包）。
        //     · 开背包再关也常驻 —— 日志实锤 `OPEN hit=Widget_Inventory_C size=0x0`：
        //       **UWidget::IsVisible() 只读 Visibility 属性，不代表它真在屏幕上布局过**，
        //       未布局/未挂载的实例同样返回 true ⇒ 判据被这些"幽灵实例"长期点亮。
        //     （上述 [inventory-probe] 系列日志是当时的排障探针，收尾时已移除；
        //       这两条尺寸是结论性证据，留作判据依据。）
        //   ⇒ 本节三条修法，缺一不可：
        //     ① 建造枪只要不是 BGS_NONE（菜单/建造/拆除/喷漆任一态），一律判「背包没开」。
        //        玩家要取货必须先退出建造、开背包，建造时挂取货面板没有意义。
        //     ② 几何尺寸为 0（未布局）的实例**直接跳过** —— 它没在屏幕上，不能作为"开着"的证据。
        //     ③ 必须是**竖长条**（高/宽 ∈ [1.15, 3.2]）：真背包实测 305×620 = 2.03；
        //        太空电梯里那个 414×1794 = 4.33（超上限 3.2）会被排除；
        //        列表项 336×63 = 0.19、方格 76×76 = 1.0 同样排除。
        //        ★下限从 1.3 放宽到 1.15 的原因见下方循环内注释（1.3 会把真背包也挡掉）。
        //   ★为什么用"比例"而不是"绝对高度上限"：比例不受 UI 缩放/分辨率影响，
        //     而侧栏与内容面板的宽高比天然分离（2.0 vs 4.3 / 0.2），这是最稳的分离特征。
        if (APlayerController* PC = World->GetFirstPlayerController())
        {
            if (AFGCharacterPlayer* Char = Cast<AFGCharacterPlayer>(PC->GetPawn()))
            {
                if (AFGBuildGun* BuildGun = Char->GetBuildGun())
                {
                    if (!BuildGun->IsInState(EBuildGunState::BGS_NONE))
                    {
                        Diag(TEXT("buildgun-not-none"), TEXT(""));
                        CachedOpen = false;
                        LastProbe = static_cast<double>(World->GetTimeSeconds());
                        if (bOutFresh)
                        {
                            *bOutFresh = true;
                        }
                        return false;
                    }
                }
            }
        }

        // 「大界面拦阻」见 WT_IsBlockedByBigUI（第二十六次修已抽成独立判据，行为完全一致）。
        {
            FString StackNames;
            if (WT_IsBlockedByBigUI(World, &StackNames))
            {
                Diag(TEXT("interact-stack-blocked"), StackNames);
                CachedOpen = false;
                LastProbe = static_cast<double>(World->GetTimeSeconds());
                if (bOutFresh)
                {
                    *bOutFresh = true;
                }
                return false;
            }
        }

        // ★★★2026-11-05【第二十六次修】身份优先 —— 钉住了就直接问它本人，不再猜形状。
        //   位置：必须放在「建造枪 / 大界面」两条硬拦阻**之后**（那两条是"即便背包控件还
        //   IsVisible() 也算没开"的规则，身份判据不能把它们绕过去），又放在 0.1s 节流**之前**
        //   （问一个指针 + 读一次几何，代价可以忽略，没必要缓存）。
        const double NowPin = static_cast<double>(World->GetTimeSeconds());
        if (UUserWidget* Pinned = G_PinnedSidebar.Get())
        {
            const FVector2f PSz = Pinned->GetCachedGeometry().GetLocalSize();
            const bool bPinnedOpen = Pinned->IsVisible() && PSz.X >= 2.f && PSz.Y >= 2.f;
            // ★两个条件任一成立就直接按身份出结论：
            //   · 它说开着 ⇒ 就是开着；· 已经过了改钉窗口 ⇒ 它说关就是关。
            //   ★这里**不能**先写 LastProbe/CachedOpen 再往下走：下面的 0.1s 节流会拿着
            //     刚写的缓存直接把函数顶回去，改钉枚举就永远跑不到（写成"总是返回关"）。
            if (bPinnedOpen || NowPin > G_PinDeadline)
            {
                CachedOpen = bPinnedOpen;
                LastProbe = NowPin;
                if (bOutFresh)
                {
                    *bOutFresh = true;
                }
                if (!bPinnedOpen)
                {
                    Diag(TEXT("pinned-hidden"),
                         FString::Printf(TEXT("%s(%.0fx%.0f)"), *Pinned->GetClass()->GetName(), PSz.X, PSz.Y));
                }
                return bPinnedOpen;
            }
            // 走到这里 = 钉住的对象说关了、但改钉窗口还开着 ⇒ 落到下面用形状再确认一次，
            //   以便把身份更新到"原版新建的实例"上（见 WT_ArmPinSidebar 注释）。
            Diag(TEXT("pinned-hidden-recheck"),
                 FString::Printf(TEXT("%s(%.0fx%.0f)"), *Pinned->GetClass()->GetName(), PSz.X, PSz.Y));
        }

        const double Now = static_cast<double>(World->GetTimeSeconds());
        if (Now - LastProbe < 0.1 && LastProbe > 0.0)
        {
            return CachedOpen;   // ★陈旧缓存：*bOutFresh 保持 false
        }
        LastProbe = Now;
        if (bOutFresh)
        {
            *bOutFresh = true;   // ★这一拍是真的枚举过了
        }

        // ★★★2026-11-05【第二十六次修】形状判据降级为「发现身份」——
        //   真正的判定逻辑与阈值全部收进 WT_FindSidebarWidget（唯一实现，见其注释）。
        //   这里只剩三件事：① 拿到最靠左的那块侧栏；② 在改钉窗口内把身份钉到它上面；
        //   ③ 判 false 时把探针明细打出去。
        FString Cand;
        UUserWidget* Hit = WT_FindSidebarWidget(World, &Cand);
        const bool bOpen = (Hit != nullptr);
        if (bOpen)
        {
            // 窗口期内（委托刚报 open=1 的 1.5 秒）才允许钉/改钉，见 WT_ArmPinSidebar 注释。
            if (Now <= G_PinDeadline)
            {
                UUserWidget* const Prev = G_PinnedSidebar.Get();
                if (Prev != Hit)
                {
                    G_PinnedSidebar = Hit;
                    const FVector2f HSz = Hit->GetCachedGeometry().GetLocalSize();
                    WT_WARNF(TEXT("[inventory-probe] pinned sidebar: %s size=%.0fx%.0f (%s)"),
                             *Hit->GetClass()->GetName(), HSz.X, HSz.Y,
                             Prev ? TEXT("retarget") : TEXT("first"));
                }
            }
        }
        else
        {
            Diag(TEXT("no-shape-match"), Cand);
        }
        CachedOpen = bOpen;
        return bOpen;
    }
}   // ← 匿名命名空间闭合（★勿删：曾误删导致整文件多一层括号）

// 世界注册入口：由 GameWorldModule 在 POST_INITIALIZATION 阶段调 Remote_FactoryWarehousePanel_Init 转进来。
// ★调用时机很早，PlayerController 常常还不存在，所以本函数的核心是「等到控制器再建」：
//   ① 先试一次；② 不行就挂 0.1s 重试 ticker（上限 60s）；
//   ③ 成功即 RegisterToggleListener（绑委托 + 起每帧 ticker）并自我注销。
// ⚠ 重试上限不能省：主菜单挂机（没进存档）时控制器一直不来，无限重试会每帧空转。
// ⚠ 换世界/读档会重新调本函数；GPanel 若已有效则只补注册监听（bRegistered 幂等守卫），不会建第二个面板。
void URemote_FactoryWarehousePanel::RegisterWorld(UWorld* World)
{
    WT_WARNF(TEXT("[warehouse-panel] RegisterWorld: creating left-side container list panel. world=%s"),
             World ? TEXT("ok") : TEXT("null"));
    // ★2026-11-05【第二十六次修】换世界/读档：上一轮钉住的"背包控件身份"已经无效，
    //   不清掉会让身份判据永远返回"关" ⇒ 面板永远不出现（正是要根治的那个症状）。
    WT_ResetPinnedSidebar();
    EnsurePanelCreated(World);
    if (GPanel.IsValid())
    {
        GPanel->RegisterToggleListener();
        WT_WARNF(TEXT("[warehouse-panel] RegisterWorld: panel ready immediately, listener registered."));
        return;
    }

    // ★2026-11-04 兜底第二条腿：与调用时机无关的自愈 ticker（0.25s 一次，建出来即注销）。
    //   放在这里而不是 EnsurePanelCreated 里，是为了让"世界注册"这件事显式声明它需要自愈。
    InstallSelfHealTicker(World);

    // ★2026-11-04 修「面板时有时无」：本函数在世界模块初始化阶段调用，那时玩家控制器
    //   常常还没创建 ⇒ EnsurePanelCreated 静默失败 ⇒ GPanel 永远为空 ⇒ 面板再也不出现。
    //   这里挂一个 0.1s 重试 ticker：一旦控制器可用就创建并注册，成功即自我注销。
    //
    // ★2026-11-04 第二次修（用户报"进游戏啥也没有"、日志里连本行都没出现）：旧版 lambda 裸捕获
    //   `World` 指针。读档/切换宇宙后旧 World 被销毁而 lambda 仍持有它 ⇒ 悬空指针解引用，最坏的
    //   表现就是静默崩溃/流程中断且无日志（Shipping 下崩溃也不一定留日志）。改捕获
    //   TWeakObjectPtr<UWorld>，并且**每次都打一行重试日志**（前 5 次，之后每 50 次一行），
    //   这样下次"没面板"时能立刻看出是 ticker 没跑、还是跑了但创建一直失败。
    WT_WARNF(TEXT("[warehouse-panel] RegisterWorld: no player controller yet, retrying until ready."));
    TSharedRef<int32> Tries = MakeShared<int32>(0);
    TWeakObjectPtr<UWorld> WeakWorld(World);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, Tries](float) -> bool
    {
        UWorld* W = WeakWorld.Get();
        if (!W)
        {
            WT_WARNF(TEXT("[warehouse-panel] deferred create: world gone after %d tries, stopping."), *Tries);
            return false;   // 世界没了，停止重试
        }
        if (GPanel.IsValid())
        {
            return false;   // 已由别处（自愈 ticker）创建
        }
        ++(*Tries);
        if (*Tries > 600)
        {
            WT_WARNF(TEXT("[warehouse-panel] deferred create: gave up after 60s (no player controller)."));
            return false;
        }
        EnsurePanelCreated(W);
        if (GPanel.IsValid())
        {
            // ★走公开静态入口（自由 lambda 无权调 private 的 RegisterToggleListener）。
            URemote_FactoryWarehousePanel::RegisterToggleListenerForCurrentPanel();
            WT_WARNF(TEXT("[warehouse-panel] deferred create: panel + listener ready (try %d)."), *Tries);
            return false;   // 成功，注销重试
        }
        // 前 5 次 + 之后每 50 次打一行：能看出"ticker 在跑但控制器一直没来 / 创建一直失败"。
        if (*Tries <= 5 || (*Tries % 50) == 0)
        {
            WT_WARNF(TEXT("[warehouse-panel] deferred create: retry %d, pc=%s."), *Tries,
                     W->GetFirstPlayerController() ? TEXT("ok") : TEXT("null"));
        }
        return true;        // 继续等控制器
    }), 0.1f);
}

void URemote_FactoryWarehousePanel::RegisterToggleListenerForCurrentPanel()
{
    if (GPanel.IsValid())
    {
        GPanel->RegisterToggleListener();
    }
}

// 注册监听 = 两件事：① 绑 AFGPlayerController::OnToggleInventory（立即响应）② 起每帧 ticker（状态机主驱动）。
// ★委托只是加速通道，不是真相：它会静默失效（见 WT_IsInventoryWidgetOpen 注释），真相在 TickRefresh 自观测。
// ★bRegistered 是「委托已绑」的守卫，但 ticker 不受它保护 —— 本函数整体只在无面板→有面板时调一次，
//   重复调用会多挂 ticker（多个 ticker 同时推动画会让动画速度翻倍），故不要在高频路径里调它。
// ★间隔必须 0（每帧）：0.5s 一帧喂不动 0.22s 的滑入动画（用户反馈"不够平滑"就是这个）。
void URemote_FactoryWarehousePanel::RegisterToggleListener()
{
    // ★2026-11-04 拆成两个独立守卫（旧版只用一个 bRegistered 挡两件事，会踩两个坑）：
    //   坑A：若 GetFirstPlayerController 此刻为 null（在自愈 ticker 里可能出现），bRegistered 保持 false，
    //        但 ticker 已经挂了 ⇒ 下次再调本函数又会挂一个 ticker ⇒ 多个 ticker 同时推动画，动画翻倍速。
    //   坑B：委托绑定失败后没有重试机会 ⇒ 只能永远靠 TickRefresh 自观测兜底（能工作，但少了"立即响应"）。
    //   故：ticker 用 bTickerStarted 单独守；委托每帧检查 FPC 有效性，绑上就打一次日志。
    if (!bTickerStarted)
    {
        // ★间隔必须 0（每帧）：滑入动画必须逐帧更新才平滑。旧版 0.5s 一次 ⇒ 0.18s 动画只插值一个点 = 跳一下。
        //   为免每帧做重活，TickRefresh 内部把「布局/内容刷新」按 0.5s 节流，每帧只做动画 + 轻量状态观测。
        bTickerStarted = true;
        FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateUObject(this, &URemote_FactoryWarehousePanel::TickRefresh), 0.f);
        WT_WARNF(TEXT("[warehouse-panel] per-frame ticker started."));
    }

    if (bRegistered)
    {
        return;
    }
    if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
    {
        if (AFGPlayerController* FPC = Cast<AFGPlayerController>(PC))
        {
            // 动态 multicast 委托：必须用 AddDynamic + UFUNCTION（见头文件 OnInventoryToggled 声明）。
            FPC->OnToggleInventory.AddDynamic(this, &URemote_FactoryWarehousePanel::OnInventoryToggled);
            bRegistered = true;
            WT_WARNF(TEXT("[warehouse-panel] OnToggleInventory bound."));
        }
        else
        {
            WT_WARNF(TEXT("[warehouse-panel] controller is not AFGPlayerController (%s); delegate not bound, relying on self-probe."),
                     PC->GetClass() ? *PC->GetClass()->GetName() : TEXT("?"));
        }
    }
}

// 每帧状态机 —— 面板何时出现/消失全在这里决定，是本功能最核心也最容易改坏的一段。
// 每帧的活分三层，代价从低到高：
//   ① 轻：判暂停、推动画（TickSlide，纯数学）。
//   ② 中：观测背包开/关（WT_IsInventoryWidgetOpen 自带 0.1s 节流，不会每帧扫 widget）。
//   ③ 重：LayoutLeftOfInventory + RefreshContents（枚举全部 widget / 重建行）—— 按 0.5s 节流。
//
// ★委托与观测的关系（2026-11-05 第二十六次修定案，别再按老理解改）：
//   **委托是权威，观测是兜底** —— 但委托历史上确实出现过"开关若干次后静默失效"
//   （日志里关背包后再无 OnInventoryToggled 记录）。所以现在是"两路互补"：
//     · 收到过委托 ⇒ 以委托为准（游戏自己发的事件，不是推断）；
//     · 从没收到过 ⇒ 用观测；
//     · 两者持续打架 ⇒ 观测在**可信方向**上赢（详见 ② 段注释），并打 `delegate stale` 自愈日志。
//   ★为什么不能只信观测：观测靠"屏幕上有没有一块竖长侧栏"，UI 缩放/原版改版会让它永远失效
//     （面板永远不出现），也会被别的面板间歇误命中（面板自己闪）。这正是第四十次修要根治的。
//
// ★为什么暂停期间要冻结：ESC 打开暂停菜单时原版背包 widget 仍在但不可见 ⇒ 旧逻辑判"背包关了"→ HidePanel；
//   从暂停菜单返回后背包 widget 何时恢复可见并不稳定，于是面板再也挂不回来（用户原话"按了一下 ESC 就没了"）。
//   冻结后：暂停期间面板原样留着，回游戏时自然还在。
//
// ★为什么判"关"要连续两次：可见性可能单帧抖动（界面切换中间态），一次就收会让面板"闪一下就没了"。
//   反过来判"开"是立即补挂（不等），因为那是无害的、玩家期待的。
bool URemote_FactoryWarehousePanel::TickRefresh(float DeltaSeconds)
{
    // ── ① 暂停菜单：冻结，什么都不改（只把没播完的动画推完，免得回游戏时面板卡在半路）。
    if (WT_IsPauseMenuOpen(GetWorld()))
    {
        bPauseFrozen = true;
        if (bAttached)
        {
            TickSlide(DeltaSeconds);
        }
        return true;
    }
    if (bPauseFrozen)
    {
        // 刚退出暂停菜单：先清冻结标记，**这一帧照常采样**。
        //   注意别在这里"沿用冻结前状态再跳一帧"——背包真被关掉时会导致面板多留一次采样周期，
        //   反正判关有 CloseTicks 防抖兜着，直接采样更简单可靠。
        bPauseFrozen = false;
        WT_WARNF(TEXT("[warehouse-panel] pause menu closed, resume probing."));
    }

    // ── ② 采样背包开关。这一段是整套状态机的**唯一输入**。
    //
    // ★★★2026-11-05【第二十六次修「面板彻底不出现」的根治】——「委托优先、轮询兜底」
    //   病根：`AFGPlayerController::OnToggleInventory`（FGPlayerController.h:29/299）是游戏
    //   自己发的开关事件，本该是权威；但旧代码在回调里写进 bInventoryOpen 之后，
    //   **每一帧又被 WT_IsInventoryWidgetOpen() 的"猜形状"结果覆盖掉**。于是形状判据一旦失效
    //   （UI 缩放变化 / 原版改版 / 被别的大面板干扰），面板就**永远不出现**，日志里连一条
    //   ShowPanel 都没有 —— 与崩溃、遮挡的表现完全一样，极难分辨。
    //   ⇒ 三个来源排优先级，顺序不能动：
    //     ②-a 硬拦阻（独立执行）：大界面（太空电梯 / HUB / 里程碑 / 交易站）开着 ⇒ 一律当
    //          "背包没开"。**必须在这里单独调一次**，不能只靠轮询的返回值：下面 ②-b 一旦
    //          跳过轮询，这条硬规则就没人执行了，面板会盖到 HUB 界面上。
    //          （建造枪那条硬规则仍在轮询里，因为"背包没开"和"轮询说没开"效果相同，
    //            不存在被跳过的问题。）
    //     ②-b 权威来源：收到过委托 ⇒ 以委托为准；从没收到过（回调绑晚了 / 控制器换了）
    //          ⇒ 用轮询。
    //     ②-c 自愈：委托与轮询**持续不一致 ≥3 秒** ⇒ 采纳轮询并打日志。
    //          防的是委托静默失效（历史实测发生过）之后状态永远卡在旧值上；
    //          3 秒远大于"我们重建 UI 导致轮询假 false"的窗口（实测 ≤0.4s），不会来回抖。
    UWorld* const SWorld = GetWorld();
    const bool bPollOpen = WT_IsInventoryWidgetOpen(SWorld);

    // ②-a 硬拦阻（每一拍都执行，见上）
    const bool bBigUIBlocked = WT_IsBlockedByBigUI(SWorld, nullptr);

    // ②-c 自愈（必须排在 ②-b 取结论**之前**，采纳的那一拍就要立刻生效）
    //   ★"不一致"只在**可信方向**才累计 —— 这一条是整段的关键，别当成可有可无的优化：
    //     · 轮询说**开着**：一定真有一块竖长侧栏摆在屏幕上（形状/身份两个判据都过），
    //       可信 ⇒ 它和委托打架，是委托陈旧。
    //     · 轮询说**关着**：可能只是"钉不上 / 形状没命中 / 控件这一拍还没布局"
    //       —— 恰恰是我们要兜住的那种失效，**不能拿它去否定委托**。
    //       除非身份已经钉住（那时它说关就是真关），否则这一路不动委托。
    //   ★阈值：poll 说开着 3s（要压过我们重建 UI 造成的假 false 窗口，实测 ≤0.4s）；
    //     poll 说关着且已钉住 2s（有身份时它更可信，可以快一点）。
    if (!bBigUIBlocked && bDelegateEverFired && bPollOpen != bDelegateOpen)
    {
        const bool bPollTrustworthy = bPollOpen || G_PinnedSidebar.IsValid();
        if (bPollTrustworthy)
        {
            PollDisagreeAcc += DeltaSeconds;
            const float AdoptSec = bPollOpen ? 3.f : 2.f;
            if (PollDisagreeAcc >= AdoptSec)
            {
                WT_WARNF(TEXT("[warehouse-panel] delegate stale: poll=%d wins over delegate=%d after %.1fs, adopting poll."),
                         bPollOpen ? 1 : 0, bDelegateOpen ? 1 : 0, PollDisagreeAcc);
                bDelegateOpen = bPollOpen;
                PollDisagreeAcc = 0.f;
                // ★采纳"开着"时必须同时钉身份，否则一旦是误命中就再也纠正不回来
                //   （详见 WT_PinSidebarNow 注释）。
                if (bPollOpen)
                {
                    WT_PinSidebarNow(SWorld);
                }
            }
        }
        else
        {
            PollDisagreeAcc = 0.f;
        }
    }
    else
    {
        PollDisagreeAcc = 0.f;
    }

    // ②-b 权威来源
    bInventoryOpen = (bDelegateEverFired ? bDelegateOpen : bPollOpen) && !bBigUIBlocked;
    if (!bInventoryOpen)
    {
        // 背包真的关了 ⇒ 清掉「用户点过 ×」标记：下次开背包面板要重新出现。
        bUserClosed = false;
    }

    // ── ③ 按状态决定显隐（显隐唯一入口 Show/HidePanel，判据用 bAttached 不用 IsInViewport）。
    //
    // ★★★2026-11-05【第十三次修】先做"背包开着"的兜底复位。
    //   为什么要单独一段、而不是并在下面的 if 里：下面两个分支是**互斥**的
    //   （`bInventoryOpen && !bAttached` / `!bInventoryOpen && bAttached`），
    //   于是存在一种**两个都不进**的组合：**背包开着 且 面板已挂**（bInventoryOpen=1 && bAttached=1）。
    //   这正是"一切正常"的常态 —— 但如果上一拍因为误判把 bClosing 置了位，
    //   这一拍进了"两个都不进"的空档，就**没有任何人负责把 bClosing 清回去** ⇒ ④ 段永远冻结
    //   ⇒ 面板卡死不动（位置不跟随、内容不刷新）。这是把 bClosing 加进判关分支后必然出现的死锁，
    //   必须靠这一段显式复位来解。
    if (bInventoryOpen)
    {
        CloseTicks = 0;      // 背包开着就不存在"判关进行中"
        bClosing = false;    // 解除关闭冻结（幂等，常态下本来就是 false）
        CloseWaitAcc = 0.f;  // ★判关计时清零 —— 背包又可见了 ⇒ 之前那次"不可见"只是噪声
        // ★★★2026-11-05【第十八次修 v2】这里**不再**用 `if (bProbeFresh) bWantClose = false;` 去清请求。
        //   那条是第十四次修写的，本意是"轮询说背包还开着 ⇒ 委托那条关闭请求是误报，清掉"。
        //   但关闭瞬间原版背包要播 0.38s 滑出动画，这段时间它**确实还 IsVisible()**——
        //   那是真值，不是误报！拿它去清 bWantClose，等于把最可靠的关闭信号当场丢掉，
        //   然后只能等 CloseWaitAcc 从"背包彻底不可见"起再累计 0.6s
        //   ⇒ 总延迟约 1 秒（仿真复现：关背包在 3.0s，HidePanel 落在 3.984s）。
        // ★★★2026-11-05【第二十一次修「里程碑界面关掉后 3~5 秒面板才消失」】
        //   第十八次修在这里放了一个「bWantClose 超过 1.5 秒就作废」的超时。它当时是为了
        //   兜住"open=0 误发后永远不关"，但那 1.5 秒的假设**建立在"背包很快就不可见"之上**，
        //   而 HUB / 太空电梯里程碑界面不满足这个假设：
        //     玩家点关闭 → 委托发 open=0（bWantClose=1）
        //     → 原版 HUB 要播 3~4 秒的淡出/收场，这期间背包 widget **确实还 IsVisible()**
        //     → 1.5 秒一到 bWantClose 被作废
        //     → 只剩轮询兜底：等背包真正不可见，再累计 0.6s
        //     ⇒ 总延迟 3~5 秒，正是玩家报的那个数字。
        //   ★修法：**不再超时作废**，委托说了算。
        //     委托是原版主动通知的开关事件，日志实测 21 次开关 1:1 严格配对、0 次误发，
        //     它比任何"背包可见性"推断都可信。
        //   ★安全性：bWantClose 现在只在三个地方被清 ——
        //     · OnInventoryToggled(open=1)：玩家真又打开了
        //     · HidePanel()：已经收掉了
        //     · ShowPanel()：即将显示（防上一轮残留把它当场收掉）
        //     所以它不会无限期挂着，也不存在"开着却被立刻收掉"的死循环。
    }

    // ★★★2026-11-07 智能仓储开关闸门：功能关着且面板还挂着 ⇒ 立刻收掉。
    //   覆盖"开着背包时把开关切到关"的情形——ShowPanel 已被同闸门拦截不会重新挂上，这里只负责主动隐藏。
    if (bAttached && !Remote_FactorySmartWarehouse::IsSmartWarehouseEnabled())
    {
        HidePanel();
    }
    else if (bInventoryOpen && !bUserClosed && !bAttached)
    {
        // ★★★2026-11-05【第十八次修「关闭比原版慢约 1 秒」】
        //   这一行原来直接 ShowPanel()，是那 1 秒延迟的元凶：
        //   HidePanel 刚把面板藏起来（bAttached=false），紧接着的下一拍轮询却发现
        //   原版背包**还在播 0.38s 的滑出动画、IsVisible() 仍为真** ⇒ 判定"背包开着"
        //   ⇒ 立刻把面板又显示出来 ⇒ 玩家看到"关了又冒出来"，反复几拍后才真正消失。
        //   ★为什么以前没这问题：以前每次 HidePanel 都 RemoveFromParent，日志里有
        //     `AddToPlayerScreen done=...` 一行行刷出来；现在永不摘除、只切可见性，
        //     重新显示**一条日志都不留**，所以只能在关闭时刻上做冷却。
        //   ⇒ 隐藏后 0.6s 内的"背包可见"一律当成滑出残影，不予重开。
        //     真重开走 OnInventoryToggled(open=1) 委托，那条路不受这里限制。
        const float NowSec = GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.f;
        const float SinceHide = NowSec - LastHideAt;
        if (SinceHide >= 0.6f)
        {
            CloseTicks = 0;
            bClosing = false;
            ShowPanel();
        }
    }
    // ★★★2026-11-05【第十八次修 v2】条件从 `!bInventoryOpen && bAttached` 扩成
    //   `bAttached && (!bInventoryOpen || bWantClose)` —— **委托优先**。
    //   为什么要给 bWantClose 开这条直达通道：委托说"背包关了"的那一刻，原版背包正开始播
    //   0.38s 的滑出动画，`IsVisible()` 还是 true ⇒ 旧条件进不来 ⇒ 得等动画播完、
    //   再等 CloseWaitAcc 累计 0.6s ⇒ 玩家体感"慢了一秒"。
    //   而委托是原版**主动通知**的事件，比任何轮询都准，它说关了就该立刻收。
    //   ★误发（ESC/暂停菜单）的代价已被两道兜住：① bWantClose 1.5s 超时作废；
    //     ② HidePanel 后有 0.6s 关闭冷却，即便误收也不会立刻弹回来
    //     （见上一段的 SinceHide 判断）。
    else if (bAttached && (!bInventoryOpen || bWantClose))
    {
        // ★★2026-11-05【第十一次修「关闭的时候会闪一下」】
        //   病根**不在透明度，在位置**。关闭瞬间的时间线：
        //     t0     玩家按 E/ESC ⇒ 原版背包开始**下滑离场动画**，但它此刻仍 IsVisible()
        //     t0~tX  WT_IsInventoryWidgetOpen 有 0.1s 节流 + 判关要连续多拍 ⇒ 本面板仍 bAttached
        //            ⇒ 每帧照跑 LayoutLeftOfInventory()，而它参考的正是那个**正在下滑**的背包
        //            ⇒ 面板跟着一起往下掉（用户看到的"闪一下"就是这个位移）
        //     tX     判关成立 ⇒ HidePanel，但玩家已经看见它动过了
        //   ★更糟的是：背包滑出后可能被判不可见 ⇒ Layout 找不到参照面板 ⇒ 走"纵向居中"回退
        //     ⇒ 面板从左边中间**跳到屏幕正中**再消失。这才是那一下最刺眼的闪。
        //   ★修法（两道，都对"不让面板跟着离场动画跑"）：
        //     ① 一旦本拍采样到"背包没开"（哪怕还没达到确认拍数），立刻置 bClosing，
        //        让 ④ 段**跳过布局**，位置冻在当前值 —— 面板停在原地等消失，不跟随、不跳居中。
        //     ② bClosing 期间也不做内容刷新（行重建会让列表短暂空白，也是"闪"的一种）。
        //
        // ★★★2026-11-05【第十三次修「轮询误判」—— 在上面两道之上再加一道前置门控】
        //   本分支的输入 `!bInventoryOpen` **不完全可信**：它来自 0.1s 一次的
        //   GetAllWidgetsOfClass + IsVisible() 枚举，而**我们自己重建行/挂载/摘除的那几帧**
        //   会让 Slate 父链短暂处于未布局状态 ⇒ 原版背包的 IsVisible() 返回 false。
        //   于是这个分支被自己的 UI 操作触发 ⇒ 冻结 + 收面板 ⇒ 背包恢复可见 ⇒ 又判"开了" ⇒
        //   再挂 ⇒ 自激振荡（日志 open=0:open=1 恒为 2:1 就是这个节奏）。
        //
        //   ⇒ 判据改成"委托事件优先 + 轮询长时间兜底"，两路互补：
        //
        //   【第一路：委托事件（可信、立即）】
        //     AFGPlayerController::OnToggleInventory 是原版**主动通知**的开关事件，日志证明它确实在触发
        //     （`OnInventoryToggled: open=0` 有记录）。它唯一的已知问题是"开关若干次后会静默失效"，
        //     所以不能**只**靠它 —— 但它是"玩家真的关了背包"最干净、最实时的信号。
        //     ⇒ OnInventoryToggled(open=0) 置 bWantClose，本段看到就直接收，零延迟。
        //
        //   【第二路：轮询（兜底，必须慢）】
        //     WT_IsInventoryWidgetOpen 的 IsVisible 瞬时值会被**我们自己的 UI 重建**污染
        //     （Slate 重建子树那几帧父链未布局 ⇒ 假 false）。实测这种假象可持续到 0.4s
        //     （自检仿真：噪声 0.4s 时 0.45s 阈值仍会误判一次）。
        //     ⇒ 兜底阈值必须显著长于可能的噪声窗口，取 1.0s。
        //     代价：委托失效时要等 1.0s 才收面板 —— 但那种情况本来就不精确，
        //     而且 bClosing 冻结让面板静止等待，不是"往下掉"。
        //
        //   ★为什么不再用 CloseTicks 拍数：拍数依赖"每拍都采到"，而采样有 0.1s 节流，
        //     `>=2` 与 `>=3` 在时间上只差 0.1s —— 恰好落在噪声量级里，既挡不住也说不清。
        //     换成"秒"之后，阈值与噪声窗口是**同一个量纲**，可以算清楚。
        CloseWaitAcc += DeltaSeconds;
        // ★2026-11-05【第十八次修】1.0f → 0.6f。
        //   0.6 仍显著大于实测的轮询噪声窗口（0.4s），不会因自己重建 UI 就误收；
        //   而委托路径失效时的最坏延迟从"1.0s + 背包滑出 0.38s ≈ 1.4s"降到约 1.0s。
        //   ★误收的代价已被第十八次修的关闭冷却兜住：万一真误收，0.6s 后会自动重新显示。
        constexpr float PollConfirmSec = 0.6f;
        if (bWantClose || CloseWaitAcc >= PollConfirmSec)
        {
            CloseWaitAcc = 0.f;
            CloseTicks = 0;
            HidePanel();
        }
        else
        {
            // 还在确认期内：冻结位置，停住等结果 —— 不跟随背包的下滑离场动画。
            bClosing = true;
        }
    }

    // ── ④ 已挂载时：动画逐帧 + 布局/内容 0.5s 节流。
    //   为什么节流：LayoutLeftOfInventory 要枚举全部 UserWidget 算坐标，RefreshContents 要重建行，
    //   都是重活；但动画必须逐帧，否则又回到"跳一下"。
    if (bAttached)
    {
        // ★★2026-11-05【第十一次修「关闭的时候会闪一下」】关闭流程中：位置冻结，什么都不改。
        //   判据 bClosing 由 ③ 段在"采样到背包没开"的那一拍立刻置位（不等 2 拍确认）。
        //   为什么这里要整段跳过：
        //     · LayoutLeftOfInventory：它的唯一目的是**跟随原版背包位置**。背包已在退场，
        //       跟随对象没了 ⇒ 读到的要么是"正在下滑的中间值"（面板跟着掉），
        //       要么干脆判不可见 ⇒ 走回退分支**跳到屏幕正中**（那一下最刺眼）。
        //       两种都是纯劣化，没有一种是对的 ⇒ 干脆不跑，停在上一次的正确位置。
        //     · RefreshContents：重建行会让列表瞬间空白，本身也是一种"闪"。
        //     · TickSlide：此时若有残留动画也一并冻住，避免位移叠加。
        //   ★代价：关闭会晚 1~2 拍（约 0.1~0.2s）。但面板是"停在原地等消失"，
        //     玩家感知不到延迟；而原来那 1~2 拍正是"看到它掉下去"的时长。
        //   ★实现上用 `if (!bClosing)` 包住整段，**不用提前 return**：
        //     return 会把后面的 ⑤ 段（对照探针）一起跳过 —— 而"关闭时面板到底动了没"
        //     恰恰只能靠那段日志证明。提前 return 等于把最需要的证据掐掉（这是老坑了）。
        if (bClosing)
        {
            // 关闭流程：位置/内容/动画全部冻结，只留 ticker 跑（等 ③ 段凑满 2 拍收面板）。
        }
        else
        {

        // ★2026-11-04【第六次修·日志实测定案】
        //   实测证明：原版背包在打开的**头 0.22 秒**里自己从下往上浮了 49.2px
        //   （absY: 347.3 → 298.1，f23 后停住不动）。这是原版自己的运动。
        //   ⇒ 我们不需要自己算任何动画，只要在这 0.6 秒窗口里**每帧**重算布局，
        //     自然就跟着原版一起上浮。窗口过后恢复 0.5s 节流，避免每帧枚举 widget 掉帧。
        //
        //   ★为什么用 0.6s 而不是 0.22s：原版动画时长会随 UI 缩放/帧率变化，
        //     留 3 倍余量确保一定覆盖完整段；窗口过后归零，不会常驻开销。
        constexpr float FollowWindow = 0.6f;
        if (FollowT < FollowWindow)
        {
            FollowT += DeltaSeconds;
            LayoutLeftOfInventory();   // 每帧跟随原版背包的上浮
        }

        // ★2026-11-05【第九次修·安全网】几何已就绪但仍透明 ⇒ 强制拉回 1。
        //   为什么需要这道保险：ShowPanel 在"几何未就绪"分支里会把透明度压到 0 做遮挡，
        //   正常路径下由"下一次几何就绪的 ShowPanel"推回 1。但 ShowPanel 是幂等的，
        //   可能**再也不会被调用**（bAttached 已为真）⇒ 面板会卡在透明状态。
        //   ★这里判据用 bGeometryReady 而**不是**时间：几何好了就该看得见，没有例外。
        //   开销：每帧一次 float 比较，可忽略。
        //
        // ★2026-11-05【第十次修·必须同时清欠账】光把透明度拉回 1 还不够：
        //   bHoldHidden 是"这一次的 0 还没被合法撤销"的账本。若这里只改透明度不清账本，
        //   下一次 ShowPanel 走正常路径时账本仍是 true（状态不干净，后续判断失去意义）。
        //   ★顺序很重要：先清账本再改透明度，避免这一帧内出现"账本已清、还透明"的中间态。
        //   ★但**不能只看透明度**就清账本：面板可能本来就该半透明（未来若做淡入）。
        //     故判据是"账本欠着 且 几何已就绪" —— 账本欠着说明是我自己压的 0，我负责收。
        //     bGeometryReady 保证"收"发生在位置已经写对的那一帧之后，不会再露出 y=0。
        if (bHoldHidden && bGeometryReady)
        {
            bHoldHidden = false;
            if (GetRenderOpacity() < 0.99f)
            {
                SetRenderOpacity(1.f);
                WT_WARNF(TEXT("[warehouse-panel] opacity watchdog: held 0 released (geometry ready) -> restored to 1."));
            }
        }

        TickSlide(DeltaSeconds);
        // ★2026-11-05【第十二次修】布局目标的平滑跟随 —— 必须在 TickSlide **之后**：
        //   入场动画期间 Slot 的 Y 由 TickSlide 独占（布局期间 Y 已被 SlideLockedY 冻结，
        //   不会产生 bFollowing），两者不会同帧抢写；放在后面只是保证"入场优先"。
        TickFollow(DeltaSeconds);
        RefreshAccum += DeltaSeconds;
        if (RefreshAccum >= 0.5f)
        {
            RefreshAccum = 0.f;
            if (FollowT >= FollowWindow)   // 跟随窗口内已在每帧刷新，不必重复
            {
                LayoutLeftOfInventory();   // 重新对齐（位面面板开关、分辨率变化后自动跟随）
            }
            RefreshContents();            // 三类数量变化后重刷行（签名相同则不重建）
        }

        }   // ← if (!bClosing) 的收尾。★与上方 `else {` 成对，删一个必须同时删另一个。
    }

    // ── ⑤ ★2026-11-04【对照探针】把「原版物品栏」和「我们的面板」逐帧打进**同一行**。
    //
    //   为什么必须同一行：用户说"这个问题我有点不太好描述" —— 分开看两份日志、
    //   靠时间戳对齐，成本太高且容易看错。同一行里并排 + 直接给出 Δ，一眼就能判断：
    //     · inv 和 panel 的 y 都在动、差值恒定  → 完美跟随（我们做对了）
    //     · inv 动了 panel 没动                 → 跟随窗口没生效
    //     · panel 的 y 是 0 或 -1               → 面板没挂 / 几何没就绪
    //     · 差值几十上百                        → 布局参照物找错了
    //
    //   ★关键设计：**不管 bAttached 是否成立都输出**。
    //     "面板没出现"恰恰是最需要看到的情况，如果只在挂载后才打日志，
    //     就正好把要诊断的那个场景整个漏掉 —— 这是上一轮踩过的坑。
    //
    //   ★坐标口径：inv 用**绝对坐标**（GetAbsolutePosition），panel 用**画布局部坐标**
    //     （Slot 的 Position，因为面板锚点在画布原点 0,0）。两者原点不同，
    //     所以 panel 这边额外输出 originY，Δ 用 (panelY + originY) - invY 才是同一坐标系下的差。
    if (bCompareProbe && CompareProbeFrame < CompareProbeBudget)
    {
        ++CompareProbeFrame;

        // 原版物品栏：取绝对坐标与尺寸。找不到就如实打 -1，绝不假装有值。
        float InvAbsY = -1.f, InvH = -1.f, InvAbsX = -1.f, InvW = -1.f;
        int32 InvFound = 0;
        if (UWorld* PW = GetWorld())
        {
            TArray<UUserWidget*> AllP;
            UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PW, AllP, UUserWidget::StaticClass(), false);
            for (UUserWidget* Wdg : AllP)
            {
                if (!Wdg || !Wdg->GetClass() || Wdg == this) { continue; }
                const FString CN = Wdg->GetClass()->GetName();
                if (!CN.Contains(TEXT("Widget_Inventory")) || CN.Contains(TEXT("Slot"))) { continue; }
                if (!Wdg->IsVisible()) { continue; }
                const FGeometry G = Wdg->GetCachedGeometry();
                const FVector2f S = G.GetLocalSize();
                const FVector2f P = G.GetAbsolutePosition();
                InvAbsX = P.X; InvAbsY = P.Y; InvW = S.X; InvH = S.Y;
                InvFound = 1;
                break;
            }
        }

        // 我们面板：读**实际写进 Slot 的值**（不是 LayoutY 这个"算出来的意图"）。
        //   ★为什么读 Slot 不读 LayoutY：LayoutY 只说明"我们算了多少"，
        //     而玩家看到的是"Slot 里真正的值"。两者不一致时，问题就在"算了但没写进去"。
        //   ★变量名必须带 Slot 前缀：类里已有成员 PanelH / PanelW，同名局部变量会触发
        //     MSVC C4458（declaration hides class member），而本工程开了 -WarningsAsErrors ⇒ 直接编译失败。
        float SlotY = -1.f, SlotH = -1.f, SlotX = -1.f, SlotW = -1.f;
        int32 PanelFound = 0;
        if (UCanvasPanel* RCP = Cast<UCanvasPanel>(WidgetTree ? WidgetTree->RootWidget : nullptr))
        {
            if (RCP->GetSlots().Num() > 0)
            {
                if (UCanvasPanelSlot* CSP = Cast<UCanvasPanelSlot>(RCP->GetSlots()[0]))
                {
                    const FVector2D Pos = CSP->GetPosition();
                    const FVector2D Sz  = CSP->GetSize();
                    SlotX = Pos.X; SlotY = Pos.Y; SlotW = Sz.X; SlotH = Sz.Y;
                    PanelFound = 1;
                }
            }
        }

        // Δ = 面板顶边(绝对) - 背包顶边(绝对)。0 = 顶边齐平；负 = 面板更靠上。
        //
        // ★画布原点必须在这里**独立再取一次**，不能用成员 CanvasOriginY。
        //   原因：CanvasOriginY 只在 LayoutLeftOfInventory 里更新，而面板没挂载时那个函数根本不跑
        //   ⇒ 它会一直是 0 ⇒ Δ 算出来是错的（把"没有原点"当成"原点在 0"）。
        //   探针的价值就在于**任何状态下都可信**，所以它自带一次取几何，不依赖别人的执行顺序。
        float CanvasOriginYNow = 0.f;
        if (UCanvasPanel* RCO = Cast<UCanvasPanel>(WidgetTree ? WidgetTree->RootWidget : nullptr))
        {
            const FVector2f O = RCO->GetCachedGeometry().GetAbsolutePosition();
            CanvasOriginYNow = O.Y;
        }

        // ★Δ 只在**两边都有有效数据**时才算。
        //   反例（差点写成 bug）：面板没挂载时 Slot 里是 0/0，若照样相减会算出 dy≈-347，
        //   看起来像"面板跑到屏幕顶上去了"，其实只是"面板不存在"。
        //   差值是给人看的判据，绝不能给出一个看着合理、实则无意义的数字 ⇒ 无效时打 n/a。
        const bool bBothValid = (InvFound != 0) && (PanelFound != 0) && bAttached;
        const float Delta = bBothValid ? ((SlotY + CanvasOriginYNow) - InvAbsY) : 0.f;
        const FString DeltaStr = bBothValid
            ? FString::Printf(TEXT("%+.1f"), static_cast<double>(Delta))
            : FString(TEXT("n/a"));

        // ★变参提升：FVector2f/float 直接传 %f 在 MSVC 变参下是未定义行为，统一 static_cast<double>。
        //   ★dy 用 %s 传字符串：无效时显示 n/a，避免"没数据"被误读成"差 300 多像素"。
        WT_WARNF(TEXT("[cmp] f%-3d inv(vis=%d x=%.0f y=%.0f w=%.0f h=%.0f) | panel(att=%d x=%.0f y=%.0f w=%.0f h=%.0f op=%.2f) | dy=%s geo=%d"),
                 CompareProbeFrame,
                 InvFound,
                 static_cast<double>(InvAbsX), static_cast<double>(InvAbsY),
                 static_cast<double>(InvW),    static_cast<double>(InvH),
                 bAttached ? 1 : 0,
                 static_cast<double>(SlotX),   static_cast<double>(SlotY),
                 static_cast<double>(SlotW),   static_cast<double>(SlotH),
                 static_cast<double>(GetRenderOpacity()),
                 *DeltaStr,
                 bGeometryReady ? 1 : 0);
    }

    return true;   // 持续 tick（面板活着期间一直跑）
}

// 显示面板 = 唯一的"出现"入口，幂等（已挂载时只重排+重刷，不会重复 AddToViewport）。
// ★步骤顺序是硬约束，调换会出问题：
//   ① AddToViewport（仅首挂）② SetVisibility ③ Layout（算出真实目标 Y 写进 SlideTargetY）
//   ④ StartSlideIn（必须在 Layout 之后，否则动画从错误的 Y 开始）⑤ RefreshContents。
void URemote_FactoryWarehousePanel::ShowPanel()
{
    // ★★★2026-11-07 智能仓储开关闸门：功能关着时，整个面板都不该出现在背包页（连"未开启"提示都不显示）。
    //   旧逻辑：背包一开就 ShowPanel，关着时面板仍挂在视口里、显示"（智能仓储未开启）"提示，
    //   正是用户截图里红框所指的 bug。现改为：关着时确保隐藏并直接返回，任何想让它"出现"的调用
    //   （OnInventoryToggled 与 TickRefresh 两路）都无效。开着背包时把开关切到"开"也由 TickRefresh
    //   下一帧自动走到这里、正常挂载显示，无需额外联动。
    if (!Remote_FactorySmartWarehouse::IsSmartWarehouseEnabled())
    {
        if (bAttached)
        {
            HidePanel();
        }
        else
        {
            SetVisibility(ESlateVisibility::Hidden);
        }
        WT_WARNF(TEXT("[warehouse-panel] ShowPanel skipped: Smart Warehouse disabled (panel hidden)."));
        return;
    }
    // ★★★2026-11-05【第十四次修】判据从 `!bAttached` 改成 `!bEverAttached`。
    //   原因见 .h 里 bEverAttached 的注释：面板首次挂载后**永不 RemoveFromParent**，
    //   显隐只切可见性。所以"要不要执行 AddToPlayerScreen"这件事一辈子只该发生一次。
    //   旧写法 `if (!bAttached)` 会让每次重新打开都再 Add 一遍，而 HidePanel 每次都会 Remove，
    //   ⇒ 每开关一次就重建一次 Slate 子树 ⇒ 打断原版背包的蓝图动画状态机 ⇒ 0.73s 自激振荡。
    if (!bEverAttached)
    {
        // ★AddToViewport 走 UGameViewportSubsystem，默认槽是满屏（Anchors=(0,0,1,1)）。
        //   SetAnchorsInViewport / SetDesiredSizeInViewport / SetPositionInViewport 都会改写同一个
        //   Offsets，混用会把面板压成内容大小并停在左上角 ⇒ 这里【只挂载，不碰外层槽】，
        //   真正落点交给面板内部 RootCanvas 上的 RootBorder Slot（见 RebuildWidget / LayoutLeftOfInventory）。
        //
        // ★★★2026-11-05【第十三次修 —— 「随机出现 / 开多了直接不显示 / 连闪 2~3 次」的统一根因】
        //
        //   【日志铁证】19:54 那次会话：open=0 出现 12 次、open=1 出现 6 次，永远是 2:1；
        //   18:44（正常）只有 3:2。更关键的是这一串（同一秒内）：
        //     open=1 bAttached=0 → AddToViewport → open=0 bAttached=1 → open=1 bAttached=0 → AddToViewport …
        //   面板每 0.6 秒被"挂上→摘掉→再挂上"，而且每次 layout 都是 `canvasW=0 geo=0 y=0`。
        //
        //   【根因】`AddToViewport` 挂进的是 **Game 全局层**（引擎 UserWidget.h:338 注释原文
        //   "Adds it to the game's viewport"），而原版背包 / 位面面板 / 开拓者都挂在
        //   **Player 专属层**（同文件 :347 `AddToPlayerScreen` 注释 "in a section dedicated to the player"）。
        //   我们用 ZOrder=20 挂进了另一层且盖在全局层最上面 ⇒ 引发两个后果：
        //     ① 原版背包在 Player 层，被 Game 层的东西抢了焦点 ⇒ 它自己走关闭流程
        //        ⇒ AFGPlayerController::OnToggleInventory 广播 open=0 ⇒ 我们 HidePanel；
        //     ② 摘掉之后焦点还给背包 ⇒ 背包又开 ⇒ 再挂 ⇒ 稳定 0.6s 自激振荡。
        //        这就是「随机出现」（取决于你按 E 的那一刻振荡处在哪个相位）
        //        和「开关多了直接不显示」（振荡被你手动操作打乱后停在"已摘掉"那个相位）。
        //     ③ 每次重挂，Slate 重建子树 ⇒ 画布几何归零 ⇒ `canvasW=0` ⇒ y=0（屏幕最上沿）
        //        ⇒ 「从上往下闪一下」和「连闪 2~3 次」全都从这里来。
        //   前十二次修全在"遮挡/动画/吸附"上打转，都是在治症状 —— 因为没人问过"面板为什么会被反复重挂"。
        //
        //   【修法】改用 `AddToPlayerScreen` 挂进**同一个 Player 层**（与原版 UI 同层）。
        //   ★★★2026-11-05【第二十五次修：ZOrder 0 → 100】一手事实把它改了：
        //     **全部历史日志里 `OnCloseClicked` 一次都没触发过**（"取出"按钮的
        //     take-from-container 同样 0 次）⇒ 不是按钮尺寸问题，是**面板整个收不到鼠标**。
        //     面板明明画得出来 ⇒ 只可能是「有东西压在它上面，只吃点击不出画面」
        //     —— 同 ZOrder 时 Slate 平局按"后加入者在上"，原版背包界面是后创建的，
        //     于是在我们之上；它若带一块"看不见但参与命中测试"的满屏遮挡层，就正好：
        //     看得见我们、点不中我们。
        //   ⇒ 抬到 ZOrder=100（同层内明确压过原版 UI）。**不会挡原版**：面板只占物品栏左侧
        //     那条窄带，且 RootCanvas 已改成 SelfHitTestInvisible（空白区域点击照样穿透）。
        //   ★若将来又出现"原版背包点不动"，先查这里：把 100 调回去即可二分定位。
        //   ★返回值必须检查：AddToPlayerScreen 返回 bool（引擎 :350），失败时 bAttached 不能置真，
        //     否则状态机以为挂着、实际没挂 ⇒ 又回到「怎么都不显示」。
        //   ⚠ 补充（第十四次修核实的引擎事实）：UserWidget.cpp:1309 的 AddToPlayerScreen 内部
        //     只要 GetOwningLocalPlayer() 非空就 return true，**不检查 AddToScreen 是否真的成功**
        //     （AddToScreen :130 遇到"已有 parent"会返回 false，但上层把结果吞了）。
        //     ⇒ 所以 done=1 只代表"调用走通"，不代表"真的在视口里"。这里额外打一个 inViewport
        //     作为真实判据的旁证，方便下次排查时不必再翻引擎源码。
        //
        // ★★★2026-11-05【第三十次修：Player 层 → Game 层（改回来了，但这次有实锤）】
        //   第十六次修之后一直挂在 Player 层，ZOrder 从 0 抬到 100 再到自适应 1350，
        //   **遮挡物 `Widget_ShoppingList_C` 一点没让** —— 日志铁证：
        //      z=100 → z=350 → 600 → 850 → 1100 → 1350，`chain` 与 `n=18` **逐帧完全一致**。
        //   同 ZOrder 排序只能改**同一个 SConstraintCanvas 内**的先后。抬了 1250 都没动静
        //   ⇒ 说明我们和它在**两个不同的画布**里：Player 画布整体位于原版 UI 所在画布之下。
        //   ⇒ 结论：想压过原版 UI，必须和它待在**同一层**（Game 层 / `AddToViewport`）。
        //   ★那为什么第十三次修把 Game 层换成 Player 层？因为当时【每次关闭都 RemoveFromParent】，
        //     每 0.73s 摘挂一次 → 打断原版背包的蓝图动画状态机 → 自激振荡。
        //     **第十四次修起已经"挂一次永不摘"**，那个成因不复存在 ⇒ 现在回 Game 层是安全的。
        //   ⚠ 监控点：若重新出现 `open=0` 紧跟 `open=1` 的 2:1 节奏，说明焦点问题回来了，立刻回滚。
        AddToViewport(PanelZOrder);
        const bool bInViewport = IsInViewport();
        const bool bAdded = bInViewport;   // AddToViewport 返回 void，用 inViewport 当真值
        bEverAttached = bAdded;
        bAttached = bAdded;
        const bool bRootOk = (WidgetTree && WidgetTree->RootWidget != nullptr);
        WT_WARNF(TEXT("[warehouse-panel] ShowPanel: AddToViewport(GAME layer) z=%d done=%d, inViewport=%d, rootOk=%d"),
                 PanelZOrder, bAdded ? 1 : 0, bInViewport ? 1 : 0, bRootOk ? 1 : 0);
        if (!bAdded)
        {
            // 挂载失败：立刻把状态归零，让 TickRefresh 下一帧重试（而不是假装已挂）。
            bEverAttached = false;
            bAttached = false;
            WT_WARNF(TEXT("[warehouse-panel] ShowPanel: AddToPlayerScreen FAILED, will retry next tick."));
            return;
        }
    }
    // ★★★2026-11-05【第十六次修 —— 第十四次修留下的致命漏设，一行引发的连锁塌方】
    //   `bAttached = bAdded` 原来只写在上面 `if (!bEverAttached)` 块**里面**。
    //   于是 bEverAttached 一旦为真（首次挂载之后），ShowPanel 就再也不把 bAttached 设回 true。
    //   HidePanel 把它置 false 之后 ⇒ bAttached **永远停在 false**，后果是连锁的：
    //     · TickRefresh 每帧都满足 `bInventoryOpen && !bUserClosed && !bAttached` ⇒ **每帧调 ShowPanel**；
    //     · 每帧 ShowPanel 都执行 `LastWrittenPanelY = -99999.f` 和 `LastSig.Reset()`；
    //     · ⇒ 位置账本永远是哨兵值（日志铁证：17 条 `layout WRITE ... (prev=-99999.0)` 连成串）；
    //     · ⇒ 吸附 / 棘轮 / 平滑过渡全部失效（HasLastPanelY() 恒假）⇒ 目标一变就瞬跳
    //          ⇒ 第二帧 y 从 205.2 猛跳到 226.7，再一路滑走，看起来就是"闪一下"；
    //     · ⇒ LastSig 每帧被清 ⇒ 每帧把整张物品行表拆了重建 ⇒ 又闪又卡。
    //   ⇒ 修法：只要函数能走到这里，就说明"面板此刻应该是显示状态"，无条件置真。
    //     这是 ShowPanel 与 HidePanel 的状态契约，绝不能只挂在"首次挂载"分支里。
    bAttached = true;
    // SelfHitTestInvisible = 自身不拦鼠标、但子控件（按钮/搜索框）可交互。
    //   若改成 HitTestInvisible，取出按钮和搜索框就点不动了。
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    // ★★2026-11-04【第八次修「面板呢」—— 透明度必须在这里就收敛到 1】
    //   旧写法把 SetRenderOpacity(1.f) 放在函数**最末尾**（RefreshContents 之后）。
    //   风险链：AddToViewport 时置 0 作遮挡 → 中途任何一处提前返回/异常/重建失败
    //   ⇒ 末尾那句永远执行不到 ⇒ 面板**明明挂在视口里，却全透明** ⇒ 玩家看到「面板没了」。
    //   日志里 `AddToViewport done` 反复刷、却看不到面板，正是这条链。
    //   ★修法：可见性定下来就立刻把透明度收敛到 1，让"能不能看见"与"后面的布局/刷新"解耦。
    //   入场不做淡入（原版是实打实的位移，没有透明度变化），故这里设 1 不会破坏观感。
    SetRenderOpacity(1.f);
    // ★★★2026-11-05【第二十九次修】每次"出现"都从展开态开始（用户要求：每次开背包都看得见完整面板，
    //   收起只在本次背包会话内有效）。放在这里而不是 HidePanel，是因为 HidePanel 可能因竞态没跑到。
    ExpandPanel(/*bSilent=*/true);
    LastSig.Reset();     // 重开时强制重建一次（数据可能已变）
    // ★2026-11-05【第十一次修】打开时必须清关闭标记（第三条保险）。
    //   正常路径下 HidePanel 已清过；但 HidePanel 走的是 `if (bAttached)` 内那条路，
    //   若曾经因异常/竞态没执行到，标记会残留 ⇒ 新一次打开从第一帧就冻结、不跟随背包。
    //   在"唯一的出现入口"里再清一次，成本为零。
    bClosing = false;
    // ★2026-11-05【第二十一次修】bWantClose 也必须在这里清。
    //   第二十一次修取消了 bWantClose 的 1.5s 超时作废（见 TickRefresh ② 段），代价是
    //   它可能比过去更"持久"。而 ③ 段的关闭直达条件里带 `|| bWantClose` ——
    //   若这里残留一个 true，本次打开会在**第一帧**就被判成"该关"⇒ 面板刚出现就没了。
    //   （OnInventoryToggled(open=1) 也会清，但它可能因委托静默失效而不触发，所以这里必须兜。）
    bWantClose = false;
    // ★★★2026-11-05【第十七次修·第四道 —— 与第十一次修正好相反，这里**不再清账本**】
    //   第十一次修清它的理由是"新一次打开必须允许自由摆放，不能沿用上次的位置钳制"，
    //   那是在**每次关闭都 RemoveFromParent** 的前提下成立的：面板被拆掉，Slot 里的位置没了，
    //   账本却还留着旧值 ⇒ 棘轮会把新位置钳在旧值上。
    //   第十四次修之后面板**永不摘除**，Slot 里始终停在上次的位置 ⇒ 账本与真实状态一致，
    //   清掉反而制造出"账本说没历史、画面在旧位置"的错位 ⇒ 每次打开都先写一次回退值 205.2。
    //   故：保留账本。NativeDestruct（Slate 真被拆）里才复位成哨兵。
    //   bFollowing / GoalY 这些**过渡中间态**仍然要清（见下），它们和"看到的位置"不是一回事。

    // ★2026-11-05【第十二次修】过渡状态一并归零：新一次打开从干净状态起步，
    //   否则若上一次是被 × 关掉的（HidePanel 里已清，但这是保险）残留 bFollowing，
    //   第一帧 TickFollow 就会把面板从旧位置拉过来 —— 那正是"点×后重开连闪"的画面。
    bFollowing = false;
    FollowTweenT = 0.f;
    // ★2026-11-05【第二十次修】标记"本次打开的第一帧有效布局" —— 让下面的
    //   LayoutLeftOfInventory 跳过平滑过渡、直接把面板摆到目标位置。
    //   为什么必须：面板永不摘除，Slot 里还停着上次的位置（如 216），而这次算出的目标
    //   可能是 226（物品栏刚开始上浮）⇒ 过渡会让面板从 216 慢慢挪到 226，
    //   同时物品栏自己已经浮到 177 ⇒ 面板看起来"飘着跟不上"。
    //   直接写就没有这个问题：面板一出现就贴在物品栏当时的真实位置，之后跟着一起浮。
    bFreshOpen = true;
    // ★2026-11-05【第十二次修】目标也归零 —— 这是"点×后重开连闪"的最后一环：
    //   上次的目标若是 250（贴着某个参照面板算出来的），这次打开时参照物可能还没就绪（回退居中 205），
    //   目标一出现就与旧目标差 45px ⇒ 立刻起过渡 ⇒ 玩家看到"刚打开就滑一下"。
    //   清成哨兵后，本次打开的第一帧目标被无条件接受，之后才谈吸附/过渡。
    GoalY = -99999.f;
    GoalH = 0.f;

    // ★2026-11-04【照原版重写】入场动画 = 淡入为主 + 小幅位移（原版做法见 .h 的 SlideDist 注释）。
    //
    //   旧实现的问题（用户实测"从上往下弹"+"不顺滑"）：首次打开时 HasStableLayout 为 false ⇒ 必然走 else
    //   分支 ⇒ 面板被**直接摆到最终位置**（瞬间出现），然后 TickRefresh 半秒后补播动画，
    //   从"目标 + 42"再滑回目标 ⇒ 玩家先看到面板已经在了，再看到它动一下。
    //   而如果那半秒里布局因为背包几何变化把目标改小了（Y 变小 = 更靠上），
    //   补播时"目标 + 42"反而落在比当前视觉位置更靠上的地方 ⇒ 观感就是**从上往下掉**。
    //
    //   新做法（两段式 + 淡入，结构上保证"只会单调上浮一次"）：
    //     ① 第一次进入（bSlideArmed == false）：只测位置，**把面板摆到起点**（目标 + SlideDist）
    //        并**把 RenderOpacity 清零**。这一帧面板在下方且完全透明 —— 玩家看不到它，也就没有"先到位"。
    //     ② 下一帧（TickRefresh 每帧都会进来，见 TickRefresh 的补播逻辑）bSlideArmed 为真 ⇒
    //        用同一份锁定值 StartSlideIn，从透明+下方淡入到不透明+目标位。整段只有一次上浮。
    //     ★关键：透明度与位移都由本函数起始、由 TickSlide 单调推进，不存在反向。
    LayoutLeftOfInventory();
    if (!bGeometryReady)
    {
        // ★2026-11-04【第七次修「面板呢」—— 提前 return 造成面板彻底不出现】
        //   上一版这里直接 return（想着"等几何就绪再显示"），结果：
        //   ① bAttached 已置 true，但函数没走到刷新/显示，面板透明；
        //   ② RefreshContents 提前跑 + 反复 ShowPanel ⇒ 日志里 AddToViewport 被刷了一长串；
        //   ③ 玩家看到的就是「面板没了」。
        //   ★教训：拦截"坏值"不能靠在**入口提前 return**，那会把整个显示流程一起掐掉。
        //   正确做法：几何没好就用**当前已有位置**照常显示（哪怕位置暂不准），
        //   等下一秒几何就绪，下面的跟随逻辑自然把它摆正。宁可短暂位置不准，也绝不能不见。
        WT_WARNF(TEXT("[warehouse-panel] ShowPanel: geometry NOT ready, showing at fallback pos."));
        // ★2026-11-05【第九次修「首帧闪一下」】画布几何要 ~25ms 才就绪（日志实证：
        //   18:44:00.197 挂上 → 00.222 才 canvasW=1920）。这 25ms 里面板站在回退位置 y=0
        //   （屏幕左上角），紧接着就被摆到 y≈227 ⇒ 观感是"左上角闪一下再跳到左边中间"。
        //
        //   ★为什么这次敢用透明度遮挡（第八次修刚把它删了）：
        //     第八次删它的理由是"置 0 后若中途没执行到 1，面板永久透明"。
        //     这次加了**两道保险**，把那条风险链彻底掐死：
        //       ① 位置真正写对的那一次（bPlaced正确）明确推回 1；
        //       ② TickRefresh 每帧兜底：几何已就绪却仍是透明 ⇒ 强制拉回 1（见那边 watchdog）。
        //     即"透明"只可能出现在几何未就绪的几十毫秒里，一定会被恢复。
        //
        // ★★★2026-11-05【第十次修「从上往下闪一下再落下来」—— 上一版(第九次)的补丁自己把自己撤了】
        //   用户实机反馈：**每次**开背包都看到面板从上方落下来。翻回本函数执行顺序就明白了：
        //     ① 第 1 次 LayoutLeftOfInventory()：画布几何=0 ⇒ 算不出位置 ⇒ 走到这里 SetRenderOpacity(0)
        //     ② 中间设 bSlideArmed / 写 Slot (LayoutX=16, LayoutY=**0**)
        //     ③ **末尾无条件 SetRenderOpacity(1.f)**   ← 这一步把 ① 的遮挡当场撤掉了
        //     ④ RefreshContents()
        //   ⇒ 面板带着 y=0（屏幕最上沿）**不透明**地挂上去，几帧后几何就绪才被摆到 y≈227
        //      ⇒ 观感正是"从上面掉下来"。遮挡设了 0 却同一次调用里就收了，等于没设。
        //   ★修法：设 0 这件事必须**记账**（bHoldHidden 成员），同一次调用里不许收；
        //     只有等"几何真就绪且位置真写进去"的那一次，才把账面清掉并收回 1。
        //   ★为什么不干脆不管：y=0 那一帧是真实的可见错位，必须遮；
        //     而"永久透明"的风险由 ② 的 watchdog 兜底（它的判据是 bGeometryReady，不依赖调用次数）。
        SetRenderOpacity(0.f);
        bHoldHidden = true;   // ★记账：这一次的 0 还没被合法撤销
    }
    // ★2026-11-04【第六次修·日志实测定案 —— 最终方案】
    //   实测原版背包打开时自己从下往上浮 49.2px / 0.22s（absY 347.3→298.1，之后静止）。
    //   结论：**位移是原版自己做的，我们不该模拟，而应该跟随**。
    //   故这里不再摆起点、不再自己插值 —— 直接把面板放到当前布局位置并显示；
    //   接下来 0.6 秒内 TickRefresh 会每帧重算布局，面板自然跟着原版一起上浮。
    //   好处：方向、幅度、时长、缓动曲线全部与原版**逐帧一致**，不存在调参偏差。
    bSlideArmed = true;
    bSlidePlayed = true;
    bSliding = false;
    SlideLockedY = LayoutY;
    SlideLockedH = LayoutH;
    SlideTargetY = LayoutY;
    SlideTargetH = LayoutH;
    // ★2026-11-04【第七次修】跟随窗口只在**本次打开**开始时重置一次。
    //   旧写法无条件 FollowT=0，而 ShowPanel 可能被 TickRefresh 反复调用（幂等设计），
    //   一旦反复重置，跟随窗口永远从头开始 ⇒ 每一帧都在"跟随第一帧"。
    //   用 bFollowWindowStarted 保证每次打开只开一次窗口，HidePanel 时复位。
    if (!bFollowWindowStarted)
    {
        bFollowWindowStarted = true;
        FollowT = 0.f;   // ★开启跟随窗口（见 TickRefresh 的 FollowWindow）
    }
    if (UCanvasPanel* RC0 = Cast<UCanvasPanel>(WidgetTree ? WidgetTree->RootWidget : nullptr))
    {
        if (RC0->GetSlots().Num() > 0)
        {
            if (UCanvasPanelSlot* CS0 = Cast<UCanvasPanelSlot>(RC0->GetSlots()[0]))
            {
                CS0->SetPosition(FVector2D(LayoutX, LayoutY));
                CS0->SetSize(FVector2D(CS0->GetSize().X, LayoutH));
                // ★★2026-11-05【第十二次修】账本必须与 Slot 同步 —— 这里是**第三处**写 Y 的地方
                //   （前两处在 LayoutLeftOfInventory）。漏了这一行，账本就停在 -99999，
                //   下一轮布局认为"没有历史"，走直接写分支，可能把面板瞬间搬到新目标 ⇒ 闪。
                //   ★只在几何就绪时记：几何没就绪时 LayoutY 是回退值（0/居中），
                //     记下来会让吸附和棘轮基于一个玩家根本没看到过的数工作。
                if (bGeometryReady)
                {
                    LastWrittenPanelY = LayoutY;
                }
            }
        }
    }
    // ★2026-11-05【第十次修】透明度在这里收敛 —— 但**只收自己欠的那一笔**。
    //   上方 if(!bGeometryReady) 分支把面板压到 0（遮挡 y=0 的错位闪现），并置 bHoldHidden。
    //   ★上一版（第九次修）这里是**无条件** SetRenderOpacity(1)，于是：
    //     几何没就绪的那一次调用里，刚设上的 0 被同一函数末尾撤掉 ⇒ 遮挡完全失效 ⇒ 每次开背包
    //     都能看到面板从屏幕最上沿落到左边中间。这正是用户这次报的「从上往下闪一下再落下来」。
    //   ★现在的判据分两种情况：
    //     · 本次几何已就绪（没进 if）⇒ 位置写的就是真值 ⇒ 直接收，面板立刻可见（正常路径，零延迟）。
    //     · 本次几何未就绪（进了 if）⇒ 位置写的是回退值，**不许收**；留给下一次调用
    //       （那时 bGeometryReady 为真、位置是真值）或 TickRefresh 的 watchdog 收。
    //   与上方那个 if 严格成对：一个置 bHoldHidden，一个清 bHoldHidden，删一个必须同时删另一个。
    if (bGeometryReady)
    {
        bHoldHidden = false;
        SetRenderOpacity(1.f);
    }

    RefreshContents();
}

// 隐藏面板 = 唯一的"消失"入口。
// ★★★2026-11-05【第十四次修 —— 这条注释原本写的是彻头彻尾的反面，务必认清】
//   旧注释/旧实现：「用 RemoveFromParent 而不是 SetVisibility(Hidden)，隐藏但留在视口里的 widget
//   仍会吃掉输入、且重新 SetVisibility(Visible) 不会重播 Slate 入场，彻底移除更干净」。
//   听起来合理，**但它正是十四轮振荡的源头**：每次关都摘、每次开都挂 ⇒ Slate 子树反复重建
//   ⇒ 打断原版背包那套蓝图动画状态机 ⇒ 0.73s 自激振荡（实测 open=0:open=1 恒 >1）。
//   现在改成 SetVisibility(Hidden)，而且**永不摘除**：
//     · 输入：本面板根可见性用的是 SelfHitTestInvisible，本身就不拦鼠标；Hidden 更是完全不参与命中，
//       不存在"吃掉输入"的问题；
//     · "不会重播入场"：本项目早已不用 Slate 入场（第十次修起改为跟随原版背包位移），无影响。
//   代价只是常驻几百 KB 内存，远比每 0.73 秒重建一次子树划算。
// ★必须复位 bAttached —— 这是 ShowPanel 判断"要不要 AddToViewport"的唯一依据。
//   （NativeDestruct 里也会复位一次，因为 RemoveFromParent 会触发它，两条路都要覆盖。）
void URemote_FactoryWarehousePanel::HidePanel()
{
    if (bAttached)
    {
        // ★★★2026-11-05【第十四次修】把 RemoveFromParent() 换成 SetVisibility(Hidden)。
        //
        //   【为什么必须换】（日志实证，别再改回去）
        //     旧写法每次关面板都 RemoveFromParent ⇒ 面板从视口摘掉 ⇒ 下次打开再 AddToPlayerScreen
        //     ⇒ Slate 重建整棵子树。而原版背包的开关状态机在蓝图里（源码中连
        //     `OnToggleInventory.Broadcast` 都不存在，只有 BlueprintImplementableEvent 的
        //     NotifyOnToggleInventory，见 FGCharacterPlayer.h:1010），它靠动画时长推进；
        //     我们每次 Add/Remove 都在打断那套动画 ⇒ 状态机在两态间来回翻
        //     ⇒ 每翻一次就广播 open=0 / open=1 ⇒ 我们再跟着摘/挂 ⇒ **0.73s 自激振荡**
        //     （0.73 ≈ 背包滑入 0.35s + 滑出 0.38s）。日志里 open=0:open=1 恒 >1 就是这个节奏。
        //     症状映射：玩家看到面板自己关了又开（"点 × 点不掉"）、Y 跳到屏幕最上沿、
        //     连闪 2~3 次、"开关多了以后干脆不显示"（停在摘掉的相位）。
        //
        //   【为什么用 Hidden 而不是 Collapsed】
        //     Collapsed 不参与布局 ⇒ GetCachedGeometry 失效 ⇒ 下次打开又要经历
        //     canvasW=0 / y=0 那个回退帧 ⇒ 又闪一下。Hidden 仍占布局，几何一直有效，
        //     重新显示时第一帧就能摆对位置。
        //
        //   【代价】面板的 UObject / Slate 子树常驻内存（约几百 KB），且
        //     GetAllWidgetsOfClass 枚举时会看到它（本就属于 UUserWidget，无副作用）。
        //     相比"每 0.73s 重建一次的振荡"，这个代价可以忽略。
        SetVisibility(ESlateVisibility::Hidden);
        bAttached = false;
        // ★2026-11-05【第十八次修】记下隐藏时刻 —— 供 TickRefresh 在 0.6s 内拒绝"轮询说背包可见"
        //   导致的重新显示（那段时间的"可见"是原版背包滑出动画的残影）。
        if (UWorld* W = GetWorld())
        {
            LastHideAt = static_cast<float>(W->GetTimeSeconds());
        }
        WT_WARNF(TEXT("[warehouse-panel] HidePanel: -> Hidden (bEverAttached=%d)"),
                 bEverAttached ? 1 : 0);
    }
    // ★2026-11-05【第二十九次修】隐藏时静默复位成展开态：下次开背包看到的是完整面板。
    //   （CollapsePanel 只换 UBorder 的 Content，这里复位没有任何代价。）
    ExpandPanel(/*bSilent=*/true);
    bSliding = false;   // 关掉时中断动画，下次开重新播
    // ★2026-11-04：复位跟随状态 —— 下次打开是全新过程，要重新进入 0.6s 跟随窗口，
    //   跟着原版背包重新做一遍上浮。
    bSlidePlayed = false;
    bSlideArmed = false;
    FollowT = 0.f;
    bFollowWindowStarted = false;   // 下次打开重新开一次跟随窗口
    // ★收起后透明度必须回到 1，否则若这条路径没走到清理分支，下次 AddToViewport 会沿用旧值闪一下。
    // ★2026-11-05【第十次修】账本必须和透明度一起清 —— 两者是同一个状态的两面。
    //   若只把透明度设回 1 而账本残留 true，下次打开时 TickRefresh 的 watchdog 会以为
    //   "我压过一个 0 但没收回"，于是无条件把透明度推回 1 —— 那一刻若面板正好处在
    //   几何未就绪的遮挡期，遮挡又会被撤掉一次 ⇒ 同一个 bug 从另一个入口复现。
    //   ★顺序：先清账本，再设透明度（与 watchdog 保持一致）。
    bHoldHidden = false;
    // ★2026-11-05【第十一次修】关闭流程标记同样要清：面板已经收掉了，"正在关闭"这件事结束了。
    //   不清的话，下次打开会在第一帧就走进"冻结"分支 ⇒ 面板卡在初始位置不跟随。
    bClosing = false;
    CloseWaitAcc = 0.f;   // ★2026-11-05【第十三次修】判关计时归零（面板已收起，本轮判关结束）
    // ★2026-11-05【第二十一次修】本轮关闭请求已兑现，清掉它 —— 否则下一次打开会被这条
    //   残留的请求当场收掉（③ 段的关闭直达条件里带 `|| bWantClose`）。
    bWantClose = false;
    // ★2026-11-05【第十二次修】平滑跟随的过渡状态也在这里清 —— ★这就是「点×之后再打开
    //   会发现面板停在旧位置/连续跳几下」的那条链：HidePanel 只复位了位置账本没复位过渡状态，
    //   下次打开第一帧 TickFollow 还记着上一次的终点，先把面板拉向旧终点再被布局拉回来。
    //   （ShowPanel 里也清一次，双保险：× 关 和 关背包 两条路径都覆盖。）
    bFollowing = false;
    FollowTweenT = 0.f;
    GoalY = -99999.f;   // ★2026-11-05【第十二次修】目标同样归零（理由见 ShowPanel 同处注释）
    GoalH = 0.f;
    // ★★★2026-11-05【第十七次修·第五道 —— 与第十一次修相反，这里也**不再清账本**】
    //   第十一次修的顾虑是"棘轮会把下次打开的新位置钳在旧值上"。
    //   那条顾虑的前提是"关闭时面板被摘掉、位置没了但账本还在"——
    //   第十四次修之后面板永不摘除，Slot 里始终就是账本记的那个位置，两者一致，
    //   棘轮比的是"目标 vs 账本"，账本又是真实位置 ⇒ 不会误钳。
    //   反过来，清掉账本会造成"账本说没历史、画面却在旧位置"的错位：
    //   下次打开第一帧 `HasLastPanelY()` 为假 ⇒ 吸附/过渡全不生效 + 找不到参照时跳回居中 205.2。
    //   ⇒ 只清**过渡中间态**（bFollowing/GoalY/FollowTweenT），不清"看到的位置"。
    SetRenderOpacity(1.f);
}

// ★2026-11-04 入场动画 —— 照原版 BP_GameUI 的 FadeAnim（RenderOpacity + Translation + EaseOut）重写。
//   做法：LayoutLeftOfInventory 每次都会把 Y 直接写成目标值，故动画不能靠「改目标」实现，
//   而是记下目标 Y（SlideLockedY），每帧把 Slot 的 Y 写成「目标 + 剩余偏移」，
//   剩余偏移按 EaseOut 从 SlideDist 衰减到 0；同时 RenderOpacity 从 0 升到 1。
//   动画期间 Layout 照常跑（负责 X/宽/高），TickSlide 在其后覆盖 Y，因此不冲突。
//
// 【为什么必须冻结 Y】旧实现有两个 Y 的写者，它们在动画期间打架：
//   ① TickSlide 每帧写 `目标 + 剩余偏移`
//   ② LayoutLeftOfInventory 每 0.5s 写一次"真正的目标 Y"（并刷新 SlideTargetY）
//   而 Layout 的 Y 本身会变：面板刚挂上时原版背包 widget 的几何还没算好（GetCachedGeometry 返回 0
//   或过小），于是走"回退：纵向居中"；下一次刷新时几何到位，改成"顶底与背包齐平"——
//   **这个数值变化发生在动画过程中 ⇒ 面板当场跳一下**，玩家看到的就是"弹出来后往下弹了一点"。
//   ⇒ bSliding 期间 Layout 只更新 X/宽，Y/H 一律沿用 SlideLockedY/H，动画结束才恢复跟随。
void URemote_FactoryWarehousePanel::StartSlideIn()
{
    SlideT = 0.f;
    bSliding = true;
    // ★2026-11-04【关键】两段式入场时 SlideLockedY/H 已在 ShowPanel 第一段锁定过，
    //   **绝不能在这里用 SlideTargetY 重新锁定**：
    //   两段之间隔了一帧，那一帧里 LayoutLeftOfInventory 可能跑过（此时 bSliding 为 false，
    //   它会更新 SlideTargetY），重新锁定会让动画终点与"已经摆好的起点"对不上 ⇒ 凭空跳一下。
    //   故：仅当还没锁定过（bSlideArmed 为假，即非两段式路径）时才从 SlideTarget 取值。
    if (!bSlideArmed)
    {
        SlideLockedY = SlideTargetY;
        SlideLockedH = SlideTargetH;
    }
    // 两段式路径：SlideLockedY/H 沿用 ShowPanel 第一段写入的值，起点也已经摆在 SlideLockedY + SlideDist。
    // ★透明度从 0 起步（两段式第一段已清零；非两段式路径这里兜底清一次）。
    SetRenderOpacity(0.f);
}


void URemote_FactoryWarehousePanel::TickSlide(float DeltaSeconds)
{
    if (!bSliding)
    {
        return;
    }
    UCanvasPanel* RC = Cast<UCanvasPanel>(WidgetTree ? WidgetTree->RootWidget : nullptr);
    if (!RC || RC->GetSlots().Num() == 0)
    {
        bSliding = false;
        SetRenderOpacity(1.f);   // ★中断也要复位，否则面板永远半透明/隐形
        return;
    }
    UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(RC->GetSlots()[0]);
    if (!CS)
    {
        bSliding = false;
        SetRenderOpacity(1.f);
        return;
    }

    // ★2026-11-04 参数（见 StartSlideIn / ShowPanel 的两段式说明）：
    //   时长 SlideDur 0.14s（照原版 FadeAnim 的快速淡入；再长显得迟钝）
    //   曲线 EaseOutCubic（1-(1-t)^3）。五次方在 t→1 时导数趋零，最后十几毫秒几乎不动，
    //     观感像"到位后又黏/坠一下"；三次方收尾干净。透明度与位移共用同一条曲线，保证同步。
    //   dt 钳到 1/30s：切场景/掉帧时 dt 突增会让整段动画一帧跳完。
    //   ★位移一律以 SlideLockedY（锁定的目标）为基准：
    //     Y 轴向下为正 ⇒ 起点 = 目标 + SlideDist（在下方），Offset 从 +SlideDist 衰减到 0 ⇒ **向上浮起**。
    //     全程 Offset 单调减小 ⇒ 面板只向上移动，结构上不可能出现"往下掉"。
    constexpr float Dur = SlideDur;

    SlideT += FMath::Min(DeltaSeconds, 1.f / 30.f);
    if (SlideT >= Dur)
    {
        bSliding = false;
        // 收尾：精确落到锁定终点 + 完全不透明，并把布局目标也对齐，避免结束后布局一刷新又跳。
        SlideTargetY = SlideLockedY;
        CS->SetPosition(FVector2D(CS->GetPosition().X, SlideLockedY));
        SetRenderOpacity(1.f);
        return;
    }

    // EaseOutCubic：起步快、收尾平滑无硬停。
    const float P = SlideT / Dur;               // 0 → 1
    const float Rem = 1.f - P;                  // 1 → 0
    const float Eased = 1.f - Rem * Rem * Rem;  // 0 → 1，EaseOutCubic
    const float Offset = SlideDist * (1.f - Eased);   // +SlideDist → 0（正值 = 目标下方）

    CS->SetPosition(FVector2D(CS->GetPosition().X, SlideLockedY + Offset));
    SetRenderOpacity(Eased);                    // ★照原版 FadeAnim：与位移同曲线淡入
}

// ★2026-11-05【第十二次修】平滑跟随 —— 把"目标 Y 变了"从**硬跳**变成 0.2s 过渡。
//
// 为什么需要它（用户原话：「打开的时候有那么一瞬间会闪一下，不够平滑，
// 点×以后再打开会连续闪 2~3 次才正常」）：
//   面板 Y 取自"参照面板"（原版背包那类整块面板）的顶边。而参照面板会动：
//     · 原版物品栏打开时自己从下往上浮 49.2px / 0.22s（已逐帧实测）；
//     · 位面面板/开拓者显隐变化 ⇒ "最靠左那块"换人 ⇒ RefTop 直接换成一个不同的值；
//     · 首帧参照物没就绪 ⇒ 回退"纵向居中"，和 RefTop 差几十像素。
//   旧实现每次 0.5s 节流布局都把这些变化**一次性写进 Slot** ⇒ 那是 0.1 秒内的硬位移
//   ⇒ 玩家看到的就是"闪一下"。点 × 重开时连闪 2~3 次 = 重开瞬间参照物连续变了好几次。
//
// 做法：布局算出新目标后**不直接写**，而是记下终点（FollowGoalY）让本函数每帧逼近。
//   · 曲线沿用 EaseOutCubic，与入场动画同一套手感，玩家不会觉得"两种动画"；
//   · 时长 FollowDur = 0.2s（与 SlideDur 同量级，够平滑又不迟钝）；
//   · 每帧写完后同步 LastWrittenPanelY —— 账本必须等于 Slot 真实值（见 Layout 末尾的说明）。
//
// ★为什么不复用 TickSlide：
//   TickSlide 是"入场一次性动画"，由 ShowPanel/StartSlideIn 显式启动，语义是"从起点播到终点"；
//   而这里是"布局目标随时可能改，每次改都要柔和跟上"，是**常驻**的。
//   两者混在一起会出现"入场动画被布局过渡打断"的相互覆盖。分开各自管各自的状态位最干净。
//
// ★dt 钳 1/30s：切场景/长时间卡顿时 dt 可能上百毫秒，不钳会让整段过渡一帧跳完（等于没做）。
void URemote_FactoryWarehousePanel::TickFollow(float DeltaSeconds)
{
    if (!bFollowing)
    {
        return;
    }
    // ★2026-11-05【第十二次修】终点实时读 GoalY，不读启动时拷下来的 FollowGoalY。
    //   理由：GoalY 是布局的唯一真值，过渡期间若布局又算出新目标，GoalY 已经更新，
    //   过渡应当**直接改道**去新目标（起点保持当前 Slot 值即可），而不是跑完旧终点再起一段
    //   —— 后者就是"连闪 2~3 次"的成因（每次换目标都重头滑一遍）。
    //   改道时把起点重置为当前 Slot 值、计时归零，视觉上是一段连续的平滑曲线，不会顿。
    if (!FMath::IsNearlyEqual(FollowGoalY, GoalY, 0.5f))
    {
        FollowStartY = LastWrittenPanelY;   // 从"现在画面在哪"重新出发
        FollowGoalY = GoalY;
        FollowTweenT = 0.f;
    }
    UCanvasPanel* RC = Cast<UCanvasPanel>(WidgetTree ? WidgetTree->RootWidget : nullptr);
    if (!RC || RC->GetSlots().Num() == 0)
    {
        bFollowing = false;   // 控件树没了 ⇒ 放弃这次过渡（下一轮布局会重建目标）
        return;
    }
    UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(RC->GetSlots()[0]);
    if (!CS)
    {
        bFollowing = false;
        return;
    }

    FollowTweenT += FMath::Min(DeltaSeconds, 1.f / 30.f);
    const float P = FMath::Clamp(FollowTweenT / FollowDur, 0.f, 1.f);
    const float Rem = 1.f - P;
    const float Eased = 1.f - Rem * Rem * Rem;   // EaseOutCubic，与入场同一曲线

    // 起点 = FollowStartY（过渡开始那一刻 Slot 里的真实值），终点 = FollowGoalY。
    // ★不用"每帧向终点挪 10%"那种指数逼近：指数逼近永远到不了终点，会留下亚像素抖动，
    //   且帧率不同收敛速度不同（高刷屏上观感差异明显）。线性参数化 + 缓动可精确收尾。
    if (P >= 1.f)
    {
        bFollowing = false;
        CS->SetPosition(FVector2D(CS->GetPosition().X, FollowGoalY));
        LastWrittenPanelY = FollowGoalY;   // 收敛完成：账本对齐真实值
        return;
    }

    const float Y = FollowStartY + (FollowGoalY - FollowStartY) * Eased;
    CS->SetPosition(FVector2D(CS->GetPosition().X, Y));
    LastWrittenPanelY = Y;    // ★账本 = Slot 真实值。画面在哪，账本就记在哪。
}

// 背包开关回调 —— "立即响应"加速通道（真相在 TickRefresh 自观测，这里只求快）。
// ★会遇到的四种情况，必须都对：
//   a) 正常开背包 open=1        → 清 ×标记 + 清几何缓存 + ShowPanel
//   b) 正常关背包 open=0        → 清 ×标记 + HidePanel
//   c) 按 ESC（暂停菜单）open=0 → **忽略**（这不是玩家关背包，是暂停菜单把背包顶掉了）
//   d) 委托已失效（若干次开关后）→ 这里根本不触发，由 TickRefresh 兜底，仍然正确
// ★c 是用户实际踩到的坑（"按了一下 ESC 就没了"）：暂停菜单加载时原版会广播 open=0，
//   日志里这条 open=0 后面紧跟 Widget_PauseMenu_* 的加载。若响应它就会收面板，且回游戏后挂不回来。
//   TickRefresh 那边也有冻结兜底，两处都要有 —— 只冻轮询挡不住这条同步回调。
void URemote_FactoryWarehousePanel::OnInventoryToggled(bool bIsOpen)
{
    bInventoryOpen = bIsOpen;
    // ★2026-11-05【第二十六次修】把委托结果记成**权威状态**（轮询将来只做兜底，见 TickRefresh）。
    bDelegateOpen = bIsOpen;
    bDelegateEverFired = true;
    if (UWorld* WNow = GetWorld())
    {
        LastDelegateAt = static_cast<float>(WNow->GetTimeSeconds());
    }
    PollDisagreeAcc = 0.f;
    // ★★★2026-11-05【第二十六次修】委托说"开了"的这一刻是**唯一可信的钉身份时机**：
    //   背包一定真开着，此刻形状命中的就是它本人。开一个 1.5 秒窗口让轮询去钉/改钉
    //   （委托触发的这一帧控件常常还没布局，只试一帧必然钉不住）。
    if (bIsOpen)
    {
        WT_ArmPinSidebar(GetWorld());
    }
    WT_WARNF(TEXT("[warehouse-panel] OnInventoryToggled: open=%d bAttached=%d bUserClosed=%d"),
             bIsOpen ? 1 : 0, bAttached ? 1 : 0, bUserClosed ? 1 : 0);
    if (WT_IsPauseMenuOpen(GetWorld()))
    {
        WT_WARNF(TEXT("[warehouse-panel] OnInventoryToggled ignored: pause menu open."));
        return;
    }
    if (bIsOpen)
    {
        // ★★★2026-11-05【第十四次修「点 × 点不掉」】1 秒内刚点过 × 的 open=1 一律无视。
        //   为什么需要：下面那句 `bUserClosed = false` 的本意是"玩家重新打开背包，面板该再出现"，
        //   完全正确。但在振荡期间 open=1 每 0.73s 自己就来一次，玩家刚点的 × 会被它当场冲掉
        //   ⇒ 表现就是"点 × 怎么点都关不掉"。日志实证：03.192 open=0 → 03.537 open=1（341ms）。
        //   修法：记下点 × 的时刻，1 秒内的 open=1 只当振荡回声，不开面板、也不清 bUserClosed。
        //   ★为什么不干脆删掉 `bUserClosed = false`：那会让"点 × 关掉后再开背包"永远看不到面板，
        //     破坏用户明确要求的"每次开背包都要看得见"。所以只在时间窗内压制。
        {
            UWorld* W = GetWorld();
            const float NowSec = W ? static_cast<float>(W->GetTimeSeconds()) : 0.f;
            const float SinceClose = NowSec - LastUserCloseAt;
            if (bUserClosed && SinceClose < 1.0f)
            {
                WT_WARNF(TEXT("[warehouse-panel] OnInventoryToggled open=1 IGNORED: user closed %.2fs ago."),
                         SinceClose);
                return;
            }
        }
        // ★面板常驻：每次开背包都强制显示（清掉上次手动关闭标记）。
        bUserClosed = false;
        // ★2026-11-04 左移：开背包时重置物品栏几何缓存与重试计数，确保重新对齐到物品栏左侧。
        //   位面面板可能刚开/刚关，几何变了，缓存必须作废。
        CachedInventory = nullptr;
        InvFindAttempts = 0;
        CloseTicks = 0;   // 清判关计数，免得旧计数把刚挂上的面板立刻收掉
        // ★2026-11-05【第十三次修】开背包 ⇒ 清掉可能残留的关闭请求。
        //   例：先 open=0（被 ESC 暂停菜单忽略）紧接着 open=1，若不清，下一拍就会被误收。
        bWantClose = false;
        CloseWaitAcc = 0.f;
        ShowPanel();
    }
    else
    {
        // ★★2026-11-04【第八次修「面板呢」—— 关背包别在回调里立刻摘，交给轮询防抖】
        //   旧写法在这里直接 HidePanel()。但 WT_IsInventoryWidgetOpen 自带 0.1 秒节流，
        //   它采到的 bInventoryOpen 可能还是上一拍的 true ⇒ 下一帧 TickRefresh 判「背包还开着、
        //   但 bAttached 已 false」⇒ 立刻 ShowPanel() 重新挂。
        //   日志里那串交替的 `open=0 → AddToViewport` / `open=1 → AddToViewport` 就是这么来的：
        //   摘了又挂、挂了又摘，玩家看到的就是「面板闪一下就没了 / 干脆没有」。
        //   ★修法：回调只清标记、不复位状态机，把「关」的判定统一交给 TickRefresh 的轮询兜底。
        //
        // ★★★2026-11-05【第十三次修】但这里**要留下一个关闭请求**（bWantClose = true）。
        //   为什么之前"只清标记"现在不够：判关现在要靠"委托优先、轮询慢兜底"两路（见 TickRefresh ③ 段）。
        //   如果没有这个标志，就只剩 1.0s 的轮询兜底 ⇒ 玩家每次关背包面板都慢 1 秒才消失。
        //   有了它：委托一响，TickRefresh 下一拍（≤16ms）就收面板，零延迟。
        //   ★为什么仍然不在这里直接 HidePanel（这是第八次修踩过的坑）：
        //     WT_IsInventoryWidgetOpen 有 0.1s 节流，回调里摘掉之后，下一拍轮询可能还报"背包开着"
        //     ⇒ TickRefresh 立刻 ShowPanel 重新挂 ⇒ 摘了又挂的闪烁。
        //     ⇒ 只置标志、让 TickRefresh 在**同一拍里**先判后动，时序才自洽。
        bUserClosed = false;   // 下次开背包要再出现
        bWantClose  = true;    // ★委托说关了 ⇒ 交给 TickRefresh 立刻执行（不再等 1s 轮询兜底）
        // ★2026-11-05【第十八次修 v2】记下置位时刻 —— TickRefresh 用它做 1.5s 超时作废，
        //   也用来给"委托直达关闭"提供时间基准（见 .h 里 WantCloseAt 的注释）。
        if (UWorld* W = GetWorld())
        {
            WantCloseAt = static_cast<float>(W->GetTimeSeconds());
        }
        CloseTicks  = 0;
        CloseWaitAcc = 0.f;
    }
}

// ★建控件树必须在 RebuildWidget：Slate 在 TakeWidget() 时就按当时（空）的树建完，之后在
//   NativeConstruct 里再加 UWidget 不会反映到 Slate → 面板空白（用户实测「没有面板」）。
//   照 Remote_FactoryItemSelectPanel 的建法：做 RootCanvas，最后 return RootCanvas->TakeWidget()。
// 建控件树。★必须在这里建，不能在 NativeConstruct（Slate 在 TakeWidget() 时就按当时的树建完，
// 之后在 NativeConstruct 里再加 UWidget 不会反映到 Slate ⇒ 面板全空白，用户实测过"没有面板"）。
// 结构（自上而下）：
//   RootCanvas（满屏画布，供 LayoutLeftOfInventory 绝对定位）
//     └ RootBorder（深色半透明底板）
//         └ RootV（VerticalBox）
//             ├ 标题栏：Border(深) → HorizontalBox[ 标题"智能仓储（集装箱）" | > 收起 ]
//             ├ 搜索框：UEditableTextBox（OnTextChanged → OnSearchChanged）
//             └ ScrollBox（Fill，占满剩余高度）→ RowBox（行容器，RefreshContents 往里塞 URemote_FactoryWarehouseRow）
// ★单例会被反复 AddToViewport/RemoveFromParent，RebuildWidget 可能多次进入：`if (!WidgetTree->RootWidget)`
//   守卫保证只建一次树，否则每次开背包都重建一遍、Slate 句柄泄漏。
TSharedRef<SWidget> URemote_FactoryWarehousePanel::RebuildWidget()
{
    // 单例会被反复 AddToViewport/RemoveFromParent，RebuildWidget 可能多次进入：只建一次。
    if (!WidgetTree->RootWidget)
    {
    UWidgetTree* Tree = WidgetTree;

    // ★根画布：整个面板挂在这块画布上，占满 widget 的视口槽。
    UCanvasPanel* RootCanvas = Tree->ConstructWidget<UCanvasPanel>();
    // ★★★2026-11-05【第二十五次修「点标题栏 `>` 无效」】
    //   本 UUserWidget 的槽是**满屏**的，所以 RootCanvas 也是满屏的。
    //   UCanvasPanel 默认 Visibility=Visible ⇒ **命中测试可见** ⇒ 这块看不见的满屏画布会
    //   吃掉整个屏幕上的鼠标点击（包括原版背包自己的）。历史上它一直默认 Visible，
    //   一旦我们在层级上压过原版 UI，原版就点不动了；反过来被压住时又是我们点不动。
    //   ⇒ 改成 SelfHitTestInvisible：画布自身不参与命中测试，只让真正的内容（底板/按钮）响应。
    //   ★这是"点击穿透"的正确做法，与整块面板的 SelfHitTestInvisible 是同一套口径。
    RootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

    // 面板底：深色圆角底板（WhiteBrush 染深色，否则默认 brush 为空画不出来）。
    // ★RootCanvas 引用不再存成员：NativeConstruct 直接取 WidgetTree->RootWidget（即此处建的 RootCanvas）做视口自适应。
    UBorder* RootBorder = Tree->ConstructWidget<UBorder>();
    RootBorder->SetBrush(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
    RootBorder->SetBrushColor(COL_ROOT_BG);
    RootBorder->SetPadding(FMargin(2.f));

    UVerticalBox* RootV = Tree->ConstructWidget<UVerticalBox>();
    RootBorder->SetContent(RootV);

    // ★★★2026-11-05【第二十九次修】记住底板与主容器：收起时把底板内容换成窄条，展开时换回来。
    //   ★换 Content 而不是改可见性：行控件/搜索框都留在内存里，展开不需要重建。
    RootBorderRef = RootBorder;
    RootVRef = RootV;

    // 折叠条：26x46 的一条，里面只有一个向左的 `‹`（= "点我展开"）。
    //   ★用 ASCII '<'：Unicode 的 '‹'/'◀' 在 Roboto 里未必有字形，缺字会变豆腐块。
    //   ★它现在不挂进树，等收起时才作为 RootBorder 的 Content 换上去。
    {
        CollapsedBoxRef = Tree->ConstructWidget<USizeBox>();
        CollapsedBoxRef->SetWidthOverride(26.f);
        CollapsedBoxRef->SetHeightOverride(46.f);
        UButton* TabBtn = WH_MakeButton(Tree, FText::FromString(TEXT("<")),
                                        COL_MUTED_BG, nullptr, ETextJustify::Center, 15);
        TabBtn->OnPressed.AddDynamic(this, &URemote_FactoryWarehousePanel::OnTabClicked);
        CollapsedBoxRef->SetContent(TabBtn);
    }

    // ---- 标题栏：标题 + 关闭 ----
    {
        UBorder* TitleBg = Tree->ConstructWidget<UBorder>();
        TitleBg->SetBrush(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
        TitleBg->SetBrushColor(COL_TITLE_BG);
        TitleBg->SetPadding(FMargin(10.f, 6.f));

        UHorizontalBox* TitleRow = Tree->ConstructWidget<UHorizontalBox>();

        UTextBlock* TitleTxt = WH_MakeText(Tree, FText::FromString(TEXT("智能仓储（集装箱）")),
                                          13, true, COL_CONTAINER, ETextJustify::Left);
        if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(TitleTxt))
        {
            S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            S->SetVerticalAlignment(VAlign_Center);
            S->SetHorizontalAlignment(HAlign_Left);
        }

        // 【2026-11-05】按钮图标由「×」改为向右箭头「>」：语义=把面板收起来（滑回右侧），
        //   行为不变（仍走 OnCloseClicked → bUserClosed + HidePanel）。
        //   ★用 ASCII '>' 而非 '›'/'▸'：后者在 Roboto 里未必有字形，缺字会变豆腐块。
        // ★★2026-11-05【第二十二次修「箭头点不了」】`>` 的字形宽度远小于 `×`，而按钮在
        //   HorizontalBox 里是 Automatic 宽（= 文字宽 + 8px padding）⇒ 实测只剩十几像素，
        //   鼠标基本命不中，用户点上去毫无反应。
        //   ⇒ 外面套一层 USizeBox 固定尺寸，不管里面放什么字符都保证有可点面积。
        //   ★别改成"给文本加空格"凑宽度：那是靠字符撑尺寸，换字体/换字号又会塌回去。
        // ★★★2026-11-05【第二十五次修】尺寸 24×20 → 30×22，且**改绑 OnPressed**：
        //   一手事实：**全部历史日志里 `OnClicked` 一次都没触发过**（连"取出"按钮的
        //   take-from-container 也是 0 次）⇒ 不能再把"点不动"当尺寸问题。
        //   OnClicked 要求"按下+抬起落在同一个控件上"，而本面板跟着背包滑动、位置逐帧在变
        //   ⇒ 按下与抬起可能不在同一控件 ⇒ 点击被丢弃。OnPressed 在按下那一刻就触发，
        //   不受位置漂移影响；对"收起面板"这种动作，用户按下去就收起也更顺手。
        UTextBlock* CloseLabel = nullptr;
        UButton* CloseBtn = WH_MakeButton(Tree, FText::FromString(TEXT(">")),
                                          COL_MUTED_BG, &CloseLabel, ETextJustify::Center, 15);
        CloseBtn->OnPressed.AddDynamic(this, &URemote_FactoryWarehousePanel::OnCloseClicked);
        USizeBox* CloseBox = Tree->ConstructWidget<USizeBox>();
        CloseBox->SetWidthOverride(30.f);
        CloseBox->SetHeightOverride(22.f);
        CloseBox->SetContent(CloseBtn);
        if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(CloseBox))
        {
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            S->SetVerticalAlignment(VAlign_Center);
            S->SetHorizontalAlignment(HAlign_Right);
        }

        TitleBg->SetContent(TitleRow);
        RootV->AddChildToVerticalBox(TitleBg);
    }

    // ---- 搜索框 + 全部存入 ----
    //   ★2026-11-05：原来搜索框独占一行，现在右侧并一颗【全部存入】。
    //     搜索框用 Fill 吃掉剩余宽度，按钮用 Automatic 只占自身宽度 ⇒ 面板收窄时先缩搜索框，按钮不会被挤没。
    {
        UHorizontalBox* SearchRow = Tree->ConstructWidget<UHorizontalBox>();

        SearchBox = Tree->ConstructWidget<UEditableTextBox>();
        SearchBox->SetHintText(FText::FromString(TEXT("搜索...")));
        SearchBox->OnTextChanged.AddDynamic(this, &URemote_FactoryWarehousePanel::OnSearchChanged);
        if (UHorizontalBoxSlot* S = SearchRow->AddChildToHorizontalBox(SearchBox))
        {
            S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            S->SetVerticalAlignment(VAlign_Center);
        }

        // 【全部存入】：把背包里所有建造材料一次塞进集装箱。
        //   ★绑 OnPressed：本面板跟着背包滑动，OnClicked 要求按下+抬起落在同一控件，位置漂移时会丢点击。
        //   ★只存"物资"由服务端判定（Remote_FactorySmartWarehouse::IsStorableMaterial）：
        //     装备与消耗品不参与（免得把手上在用的东西存走）。
        //     ★2026-11-05 弹药已放开（用户要求面板能读/存子弹），ToolTip 文案同步去掉"弹药"二字。
        UButton* StoreAllBtn = WH_MakeButton(Tree, FText::FromString(TEXT("全部存入")),
                                             COL_STORE_BG, nullptr, ETextJustify::Center, 10);
        StoreAllBtn->SetToolTipText(FText::FromString(
            TEXT("把背包里所有物资一次存进集装箱\n装备和消耗品不会被存入")));
        StoreAllBtn->OnPressed.AddDynamic(this, &URemote_FactoryWarehousePanel::OnStoreAllClicked);
        if (UHorizontalBoxSlot* S = SearchRow->AddChildToHorizontalBox(StoreAllBtn))
        {
            S->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            S->SetVerticalAlignment(VAlign_Center);
        }

        if (UVerticalBoxSlot* S = RootV->AddChildToVerticalBox(SearchRow))
        {
            S->SetPadding(FMargin(8.f, 6.f));
        }
    }

    // ---- 滚动列表 ----
    //   ★2026-11-05 用户问「每个产物放一个集装箱，后面怎么往下翻」：
    //     列表本来就是 UScrollBox，但默认值不敢赌 —— 这里把三件事显式定死：
    //       ① 滚动条常显（Visible）：箱子多的时候玩家一眼就看到"下面还有"，不会以为列表就这么多。
    //       ② 滚轮只在需要滚动时消耗（WhenScrolling）：否则面板区域会吃掉整屏的滚轮事件。
    //       ③ 开启滚轮平滑动画：滚一档跳一屏太跳，平滑后能精确停在哪一行。
    {
        ScrollBox = Tree->ConstructWidget<UScrollBox>();
        ScrollBox->SetScrollBarVisibility(ESlateVisibility::Visible);
        //   ★枚举值核对（SlateTypes.h:25 `enum class EConsumeMouseWheel`）：只有
        //     WhenScrollingPossible / Always / Never 三个值，**没有** WhenScrolling。
        ScrollBox->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
        ScrollBox->SetAnimateWheelScrolling(true);
        RowBox = Tree->ConstructWidget<UVerticalBox>();
        ScrollBox->AddChild(RowBox);
        if (UVerticalBoxSlot* S = RootV->AddChildToVerticalBox(ScrollBox))
        {
            S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        }
    }

    // ★2026-11-04 左移：落点 = 屏幕左侧、落在【物品栏】左侧的空白带，顶底与紧邻的位面/物品栏面板齐平。
    //   锚点(0,0) 左上 + Alignment(0,0)：Slot 的 Position 即面板左上角，宽高即自身尺寸。
    //   精确落点/尺寸由 LayoutLeftOfInventory 读原版面板几何覆盖。
    if (UCanvasPanelSlot* CS = RootCanvas->AddChildToCanvas(RootBorder))
    {
        CS->SetAnchors(FAnchors(0.f, 0.f));
        CS->SetAlignment(FVector2D(0.f, 0.f));
        CS->SetSize(FVector2D(312.f, 560.f));   // 占位；NativeConstruct / LayoutLeftOfInventory 覆盖
        CS->SetPosition(FVector2D(16.f, 120.f));
    }

    WidgetTree->RootWidget = RootCanvas;   // 根改为画布（内部套深色底板）
    }

    if (UWidget* Root = WidgetTree->RootWidget)
    {
        return Root->TakeWidget();
    }
    return Super::RebuildWidget();
}

// 控件挂进 Slate 后（每次 AddToViewport 都会走一遍，单例复用故可能多次）。
// 这里只做两件事：① 按视口算【自然尺寸】PanelW/PanelH ② 布局 + 填内容（为首次显示兜底）。
// ★真正的定位逻辑全在 LayoutLeftOfInventory（它每 0.5s 还会再跑），这里只是"首帧先摆个样子"。
// ★别在这里注册委托或起 ticker —— 那时还没人保证调用次数，会重复挂。
void URemote_FactoryWarehousePanel::NativeConstruct()
{
    Super::NativeConstruct();

    // ★2026-11-04 视口自适应（与终端/物品选择面板同套范式）：按视口尺寸等比缩放面板，避免固定像素
    //   在 4K 偏小、小屏偏大；锚点(0,0.5) 左中已保证位置随分辨率自适应。
    //   WidgetTree->RootWidget 即 RebuildWidget 里建的 RootCanvas（UCanvasPanel），其 Slot[0] 是外层 RootBorder。
    if (UCanvasPanel* RC = Cast<UCanvasPanel>(WidgetTree ? WidgetTree->RootWidget : nullptr))
    {
        if (RC->GetSlots().Num() > 0)
        {
            if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(RC->GetSlots()[0]))
            {
                FVector2D Viewport(1920.f, 1080.f);
                if (UGameViewportClient* VP = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
                {
                    VP->GetViewportSize(Viewport);
                }
                // 侧栏式面板【自然尺寸】：宽占视口 19%（下限 300 / 上限 380），高占视口 62%（下限 480 / 上限 700）。
                // ★这是"自然尺寸"，不是最终尺寸 —— 最终由 LayoutLeftOfInventory 按空白带宽度收窄。
                //   分开存是为了空间变大（关掉位面面板）时能还原回自然宽，而不是一直窄着。
                const float W = FMath::Clamp(Viewport.X * 0.19f, 300.f, 380.f);
                const float H = FMath::Clamp(Viewport.Y * 0.62f, 480.f, 700.f);
                PanelW = W;   // 缓存【自然宽】供 LayoutLeftOfInventory 定位/收窄用
                PanelH = H;
                CS->SetSize(FVector2D(W, H));
                // ★锚点必须与 LayoutLeftOfInventory 保持一致：(0,0) + Alignment(0,0)。
                //   两者混用会让面板先按一种口径摆、再被另一种口径覆盖 ⇒ 抖一帧。
                // ★2026-11-04【修「弹出来后往下弹一点」】动画期间不要写 Y。
                //   NativeConstruct 在 AddToViewport 时触发，而 ShowPanel 里可能已经起了滑入动画；
                //   这里再把 Y 写成"居中回退值"，动画的起点就被改掉 ⇒ 玩家看到面板先跳回中间再滑。
                //   故 bSliding 为真时只对齐锚点与宽，Y 交给动画（SlideLockedY）管。
                CS->SetAnchors(FAnchors(0.f, 0.f));
                CS->SetAlignment(FVector2D(0.f, 0.f));
                // ★2026-11-04【第三次修「从上往下弹出来」—— 真凶在这里】
                //   旧代码在这里无条件写 Y = 纵向居中 (Viewport.Y - H) * 0.5。
                //   而 LayoutLeftOfInventory 随后算出的「顶底与背包齐平」(RefTop) 比居中值**更靠下**
                //   （1080 视口 H≈670 时居中≈205，RefTop≈250~300）⇒ 面板先出现在偏上位置，
                //   等布局刷新才落到 RefTop ⇒ 观感就是「从上面弹下来落回原位」。
                //   ★这不是动画问题：SlideDist 已归零，位移来自 NativeConstruct 这一次写 Y。
                //
                //   修法（两道保险）：
                //     a) ShowPanel 现在以 Hidden 挂载，显示动作推迟到 Layout 之后 ⇒ 这里的 Y 天然不可见；
                //     b) 这里干脆**不写 Y**，只由 LayoutLeftOfInventory 决定位置。
                //   保留一次 SetPosition 仅为确保 Slot 的初始值合法（(0,0) 也安全，因为不可见）。
                if (!bSliding)
                {
                    CS->SetPosition(FVector2D(16.f, 0.f));
                }
            }
        }
    }

    // 把面板停靠到【物品栏】左侧（读原版面板几何；读不到则回退固定内缩）。
    // ★2026-11-04：动画进行中不要再跑一次布局 —— 它虽已冻结 Y/H，但会额外刷新 LayoutY，
    //   让 ShowPanel/ TickRefresh 的"稳定判定"多一轮无意义比较。动画结束后自然由节流刷新接管。
    if (!bSliding)
    {
        LayoutLeftOfInventory();
    }

    // 控件树已在 RebuildWidget 建好，这里只填充内容（RowBox / ScrollBox 已就绪）。
    // ★首帧拿不到集装箱数据是正常的（聚合表要等智能仓储 ticker 刷出来），届时显示"无货物"，
    //   0.5s 后 TickRefresh 会自动刷出来，不需要在这里重试。
    RefreshContents();
}

// Slate 被拆时（RemoveFromParent 也会走到这里，不只是真销毁）。
// ★核心约束：【不要】在此 Reset GPanel。背包关闭时 RemoveFromParent 同样触发 NativeDestruct，
//   若在这里把单例清空，下次开背包就再也拿不到面板（用户踩过"面板又没了"）。
//   单例是 weak ptr，进程退出/世界销毁时由委托强引用释放后自动失效，不需要手动清。
// ★必须复位 bAttached：这条路径是 RemoveFromParent 触发的"隐形"卸载，若不复位，
//   TickRefresh 会以为面板还挂着，永远不再补挂。
// ★2026-11-05【第十四次修】bEverAttached 也要复位：它记账的是"Slate 侧到底还在不在"。
//   第十四次修之后 HidePanel 不再 Remove（只切 Hidden），所以 NativeDestruct 正常只在
//   Slate 真被拆（世界销毁 / 引擎强制重建）时走到 ⇒ 此时必须允许下一次 ShowPanel 重新挂载，
//   否则面板会因为 bEverAttached 卡在 true 而永远只 SetVisibility（挂在已拆掉的树上）⇒ 不显示。
void URemote_FactoryWarehousePanel::NativeDestruct()
{
    bAttached = false;
    bEverAttached = false;
    // ★2026-11-05【第十七次修】Slate 真被拆了 ⇒ Slot 里的位置随之消失 ⇒ 账本必须作废，
    //   否则下次挂载会以为"我还停在上次那个位置"，沿用到一个已经不存在的坐标系里。
    //   这是唯一还该清账本的地方（ShowPanel/HidePanel 都已经不清了）。
    LastWrittenPanelY = -99999.f;
    GoalY = -99999.f;
    bFollowing = false;
    Super::NativeDestruct();
}

// ★2026-11-04 左移（第三版，修「与位面面板不齐、顶底错位」）：
//   原版三个面板（位面存储站 / 物品栏 / 开拓者）在屏幕中左成一排，左侧留一条空白带。
//   本面板要塞进这条空白带里，并且【顶边、底边与紧邻的那个面板对齐】，才像同一套 UI。做法：
//     ① 枚举全部 UserWidget，只认「面板级」控件（宽高都像一整块面板，滤掉按钮/文字/行/全屏容器），
//        取其中最靠左的那条竖边 LeftMost —— 它就是原版面板群的左边界（有【位面存储站】时= 它的左缘，
//        没开位面时= 【物品栏】的左缘）；同时记下该面板的 Top/Bottom。
//     ② 空白带 = [SideGap, LeftMost - SideGap]，面板宽度先按自然宽、放不下就收窄到带内，
//        左右各留 SideGap ⇒ 左边距与右边距对称。
//     ③ 纵向照抄参照面板：Y=Top、H=Bottom-Top（顶底齐平）；读不到或数值异常则回退居中。
//     ④ 读不到任何面板（首帧/界面未开）则回退「贴左 SideGap + 居中」，保证始终可见。
//   坐标口径：锚点用 (0,0)+Alignment(0,0)，Slot 的 Position 就是面板左上角；位置是 RootCanvas
//   局部坐标，故把所有绝对位置减去本画布的绝对原点再比较（DPI/视口偏移安全）。
void URemote_FactoryWarehousePanel::LayoutLeftOfInventory()
{
    UCanvasPanel* RC = Cast<UCanvasPanel>(WidgetTree ? WidgetTree->RootWidget : nullptr);
    if (!RC || RC->GetSlots().Num() == 0)
    {
        WT_WARNF(TEXT("[warehouse-panel] layout BAIL: RC=%s slots=%d"),
                 RC ? TEXT("ok") : TEXT("null"), RC ? RC->GetSlots().Num() : -1);
        return;
    }
    UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(RC->GetSlots()[0]);
    if (!CS)
    {
        WT_WARNF(TEXT("[warehouse-panel] layout BAIL: slot0 not CanvasPanelSlot"));
        return;
    }

    // 锚点(0,0) 左上 + Alignment(0,0) ⇒ Slot 的 Position 就是面板左上角，宽高就是自身尺寸。
    // ★这两行是"顶底与位面面板齐平"的前提：只有左上锚点才能直接给绝对 Y。
    //   早期用 (0,0.5) 居中锚点，无论怎么算都做不到顶边对齐（Position 的含义变了），别改回去。
    CS->SetAnchors(FAnchors(0.f, 0.f));
    CS->SetAlignment(FVector2D(0.f, 0.f));

    // 三个待定值，先全部设成"回退值"，读到参照面板后再覆盖。这样任何一步失败都退化成"至少看得见"。
    float W = PanelW;          // 自然宽（NativeConstruct 算的，此处不改写它，便于空间变大时还原）
    float H = PanelH;          // 自然高；对齐成功时改成参照面板的高度
    float X = 16.f;            // 回退：贴左
    float Y = 0.f;             // 回退：纵向居中（下面读到画布尺寸后再算）

    UWorld* World = GetWorld();

    // 画布尺寸/绝对原点（画布满屏，故它等价于视口）。
    // ★这是"局部坐标"与"绝对坐标"的换算基准：GetAllWidgetsOfClass 拿到的都是绝对坐标，
    //   而 Slot->SetPosition 要的是本画布内的局部坐标 ⇒ 一律减去 Origin。
    //   不换算的话，玩家开界面缩放/DPI 变化时面板会偏。
    FVector2f Origin(0.f, 0.f);
    FVector2f Canvas(0.f, 0.f);
    {
        const FGeometry RG = RC->GetCachedGeometry();
        const FVector2f RS = RG.GetLocalSize();
        if (RS.X > 200.f && RS.Y > 200.f)
        {
            Canvas = RS;
            Origin = RG.GetAbsolutePosition();
        }
    }

    // ★2026-11-04【第四次修】把"几何是否已就绪"记进成员，供 ShowPanel / TickRefresh 判断
    //   能不能显示。日志实证：AddToViewport 同帧 canvasW=0（画布还没布局），0.4s 后才有真实值。
    //   没就绪时算出的 y=0 是垃圾值，绝不能拿来定位并显示。
    bGeometryReady = (Canvas.X > 200.f && Canvas.Y > 200.f);
    CanvasOriginY = Origin.Y;   // 供 FollowInventoryMotion 换算"绝对坐标 → 画布局部坐标"

    // 回退落点：纵向居中（★必须放在 Canvas 求值之后，否则先用后声明 —— C2065）。
    // ★★★2026-11-05【第十五次修「首帧 y=0 依然肉眼可见」】
    //   旧写法 `Y = FMath::Max(0.f, (Canvas.Y - PanelH) * 0.5f)` 在几何未就绪时是**有毒**的：
    //   Canvas.Y = 0 ⇒ (0 - 620) * 0.5 = -310 ⇒ **Max 把负值钳成 0** ⇒ 面板在挂载那一帧
    //   实实在在站在屏幕最上沿（y=0）。
    //   为什么之前认为"挡住看不见"却还是被看到：ShowPanel 里那句 SetRenderOpacity(0) 是在
    //   LayoutLeftOfInventory() **返回之后**才执行的，而 Slate 在挂载那一刻已经用 y=0 布过一帧了
    //   ⇒ 实测有 33ms（约 2 帧 @60fps）是"y=0 且不透明"地画在屏幕上的。
    //   ⇒ 修法：几何没就绪就摆到**屏幕外**（y = -PanelH - 2000），物理上不可见，
    //     不再依赖"透明度遮挡"这条随时可能被别的分支推回 1 的脆弱路径。
    //     几何一就绪立刻被下面的 RefTop 逻辑摆正（实测 33ms 后）。
    //   ★超时兜底：万一某种界面下几何长期读不到（历史无此案例，但不能假设永不发生），
    //     1.5 秒后放弃躲避、回退 y=0 —— 宁可位置不完美，也绝不能变成"面板永远不出现"。
    if (bGeometryReady)
    {
        GeoNotReadySince = -1.f;
        Y = FMath::Max(0.f, (Canvas.Y - PanelH) * 0.5f);
    }
    else
    {
        const float NowSec = World ? static_cast<float>(World->GetTimeSeconds()) : 0.f;
        if (GeoNotReadySince < 0.f)
        {
            GeoNotReadySince = NowSec;
        }
        const bool bGiveUpHiding = (NowSec - GeoNotReadySince) > 1.5f;
        Y = bGiveUpHiding ? 0.f : (-PanelH - 2000.f);
        // ★2026-11-05 收尾：加限流。这一行在"几何长期读不到"的界面下会**每帧**刷 —— 
        //   正常情况只发生 2 帧（33ms），但异常时会淹掉日志。限 10 条足够定性。
        static int32 GeoNotReadyLogs = 0;
        if (GeoNotReadyLogs < 10)
        {
            ++GeoNotReadyLogs;
            WT_WARNF(TEXT("[warehouse-panel] geometry NOT ready: park offscreen y=%.0f (giveUp=%d, waited=%.2fs)"),
                     Y, bGiveUpHiding ? 1 : 0, NowSec - GeoNotReadySince);
        }
    }

    // ★2026-11-10 改用「按类名识别原版背包」作参照（见 WT_FindSidebarWidget）：
    //   旧版用形状猜「最靠左竖长面板」，会误命中太空电梯/位面存储子面板，
    //   导致面板被压扁、位置掉到屏幕下半（用户实锤）。现在只认真背包，其余一律不当参照。
    float LeftMost = -1.f;
    float RefTop = -1.f;       // 参照面板（玩家背包）的顶/底边，局部坐标
    float RefBottom = -1.f;
    if (World && Canvas.X > 200.f)   // Canvas 无效（首帧）时不做，直接用回退值
    {
        if (UUserWidget* Inv = WT_FindSidebarWidget(World, nullptr))
        {
            const FGeometry G = Inv->GetCachedGeometry();
            const FVector2f Abs = G.GetAbsolutePosition();
            const FVector2f Sz  = G.GetLocalSize();
            if (Sz.X >= 2.f && Sz.Y >= 2.f)
            {
                const float L = Abs.X - Origin.X;
                if (L >= 0.f)
                {
                    LeftMost  = L;
                    RefTop    = Abs.Y - Origin.Y;
                    RefBottom = RefTop + Sz.Y;
                }
            }
        }
    }

    // 侧边间隙：按视口宽度的 0.6% 算（1920 宽 ≈ 11.5px），并夹到 [6,16]。
    //   这样小屏不贴边、4K 不显散。左右各留一个 SideGap ⇒ 面板在空白带里居中。
    const float SideGap = FMath::Clamp(Canvas.X * 0.006f, 6.f, 16.f);
    if (LeftMost > 0.f)
    {
        const float BandL = SideGap;
        const float BandR = LeftMost - SideGap;
        const float BandW = BandR - BandL;
        if (BandW >= 240.f)
        {
            // 正常情况：空白带够宽（≥240）⇒ 面板用自然宽，放不下就收窄到带内。
            W = FMath::Min(PanelW, BandW);   // 宁可窄一点，也不压原版面板
            X = BandL;
        }
        else
        {
            // 空白带太窄（原版面板几乎贴左，或玩家把界面缩放调得很大）：仍贴左，
            // 但宽度必须收在参照面板左缘之内，否则会以自然宽压上去（空带为负时尤其明显）。
            // 最小 120px 保证还点得动（再窄按钮就挤没了）。
            X = SideGap;
            W = FMath::Clamp(LeftMost - SideGap * 2.f, 120.f, PanelW);
        }

        // 纵向照抄参照面板：顶底齐平，像同一套 UI。
        // ★三个条件缺一不可：RefTop 有效、高度合理（≥200，排除读到按钮之类）、底边没超出画布
        //   （超出说明读到的是滚动中的/正在动画的控件，那值不可信，宁可退回居中）。
        const float RefH = RefBottom - RefTop;
        if (RefTop >= 0.f && RefH >= 200.f && RefBottom <= Canvas.Y + 1.f)
        {
            Y = RefTop;
            H = RefH;
        }
        else if (HasLastPanelY())
        {
            // ★★★2026-11-05【第十七次修「每次打开都先出现在 y=205 居中位置」】
            //   能走到这个 else，说明参照面板没读到（RefTop<0）或数值不可信。
            //   旧行为：Y 保持上面的"回退：纵向居中"= (1080-670)*0.5 = **205.2**。
            //   为什么这在以前没问题、现在致命：以前面板每次关闭都 RemoveFromParent，
            //   重新挂载时本来就得从零算，中间那几十毫秒的居中位被入场动画盖住了。
            //   现在面板**永不摘除**，Slot 里本来就停在上次正确位置（≈217/226），
            //   而 open=1 那一刻原版背包 widget 还没 IsVisible()（它刚开始播打开动画）
            //   ⇒ 必然读不到参照 ⇒ 每次都先写 205.2，等 0.5s 节流后再跳回 226
            //   ⇒ 观感正是「每次打开都先闪一下、位置不对」。
            //   ★修法：有历史账本就沿用账本值（=观众上次看到的位置），本次不写回退值。
            //     等下一轮节流读到参照，再按正常路径平滑过渡到真值。
            Y = LastWrittenPanelY;
            if (SlideTargetH >= 200.f)
            {
                H = SlideTargetH;   // 高度同样沿用，别被 PanelH 带跑
            }
        }
    }

    // ★★★2026-11-05【第十七次修·第二道】上面整块都在 `if (LeftMost > 0.f)` 里 ——
    //   若一个参照面板都没枚举到（LeftMost 仍是 -1），Y 会保持"回退：纵向居中"= 205.2。
    //   这正是日志里每次打开都出现 `layout WRITE y=205.2` 的直接来源。
    //   面板永不摘除 ⇒ Slot 里本来就停在上次的正确位置 ⇒ 有账本就沿用，不要中途跳到居中。
    if (LeftMost <= 0.f && HasLastPanelY())
    {
        Y = LastWrittenPanelY;
        if (SlideTargetH >= 200.f)
        {
            H = SlideTargetH;
        }
    }


    // ★2026-11-04【修「弹出来后又往下弹一下」】动画期间不碰 Y/H —— 这是本轮最关键的一行逻辑。
    //   病根：面板刚挂上时原版背包的几何还没算好，本函数算出的是"回退：纵向居中"；
    //   0.5s 后几何到位又改成"顶底齐平" ⇒ 动画中途 Y 被改 ⇒ 玩家看到面板跳一下。
    //   修法：bSliding 期间 Y/H 一律沿用 StartSlideIn 锁定的值（SlideLockedY/H），
    //   只允许更新 X 与宽（横向本来就稳定，不会跳）。动画结束后下一轮刷新才让 Y 跟随原版背包。
    //   ★注意：X/宽必须照常写，否则动画期间玩家改分辨率/开关位面面板会让面板横向错位。
    float OutY = Y;
    float OutH = H;
    if (bSliding)
    {
        OutY = SlideLockedY;
        OutH = SlideLockedH;
    }
    // ★★★2026-11-05【第十二次修「打开有一瞬间闪 / 点×后重开连闪 2~3 次」—— 真凶：目标 Y 会跳变】
    //   用户原话：「打开的时候有那么一瞬间会闪一下，不够平滑，点×以后再打开会连续闪 2~3 次才正常」。
    //   ★为什么这条注释比修法本身重要：这就是「闪」的唯一来源，之前十一次全在改遮挡/动画曲线，方向都错了。
    //
    //   面板 Y 来自"参照面板"（原版背包那类整块面板）的顶边 RefTop。而参照面板**不是恒定的**：
    //     · 原版物品栏自己有一段**从下往上浮 49.2px / 0.22s** 的入场动画（已实测，见 TickRefresh ④）；
    //     · 位面面板、开拓者面板的显隐由玩家决定，它们出现/消失会改变"最靠左那块面板"是谁；
    //     · 首帧参照物还没布局完 ⇒ 走"纵向居中"回退（和 RefTop 差几十像素）。
    //   ⇒ 0.5s 节流布局里算出的目标 Y 会**在几个值之间跳来跳去**，写进 Slot 就是可见的"闪"。
    //   ⇒ 越依赖"这一次算出的目标"就越闪 —— 这正是为什么改遮挡、改曲线都没用。
    //
    //   ★解法（两道量，缺一不可）：
    //     ① 吸附（Sticky）：目标与**正在进行的目标**相差不大（≤ YSnapEps）就沿用旧目标。
    //        ★判据必须是"目标 vs 目标"，不是"目标 vs Slot 当前位置" —— 这一点本轮踩过：
    //          过渡正在跑的时候 Slot 逐帧在变，拿它做判据会让"目标稳定"的场景被误判成"还在变"，
    //          每次布局都重新起一段过渡 ⇒ 永远收敛不了（仿真实测：末值停在 259.3，目标 250）。
    //     ② 平滑过渡（tween）：目标真的变了，就用 0.2s EaseOutCubic 过去，而不是一次跳过去。
    //        旧写法每 0.5s 把目标整块搬过去（0.1s 内的硬位移）—— 那正是"闪一下"。
    //
    //   ★为什么用相对判据（差值）而不是"只许向上"（棘轮）：棘轮只挡"Y 变大"，
    //     而这里的跳变是**双向**的（参照物换人后有时更上、有时更下），单向约束挡不住。
    //
    //   ★GoalY 的语义：**面板应该待在的 Y**。它是"这一次布局目标"与"上一次目标"吸附后的结果，
    //     Slot 里的实际像素值只由 TickFollow / 直写去追它。所以：
    //       · 吸附  → 比 GoalY 与本次目标，同则 GoalY 不变；
    //       · 过渡  → 比 Slot 实际值与 GoalY，差得多才起过渡。
    //     两个比较对象**不同**，混用就会出 bug（上面 ① 的踩坑就是这个）。
    if (!bSliding && HasGoalY() && FMath::Abs(OutY - GoalY) <= YSnapEps)
    {
        OutY = GoalY;   // 抖动吸收：这点差异玩家看不出，但跳出来就是一次"闪"
    }

    // ★★2026-11-05【第十一次修·下半｜「关闭的时候会闪一下」的真凶在这儿】
    //   光靠 ③ 段的 bClosing（判"背包不可见"后才冻结）**挡不住**这一下，因为：
    //   玩家按下关闭键后，原版背包先播一段**下滑离场动画**，这期间它依然 IsVisible()
    //   ⇒ bInventoryOpen 仍是 true ⇒ bClosing 还没置位 ⇒ 本函数每帧照跑
    //   ⇒ 面板跟着背包一路往下滑（时序模型两帧内下滑 122px），然后才被收掉。
    //   玩家看到的「闪一下」就是这段下滑。还可能叠加另一跳：背包滑出后被判不可见，
    //   本函数找不到参照面板 ⇒ 走"纵向居中"回退 ⇒ 面板从左边中间**跳到屏幕正中**。
    //
    //   ★解法：面板 Y **只许向上跟随，不许向下跟随**（棘轮 / ratchet）。
    //     依据（一手观察，非猜测）：
    //       · 打开时背包的位移是**从下往上浮**（实测 49.2px，见 TickRefresh ④ 段注释）；
    //       · 关闭时背包的位移是**往下滑出屏幕**（离场动画）；
    //       · 回退分支"纵向居中"对已摆好的面板通常也更靠下（RefTop 一般大于居中值）。
    //     ⇒ 所有"Y 变大"的情形没有一种是玩家想看到的；而"Y 变小"只有背包上浮这一种，
    //       恰是我们要跟随的。这条单向约束与需求完全同向，不会误伤。
    //   ★为什么必须在这里而不是在算出 Y 的地方：OutY 才是**真正写进 Slot** 的值
    //     （上面 bSliding 分支可能把它换成 SlideLockedY）。棘轮必须卡最终值。
    //   ★棘轮什么时候生效：GoalY 有真实历史值时才生效（判据统一走 HasGoalY()）。
    //     首帧没有历史值，必须放行，否则面板永远摆在占位位置。
    //   ★棘轮不参与 bSliding：动画自己就在做位移，交给 TickSlide 管，这里别插嘴。
    //   ★棘轮比的是 **GoalY**（目标），不是 Slot 实际值：目标一旦被钳住，Slot 会一路追到那个目标，
    //     不会再因为"Slot 还没到位"而反复钳（旧写法比 Slot 值，过渡期间会自锁）。
    if (!bSliding && HasGoalY() && OutY > GoalY + YSnapEps)
    {
        // 只钳 Y，不动 H：高度变化（顶底齐平）是另一回事，不产生"滑动"观感。
        OutY = GoalY;
    }
    // 到这里 OutY 就是**本次认可的目标**（已吸附、已棘轮钳制）⇒ 记进 GoalY，作为下一次比较的基准。
    // ★必须在起过渡之前记：TickFollow 收敛时要拿它当终点。
    //
    // ★★★2026-11-05【第十九次修「面板呢」—— 棘轮被屏幕外哨兵锁死】
    //   加 `if (bGeometryReady)` 是必须的，否则会出现下面这条死锁（日志铁证）：
    //     ① 首次挂载那一帧几何 = 0 ⇒ 走 park offscreen ⇒ Y = -2670
    //        ⇒ 这里无条件 `GoalY = -2670`（旧代码就是这样）
    //     ② 35ms 后几何就绪，算出正确值 Y = 226
    //     ③ 吸附判据 `HasGoalY()` = (-2670 > -9000) = **真** ⇒ 认为"有历史"
    //     ④ 棘轮 `OutY(226) > GoalY(-2670) + 6` = **真** ⇒ 判成"往下滑，不许"
    //        ⇒ `OutY = GoalY = -2670` ⇒ **算出来的 226 被自己的历史值钳回屏幕外**
    //     ⑤ 于是面板永远停在 y=-2670（屏幕外），玩家看到的就是「面板呢」。
    //   日志里那对矛盾值正是这条链的化石：
    //     `layout WRITE y=-2669.6`（OutY，被钳后） 与 `layout: ... y=226`（Y，真实目标）
    //     同一毫秒打印，说明"算得对、写出去错了"。
    //   ⇒ 屏幕外的哨兵位置**不许成为棘轮基准**：几何没就绪就没有"历史目标"可言。
    if (bGeometryReady)
    {
        GoalY = OutY;
        GoalH = OutH;
    }

    // ★2026-11-05【第十二次修】Slot 的实际值怎么追 GoalY：
    //   · 差得多（> YSnapEps）且不是首帧 ⇒ 起一段 0.2s 过渡（TickFollow），不硬跳；
    //   · 其余情形（首帧 / 已经够近）⇒ 直接写。
    //   ★首帧必须直接写：面板刚建或刚从 × 重开时 Slot 在占位位置，
    //     走过渡会让面板从占位位置慢慢飘到目标 —— 那正是用户说的"打开时从上往下闪一下"。
    //   ★bFollowing 已经在跑的话**不重启**（连续两帧布局都要求动，只保留第一段过渡）：
    //     每帧重启会把进度归零，过渡看起来像卡住不动。
    // ★★★2026-11-05【第二十九次修·折叠态】收成一条窄标签时，不走过渡/棘轮/跟随：
    //   直接把 Slot 强制压成 26x46 并返回。折叠是"静态小条"，任何平滑逻辑都只会让它抖。
    //   ★X 沿用本帧算出的停靠左边界，Y 沿用本帧算出的目标 Y —— 于是标签正好贴在原面板左上角，
    //     视觉上就是"面板向左收进去了"。
    if (bCollapsed)
    {
        CS->SetSize(FVector2D(26.f, 46.f));
        CS->SetPosition(FVector2D(X, OutY));
        LastWrittenPanelY = OutY;
        GoalY = OutY;
        GoalH = 46.f;
        return;
    }

    const bool bNeedTween = !bFollowing && !bFreshOpen && HasLastPanelY() &&
                            (FMath::Abs(OutY - LastWrittenPanelY) > YSnapEps);
    if (bNeedTween)
    {
        // 记下终点，Slot 本帧先不动；TickFollow 每帧往它逼近。
        FollowGoalY = OutY;
        FollowGoalH = OutH;
        FollowStartY = LastWrittenPanelY;   // 起点 = 观众现在看到的位置（Slot 真实值）
        FollowTweenT = 0.f;
        bFollowing = true;
        // ★W/H 照常写：横向本来就稳（X 由空白带决定，不跳），不必做过渡。
        CS->SetSize(FVector2D(W, OutH));
    }
    else
    {
        CS->SetSize(FVector2D(W, OutH));
        // ★bFollowing 为真时不能写 Y：那一帧的位置由 TickFollow 独占，两边都写会打架。
        //   但**如果过渡的终点刚好等于本帧目标**（目标稳定），把计时直接推到末尾让它立刻收尾，
        //   避免"目标已经对了、过渡还在跑"这种无谓的滑动。
        if (bFollowing)
        {
            FollowGoalY = OutY;
            FollowGoalH = OutH;
        }
        else
        {
            CS->SetPosition(FVector2D(X, OutY));
        }
    }
    // ★2026-11-04【修「往上弹约50」】布局的 Y 一旦被后续刷新改动，面板就会跳。
    //   这里记录"上一次真正写进 Slot 的 Y"，值变了才打日志 —— 只打前 20 次变化，
    //   便于用日志确认是不是 0.5s 节流里又算了不同的值。
    // ★行1482 原是 `static float LastWrittenY`（函数级静态）。2026-11-05【第十一次修】
    //   提升为**成员** LastWrittenPanelY —— 因为上面的棘轮逻辑需要在写 Slot **之前**读它，
    //   而函数内 static 只能在声明点之后使用（C2065 先用后声明）。
    //   提升为成员后还顺带修掉一个旧隐患：静态变量跨面板实例共享，换世界/重建面板时会读到
    //   上一个实例的 Y，导致第一帧被误判成"向下"而钳住。
    //
    // ★★2026-11-05【第十二次修 —— 这里的语义改了，别再按"记录一次算出的值"去理解】
    //   现在 LastWrittenPanelY 的含义严格是：**Slot 里此刻的 Y（观众看到的位置）**。
    //   · 直接写 Slot 的分支：写多少记多少；
    //   · 正在做过渡的分支：Slot 由 TickFollow 逐帧推进，**那里负责记**，这里不许动。
    //     若这里无条件 `= OutY`，账本会立刻变成"终点"，而画面还在起点 ⇒ 下一轮布局
    //     读到的历史值是个观众根本没看到过的数 ⇒ 吸附/过渡判据全失效，又开始闪。
    //     ★这就是"账本必须与真实状态一致"的又一个实例（第十次修的 bHoldHidden 同理）。
    // ★★★2026-11-05【第十七次修·第三道】加 `&& bGeometryReady` —— 几何没就绪时算出的 Y 是
    //   **屏幕外哨兵**（y = -PanelH-2000 = -2670），绝不能进账本。
    //   旧行为让它进了账本，于是下一帧 `HasLastPanelY()` 为真、`|226-(-2670)| > 6`
    //   ⇒ 起一个**从屏幕外滑到屏幕内**的 0.2s 过渡 ⇒ 首次打开看起来"面板没出来 / 从下面飞上来"。
    //   账本语义是"观众看到的位置"，屏幕外的位置观众没看到，不该被记住。
    if (!bFollowing && bGeometryReady)
    {
        static int32 ChangeLogs = 0;
        if (!FMath::IsNearlyEqual(OutY, LastWrittenPanelY, 0.5f) && ChangeLogs < 20)
        {
            ++ChangeLogs;
            WT_WARNF(TEXT("[warehouse-panel] layout WRITE y=%.1f (prev=%.1f) sliding=%d geo=%d closing=%d"),
                     OutY, LastWrittenPanelY, bSliding ? 1 : 0, bGeometryReady ? 1 : 0, bClosing ? 1 : 0);
        }
        LastWrittenPanelY = OutY;
    }
    // ★2026-11-05【第二十次修】"本次打开的第一帧"用掉了就清掉 —— 只在 bGeometryReady 时清：
    //   几何没就绪那次算的是屏幕外哨兵，不算"有效布局"，不能把这次机会浪费掉。
    if (bGeometryReady)
    {
        bFreshOpen = false;
    }
    // ★SlideTargetY/H 是动画加载中被锁定的值。动画期间**不要更新它**（否则动画就跟着跳）；
    //   不在动画时才跟随布局，这样下次 StartSlideIn 拿到的就是最新几何。
    if (!bSliding)
    {
        SlideTargetY = Y;
        SlideTargetH = H;
    }
    // 供 ShowPanel 判定"布局是否已稳"用：本次真实算出的值（不受 bSliding 冻结影响）。
    LayoutX = X;
    LayoutY = Y;
    LayoutH = H;

    // 一次性诊断：布局来源不明的视觉问题时靠它定位（只打前 6 次，不刷屏）。
    static int32 LogCount = 0;
    if (LogCount < 6)
    {
        ++LogCount;
        WT_WARNF(TEXT("[warehouse-panel] layout: canvasW=%.0f leftMost=%.0f refTop=%.0f refBot=%.0f -> x=%.0f y=%.0f w=%.0f h=%.0f"),
                 Canvas.X, LeftMost, RefTop, RefBottom, X, Y, W, H);
    }
}

// 搜索框文字变化。★每敲一个字符都会触发一次（不是回车才触发），所以这里必须便宜：
//   只改过滤词 + 作废签名 + 重建列表；实际过滤是大小写敏感的 Contains（见 RefreshContents）。
// ★TrimStartAndEnd 是必要的：玩家常带前导空格粘贴，不去空格就永远搜不到。
void URemote_FactoryWarehousePanel::OnSearchChanged(const FText& Text)
{
    SearchText = Text.ToString().TrimStartAndEnd();
    LastSig.Reset();     // 过滤词变了，强制重建
    RefreshContents();
}

// 标题栏 `>` —— 「收起成一条窄标签」。
// ★★★2026-11-05【第二十九次修·语义改了，别再按旧注释理解】
//   旧行为：整块隐藏（HidePanel），要再看到必须关背包重开 —— 用户明确说这不对，
//   他要的是**原版位面面板那种**：收起来后左边留一个向左的小箭头，点它立刻展开。
//   ⇒ 现在只置 bUserClosed（本次会话不再自动弹出）+ CollapsePanel()，**不隐藏**。
//   以下两种事件仍然会复位到展开态（保持"每次开背包都看得见"）：
//     ① 关背包（HidePanel 时 ExpandPanel 静默复位）
//     ② 重新开背包（ShowPanel 时静默复位）
void URemote_FactoryWarehousePanel::OnCloseClicked()
{
    bUserClosed = true;
    // ★2026-11-05【第十四次修】记下点 × 的时刻 —— OnInventoryToggled(open=1) 用它压制
    //   1 秒内的振荡回声，否则刚点的 × 会被 341ms 后的 open=1 当场冲掉（"点 × 点不掉"）。
    if (UWorld* W = GetWorld())
    {
        LastUserCloseAt = static_cast<float>(W->GetTimeSeconds());
    }
    WT_WARNF(TEXT("[warehouse-panel] OnCloseClicked: collapse to tab (not hide)."));
    CollapsePanel();
}

// 收起成窄条：把底板内容换成只有一个 `‹` 的 SizeBox；尺寸由 LayoutLeftOfInventory 末尾强制为 26x46。
void URemote_FactoryWarehousePanel::CollapsePanel()
{
    if (bCollapsed)
    {
        return;
    }
    bCollapsed = true;
    if (RootBorderRef && CollapsedBoxRef)
    {
        RootBorderRef->SetContent(CollapsedBoxRef);
    }
    // ★必须立刻重排一次：布局是 0.5s 节流的，不马上跑的话会先出现"整块大底板 + 中间一个小 `<`"
    //   持续半秒 —— 那观感比不收起还糟。
    LayoutLeftOfInventory();
    WT_WARNF(TEXT("[warehouse-panel] collapsed to tab."));
}

// 展开：换回完整内容并重算布局。
// ★bSilent 用于"关背包/重新开背包时静默复位"—— 那种场合不该打日志、也不该清 bUserClosed
//   （bUserClosed 由调用方自己的语义决定；ShowPanel 里会清）。
void URemote_FactoryWarehousePanel::ExpandPanel(bool bSilent)
{
    if (!bCollapsed)
    {
        return;
    }
    bCollapsed = false;
    if (RootBorderRef && RootVRef)
    {
        RootBorderRef->SetContent(RootVRef);
    }
    LastSig.Reset();       // 内容尺寸变了，强制重建一次
    if (!bSilent)
    {
        WT_WARNF(TEXT("[warehouse-panel] expanded from tab."));
        LayoutLeftOfInventory();   // 立刻恢复完整尺寸，别等 0.5s 节流
        RefreshContents();
    }
}

// 折叠条上的 `‹`：展开回完整面板。
void URemote_FactoryWarehousePanel::OnTabClicked()
{
    WT_WARNF(TEXT("[warehouse-panel] OnTabClicked: expand."));
    bUserClosed = false;    // 玩家主动展开 ⇒ 本次会话重新允许常驻
    if (UWorld* W = GetWorld())
    {
        LastUserCloseAt = static_cast<float>(W->GetTimeSeconds());
    }
    ExpandPanel();
}

// 重刷列表 —— 面板"内容"的唯一来源。每 0.5s 一次 + 开面板/搜索/取货后各一次。
// 三类数据各自从哪来（★别搞混，"位面"这里指原版中央存储，不是本面板）：
//   · Container = Remote_FactorySmartWarehouse::GetContainerAvailability()
//       「全世界所有原版存储集装箱」的聚合量，由智能仓储的 ticker 每秒刷。开关关着时它是空的。
//   · CloudReal = GetCentralStorageRealCount()
//       **纯原版云端**存量（不含集装箱）。因为 GetNumItemsFromCentralStorage 已被本模块 hook 成
//       「云端+集装箱」，要显示"纯位面"必须单独取这个表，否则"云"的数字会把集装箱量算进去、重复计数。
//   · Backpack  = 本玩家背包 GetNumItems(Cls)。
//
// ★性能/交互两条铁律：
//   ① 内容签名 LastSig 相同 ⇒ 直接 return，绝不重建行。否则每 0.5s 把按钮全拆了重建，
//      玩家点下去的那一帧按钮刚好被销毁 ⇒ 点击丢失（这个 bug 真出现过）。
//   ② 重建前后保存/恢复 ScrollOffset，否则列表每 0.5s 弹回顶部，几百个物品根本没法滚。
//
// ★显示规则：只列「集装箱里有货」的物品（N>0）；搜索词非空时再按物品名 Contains 过滤（大小写敏感）。
// ★液体不会出现：集装箱收集只认 AFGBuildableStorage，流体是 PipeReservoir 支系，天然被排除（用户铁律）。
void URemote_FactoryWarehousePanel::RefreshContents()
{
    UWorld* W = GetWorld();
    if (!W || !RowBox)
    {
        return;
    }

    // 保留滚动位置，避免 0.5s 刷新时列表弹回顶部。
    const float Off = ScrollBox ? ScrollBox->GetScrollOffset() : 0.f;

    UFGInventoryComponent* Backpack = GetLocalPlayerInventory(W);

    // 全局聚合仍要刷新一次：建造/制作的扣料 hook 读 G_ContainerAvailability，
    // 面板顺手把它刷到最新，避免"面板对了、建造那边还拿旧数"。
    Remote_FactorySmartWarehouse::RefreshContainerAvailability(W);

    const TMap<TSubclassOf<UFGItemDescriptor>, int32>& CloudReal = Remote_FactorySmartWarehouse::GetCentralStorageRealCount();

    // ★★★2026-11-05【一个容器一行】按容器采集，不再按物品聚合。
    //   用户原话：「同一个物品可能用不同集装箱存放」+「一个容器一行，然后加个箭头展示」。
    //   按物品聚合时，玩家看到"铁板 1500"却不知道分散在哪几个箱子，也没法只从某一个箱子取。
    TArray<Remote_FactorySmartWarehouse::FContainerSnapshot> Snapshots;
    Remote_FactorySmartWarehouse::GetContainerSnapshots(W, Snapshots);

    // ★虚拟容器行「背包（可存入）」放在最前面。
    //   为什么需要：改成按容器分组后，背包里"任何箱子都还没有"的材料就没有归属行，
    //   玩家想把它存进去就找不到入口。这一行专门承接「存入」，取出按钮对它永远是灰的。
    //   Storage = nullptr ⇒ 它的存入作用于**全部容器**（服务端自动挑有空位的箱子）。
    {
        Remote_FactorySmartWarehouse::FContainerSnapshot BagSnap;
        BagSnap.Label = TEXT("背包（可存入）");
        if (Backpack)
        {
            const int32 BpSize = Backpack->GetSizeLinear();
            for (int32 i = 0; i < BpSize; ++i)
            {
                FInventoryStack S;
                if (!Backpack->GetStackFromIndex(i, S) || !S.HasItems())
                {
                    continue;
                }
                const TSubclassOf<UFGItemDescriptor> C = S.Item.GetItemClass();
                if (!C || !Remote_FactorySmartWarehouse::IsStorableMaterial(C))
                {
                    continue;
                }
                BagSnap.Items.FindOrAdd(C) += S.NumItems;
                BagSnap.NumItems += S.NumItems;
            }
        }
        BagSnap.NumKinds = BagSnap.Items.Num();
        if (BagSnap.NumKinds > 0)
        {
            // ★2026-11-08：枢纽自带储物盒已完全踢出，Snapshots 里只剩玩家集装箱，背包行直接插到最前。
            Snapshots.Insert(BagSnap, 0);
        }
    }

    // ===== 2026-11-08：按「集装箱型号」分组，同型号合并成一行 =====
    //   主行 = 型号（只显示合计数量，不写具体物品）；
    //   展开 = 该型号下每个箱子一行，单物品的箱子前面带物品图标、混装箱不带，
    //         其下是该箱内的物品 + 数量 + 取/存（取/存只作用于该箱）。
    //   背包行（Storage 无效）单独成一组，key="__BAG__"。

    // --- 1) 分组 ---
    TArray<FString> GroupKeys;
    TMap<FString, TArray<const Remote_FactorySmartWarehouse::FContainerSnapshot*>> GroupMembers;
    TMap<FString, FString> GroupTitle;
    TMap<FString, bool> GroupIsBag;
    auto WT_AddToGroup = [&](const FString& Key, const FString& Title, bool bBag, const Remote_FactorySmartWarehouse::FContainerSnapshot* S)
    {
        if (!GroupMembers.Find(Key))
        {
            GroupKeys.Add(Key);
            GroupTitle.Add(Key, Title);
            GroupIsBag.Add(Key, bBag);
            GroupMembers.Add(Key, TArray<const Remote_FactorySmartWarehouse::FContainerSnapshot*>());
        }
        GroupMembers[Key].Add(S);
    };
    for (const Remote_FactorySmartWarehouse::FContainerSnapshot& S : Snapshots)
    {
        if (!S.Storage.IsValid())
        {
            WT_AddToGroup(TEXT("__BAG__"), S.Label, true, &S);
        }
        else
        {
            WT_AddToGroup(S.BoxType, S.BoxType, false, &S);
        }
    }

    // --- 2) 内容签名（按组分，防界面假死）---
    FString Sig = SearchText;
    Sig += TEXT("|");
    for (const FString& GK : GroupKeys)
    {
        const bool bGroupExp = !SearchText.IsEmpty() || IsContainerExpanded(GK);
        int32 GroupTotal = 0;
        const TArray<const Remote_FactorySmartWarehouse::FContainerSnapshot*>* PM = GroupMembers.Find(GK);
        if (PM) { for (const auto* C : *PM) GroupTotal += C->NumItems; }
        Sig += FString::Printf(TEXT("[%s]%d%d["), *GK, GroupTotal, bGroupExp ? 1 : 0);
        if (PM)
        {
            for (const auto* C : *PM)
            {
                Sig += FString::Printf(TEXT("%s["), *C->Label);
                TArray<TSubclassOf<UFGItemDescriptor>> Keys;
                C->Items.GetKeys(Keys);
                Keys.Sort([](const TSubclassOf<UFGItemDescriptor>& A, const TSubclassOf<UFGItemDescriptor>& B)
                {
                    return UFGItemDescriptor::GetItemName(A).ToString() < UFGItemDescriptor::GetItemName(B).ToString();
                });
                for (const TSubclassOf<UFGItemDescriptor>& Cls : Keys)
                {
                    Sig += FString::Printf(TEXT("%s;"), *UFGItemDescriptor::GetItemName(Cls).ToString());
                    if (bGroupExp)
                    {
                        const int32 N = C->Items.FindRef(Cls);
                        const int32 Bp = Backpack ? Backpack->GetNumItems(Cls) : 0;
                        Sig += FString::Printf(TEXT("#%d#%d;"), N, Bp);
                    }
                }
                Sig += TEXT("]");
            }
        }
        Sig += TEXT("]");
    }

    if (Sig == LastSig)
    {
        return;
    }
    LastSig = Sig;

    RowBox->ClearChildren();

    // 情况 1：智能仓储关着 => 只给提示。
    if (!Remote_FactorySmartWarehouse::IsSmartWarehouseEnabled())
    {
        UTextBlock* Hint = WH_MakeText(WidgetTree, FText::FromString(TEXT("（智能仓储未开启）")),
                                       12, false, COL_DIM, ETextJustify::Left);
        if (UVerticalBoxSlot* S = RowBox->AddChildToVerticalBox(Hint)) { S->SetPadding(FMargin(10.f, 8.f)); }
        if (ScrollBox) ScrollBox->SetScrollOffset(0.f);
        return;
    }

    // 情况 2：世界上一个玩家存储集装箱都没有（背包组不算）。
    bool bHasContainer = false;
    bool bHasBag = false;
    for (const FString& GK : GroupKeys)
    {
        if (GroupIsBag.FindRef(GK)) bHasBag = true; else bHasContainer = true;
    }
    if (!bHasContainer && !bHasBag)
    {
        UTextBlock* Hint = WH_MakeText(WidgetTree, FText::FromString(TEXT("（世界上没有存储集装箱）")),
                                       12, false, COL_DIM, ETextJustify::Left);
        if (UVerticalBoxSlot* S = RowBox->AddChildToVerticalBox(Hint)) { S->SetPadding(FMargin(10.f, 8.f)); }
        if (ScrollBox) ScrollBox->SetScrollOffset(0.f);
        return;
    }

    // 情况 3：正常渲染（型号合并行 + 展开后的箱子/物品行）。
    APlayerController* PC = GetOwningPlayer();
    int32 RenderedRows = 0;
    for (const FString& GK : GroupKeys)
    {
        const TArray<const Remote_FactorySmartWarehouse::FContainerSnapshot*>* PM = GroupMembers.Find(GK);
        if (!PM || PM->Num() == 0) { continue; }
        const bool bBag = GroupIsBag.FindRef(GK);

        // 过滤 + 组合计（第一遍：判断是否可见、算合计）
        int32 GroupTotal = 0;
        bool bAnyVisible = false;
        for (const auto* C : *PM)
        {
            int32 FilteredTotal = 0;
            for (const TPair<TSubclassOf<UFGItemDescriptor>, int32>& KV : C->Items)
            {
                if (KV.Value <= 0 || !KV.Key) { continue; }
                if (!SearchText.IsEmpty() && !UFGItemDescriptor::GetItemName(KV.Key).ToString().Contains(SearchText)) { continue; }
                FilteredTotal += KV.Value;
            }
            if (FilteredTotal > 0) { bAnyVisible = true; }
            GroupTotal += FilteredTotal;
        }
        if (!bAnyVisible) { continue; }

        const bool bExpanded = !SearchText.IsEmpty() || IsContainerExpanded(GK);
        const bool bSingleMember = (PM->Num() == 1);

        // ---- 型号合并主行（只显示合计数量）----
        if (PC)
        {
            URemote_FactoryWarehouseContainerRow* GRow = CreateWidget<URemote_FactoryWarehouseContainerRow>(PC);
            if (GRow)
            {
                GRow->Setup(GK, GroupTitle.FindRef(GK), 0, GroupTotal, bExpanded, this);
                if (UVerticalBoxSlot* S = RowBox->AddChildToVerticalBox(GRow))
                {
                    S->SetPadding(FMargin(0.f, (RenderedRows == 0) ? 0.f : 4.f, 0.f, 0.f));
                }
                ++RenderedRows;
            }
        }

        if (!bExpanded) { continue; }

        // ---- 展开：每个箱子一行（单物品带图标/混装不带）+ 箱内物品行 ----
        for (const auto* C : *PM)
        {
            // 收集该箱过滤后的物品
            TArray<TSubclassOf<UFGItemDescriptor>> Items;
            for (const TPair<TSubclassOf<UFGItemDescriptor>, int32>& KV : C->Items)
            {
                if (KV.Value <= 0 || !KV.Key) { continue; }
                if (!SearchText.IsEmpty() && !UFGItemDescriptor::GetItemName(KV.Key).ToString().Contains(SearchText)) { continue; }
                Items.Add(KV.Key);
            }
            if (Items.Num() == 0) { continue; }
            Items.Sort([](const TSubclassOf<UFGItemDescriptor>& A, const TSubclassOf<UFGItemDescriptor>& B)
            {
                return UFGItemDescriptor::GetItemName(A).ToString() < UFGItemDescriptor::GetItemName(B).ToString();
            });

            // 多成员组才显示「箱子」子标题（单成员组直接列物品，避免标题重复）。
            if (!bSingleMember)
            {
                UHorizontalBox* HdrH = WidgetTree->ConstructWidget<UHorizontalBox>();
                // 单物品 => 前面带该物品图标；混装 => 不带图标。
                if (Items.Num() == 1)
                {
                    UImage* HdrIcon = WidgetTree->ConstructWidget<UImage>();
                    FSlateBrush HdrBrush;
                    HdrBrush.SetImageSize(FVector2D(18.f, 18.f));
                    if (UTexture2D* HdrTex = UFGItemDescriptor::GetSmallIcon(Items[0]))
                    {
                        HdrBrush.SetResourceObject(HdrTex);
                    }
                    HdrIcon->SetBrush(HdrBrush);
                    if (UHorizontalBoxSlot* S = HdrH->AddChildToHorizontalBox(HdrIcon))
                    {
                        S->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
                        S->SetVerticalAlignment(VAlign_Center);
                    }
                }
                UTextBlock* HdrTxt = WH_MakeText(WidgetTree, FText::FromString(C->Label), 11, false, COL_CONTAINER, ETextJustify::Left);
                if (UHorizontalBoxSlot* S = HdrH->AddChildToHorizontalBox(HdrTxt))
                {
                    S->SetVerticalAlignment(VAlign_Center);
                    S->SetHorizontalAlignment(HAlign_Fill);
                    S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                }
                if (UVerticalBoxSlot* S = RowBox->AddChildToVerticalBox(HdrH))
                {
                    S->SetPadding(FMargin(14.f, 3.f, 0.f, 0.f));
                }
            }

            for (const TSubclassOf<UFGItemDescriptor>& Cls : Items)
            {
                const int32 N = C->Items.FindRef(Cls);
                const int32 Bp = Backpack ? Backpack->GetNumItems(Cls) : 0;
                const int32 Cloud = CloudReal.FindRef(Cls);
                URemote_FactoryWarehouseRow* Row = PC ? CreateWidget<URemote_FactoryWarehouseRow>(PC) : nullptr;
                if (!Row) { continue; }
                // 取/存只作用于这个箱子（背包组 Storage 无效 => nullptr = 作用于全部箱）。
                Row->Setup(Cls, Bp, Cloud, N, this, C->Storage.Get());
                if (UVerticalBoxSlot* S = RowBox->AddChildToVerticalBox(Row))
                {
                    S->SetPadding(FMargin(bSingleMember ? 14.f : 28.f, 0.f, 0.f, 0.f));
                }
            }
        }
    }
    // 情况 4：搜索有词但一条都没匹配上。
    if (RenderedRows == 0 && !SearchText.IsEmpty())
    {
        UTextBlock* Hint = WH_MakeText(WidgetTree, FText::FromString(TEXT("（没有匹配的物品）")),
                                       12, false, COL_DIM, ETextJustify::Left);
        if (UVerticalBoxSlot* S = RowBox->AddChildToVerticalBox(Hint)) { S->SetPadding(FMargin(10.f, 8.f)); }
        if (ScrollBox) ScrollBox->SetScrollOffset(0.f);
    }

    if (ScrollBox) ScrollBox->SetScrollOffset(Off);   // 恢复滚动位置
}

// 手动取出 —— 面板侧唯一出口，只负责把请求发到服务端。
// 完整链路（改任何一环都要连带看其它环）：
//   行按钮 OnTakeClicked（读修饰键定 1/10/一组）
//     → TakeAmount（按本行 NumContainer 夹取，请求量不超过界面显示的量）
//       → RequestTakeFromContainer（本函数）
//         → URemote_FactoryRCO::Server_TakeFromContainer（Server, Reliable，服务端权威）
//           → Remote_FactorySmartWarehouse::TransferFromContainersToInventory（先加后删，见那边注释）
//
// ★为什么必须走 RPC：集装箱是建筑上的 actor 库存，属于服务端权威数据。
//   客户端面板就算直接改本地那份 inventory，服务端不知道，下一次复制就把本地改动覆盖回去
//   ⇒ 表现就是"取出来了又回来了"或"物品消失"。绝对不能本地直接扣。
//
// ★为什么函数末尾要 LastSig.Reset()：RPC 是异步的，回来时数量已变；作废签名让下一次
//   RefreshContents 一定重建列表，界面立刻跟着变（不等 0.5s 节流周期）。
void URemote_FactoryWarehousePanel::RequestTakeFromContainer(TSubclassOf<UFGItemDescriptor> Cls, int32 Want, AActor* OnlyContainer)
{
    if (!Cls || Want <= 0)
    {
        return;
    }
    UWorld* W = GetWorld();
    if (!W)
    {
        return;
    }
    if (APlayerController* PC = W->GetFirstPlayerController())
    {
        if (AFGPlayerController* FPC = Cast<AFGPlayerController>(PC))
        {
            if (URemote_FactoryRCO* RCO = URemote_FactoryRCO::Get(FPC))
            {
                RCO->Server_TakeFromContainer(Cls, Want, OnlyContainer);
            }
        }
    }
    LastSig.Reset();   // 取货后强制下次刷新重建（数量会变）
}

// ★2026-11-05【存入】—— 与取出同一条链，方向相反：
//   行按钮 OnStoreClicked → StoreAmount（按 NumBackpack 夹）
//     → RequestStoreToContainer（本函数）
//       → URemote_FactoryRCO::Server_StoreToContainer（Server, Reliable）
//         → Remote_FactorySmartWarehouse::TransferFromInventoryToContainers（先加后删）
//
// ★为什么同样必须走 RPC：集装箱是建筑上的 actor 库存，服务端权威。客户端本地改不会同步，
//   服务端下一次复制就把改动抹掉 —— 表现是"存进去又回来了"。
void URemote_FactoryWarehousePanel::RequestStoreToContainer(TSubclassOf<UFGItemDescriptor> Cls, int32 Want, AActor* OnlyContainer)
{
    if (!Cls || Want <= 0)
    {
        return;
    }
    UWorld* W = GetWorld();
    if (!W)
    {
        return;
    }
    if (APlayerController* PC = W->GetFirstPlayerController())
    {
        if (AFGPlayerController* FPC = Cast<AFGPlayerController>(PC))
        {
            if (URemote_FactoryRCO* RCO = URemote_FactoryRCO::Get(FPC))
            {
                RCO->Server_StoreToContainer(Cls, Want, OnlyContainer);
            }
        }
    }
    LastSig.Reset();   // 存完数量会变，作废签名强制重建
}

// ★2026-11-05【全部存入】顶部按钮：把背包里所有建造材料一次存进集装箱。
// ★为什么不在客户端拼一张物品清单再逐个发 RPC：背包内容是服务端权威数据，客户端那份可能滞后；
//   而且在客户端遍历背包再发 N 次 RPC，网络包会很多。服务端自己做快照最准、最省。
void URemote_FactoryWarehousePanel::RequestStoreAllToContainers()
{
    UWorld* W = GetWorld();
    if (!W)
    {
        return;
    }
    if (APlayerController* PC = W->GetFirstPlayerController())
    {
        if (AFGPlayerController* FPC = Cast<AFGPlayerController>(PC))
        {
            if (URemote_FactoryRCO* RCO = URemote_FactoryRCO::Get(FPC))
            {
                RCO->Server_StoreAllToContainers();
                WT_WARNF(TEXT("[warehouse-panel] store-all requested."));
            }
        }
    }
    LastSig.Reset();
}

// 顶部【全部存入】按钮。★绑 OnPressed 不绑 OnClicked：本面板跟着背包滑动，
// 按下与抬起可能不在同一控件上，OnClicked 会被 Slate 丢弃（2026-11-05 踩过的坑）。
void URemote_FactoryWarehousePanel::OnStoreAllClicked()
{
    RequestStoreAllToContainers();
}

// ---------------------------------------------------------------------------
// ★2026-11-03 集装箱面板的行控件：单行物品（图标 + 名称 + 三源数量 + 取出按钮）。
// 每行的构成（从左到右）：
//   图标 22×22 | 物品名 13字 | 弹性空隙 | 「背N/云N」9字暗色 | 「箱N」12字（三源最多者着色） | 【取出】按钮
// ★窄面板（最窄收到 120px）下的取舍：名称不换行（AutoWrapText=false），过长就被裁掉；
//   这是刻意的 —— 换行会把行高撑成两倍，列表直接挤爆。想看得全就搜。
// ---------------------------------------------------------------------------

// 填数据。★必须在 AddChild 之前调用：AddChild → TakeWidget → RebuildWidget 会立刻读这些成员建树，
// 晚一步整行就是空的（无图标、无名字、数量全 0）。
void URemote_FactoryWarehouseRow::Setup(TSubclassOf<UFGItemDescriptor> Cls, int32 InNumBackpack,
                                        int32 InNumCloud, int32 InNumContainer, URemote_FactoryWarehousePanel* Owner,
                                        AActor* InContainer)
{
    ItemCls = Cls;
    NumBackpack = InNumBackpack;
    NumCloud = InNumCloud;
    NumContainer = InNumContainer;
    OwnerPanel = Owner;
    ContainerActor = InContainer;   // ★2026-11-05 限定取/存只作用于这个箱子（nullptr = 全局）
}

// ★行控件同样必须在 RebuildWidget 建树（数据 Setup 在 AddChild 之前已设好，此处可直接读）。
TSharedRef<SWidget> URemote_FactoryWarehouseRow::RebuildWidget()
{
    if (!WidgetTree->RootWidget)
    {
    UWidgetTree* Tree = WidgetTree;

    // 行底：深色行背景，便于区分行。
    UBorder* RowBg = Tree->ConstructWidget<UBorder>();
    RowBg->SetBrush(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
    RowBg->SetBrushColor(COL_ROW_BG);   // 行底与面板底同色系、略亮一档，便于区分行
    RowBg->SetPadding(FMargin(6.f, 3.f));

    UHorizontalBox* H = Tree->ConstructWidget<UHorizontalBox>();

    // 图标 22x22。★必须显式 SetImageSize：物品图标贴图通常是 64/256 的方形，
    //   不指定尺寸时 UImage 会按贴图原始大小铺开，直接把这行撑成几百像素高（列表就废了）。
    //   图标拿不到（GetSmallIcon 返回 null）也不算错：只是这一行没图标，文字照常显示。
    {
        UImage* Icon = Tree->ConstructWidget<UImage>();
        FSlateBrush Brush;
        Brush.SetImageSize(FVector2D(22.f, 22.f));
        if (UTexture2D* Tex = UFGItemDescriptor::GetSmallIcon(ItemCls))
        {
            Brush.SetResourceObject(Tex);
        }
        Icon->SetBrush(Brush);
        if (UHorizontalBoxSlot* S = H->AddChildToHorizontalBox(Icon))
        {
            S->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
            S->SetVerticalAlignment(VAlign_Center);
            S->SetHorizontalAlignment(HAlign_Left);
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        }
    }

    // 物品名（★2026-11-05 改 Fill：吃掉中间所有剩余宽度，左对齐，超长裁掉）。
    //   ★原来是 Automatic（自然宽）+ 一根弹性空柱。那样长名字会把右边的按钮挤出去
    //     （用户截图：第一行"桶装燃油"的【存入】直接被面板右边缘切掉一半）。
    //     改成 Fill 后，名字多长都不会影响右侧，只影响自己显示的字符数 —— 布局从此绝对稳定。
    {
        UTextBlock* NameTxt = WH_MakeText(Tree, UFGItemDescriptor::GetItemName(ItemCls),
                                          13, false, COL_VALUE, ETextJustify::Left);
        if (UHorizontalBoxSlot* S = H->AddChildToHorizontalBox(NameTxt))
        {
            S->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
            S->SetVerticalAlignment(VAlign_Center);
            S->SetHorizontalAlignment(HAlign_Fill);
            S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        }
    }

    // ★★★2026-11-05【删掉「背N/云N」】用户明确要求：面板里只显示箱子数量。
    //   原来这里是「背997/云0」这种 9 号暗色小字，占掉约 60px —— 面板本来就窄，
    //   它一占，右边的【存入】就被挤出面板外（用户截图可见）。
    //   删掉后行内只剩「图标 / 名称 / 箱N / 取出 / 存入」，四段等宽对齐。
    //   ⚠ 三源数量并没有丢：成本格染色（Remote_FactoryCostSlotTint）仍然按三源判色，
    //     行里的「箱N」颜色也继续按三源里谁最多来上色，只是不再把背包/云端的数字写出来。

    // 集装箱数量「箱N」—— 这一行的主数字，颜色表示"三源里谁最多"：
    //   背包最多→橙、位面最多→紫、集装箱最多→蓝（默认蓝）。
    // ★必须**严格大于**才换色：相等时保持蓝。否则玩家背包刚好和集装箱一样多，颜色会来回跳。
    // ★与成本格染蓝（Remote_FactoryCostSlotTint）是**同一套语义**，那边判据是
    //   `NumContainer > NumCloud && NumContainer > NumBackpack && >0`，改这里要同步改那边。
    // ★每个条件都带 `> 0`：三源全为 0 时不该出现任何颜色的染色（理论上到不了这行，但防御）。
    // ★2026-11-05 固定宽度显示（用户定案：**统一宽度**，不是补零）
    //   用户原话澄清：「这5位数的0，我是举个例子，统一宽度」
    //   ⇒ 数字照常显示（箱0 / 箱500 / 箱12345），只是**给这一列一个固定宽度**，
    //     让每行的「箱N」都从同一个位置开始、止于同一个位置，后面的按钮才不会参差。
    // ★宽度 100px：用户指出「有可能大几百万」（7 位数，如 箱3500000）。
    //   12 号粗体数字约 7~8px/字 ⇒ "箱" + 10 位数字 ≈ 92px，定 100 覆盖到千万级仍有余量。
    //   5 位、7 位、10 位数字都在同一个右对齐框里，宽度不变 ⇒ 每行按钮严格对齐。
    //   ★不要为了"省地方"缩回 62/78：那两档只够 4~5 位，百万级数字会被裁掉高位，显示成错的数。
    {
        FLinearColor CountColor = COL_CONTAINER;
        if (NumBackpack > NumCloud && NumBackpack > NumContainer && NumBackpack > 0) CountColor = COL_BACKPACK;
        else if (NumCloud > NumContainer && NumCloud > NumBackpack && NumCloud > 0) CountColor = COL_CLOUD;

        UTextBlock* CountTxt = WH_MakeText(Tree, FText::FromString(FString::Printf(TEXT("箱%d"), NumContainer)),
                                           12, true, CountColor, ETextJustify::Right);
        USizeBox* CountBox = Tree->ConstructWidget<USizeBox>();
        CountBox->SetWidthOverride(100.f);
        CountBox->SetContent(CountTxt);
        if (UHorizontalBoxSlot* S = H->AddChildToHorizontalBox(CountBox))
        {
            S->SetVerticalAlignment(VAlign_Center);
            S->SetHorizontalAlignment(HAlign_Right);
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        }
    }

    // 取出按钮（三合一）：单颗按钮，靠修饰键区分数量 ——
    //   左键 = 100、Shift+左键 = 一组（该物品单格堆叠上限）、Ctrl+左键 = 全部。
    // ★2026-11-05 用户定案改成这套（旧的是 1 / 10 / 一组）：
    //   左键给 100 才够日常取用；"全部"必须显式按 Ctrl，避免误点搬空整行。
    // ★为什么合成一颗而不是并排三颗：面板最窄只有 120px，三颗按钮会把物品名挤没。
    // ★SetToolTipText 是必要的可发现性手段 —— 修饰键语义光看按钮看不出来。
    // ★2026-11-05 两颗按钮都套 USizeBox 固定 44px：文字宽度受限时布局才不会塌，
    //   也保证"取出/存入"两列在每一行都严格对齐（用户要求「全部对齐」）。
    {
        UButton* TakeBtn = WH_MakeButton(Tree, FText::FromString(TEXT("取出")),
                                         COL_TAKE_BG, nullptr, ETextJustify::Center, 10);
        TakeBtn->SetToolTipText(FText::FromString(TEXT("左键：取出 100 个\nShift+左键：取出一组（装满一格）\nCtrl+左键：全部取出")));
        TakeBtn->OnClicked.AddDynamic(this, &URemote_FactoryWarehouseRow::OnTakeClicked);
        // ★2026-11-05 列表改成并集后，会出现「集装箱 0、背包有货」的纯背包行 ——
        //   那种行的【取出】没有意义，置灰（否则玩家点了必然什么都不发生，以为是卡了）。
        TakeBtn->SetIsEnabled(NumContainer > 0);
        USizeBox* TakeBox = Tree->ConstructWidget<USizeBox>();
        TakeBox->SetWidthOverride(44.f);
        TakeBox->SetContent(TakeBtn);
        if (UHorizontalBoxSlot* S = H->AddChildToHorizontalBox(TakeBox))
        {
            S->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
            S->SetVerticalAlignment(VAlign_Center);
            S->SetHorizontalAlignment(HAlign_Right);
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        }
    }

    // ★2026-11-05 新增【存入】按钮（与【取出】并排、语义镜像）。
    //   为什么用 OnClicked 而不是 OnPressed：这里用的是**按一下就生效**的动作，和取出保持一致，
    //   实测取出的 OnClicked 在遮挡修好之后是可靠的（日志 take-from-container 有正常记录）。
    //   ⚠ 若将来又出现"点不动"，先按[收起/展开]那两个按钮的经验改 OnPressed，别先怀疑字号。
    //   ★ToolTip 必须与 OnStoreClicked 的三档同步（改一处必须改两处），否则玩家看不到提示。
    {
        UButton* StoreBtn = WH_MakeButton(Tree, FText::FromString(TEXT("存入")),
                                          COL_STORE_BG, nullptr, ETextJustify::Center, 10);
        StoreBtn->SetToolTipText(FText::FromString(TEXT("左键：存入 100 个\nShift+左键：存入一组（装满一格）\nCtrl+左键：背包里该物品全部存入")));
        StoreBtn->OnClicked.AddDynamic(this, &URemote_FactoryWarehouseRow::OnStoreClicked);
        // 背包里没有这件东西 ⇒ 没有可存的，置灰（图标仍显示，玩家能看出"这行是仓库货"）。
        StoreBtn->SetIsEnabled(NumBackpack > 0);
        USizeBox* StoreBox = Tree->ConstructWidget<USizeBox>();
        StoreBox->SetWidthOverride(44.f);
        StoreBox->SetContent(StoreBtn);
        if (UHorizontalBoxSlot* S = H->AddChildToHorizontalBox(StoreBox))
        {
            S->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
            S->SetVerticalAlignment(VAlign_Center);
            S->SetHorizontalAlignment(HAlign_Right);
            S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        }
    }

    RowBg->SetContent(H);
    WidgetTree->RootWidget = RowBg;
    }

    if (UWidget* Root = WidgetTree->RootWidget)
    {
        return Root->TakeWidget();
    }
    return Super::RebuildWidget();
}

// =============================================================================================
// ★2026-11-05 容器行 —— 「一个容器一行」的外层行：箭头 + 容器名 + 概览（几种/共几件）
//
//   为什么要这个层级：用户指出「同一个物品可能用不同集装箱存放」。
//   按物品聚合后，玩家只能看到"铁板 1500"，却不知道这 1500 分散在哪几个箱子里，
//   取出时也无法"只从某个箱子拿"。改成每个容器一行、点箭头展开，
//   数字就是**这个容器里实际有多少**，取/存也只会作用于这个容器。
// =============================================================================================
void URemote_FactoryWarehouseContainerRow::Setup(const FString& InKey, const FString& InLabel, int32 InNumKinds,
                                                 int32 InNumItems, bool bInExpanded,
                                                 URemote_FactoryWarehousePanel* Owner)
{
    Key = InKey;
    Label = InLabel;
    NumKinds = InNumKinds;
    NumItems = InNumItems;
    bExpanded = bInExpanded;
    OwnerPanel = Owner;
}

TSharedRef<SWidget> URemote_FactoryWarehouseContainerRow::RebuildWidget()
{
    if (!WidgetTree->RootWidget)
    {
        UWidgetTree* Tree = WidgetTree;

        // 整行就是一颗按钮：点哪儿都能展开/收起，不用精确点到那个小箭头。
        UButton* RowBtn = Tree->ConstructWidget<UButton>();
        RowBtn->SetBackgroundColor(COL_TITLE_BG);   // 比物品行亮一档，层级一眼可分
        RowBtn->OnPressed.AddDynamic(this, &URemote_FactoryWarehouseContainerRow::OnToggleClicked);

        UHorizontalBox* H = Tree->ConstructWidget<UHorizontalBox>();

        // 展开箭头。★用 ASCII '>' / 'v'：Unicode 的 ▸/▾ 在 Roboto 里未必有字形，缺字会变豆腐块。
        {
            UTextBlock* Arrow = WH_MakeText(Tree, FText::FromString(bExpanded ? TEXT("v") : TEXT(">")),
                                            12, true, COL_DIM, ETextJustify::Center);
            USizeBox* ArrowBox = Tree->ConstructWidget<USizeBox>();
            ArrowBox->SetWidthOverride(14.f);   // 固定宽：展开/收起时行宽不跳
            ArrowBox->SetContent(Arrow);
            if (UHorizontalBoxSlot* S = H->AddChildToHorizontalBox(ArrowBox))
            {
                S->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
                S->SetVerticalAlignment(VAlign_Center);
                S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            }
        }

        // 容器名（Fill：名字长了也只会自己裁，不会把右边的概览挤出去）
        {
            UTextBlock* NameTxt = WH_MakeText(Tree, FText::FromString(Label), 12, true, COL_CONTAINER, ETextJustify::Left);
            if (UHorizontalBoxSlot* S = H->AddChildToHorizontalBox(NameTxt))
            {
                S->SetVerticalAlignment(VAlign_Center);
                S->SetHorizontalAlignment(HAlign_Fill);
                S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            }
        }

        // 概览「合计 3500」：合并行只显示该型号的总数量（不写具体物品，用户 2026-11-08 定案）
        {
            UTextBlock* InfoTxt = WH_MakeText(Tree,
                FText::FromString(FString::Printf(TEXT("合计 %d"), NumItems)),
                10, false, COL_DIM, ETextJustify::Right);
            if (UHorizontalBoxSlot* S = H->AddChildToHorizontalBox(InfoTxt))
            {
                S->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
                S->SetVerticalAlignment(VAlign_Center);
                S->SetHorizontalAlignment(HAlign_Right);
                S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            }
        }

        RowBtn->SetContent(H);
        WidgetTree->RootWidget = RowBtn;
    }

    if (UWidget* Root = WidgetTree->RootWidget)
    {
        return Root->TakeWidget();
    }
    return Super::RebuildWidget();
}

void URemote_FactoryWarehouseContainerRow::OnToggleClicked()
{
    if (OwnerPanel)
    {
        // ★用 Key 不用 Label：Label 可能被改成「集装箱 5 · 铁板」，拿它当展开状态的 key 会错位。
        OwnerPanel->ToggleContainerExpanded(Key);
    }
}

// 展开状态读写。★存在面板里而不是行控件里：RefreshContents 会 0.5s 重建整个列表，
// 状态放控件里会一闪一闪地复位。
void URemote_FactoryWarehousePanel::ToggleContainerExpanded(const FString& Key)
{
    bool& bOpen = ExpandedContainers.FindOrAdd(Key);
    bOpen = !bOpen;
    // 不等 0.5s 节流，立刻重建 —— 点箭头必须即时响应，否则玩家会连点两下（又切回去）。
    LastSig.Reset();
    RefreshContents();
}

bool URemote_FactoryWarehousePanel::IsContainerExpanded(const FString& Key) const
{
    const bool* bFound = ExpandedContainers.Find(Key);
    return bFound && *bFound;
}

// 点【取出】时定数量。修饰键读的是**按下这一刻**的全局状态（不是按钮自身状态），
// 和原版背包里 Shift 拆半堆 / Ctrl 整堆的操作习惯一致，玩家不用学。
// ★2026-11-05 用户定案的三档（与旧版不同，别按旧注释理解）：
//   左键        = 100（日常取用；旧版是 1，取值太碎）
//   Shift+左键  = 一组 = 该物品单格堆叠上限（电线 500、铁矿石 66…）
//   Ctrl+左键   = 全部 = 本行快照里的集装箱总量（会被 TakeAmount 再夹一次）
// ★优先级 Ctrl > Shift：同时按住按「全部」，符合"要得更多"的直觉。
// ★"全部"必须显式按 Ctrl —— 曾经把它绑在 Ctrl 之外导致误点搬空整行、物品从列表消失（用户以为丢了）。
// ★新加修饰键语义时改这里 + 改按钮 ToolTip（两处必须同步，否则玩家看不到提示）。
void URemote_FactoryWarehouseRow::OnTakeClicked()
{
    int32 Want = 100;   // 左键：100 个
    if (FSlateApplication::IsInitialized())
    {
        const FModifierKeysState Mods = FSlateApplication::Get().GetModifierKeys();
        if (Mods.IsControlDown())
        {
            // 全部：拿本行快照量。NumContainer ≤ 0 时退回 1，避免 Want=0 变成"点了没反应"。
            Want = (NumContainer > 0) ? NumContainer : 1;
        }
        else if (Mods.IsShiftDown())
        {
            // 一组 = 该物品单格堆叠上限。GetStackSize 理论上不会返回 ≤0，真返回了给 100 兜底。
            Want = (ItemCls ? UFGItemDescriptor::GetStackSize(ItemCls) : 100);
            if (Want <= 0)
            {
                Want = 100;
            }
        }
    }
    TakeAmount(Want);
}

// 发请求前做面板侧的最后一道夹取。
// ★为什么这里要夹：界面上的 NumContainer 是 0.5s 前的快照，可能已经比真实库存多（别的机器刚消耗掉了）。
//   夹到快照值至少不会请求一个明显不存在的数量；真正的权威夹取在服务端（TransferFromContainersToInventory
//   按实际扫到的量转）。两道夹取都要有 —— 只靠服务端夹，客户端会白等一次 RPC 往返。
// ★NumContainer ≤ 0（快照已经空了，或这是纯背包行）时直接返回 —— 别发一个必然取不到货的 RPC。
void URemote_FactoryWarehouseRow::TakeAmount(int32 Want)
{
    if (!OwnerPanel || !ItemCls || Want <= 0 || NumContainer <= 0)
    {
        return;
    }
    const int32 Clamped = FMath::Min(Want, NumContainer);
    if (Clamped <= 0)
    {
        return;
    }
    OwnerPanel->RequestTakeFromContainer(ItemCls, Clamped, ContainerActor.Get());
}

// ★2026-11-05【存入】修饰键语义 —— 与取出**镜像**（方向相反：取出源是集装箱，存入源是背包）。
//   左键 = 100；Shift+左键 = 一组（单格堆叠上限）；Ctrl+左键 = 背包里该物品全部。
//   优先级 Ctrl > Shift（同时按住按"全部"，与取出保持同一套直觉）。
// ★"全部"以 NumBackpack 为上限：存入的源就是背包，超过背包持有量的请求没有意义，
//   服务端也会用 Min(Want, GetNumItems) 再夹一次 —— 两道都留着，客户端先夹能少一次空 RPC。
void URemote_FactoryWarehouseRow::OnStoreClicked()
{
    int32 Want = 100;
    if (FSlateApplication::IsInitialized())
    {
        const FModifierKeysState Mods = FSlateApplication::Get().GetModifierKeys();
        if (Mods.IsControlDown())
        {
            // 全部 = 本行快照里的背包量。≤0 时退回 1，避免 Want=0 变成"点了没反应"。
            Want = (NumBackpack > 0) ? NumBackpack : 1;
        }
        else if (Mods.IsShiftDown())
        {
            Want = (ItemCls ? UFGItemDescriptor::GetStackSize(ItemCls) : 100);
            if (Want <= 0)
            {
                Want = 100;
            }
        }
    }
    StoreAmount(Want);
}

// 面板侧夹取：不超过本行显示的背包快照量（真实权威夹取在服务端）。
// ★为什么 NumBackpack ≤ 0 就直接返回：这一行可能是「集装箱有货、背包没有」的仓库行，
//   此时存入按钮本就不该有可存的东西 ⇒ 静默不请求，比发一个必然失败的 RPC 干净。
void URemote_FactoryWarehouseRow::StoreAmount(int32 Want)
{
    if (!OwnerPanel || !ItemCls || Want <= 0 || NumBackpack <= 0)
    {
        return;
    }
    const int32 Clamped = FMath::Min(Want, NumBackpack);
    if (Clamped <= 0)
    {
        return;
    }
    OwnerPanel->RequestStoreToContainer(ItemCls, Clamped, ContainerActor.Get());
}

// ★2026-11-03 拆分头：GameWorldModule 只认这个自由函数（声明在 Remote_FactoryWarehousePanel.h），
// 它再转调 UCLASS 的 RegisterWorld。这样 GameWorldModule 的编译单元完全不依赖 UHT 生成头，
// 避免首个 UCLASS 引入时 UHT 与并行编译的竞态（C2653）。
void Remote_FactoryWarehousePanel_Init(UWorld* World)
{
    URemote_FactoryWarehousePanel::RegisterWorld(World);
}

// =============================================================================================
// ★2026-11-04 对照探针运行时开关：wt.PanelCmp [on|off|N]
//
//   用法（游戏内按 ~ 打开控制台）：
//     wt.PanelCmp           → 打开，输出 240 帧（约 4 秒 @60fps）
//     wt.PanelCmp 600       → 打开，输出 600 帧（约 10 秒）
//     wt.PanelCmp off       → 关闭
//
//   ★为什么做成控制台命令而不是编译期开关：
//     用户说"这个问题我有点不太好描述"，意味着要反复试、反复看日志。
//     若每次都改代码重编（本工程全量编译约 20 秒 + 部署），一轮诊断就得几分钟，
//     而"打开背包那一瞬间"却只有 0.2 秒 —— 根本抓不过来。
//     做成命令后：进游戏 → 敲命令 → 开背包 → 直接看日志。一轮几十秒。
//
//   ★为什么默认关闭：逐帧 WARN 会刷屏且拖慢（每帧还要枚举全部 UserWidget）。
//     只在诊断时开，用完关 —— 不给日常游玩留开销。
// =============================================================================================
// ★2026-11-04 对照探针开关的实现（声明在 .h 的 public 区，供 wt.PanelCmp 调用）。
void URemote_FactoryWarehousePanel::SetCompareProbe(bool bEnable, int32 Budget)
{
    bCompareProbe = bEnable;
    CompareProbeFrame = 0;                                  // 每次开都从 f1 重新计数，便于对齐时间轴
    CompareProbeBudget = FMath::Clamp(Budget, 1, 20000);
}

static FAutoConsoleCommand GPanelCmpCmd(
    TEXT("wt.PanelCmp"),
    TEXT("Remote_Factory: toggle the side-by-side position probe that logs OUR warehouse panel and the "
         "VANILLA inventory widget on the same line every frame (inv/panel coords + delta). "
         "Usage: wt.PanelCmp [on|off|frameBudget]"),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
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
        }
        if (!World)
        {
            UE_LOG(LogRemote_Factory, Warning, TEXT("[wt.PanelCmp] no game world found; run this in-game."));
            return;
        }

        // 参数解析：无参 = on；on/off；纯数字 = 帧数（同时开启）。
        bool bEnable = true;
        int32 Budget = 240;
        if (Args.Num() > 0)
        {
            const FString A = Args[0].ToLower();
            if (A == TEXT("off") || A == TEXT("0") || A == TEXT("false"))
            {
                bEnable = false;
            }
            else if (A.IsNumeric())
            {
                Budget = FMath::Clamp(FCString::Atoi(*Args[0]), 1, 20000);
            }
        }

        // 找到所有面板实例（可能有多个世界/多个 UI，全部改）。
        // ★面板实例可能**根本不存在**（还没打开过背包）。这时候不能只是"改了 0 个"就完事 ——
        //   用户敲了命令却什么都不发生，会以为命令坏了。所以要么如实报 0 个，
        //   要么提示"先开一次背包让面板挂上再来"。这里选择如实报，并给出可操作提示。
        TArray<UUserWidget*> All;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, All, UUserWidget::StaticClass(), false);
        int32 Touched = 0;
        for (UUserWidget* Wdg : All)
        {
            URemote_FactoryWarehousePanel* Panel = Cast<URemote_FactoryWarehousePanel>(Wdg);
            if (!Panel)
            {
                continue;
            }
            Panel->SetCompareProbe(bEnable, Budget);
            ++Touched;
        }

        UE_LOG(LogRemote_Factory, Warning,
               TEXT("[wt.PanelCmp] %s (budget=%d, panels touched=%d). %s"),
               bEnable ? TEXT("ON") : TEXT("OFF"), Budget, Touched,
               bEnable
                   ? (Touched > 0
                        ? TEXT("Open the inventory now; compare [cmp] lines.")
                        : TEXT("No panel instance yet - open the inventory once, then run this command again."))
                   : TEXT(""));
    }));
