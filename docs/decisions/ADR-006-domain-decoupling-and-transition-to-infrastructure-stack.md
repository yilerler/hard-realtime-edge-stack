# ADR-006: 領域剝離與基礎設施堆疊轉型 (Domain Decoupling and Transition to Infrastructure Stack)

## 1. 狀態 (Status)
* 已接受 (Accepted) - V4.4 架構重構

## 2. 背景與問題 (Context)
* 當前 V4.3 架構的硬體抽象層 (HAL, 位於 `mock_elc_core.c`) 寫死了特定業務領域的邏輯（如 PM2.5、RFID 模擬），這違反了底層驅動程式應保持領域不可知 (Domain-Agnostic) 的設計原則。
* 基於純隨機數 (`get_random_u32`) 的資料生成方式，缺乏物理連貫性，無法準確重現工業現場中常見的「高頻電磁干擾 (EMI Spikes)」與「持續性物理崩潰 (Critical Crash)」，導致軟體在環 (SIL) 測試無法對 SMP Spinlock 機制施加真實的極限壓力。
* 以 `kernel/user` 為邊界的專案結構，將底層防禦機制與上層特定業務邏輯緊密耦合，阻礙了系統泛用化與多場景擴充。

## 3. 決策 (Decision)
* **我們決定將專案從「單一應用程式」重構為「基礎設施堆疊 (Infrastructure Stack)」：**
    * **重新命名與結構重組：** 將專案重新命名為 `hard-realtime-edge-stack`。採用 Monorepo 架構，以「系統能力」取代「執行環境」作為目錄劃分基準，切分為 `core/` (物理引擎)、`middleware/` (IPC 與網路轉發) 與 `apps/`(業務邏輯與 UI)。
    * **實作南向混沌狀態機 (Southbound Chaos FSM)：** 刪除所有業務層模擬代碼。在 Kernel 中導入 `kthread`，實作包含「穩態 (Cruising)」、「突波 (Spike)」與「崩潰 (Crash)」三相位的有限狀態機，提供純粹的高頻物理壓力訊號。

## 4. 後果 (Consequences)

* **正面：** 
    * 確立了 Stack 基礎設施的穩定性。未來新增任何工業物聯網應用 (Apps)，皆不需修改 Kernel Module 與 Middleware。
    * 透過 kthread 與狀態機，SIL 測試能精準驗證系統在惡劣物理時序下的決定性延遲 (Deterministic Latency) 與防死鎖能力。

* **負面/代價：** 
    * 部署與測試流程變長。開發者需先啟動 Core Stack，再啟動特定的 App 模組，增加了系統初始化的認知負載。