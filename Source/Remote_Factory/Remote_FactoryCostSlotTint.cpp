// ★2026-11-02 成本格蓝色染色（智能仓储来源指示）。详见 Remote_FactoryCostSlotTint.h 头部说明。
// 简化版（用户定案「无列表，直接从集装箱中取用」）：物品始终留在集装箱，成本格读【集装箱可用量】
// （Remote_FactorySmartWarehouse::GetContainerAvailability）把来自智能仓储的物品格染蓝，与云端紫区分。
//
// ★2026-11-03 重写（一手日志铁证定位 bug）：
//   旧版 ReadItemDescriptorClass 读 `mItemCost` 当「类/对象」属性，但日志 diag 显示 mItemCost = (StructProperty)
//   → Cls 永远 None → 每个成本格被 continue 跳过 → 永远不蓝。真实物品来源属性是 `itemDescriptor`
//   （或 mItemCost 是 FItemAmount 结构体，内含 ItemClass 字段）。本版按一手属性名重写解析。
//   同时把扫描范围从单一 Widget_CostSlotWrapper 扩展到 CostSlot 全变体（HUD/建造菜单/配方成本/中央存储面板），
//   实现「凡原版云端显示紫色的位置全挂蓝」。
#include "Remote_FactoryCostSlotTint.h"

#include "Containers/Ticker.h"            // FTSTicker::GetCoreTicker / FTickerDelegate
#include "Engine/World.h"                 // UWorld / NetMode
#include "UObject/UnrealType.h"           // FProperty / FClassProperty / FObjectProperty / FStructProperty / FIntProperty
#include "Blueprint/UserWidget.h"         // UUserWidget::GetRootWidget / WidgetTree
#include "Blueprint/WidgetTree.h"         // UWidgetTree::GetAllWidgets
#include "Blueprint/WidgetBlueprintLibrary.h" // UWidgetBlueprintLibrary::GetAllWidgetsOfClass
#include "Components/PanelWidget.h"       // UPanelWidget::AddChild
#include "Components/Border.h"            // UBorder::SetBrush
#include "Components/Image.h"            // ★UImage::SetColorAndOpacity/SetBrushTintColor（只染数字角标底 mTextBG）
#include "Components/OverlaySlot.h"       // UOverlaySlot::SetHorizontalAlignment / SetPadding
#include "Components/GridSlot.h"          // UGridSlot::SetHorizontalAlignment / SetVerticalAlignment
#include "Components/TextBlock.h"         // UTextBlock::GetText / GetColorAndOpacity / SetColorAndOpacity（数量文字改色）
#include "Remote_FactorySmartWarehouse.h" // Remote_FactorySmartWarehouse::GetContainerAvailability (集装箱可用量)
#include "Resources/FGItemDescriptor.h"  // UFGItemDescriptor（TSubclassOf 键，完整类型安全）
#include "FGInventoryComponent.h"        // UFGInventoryComponent::GetItemClassAtIndex (按槽位取物品类)
#include "FGCharacterPlayer.h"           // AFGCharacterPlayer::GetInventory()（背包三源比大小之一）
#include "GameFramework/PlayerController.h" // APlayerController::GetPawn / UWorld::GetFirstPlayerController

namespace
{
    static TWeakObjectPtr<UWorld> GCostTintWorld;
    static bool GbCostTintRegistered = false;
    // ★2026-11-03 改：不再整格铺蓝 UBorder（会把图标也盖蓝），改为只染「数字角标底」mTextBG。
    //   槽内 mTextBG 是 UImage（深色圆角角标底 = 数字栏背景），缓存其原 ColorAndOpacity + brush tint，
    //   蓝时改成蓝、撤时还原（支持成本格复用/回收、且不污染原版配色）。
    struct FTextBgState
    {
        TWeakObjectPtr<UImage> Img;                                   // 槽内 mTextBG 那个 UImage
        FLinearColor OrigColor = FLinearColor::White;                 // 原 ColorAndOpacity
        FSlateColor  OrigTint  = FSlateColor(FLinearColor::White);    // 原 brush tint
        bool bCaptured = false;                                       // 是否已捕获原色
        bool bCurBlue  = false;                                       // 当前是否已染蓝（仅变化时打日志）
    };
    static TMap<TWeakObjectPtr<UUserWidget>, FTextBgState> GTextBgStates;
    // 一次性诊断：打印成本格 widget 的真实属性名（铁律：一手锁定，不靠猜）。
    static bool GbDiagDone = false;

