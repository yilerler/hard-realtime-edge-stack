# ADR-007: 系統自癒機制與工業實體強固 (System Auto-recovery and Industrial Hardening)
## 1. 狀態 (Status)
* 已接受 (Accepted) - V5.0 架構升級

## 2. 背景與問題 (Context)
* **硬體看門狗的介入與無聲死亡：** 在極限壓力測試（如  `stress-ng` 導致的 I/O 阻塞）下，系統級守護行程 (Systemd) 會因資源耗盡而無法按時餵食硬體看門狗 (Hardware Watchdog)，進而觸發 CPU 強制重啟。由於預設的 Systemd Journal 儲存於 Volatile RAM 中，重啟後無法留下任何死前日誌 (Silent Death)，難以進行根因分析 (RCA)。

* **缺乏自主恢復能力 (Auto-recovery)：** 系統斷電重啟或被看門狗重置後，需開發者手動透過 SSH 登入執行 `insmod` (載入核心模組) 與 `node adapter.js` (啟動霧區大使)。在真實的工業物聯網 (IIoT) 現場，不允許此種人為介入，系統必須具備拔電即用的「黑盒子」特性。

* **跨空間的權限阻抗：** Node.js 中介層在 User Space 執行時，若以一般使用者身分掛載，將無法讀寫由 Kernel 建立的字元設備檔 (`/dev/mock_elc`)，引發 `EACCES` 權限崩潰。

## 3. 決策 (Decision)
* **導入 Systemd 守護行程生命週期管理：**
    * 建立 `elc-core.service` (Type=oneshot)：確保 Linux 系統啟動網路前，自動將 `mock_elc_core.ko` 載入 Kernel，並在關機時優雅卸載。
    * 建立 elc-adapter.service (Type=simple)：綁定對 `elc-core.service` 的依賴 (`Requires=`)，並配置 `Restart=always` 與 `RestartSec=3`，實現 Node.js 崩潰後的無限次自動重啟。
* **權限提升與跨界授權：** 在 elc-adapter.service 中明確宣告 `User=root`，賦予 Node.js 讀取底層硬體設備檔的最高權限，消滅權限邊界問題。
* **解除日誌封印 (Persistent Journaling)：** 建立 `/var/log/journal` 實體目錄，強制 Systemd 將系統日誌寫入非揮發性儲存裝置 (SD 卡/eMMC)，確保看門狗重啟後的死因可被追溯。

## 4. 後果 (Consequences)
* **正面：** 
    * 達成真正的「零接觸部署 (Zero-touch Deployment)」。系統現已具備強大的容錯與自癒能力，無論是斷電或極端負載當機，皆能在重啟後 15 秒內自動恢復硬即時防禦與雲端廣播。
    * 完整的實體日誌留存，大幅提升了現場維運 (DevOps) 的除錯能力。

* **負面/代價：**
    * **快閃記憶體耗損 (Flash Wear-out)：** 將日誌永久寫入 SD 卡會增加寫入放大效應 (Write Amplification)，縮短儲存媒體壽命。未來可能需考量日誌輪替 (Log Rotation) 或轉送至遠端 Syslog 伺服器。
    * **資安風險增加：** 賦予 Node.js `root` 權限增加了潛在的攻擊面。若上層應用遭入侵，攻擊者將獲得最高系統權限，未來需考慮使用 `udev` 規則來精細化 `/dev/mock_elc` 的群組讀寫權限，以取代暴力的 `User=root` 做法。