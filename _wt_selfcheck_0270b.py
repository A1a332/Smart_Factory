# -*- coding: utf-8 -*-
# 3+3 自检（扩展）：0.2.70 修订 = 距离拉到最大 + 纯文件驱动 + 列表实时距离
#   快速 R1-R3：结构配平 / 编码基线 / 符号一致性
#   深度 R4-R6：近邻匹配行为模拟(含防自证反例) / 真实1764数据全量模拟 / 调用链
#   快速 R7 + 深度 R8-R10：实时距离特性
import re, json, math, sys

CPP = r"Source/Remote_Factory/Remote_FactoryPanelWidget.cpp"
H   = r"Source/Remote_Factory/Remote_FactoryPanelWidget.h"
src_b = open(CPP, "rb").read(); src = src_b.decode("utf-8-sig")
h_b   = open(H, "rb").read();   h   = h_b.decode("utf-8-sig")
ok = True

# R1 结构配平（剔除注释/字符串后净深度为 0，无提前负深度）
t = re.sub(rb'//[^\n]*', b'', src_b); t = re.sub(rb'/\*.*?\*/', b'', t, flags=re.S)
t = re.sub(rb'"(?:\\.|[^"\\])*"', b'""', t)
depths = {"{":0,"(":0,"[":0}; pairs = {"}":"{",")":"(","]":"["}; neg = False
for ch in t.decode("utf-8"):
    if ch in depths: depths[ch]+=1
    elif ch in pairs:
        depths[pairs[ch]]-=1
        if depths[pairs[ch]]<0: neg=True
r1 = (not neg) and all(v==0 for v in depths.values())
print("R1 brace-balance:", "PASS" if r1 else "FAIL", depths); ok &= r1

# R2 编码基线（BOM 与基线一致 / UTF-8 可解码 / 无 CRLF）
try: src_b.decode("utf-8-sig"); dec=True
except: dec=False
r2 = src_b[:3]==b"\xef\xbb\xbf" and dec and b"\r\n" not in src_b
print("R2 encoding(BOM/decode/noCRLF):", "PASS" if r2 else "FAIL"); ok &= r2

# R3 符号一致性（pass2 纯文件驱动）
checks = [
    (b"E.DistanceMeters = Live.DistanceMeters;" not in src_b, "距离不再被活体值覆盖"),
    (b"DisplayCollectibles.Add(Live);" not in src_b, "不再追加活体行(纯文件驱动)"),
    (b"E.bCollected = Live.bCollected;" in src_b, "仍叠加已拾取状态"),
    (b"E.Actor = Live.Actor;" in src_b, "仍叠加 Actor 句柄"),
    (b"1500.f * 1500.f" in src_b, "近邻半径 1500cm 生效"),
    (src_b.count(b"BestIdx = INDEX_NONE;")==1, "BestIdx 初始化恰一次"),
    (src_b.count(b"GetOwningPlayerPawn()")>=1, "GetOwningPlayerPawn 使用"),
    (b'#include "GameFramework/Pawn.h"' in src_b, "Pawn.h include"),
]
r3 = all(c[0] for c in checks)
for v,n in checks: print("R3", n, "PASS" if v else "FAIL")
ok &= r3

# R4 行为模拟：近邻匹配（坐标 cm，R=1500）
def pass2(file_pts, live_pts, R=1500.0):
    merged, appended = set(), []
    R2 = R*R
    for lk, lx, ly, lz in live_pts:
        best, bd = None, R2
        for (fi, fk, fx, fy, fz) in file_pts:
            if fk != lk: continue
            d=(fx-lx)**2+(fy-ly)**2+(fz-lz)**2
            if d<bd: bd,best=d,fi
        if best is not None: merged.add(best)
        else: appended.append(lk)
    return merged, appended
m,a = pass2([(0,0,0,0,0)], [(0,1600,0,0)]); r4a=(m==set())
m,a = pass2([(0,0,0,0,0),(1,0,840,0,0)], [(0,300,0,0)]); r4b=(m=={0})
m,a = pass2([(0,0,0,0,0),(1,0,840,0,0)], [(0,300,0,0),(0,540,0,0)]); r4c=(m=={0,1})
m,a = pass2([(0,0,0,0,0)], [(0,300,0,0)], R=200.0); r4d=(m==set())  # 旧半径必漏(防自证)
r4 = r4a and r4b and r4c and r4d
print("R4 近邻匹配(4反例):", "PASS" if r4 else "FAIL", (r4a,r4b,r4c,r4d)); ok &= r4

# R5 真实 1764 数据全量模拟（无追加）
d = json.load(open("collectibles.json"))
byk={}
for i,s in enumerate(d["spawns"]): byk.setdefault(s["kind"],[]).append((i,s["x"],s["y"],s["z"]))
file_all=[(i,k,x,y,z) for k,pp in byk.items() for (i,x,y,z) in pp]
app=mer=live=0
for k,pts in byk.items():
    for (i,x,y,z) in pts:
        if x*x+y*y>40000.0**2: continue
        live+=1; m,a=pass2(file_all,[(k,x+300,y-200,z)]); mer+=len(m); app+=len(a)
r5=(app==0) and (mer==live)
print("R5 真实数据模拟 live=%d merged=%d appended=%d:"%(live,mer,app),"PASS" if r5 else "FAIL"); ok &= r5