    // ★2026-11-03 扩展（2026-11-03 再扩）：不再只认 3 个已知 cost-slot 类，改为**按属性签名**扫描世界上所有
    // user widget——凡带 itemDescriptor / mItemCost / mFejkInventoryComponent 属性者即视为成本格。
    // 这样建造 / MAM 研究 / 制作台制作 的成本格（即便用了不同的蓝图子类或嵌套方式）全部覆盖，不漏。
    static void CollectCostSlotWidgets(UWorld* W, TArray<UUserWidget*>& Out)
    {
        // 没有无参 GetAllWidgets；用 GetAllWidgetsOfClass(UUserWidget) 取世界上全部 user widget
        // （全部都是 UUserWidget 子类），再按属性签名筛出成本格，建造/MAM/制作台通吃。
        TArray<UUserWidget*> All;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(W, All, UUserWidget::StaticClass(), /*TopLevelOnly=*/false);
        for (UUserWidget* Cand : All)
        {
            if (!Cand) continue;
            UClass* WC = Cand->GetClass();
            if (WC->FindPropertyByName(FName("itemDescriptor")) ||
                WC->FindPropertyByName(FName("mItemCost")) ||
                WC->FindPropertyByName(FName("mFejkInventoryComponent")))
                Out.AddUnique(Cand);
        }
    }

    // ★2026-11-03 重写：按一手属性名解析成本格绑定的物品类（UFGItemDescriptor 子类）。
    // 顺序：① itemDescriptor（最直接，日志确认存在）② mItemCost 结构体(FItemAmount) 内的 ItemClass
    //       ③ mFejkInventoryComponent(库存组件) + mCurrentNumInSlot(槽位索引) 用引擎 API 取。
    static UClass* ReadItemDescriptorClass(UUserWidget* W)
    {
        if (!W) return nullptr;
        UClass* Cls = nullptr;

        // ① itemDescriptor（可能是 TSubclassOf<UFGItemDescriptor> 或 UFGItemDescriptor* 实例）
        if (FProperty* P = W->GetClass()->FindPropertyByName(FName("itemDescriptor")))
        {
            if (FClassProperty* CP = CastField<FClassProperty>(P))
            {
                if (UObject* V = CP->GetObjectPropertyValue_InContainer(W))
                    Cls = Cast<UClass>(V);
            }
            else if (FObjectProperty* OP = CastField<FObjectProperty>(P))
            {
                if (UObject* V = OP->GetObjectPropertyValue_InContainer(W))
                {
                    Cls = Cast<UClass>(V);   // 若是类引用
                    if (!Cls)
                        Cls = V->GetClass(); // 若是 descriptor 实例
                }
            }
        }
        if (Cls) return Cls;

        // ② mItemCost = FItemAmount 结构体，内含 ItemClass 字段（TSubclassOf<UFGItemDescriptor>）
        if (FProperty* P = W->GetClass()->FindPropertyByName(FName("mItemCost")))
        {
            if (FStructProperty* SP = CastField<FStructProperty>(P))
            {
                if (FProperty* Inner = SP->Struct->FindPropertyByName(FName("ItemClass")))
                {
                    if (FClassProperty* ICP = CastField<FClassProperty>(Inner))
                    {
                        void* SPtr = SP->ContainerPtrToValuePtr<void>(W);
                        if (UObject* V = ICP->GetObjectPropertyValue_InContainer(SPtr))
                            Cls = Cast<UClass>(V);
                    }
                }
            }
        }
        if (Cls) return Cls;

        // ③ 兜底：伪造库存组件 + 槽位索引
        FProperty* PC = W->GetClass()->FindPropertyByName(FName("mFejkInventoryComponent"));
        UObject* InvObj = nullptr;
        if (FObjectProperty* OP = CastField<FObjectProperty>(PC))
            InvObj = OP->GetObjectPropertyValue_InContainer(W);
        UFGInventoryComponent* Inv = Cast<UFGInventoryComponent>(InvObj);
        if (Inv)
        {
            int32 Slot = 0;
            if (FProperty* PS = W->GetClass()->FindPropertyByName(FName("mCurrentNumInSlot")))
                if (FIntProperty* IP = CastField<FIntProperty>(PS))
                    Slot = IP->GetPropertyValue_InContainer(W);
            TSubclassOf<UFGItemDescriptor> ItemCls = Inv->GetItemClassAtIndex(Slot);
            Cls = ItemCls.Get();
        }
        return Cls;
    }

