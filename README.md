# 🛡️ Hard Real-Time Edge Safety Gateway (V5.0)
**基於 Linux 核心 Spinlock 與 IT/OT 解耦架構的工業級硬即時安全閘道器**

![Linux Kernel](https://img.shields.io/badge/Linux_Kernel-Module-FCC624?style=for-the-badge&logo=linux&logoColor=black)
![Node.js Middleware](https://img.shields.io/badge/Node.js-Middleware-339933?style=for-the-badge&logo=node.js&logoColor=white)
![Systemd](https://img.shields.io/badge/Systemd-Zero_Touch-blue?style=for-the-badge)
![SRE](https://img.shields.io/badge/SRE-72h_Burn--in-success?style=for-the-badge&color=success)

## 📖 Executive Summary (執行摘要)
本專案為一個概念驗證 (PoC) 的邊緣運算基礎設施，旨在解決傳統工業物聯網 (IIoT) 中「將硬體防禦邏輯與雲端業務邏輯混雜於應用層 (User Space)」所導致的嚴重延遲與可靠性災難。

透過 **IT/OT 本質性解耦**，本系統將高頻物理訊號的過濾與急停防禦 (Interlock) 下放至 Linux 核心層 (Kernel Space)，確保微秒級的**決定性延遲 (Deterministic Latency)**；同時保留 Node.js 作為無狀態的中介層 (Stateless Middleware)，負責向戰情室提供零阻塞的 JSON 狀態廣播，達成兼具「硬即時防禦」與「雲端大數據觀測」的現代化邊緣架構。

---

## 🏗️ Architecture Evolution (架構演進與痛點解決)

### ❌ 舊架構痛點 (The Soft Real-Time Trap)
在早期的 V4.0 以前版本，系統採用 Node.js 於 User Space 直接輪詢感測器。
* **致命缺陷：** 當系統遭遇極限 I/O 壓力或網路阻塞時，Node.js 的 Event Loop (事件迴圈) 會產生高達數百毫秒的延遲。這種「軟即時 (Soft Real-Time)」特質，在工業現場將導致機台無法及時煞車而撞毀。

### ✅ V5.0 革命：IT/OT 徹底解耦 (Domain Decoupling)
* **OT 控制面 (大腦與脊髓)：** 寫入 Linux Kernel。透過 `kthread` 以 1000Hz 頻率運作，直接在底層過濾 EMI 突波並執行實體斷電，完全不受上層作業系統排程干擾。
* **IT 觀測面 (外交官)：** 透過定義嚴謹的 ABI (Application Binary Interface) 記憶體合約，Node.js 降級為純粹的觀測者，僅負責讀取記憶體狀態並推播至 WebSocket。

---

## 🚀 Core Technical Highlights (核心技術突破)

### 1. 突破中斷上下文陷阱：$O(1)$ 深拷貝與 Spinlock 防禦
為解決多核心 (SMP) 環境下的快取競爭 (Cache Contention) 與 `copy_to_user` 觸發 Page Fault 導致的 Kernel Panic，本系統實作了以 24 Bytes 為單位的固定長度記憶體合約。
* 採用 `spin_lock_irqsave` 保護臨界區段 (Critical Section)。
* 即使在 `stress-ng` (CPU/VM/IO 三方滿載) 的極限壓測下，**吞吐量逆勢突破 338 萬次 IOCTLs/sec**，達成零死鎖 (Zero Deadlock)。

### 2. 南向混沌狀態機 (Southbound Chaos FSM)
為證明架構強固性，核心內建純物理的壓力生成引擎，於背景以 1 毫秒頻率隨機觸發：
* `CRUISING` (穩態)
* `EMI_SPIKE` (高頻電磁突波 - 考驗核心過濾機制)
* `EMERGENCY_STOP` (硬體急停 - 考驗系統鎖死反應)

### 3. SRE 級別維運：零接觸部署 (Zero-touch Auto-recovery)
系統深度整合 Systemd 生命週期管理與硬體看門狗 (Hardware Watchdog)。
* 遭遇極端負載斷電或當機後，系統能在 15 秒內自動掛載核心模組並重啟 Node.js 廣播。
* 解除日誌封印 (Persistent Journaling)，確保死後根因分析 (RCA) 證據完整留存。

---

## 📊 Reliability & Telemetry (穩定度與遙測驗證)

本架構已於 Raspberry Pi 5 平台上通過嚴苛的 **72 小時連續燒機測試 (72-hour Burn-in Test)**：
* **Kernel 穩定度：** 歷經超過 2.5 億次 1000Hz 狀態機循環，維持 0 系統死鎖。
* **記憶體健康度：** 透過背景 `monitor.sh` 遙測採樣證實，即使經過千萬次 WebSocket JSON 廣播，Node.js 中介層 (V8 Engine) 垃圾回收完美運作，實體記憶體常駐於 60MB 左右，**無任何 Memory Leak (記憶體洩漏) 跡象**。
* **I/O 韌性：** 長期高頻日誌寫入未引發檔案系統阻塞，確保持續運轉安全性。

---

## 📁 Repository Structure (專案結構)

採用基礎設施堆疊 (Infrastructure Stack) 的 Monorepo 結構：

```text
hard-realtime-edge-stack/
├── core/                  # [OT] Linux Kernel Module (物理防禦引擎、Spinlock、ABI)
├── middleware/            # [IT] Node.js Adapter (無狀態 JSON 轉換與 WebSocket 廣播)
├── apps/
│   └── dashboard/         # [HMI] 戰情室前端 (具備 Visual Latching 與稽核日誌)
├── tests/                 # [QA] elc_diag.c (風洞極限壓測)、monitor.sh (SRE 健康遙測)
└── docs/
    └── ADRs/              # [架構文檔] ADR-001 ~ ADR-007 架構決策演進史
```
**💡 架構師備註：** 本專案根目錄下 docs/ADRs 完整收錄了從早期 V1.0 應用層架構，一路重構至 V5.0 核心底層的思維演進史 (包含踩過的坑與決策轉折)。強烈建議從 ADR-005 開始閱讀，以理解底層記憶體機制的設計初衷。