# R6 调用链：RebuildDisplayCollectibles 定义1 + 调用2
defs=src.count("void URemote_FactoryPanelWidget::RebuildDisplayCollectibles()")
calls=len(re.findall(r"RebuildDisplayCollectibles\(\)", src))
r6=(defs==1) and (calls==3)
print("R6 调用链 定义=%d 调用=%d:"%(defs,calls-1),"PASS" if r6 else "FAIL"); ok &= r6

# R7 实时距离 — 快速符号一致性（cpp + h）
h_checks=[
    (b"UTextBlock** OutLabel = nullptr" in h_b, "h: AddListButton 暴露 OutLabel"),
    (b"void UpdateCollectibleDistances();" in h_b, "h: UpdateCollectibleDistances 声明"),
    (b"TArray<TObjectPtr<class UTextBlock>> CollectibleRowTexts;" in h_b, "h: CollectibleRowTexts 成员"),
    (b"TArray<int32> CollectibleRowSrcIdx;" in h_b, "h: CollectibleRowSrcIdx 成员"),
    (b"float DistRefreshAccum = 0.f;" in h_b, "h: DistRefreshAccum 成员"),
]
c_checks=[
    (b"static FText WTBuildCollectibleRowText(const FWT_CollectibleEntry& C)" in src_b, "cpp: 辅助函数定义"),
    (src.count("WTBuildCollectibleRowText(")>=3, "cpp: 辅助函数被调用(定义+2处)"),
    (b"CollectibleRowTexts.Reset();" in src_b and b"CollectibleRowSrcIdx.Reset();" in src_b, "cpp: 重建前重置数组"),
    (b"UpdateCollectibleDistances();" in src_b, "cpp: NativeTick 调用"),
    (b"DistRefreshAccum >= 0.1f" in src_b, "cpp: 0.1s 节流派"),
    (b"CollectibleRowTexts.Add(RowLabelText);" in src_b and b"CollectibleRowSrcIdx.Add(i);" in src_b, "cpp: 捕获文本块+源索引"),
    (src_b.count(b"void URemote_FactoryPanelWidget::UpdateCollectibleDistances()")==1, "cpp: UpdateCollectibleDistances 定义恰一次"),
]
r7 = all(x[0] for x in h_checks) and all(x[0] for x in c_checks)
for v,n in h_checks: print("R7[h]", n, "PASS" if v else "FAIL")
for v,n in c_checks: print("R7[c]", n, "PASS" if v else "FAIL")
ok &= r7

# R8 深度：WT_LOCTEXT("ColRow" / "ColState_") 定义唯一（防重复宏定义），且只出现在辅助函数
colrow_defs = src.count('WT_LOCTEXT("ColRow"')
r8a = colrow_defs == 1
# 行文本构造只出现在 WTBuildCollectibleRowText 内（不在 RebuildMachineList 内联）
inline_rowlabel = src.count("const FText RowLabel = FText::Format(\n                WT_LOCTEXT(\"ColRow\"")
r8b = inline_rowlabel == 0
r8 = r8a and r8b
print("R8 辅助函数唯一性(ColRow定义=%d, 无内联残留=%s):"%(colrow_defs, r8b),"PASS" if r8 else "FAIL"); ok &= r8

# R9 深度：UpdateCollectibleDistances 公式与 pass1 一致（FVector::Dist/100），且用 helper 重设文本
u = src[src.find("void URemote_FactoryPanelWidget::UpdateCollectibleDistances()"):]
r9a = "FVector::Dist(PlayerLoc, E.Location) / 100.f" in u
r9b = "T->SetText(WTBuildCollectibleRowText(E));" in u
r9c = "E.DistanceMeters =" in u
r9 = r9a and r9b and r9c
print("R9 UpdateCollectibleDistances(距离公式=%s, helper重设文本=%s, 写回DistanceMeters=%s):"%(r9a,r9b,r9c),"PASS" if r9 else "FAIL"); ok &= r9

# R10 深度：NativeTick 内调用 UpdateCollectibleDistances 且 DistRefreshAccum 重置（非每帧全量）
nt = src[src.find("void URemote_FactoryPanelWidget::NativeTick"):src.find("void URemote_FactoryPanelWidget::BindRCO")]
r10a = "UpdateCollectibleDistances();" in nt
r10b = "DistRefreshAccum = 0.f;" in nt
r10c = "DistRefreshAccum += InDeltaTime;" in nt
r10 = r10a and r10b and r10c
print("R10 NativeTick 接线(调用=%s, 重置=%s, 累加=%s):"%(r10a,r10b,r10c),"PASS" if r10 else "FAIL"); ok &= r10

# R11 深度：距离格式 <1km 用米 / >=1km 用 km，且旧纯米格式已移除
r11a = b"%.0fm" in src_b                       # <1km 整数米
r11b = b"%.1fkm" in src_b                      # >=1km 一位小数 km
r11c = b"FText::FromString(DistStr)" in src_b   # 距离串经 FText 包装
r11d = b"M < 1000.f" in src_b                  # 阈值 1km
r11e = b"FText::AsNumber(FMath::RoundToInt(C.DistanceMeters))" not in src_b  # 旧纯米格式已删
r11 = r11a and r11b and r11c and r11d and r11e
print("R11 距离格式(<1km米=%s, >=1km km=%s, DistStr=%s, 阈值=%s, 旧格式已删=%s):"%(r11a,r11b,r11c,r11d,r11e),"PASS" if r11 else "FAIL"); ok &= r11

print("ALL ROUNDS:", "ALL PASS" if ok else "HAS FAIL")
sys.exit(0 if ok else 1)