    // ★2026-11-03 蓝值定案：UE 的 FLinearColor 收的是**线性色**，若直接把 hex 的 0~1 数值填进去，
    //   屏幕上会明显偏深（hex 是 sRGB 编码的）。正确做法 = 走 sRGB -> linear 查表换算：
    //   FLinearColor::FromSRGBColor(FColor(R,G,B))（Color.h:123 → 构造函数 :790 查 sRGBToLinearTable）。
    //   候选档位（同一色相，由亮到深）：
    //     0x4F,0xC3,0xF7  4FC3F7  亮天蓝，深色底上最清楚、与橙黄分得最开
    //     0x29,0xB6,0xF6  29B6F6  深一档（默认值，对应用户「深蓝色」口径）
    //     0x02,0x88,0xD1  0288D1  再深一档（最沉稳，但深底上对比略弱）
    static FLinearColor GetBadgeBlue()
    {
        return FLinearColor::FromSRGBColor(FColor((uint8)0x29, (uint8)0xB6, (uint8)0xF6, (uint8)0xFF));
    }

    // 深层递归找槽内名为 mTextBG 的 UImage（数字角标圆角深底 = 用户要染的「数字栏」）。
    // 它可能在嵌套的 Widget_InventorySlot_C 里，故需递归进子 UUserWidget 的 WidgetTree 与面板子节点。
    static UImage* FindTextBGImage(UWidget* Root, int32 Depth)
    {
        if (!Root || Depth < 0) return nullptr;
        if (Root->GetName() == TEXT("mTextBG"))
        {
            if (UImage* I = Cast<UImage>(Root)) return I;
        }
        if (UUserWidget* UW = Cast<UUserWidget>(Root))
        {
            if (UW->WidgetTree && UW->WidgetTree->RootWidget)
                return FindTextBGImage(UW->WidgetTree->RootWidget, Depth - 1);
        }
        else if (UPanelWidget* P = Cast<UPanelWidget>(Root))
        {
            for (int32 i = 0; i < P->GetChildrenCount(); ++i)
                if (UImage* R = FindTextBGImage(P->GetChildAt(i), Depth - 1)) return R;
        }
        return nullptr;
    }

