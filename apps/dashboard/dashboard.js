const ws = new WebSocket(`ws://${window.location.host}`);

const els = {
    distance: document.getElementById('distance-val'),
    status: document.getElementById('system-status'),
    spikesFiltered: document.getElementById('spikes-filtered-val'), // 🛡️ 新增的 KPI 欄位
    timestamp: document.getElementById('timestamp-val'),
    banner: document.getElementById('connection-banner'),
    connStatusText: document.getElementById('conn-status-text'),
    logList: document.getElementById('event-log-list')
};

let watchdogTimer = null;
const WATCHDOG_TIMEOUT_MS = 3000;

// 📊 IT 治理統計數據
let filteredSpikesCount = 0; 

function addLog(type, message) {
    const timeStr = new Date().toLocaleTimeString(undefined, { hour12: false, fractionalSecondDigits: 2 });
    const li = document.createElement('li');
    li.className = `log-item ${type}`;
    li.innerText = `[${timeStr}] ${message}`;
    
    els.logList.prepend(li);
    if (els.logList.children.length > 8) els.logList.lastChild.remove();
}

function resetWatchdog() {
    clearTimeout(watchdogTimer);
    els.banner.className = 'banner connected';
    els.connStatusText.innerText = '✅ 邊緣閘道器連線正常 (Edge Link Active)';
    watchdogTimer = setTimeout(() => {
        els.banner.className = 'banner disconnected';
        els.connStatusText.innerText = '❌ 霧區網路中斷 (Fog Network Lost)';
    }, WATCHDOG_TIMEOUT_MS);
}

ws.onopen = () => {
    addLog('system', 'System connected to Fog Network.');
    resetWatchdog();
};

ws.onmessage = (event) => {
    resetWatchdog();
    const payload = JSON.parse(event.data);
    const engineData = payload.chaos_engine;
    
    els.timestamp.innerText = new Date(payload.timestamp).toLocaleTimeString();
    els.distance.innerText = engineData.distance_mm + " mm";

    const status = engineData.interlock_status;
    const physState = engineData.physical_state;

    // --- 🧠 IT 治理與宏觀狀態邏輯 (Brain Logic) ---
    
    if (status === "EMERGENCY_STOP") {
        // 【宏觀狀態：機台實體死鎖】 -> 這是 IT 唯一需要用強烈視覺警告的事
        els.distance.style.color = "#ff1744"; 
        els.distance.style.textShadow = "0 0 25px rgba(255, 23, 68, 0.8)"; 
        els.status.innerText = "🚨 INTERLOCK ACTIVE (硬體急停鎖死)";
        els.status.className = "status-badge danger";
        
        addLog('crash', `CRITICAL CRASH! Dist: ${engineData.distance_mm}mm. 機台已實體斷電保護。`);
    } 
    else {
        // 【宏觀狀態：機台運轉中】
        els.distance.style.color = "#00e676"; 
        els.distance.style.textShadow = "0 0 25px rgba(0, 230, 118, 0.6)"; 
        els.status.innerText = "✅ SYSTEM HEALTHY (平穩運轉)";
        els.status.className = "status-badge normal";

        // 🛡️ 微觀波動處理：如果 Kernel 回報突波，IT 不恐慌，只記嘉獎
        if (physState === "EMI_SPIKE") {
            filteredSpikesCount++;
            els.spikesFiltered.innerText = `${filteredSpikesCount} 次`;
            // 在日誌中低調記錄，不干擾主視覺
            addLog('system', `🛡️ Kernel 成功過濾一次物理雜訊/突波 (${engineData.distance_mm}mm)`);
        }
    }
};