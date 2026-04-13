# ADR-001: Hybrid Architecture for Safety-Critical Edge Systems

## 1. 狀態 (Status)
已接受 (Accepted)

## 2. 背景與問題 (Context)
本專案的「電子圍籬系統」涉及馬達的緊急停止（Emergency Stop），屬於安全關鍵 (Safety-Critical) 任務。
起初評估使用單一語言（如 Node.js 或 Python）在 User Space 直接透過 GPIO 控制硬體。
然而，Node.js 運行在非即時 (Non-Real-Time) 的 Linux 作業系統上，會面臨以下風險：
* **Context Switch 延遲：** 當系統正在處理高負載任務（如戰情板 WebSocket 傳輸）時，讀取感測器的執行緒可能被 OS 暫停。
* **Garbage Collection 卡頓：** 程式清理記憶體時會產生不可預測的延遲 (Jitter)。
* **單點故障 (SPOF)：** 如果 Node.js 因為某個未捕獲的例外 (Unhandled Exception) 崩潰，馬達將失去控制。

## 3. 決策 (Decision)
我們決定採用「異質架構整合 (Hybrid Architecture Integration)」：
1. **底層 (Kernel Space):** 開發 Linux Kernel Module (`mock_sensor.c`)。利用 Kernel Timer 提供確定性 (Deterministic) 的感測器採樣，並將「距離過近強制斷電」的保命邏輯封裝在此層。
2. **上層 (User Space):** 使用 Node.js (`adapter.js`) 負責非即時的業務邏輯（如資料聚合、雲端通訊、門禁比對）。
3. **介面 (Interface):** 雙方透過定義嚴謹的 `ioctl` 系統呼叫進行通訊。

## 4. 後果 (Consequences)
* **優點 (Positive):**
  * **工安級故障隔離 (Fail-Safe):** 即使 Node.js 崩潰，Kernel Module 依然能獨立維持電子圍籬的急停機制。
  * **解耦 (Decoupling):** 上層業務邏輯與底層硬體控制完全分離，方便未來替換真實硬體（目前使用 Mock 驗證）。
* **缺點/妥協 (Negative):**
  * 開發複雜度提升，需要維護 C 語言的 Kernel Code 與 JavaScript。
  * 需要透過 `ioctl` 跨界傳遞資料，需處理指標與記憶體安全問題。