    // 只染数字角标底（mTextBG），不碰整格/图标。开=蓝，关=还原原色。
    static void UpdateTextBgTint(UUserWidget* W, bool bBlue)
    {
        if (!W) return;
        FTextBgState& St = GTextBgStates.FindOrAdd(W);
        if (!St.Img.IsValid())
        {
            St.Img = FindTextBGImage(W, /*Depth=*/6);
            if (St.Img.IsValid())
            {
                St.OrigColor = St.Img->GetColorAndOpacity();
                St.OrigTint  = St.Img->GetBrush().TintColor;
                St.bCaptured = true;
            }
        }
        if (!St.Img.IsValid())
        {
            return;   // 槽里没有 mTextBG（非成本格/结构变了）→ 不动，绝不铺整格
        }
        if (bBlue == St.bCurBlue)
        {
            return;   // 状态没变，跳过（避免每 0.5s 重复设色/刷日志）
        }
        St.bCurBlue = bBlue;
        if (bBlue)
        {
            // 蓝：color 与 brush tint 同时改（角标底深色可能来自 brush，单改 color 会被盖成暗蓝）。
            const FLinearColor Blue = GetBadgeBlue();
            St.Img->SetColorAndOpacity(Blue);
            St.Img->SetBrushTintColor(FSlateColor(Blue));
            UClass* ItemCls = ReadItemDescriptorClass(W);
            UE_LOG(LogTemp, Warning, TEXT("[cost-tint] badge-blue applied: %s (%s)"),
                   ItemCls ? *ItemCls->GetName() : TEXT("?"), *W->GetClass()->GetName());
        }
        else if (St.bCaptured)
        {
            St.Img->SetColorAndOpacity(St.OrigColor);
            St.Img->SetBrushTintColor(St.OrigTint);
        }
    }

