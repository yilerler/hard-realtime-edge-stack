# POSTMORTEM-003: 72 小時 SRE 燒機測試與 IT/OT 解耦架構驗證

## 1. 測試背景與動機 (Context & Motivation)
在歷經 [POSTMORTEM-002](POSTMORTEM-002-io-blocking-and-kthread-migration.md) 的上下文重構與 [ADR-006](ADR-006-domain-decoupling-and-transition-to-infrastructure-stack.md) 的 IT/OT 本質性解耦後，V5.0 系統已具備微秒級的硬即時防禦能力。然而，作為工業邊緣閘道器 (Edge Gateway)，系統必須具備長效無人值守 (Zero-touch) 的穩定性。

過去基於 User Space 的軟體架構，常因 Node.js 記憶體洩漏 (Memory Leak) 或日誌檔案塞爆 SD 卡，導致設備在部署數日後觸發 OOM (Out-Of-Memory) 或 I/O 窒息而死機。本測試旨在透過 72 小時連續極限壓測，驗證 V5.0 架構的長期可靠性。

## 2. 測試環境與設定 (Test Environment Setup)
* **硬體平台：** Raspberry Pi 5 (4GB RAM)
* **OT 負載：** `chaos_engine` 於 Kernel 中以 1000Hz 頻率觸發極限物理中斷與狀態機切換。
* **IT 負載：** `adapter.js` 每 100 毫秒讀取一次 Kernel ABI 狀態，並進行 WebSocket JSON 廣播。
* **監測機制：** 部署背景 `monitor.sh`，以 60 秒為週期採樣 `Node_RAM`, `Sys_Free_RAM`, `Disk_Usage` 並輸出至 `burn_in_health.csv`。

## 3. 遙測數據剖析 (Telemetry Analysis & Observations)
經過連續 72 小時的系統運作，遙測數據呈現出教科書級的健康指標：

### 3.1 Node.js 中介層：0% 記憶體洩漏
* **數據特徵：** `Node_RAM_MB` 基準線死守於 61 MB ~ 63 MB 之間。
* **架構驗證：** 呈現完美的「鋸齒波 (Sawtooth)」特徵，證明 V8 引擎垃圾回收 (GC) 運作精準。因繁重的物理過濾已下放至 Kernel，Node.js 成功退化為「無狀態 (Stateless)」的中介層，徹底根除了舊架構的記憶體碎片化危機。

### 3.2 系統實體記憶體：健康的 Page Cache 調度
* **數據特徵：** `Sys_Free_RAM_MB` 宏觀維持在 3350 MB，期間伴隨週期性的階梯狀釋放（最低點從未跌破 3000 MB）。
* **架構驗證：** 階梯狀跳變為 Linux 核心動態調度 Page Cache (檔案快取) 的正常現象。系統不僅未被高頻運作吃光資源，且距離 OOM 崩潰線仍有高達 3GB 的巨大安全護城河。

### 3.3 磁碟 I/O：防禦日誌窒息
* **數據特徵：** `Disk_Usage` 72 小時穩定錨定於 10%。
* **架構驗證：** 證實 [ADR-007](docs/decisions/ADR-007-system-auto-recovery-and-industrial-hardening.md) 導入的 Systemd Persistent Journal 策略生效。內建的 Log Rotation (日誌輪替) 成功壓制了 Kernel 每秒千次的高頻警告，防止了檔案系統滿載導致的致命性崩潰。

## 4. 最終結論 (Final Verdict & Resolution)
本測試證實，**V5.0 架構已正式跨越「實驗室原型 (PoC)」，達到「工業級量產部署 (Production-Ready)」標準。**

透過將系統精準劃分為 OT (硬即時防禦) 與 IT (非同步觀測) 雙層架構，我們不僅在物理上達成了微秒級防禦，更在軟體上實現了系統資源的完美平衡。系統具備抵禦極端干擾與長效穩定運行的能力，本專案之核心架構演進至此宣告成功閉環。