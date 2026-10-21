# 无线终端 · AI 对话 —— 双后端设计

> 状态：设计已定，代码改造待编译窗口。
> 决策：**自用走 Local Bridge，发布走 OpenAI 兼容格式（用户自填 Key）。**

---

## 一、为什么要两套后端

| 场景 | 后端 | 为什么 |
|---|---|---|
| **自用**（本机开发） | Local Bridge `127.0.0.1:8799` | 背后是 WorkBuddy，有工程上下文，能改代码/看日志/查资料 |
| **发布**（给别人玩） | OpenAI 兼容 API | 别人机器上没有 WorkBuddy，也没跑桥接服务，必须直连 LLM |

**关键认知**：自用版的价值「背后是 WorkBuddy」**无法复制给别人**——发布版只能退而求其次，连 LLM。
所以是**同一份代码，运行时二选一**，而不是两套代码。

---

## 二、配置项（决定发布形态）

放在 `mod.json` 的 `settings` 或独立的 `Config/WirelessTerminalAI.ini`。
建议用 ini，玩家改起来直观：

```ini
[AI]
; Backend = LocalBridge | OpenAICompatible
; 发布时默认值改成 OpenAICompatible；开发时留 LocalBridge。
Backend=OpenAICompatible

; ---- LocalBridge 专用 ----
LocalBridgeURL=http://127.0.0.1:8799

; ---- OpenAICompatible 专用 ----
; 任何兼容 OpenAI /chat/completions 的服务都能用：
;   DeepSeek  https://api.deepseek.com/v1          model=deepseek-chat
;   通义千问   https://dashscope.aliyuncs.com/compatible-mode/v1
;   Kimi      https://api.moonshot.cn/v1
;   OpenAI    https://api.openai.com/v1            model=gpt-4o-mini
;   本地Ollama http://localhost:11434/v1           model=qwen2.5
ApiBaseURL=https://api.deepseek.com/v1
ApiKey=
ApiModel=deepseek-chat

; 通用
MaxHistoryTurns=10
RequestTimeoutSeconds=60
```

**默认值取舍**：发布版默认填 DeepSeek（国内可直连、便宜），但 BaseURL 可改 ⇒ 等于全覆盖。

---

## 三、代码改造点（`WirelessTerminalChatWidget.cpp`）

### 3.1 后端选择

```cpp
enum class EWTChatBackend { LocalBridge, OpenAICompatible };
static EWTChatBackend ResolveBackend();   // 读 ini，默认 OpenAICompatible
static FString ResolveApiBase();          // 读 ini
static FString ResolveApiKey();           // 读 ini
static FString ResolveApiModel();         // 读 ini
```

### 3.2 `SendMessage()` 分叉

- **LocalBridge**：保持现状（`POST /chat` + 轮询 `/poll`）。
- **OpenAICompatible**：
  - 一次性 `POST {ApiBase}/chat/completions`，**带 history**（最近 N 轮）
  - body：
    ```json
    {"model":"...","messages":[{"role":"system","content":SYSTEM_PROMPT},
                               {"role":"user","content":"..."}],
     "stream":false}
    ```
  - header：`Authorization: Bearer {ApiKey}`
  - 响应解析：`choices[0].message.content`
  - **无需轮询**（LLM 秒回），直接把结果当 done 处理

### 3.3 System Prompt（发布版）

要写死一段，告诉 LLM 它的身份：

```
你是 Satisfactory（幸福工厂）游戏内的助手，通过「无线终端」mod 的对话窗口和玩家交流。
玩家是工厂建造者，可能在问：游戏机制、物品配方、工厂布局、mod 用法。
回答要简短（200 字内）、口语化、直接给结论。不确定的就说不知道，不要编造。
```

### 3.4 无 Key 时的行为

发布版玩家没填 Key → 面板显示提示：

```
尚未配置 AI 服务。请在 Config/WirelessTerminalAI.ini 里填入 ApiKey，
或改用任意兼容 OpenAI 格式的服务（DeepSeek / 通义 / Kimi / 本地 Ollama 均可）。
```

---

## 四、发布清单

- [ ] ini 默认 `Backend=OpenAICompatible`
- [ ] zip 里带上 `Config/WirelessTerminalAI.ini`（Key 留空）
- [ ] 说明书写清「怎么申请 Key、怎么填」
- [ ] **不要打包 LocalBridge 的默认地址为 localhost**（对玩家无意义），但保留该选项
- [ ] 确认 API Key **不会**被写进日志（`WT_LOGF` 里绝不打印 Key）

---

## 五、风险与对策

| 风险 | 对策 |
|---|---|
| 玩家 Key 泄露 | Key 只存本地 ini；不写日志；不走我们的服务器（直连厂商） |
| 玩家不会申请 Key | 说明书附 DeepSeek 申请步骤（最简单：手机号注册即送额度） |
| 厂商 API 变动 | 只用 `/chat/completions` 这个最稳定的通用端点 |
| 请求超时 | 60s 超时 + 界面显示「请求中…」，失败给明确错误 |
| 中文乱码 | body 显式 UTF-8（当前已手写 JSON 转义，需确保 `SetContentAsString` 的编码正确） |