    static void CostTintTick(UWorld* W)
    {
        if (!W) return;

        // 客户端可用量刷新（自带「关→清空」门控），覆盖专用服务器与单机两种部署。
        {
            static double GLastAvailRefresh = -999.0;
            const double NowSec = W->GetTimeSeconds();
            if (NowSec - GLastAvailRefresh > 1.0)
            {
                GLastAvailRefresh = NowSec;
                Remote_FactorySmartWarehouse::RefreshContainerAvailability(W);
                static bool bLogged = false;
                if (!bLogged)
                {
                    bLogged = true;
                    const TMap<TSubclassOf<UFGItemDescriptor>, int32>& SWNow =
                        Remote_FactorySmartWarehouse::GetContainerAvailability();
                    UE_LOG(LogTemp, Warning,
                            TEXT("[cost-tint] client availability refreshed (enabled=%d): %d item types"),
                            Remote_FactorySmartWarehouse::IsSmartWarehouseEnabled() ? 1 : 0, SWNow.Num());
                }
            }
        }

        TArray<UUserWidget*> Widgets;
        CollectCostSlotWidgets(W, Widgets);
        if (Widgets.Num() == 0) return; // 世界上还没有成本格，下个 tick 重试

        // 一次性诊断：打印成本格 widget 的真实属性名 + 关键属性真值（供一手锁定物品类来源，不靠猜）。
        if (!GbDiagDone)
        {
            GbDiagDone = true;
            if (UUserWidget* S0 = Widgets[0])
            {
                FString Names;
                for (FProperty* P = S0->GetClass()->PropertyLink; P; P = P->PropertyLinkNext)
                    Names += FString::Printf(TEXT(" %s"), *P->GetName());
                UE_LOG(LogTemp, Warning, TEXT("[cost-tint] diag widget props:%s"), *Names);

                auto DumpProp = [&](const TCHAR* Name)
                {
                    FProperty* P = S0->GetClass()->FindPropertyByName(FName(Name));
                    if (!P) { UE_LOG(LogTemp, Warning, TEXT("[cost-tint] diag %s = <not found>"), Name); return; }
                    if (FClassProperty* CP = CastField<FClassProperty>(P))
                    {
                        UObject* V = CP->GetObjectPropertyValue_InContainer(S0);
                        UE_LOG(LogTemp, Warning, TEXT("[cost-tint] diag %s = class %s"), Name, V ? *V->GetName() : TEXT("null"));
                    }
                    else if (FObjectProperty* OP = CastField<FObjectProperty>(P))
                    {
                        UObject* V = OP->GetObjectPropertyValue_InContainer(S0);
                        UE_LOG(LogTemp, Warning, TEXT("[cost-tint] diag %s = object %s"), Name, V ? *V->GetName() : TEXT("null"));
                    }
                    else if (FStructProperty* SP = CastField<FStructProperty>(P))
                    {
                        UE_LOG(LogTemp, Warning, TEXT("[cost-tint] diag %s = (StructProperty %s)"), Name, *SP->Struct->GetName());
                    }
                    else if (FIntProperty* IP = CastField<FIntProperty>(P))
                    {
                        UE_LOG(LogTemp, Warning, TEXT("[cost-tint] diag %s = int %d"), Name, IP->GetPropertyValue_InContainer(S0));
                    }
                    else if (FBoolProperty* BP = CastField<FBoolProperty>(P))
                    {
                        UE_LOG(LogTemp, Warning, TEXT("[cost-tint] diag %s = bool %d"), Name, BP->GetPropertyValue_InContainer(S0) ? 1 : 0);
                    }
                    else
                    {
                        UE_LOG(LogTemp, Warning, TEXT("[cost-tint] diag %s = (%s)"), Name, *P->GetClass()->GetName());
                    }
                };
                DumpProp(TEXT("itemDescriptor"));
                DumpProp(TEXT("mItemCost"));
                DumpProp(TEXT("mCachedInventoryComponent"));
                DumpProp(TEXT("mFejkInventoryComponent"));
                DumpProp(TEXT("mOverwriteComponentNumInSlot"));
                DumpProp(TEXT("mCurrentNumInSlot"));
                UE_LOG(LogTemp, Warning, TEXT("[cost-tint] diag resolved item class = %s"), *GetNameSafe(ReadItemDescriptorClass(S0)));
            }
        }

        // ★2026-11-03 诊断②：dump 成本格子树里所有 TextBlock 的名字 + 文本 + 当前颜色。
        // 目的：用户要「把成本格的数量文字从紫色改成蓝色」，但颜色逻辑在蓝图里（无 C++ SDK 类），
        // 必须先在运行时确认那个数量 TextBlock 叫什么、当前是什么颜色，才能写定向改色（不靠猜）。
        static bool GbTextDiagDone = false;
        if (!GbTextDiagDone)
        {
            GbTextDiagDone = true;
            if (UUserWidget* S0 = Widgets[0])
            {
                TArray<UWidget*> AllW;
                if (S0->WidgetTree)
                    S0->WidgetTree->GetAllWidgets(AllW);
                UE_LOG(LogTemp, Warning, TEXT("[cost-tint] diag textblocks in slot '%s' (total widgets=%d):"),
                       *S0->GetClass()->GetName(), AllW.Num());
                for (UWidget* CW : AllW)
                {
                    UTextBlock* TB = Cast<UTextBlock>(CW);
                    if (!TB) continue;
                    const FLinearColor Cur = TB->GetColorAndOpacity().GetSpecifiedColor();
                    UE_LOG(LogTemp, Warning,
                           TEXT("[cost-tint]   TB name=%s text='%s' color=(%.3f,%.3f,%.3f,%.3f)"),
                           *TB->GetName(), *TB->GetText().ToString(),
                           Cur.R, Cur.G, Cur.B, Cur.A);
                }
            }
        }

        // 三源存量（客户端各自的缓存）：
        //   ① 背包（玩家 mInventory）② 位面云端（原版 mStoredItems 真实量）③ 集装箱（本模块缓存）。
        // 用户 2026-11-03 定案：成本格数字栏背景色 = 三者中**数量最多**的那个来源的颜色
        //   （背包橙 / 位面紫 = 原版色，集装箱蓝 = 本模块叠加层）。
        // 语义：只有集装箱量最大时才叠蓝；背包或位面更多时撤掉蓝，露原版颜色。
        const TMap<TSubclassOf<UFGItemDescriptor>, int32>& SW =
            Remote_FactorySmartWarehouse::GetContainerAvailability();
        const TMap<TSubclassOf<UFGItemDescriptor>, int32>& CloudReal =
            Remote_FactorySmartWarehouse::GetCentralStorageRealCount();

        // 玩家背包（UI 只在客户端跑，取本地玩家角色）。
        UFGInventoryComponent* PlayerInv = nullptr;
        if (APlayerController* PC = W->GetFirstPlayerController())
        {
            if (AFGCharacterPlayer* Char = Cast<AFGCharacterPlayer>(PC->GetPawn()))
                PlayerInv = Char->GetInventory();
        }

        // 清理已销毁成本格的残留状态（菜单开合会重建槽，旧 widget 弱引用失效）。
        {
            TArray<TWeakObjectPtr<UUserWidget>> Dead;
            for (const TPair<TWeakObjectPtr<UUserWidget>, FTextBgState>& KV : GTextBgStates)
                if (!KV.Key.IsValid() || !KV.Value.Img.IsValid())
                    Dead.Add(KV.Key);
            for (const TWeakObjectPtr<UUserWidget>& K : Dead)
                GTextBgStates.Remove(K);
        }

        int32 FoundCnt = 0, BlueCnt = 0, NoClassCnt = 0;
        for (UUserWidget* Slot : Widgets)
        {
            if (!Slot) continue;
            UClass* ItemCls = ReadItemDescriptorClass(Slot);
            if (!ItemCls) { ++NoClassCnt; continue; }
            ++FoundCnt;

            const TSubclassOf<UFGItemDescriptor> Key(ItemCls);
            const int32 NumContainer = SW.FindRef(Key);
            const int32 NumCloud     = CloudReal.FindRef(Key);
            const int32 NumBackpack  = PlayerInv ? PlayerInv->GetNumItems(Key) : 0;

            // 三源比大小：集装箱严格最多才染蓝（并列时不算，保持原版观感）。
            // 依据消费顺序 背包→位面→智能仓储（用户 2026-11-03 定案）：背包/位面会优先被扣，
            // 仅当集装箱量严格多于二者时，才主要靠智能仓储供应该物品 => 标蓝。
            const bool bBlue = (NumContainer > 0)
                            && (NumContainer > NumCloud)
                            && (NumContainer > NumBackpack);
            if (bBlue) ++BlueCnt;
            UpdateTextBgTint(Slot, bBlue);
        }

        // 一次性统计：实证覆盖（建造/MAM/制作台 成本格是否都被扫到、是否染蓝）。
        static bool bSummaryLogged = false;
        if (!bSummaryLogged && FoundCnt > 0)
        {
            bSummaryLogged = true;
            UE_LOG(LogTemp, Warning,
                   TEXT("[cost-tint] summary: scanned=%d blue=%d noItemClass=%d (smart-warehouse enabled=%d)"),
                   FoundCnt, BlueCnt, NoClassCnt,
                   Remote_FactorySmartWarehouse::IsSmartWarehouseEnabled() ? 1 : 0);
        }

        UE_LOG(LogTemp, VeryVerbose, TEXT("[cost-tint] tick: %d slots scanned, %d blue"), Widgets.Num(), BlueCnt);
    }
}

void RemoteFactoryCostSlotTint::Register(UWorld* World)
{
    if (!World || GbCostTintRegistered) return;
    if (World->GetNetMode() == NM_DedicatedServer) return; // UI 仅客户端
    GbCostTintRegistered = true;
    GCostTintWorld = World;

    TWeakObjectPtr<UWorld> WeakWorld(World);
    FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateLambda([WeakWorld](float) -> bool
        {
            UWorld* W = WeakWorld.Get();
            if (!W) return false;                 // 世界销毁 -> 停止
            if (W->GetNetMode() == NM_DedicatedServer) return true;
            CostTintTick(W);
            return true;                          // 持续轮询
        }), 0.5f);

    UE_LOG(LogTemp, Warning, TEXT("[cost-tint] registered client ticker (0.5s)"));
}
