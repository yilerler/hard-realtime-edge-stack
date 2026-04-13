const fs = require('fs');
const path = require('path');
const ioctl = require('ioctl-napi');
const express = require('express');
const { WebSocketServer } = require('ws');
const http = require('http');

// --- 1. 建立 Web 與 WebSocket 伺服器 (Fog Broadcast Layer) ---
const app = express();
const server = http.createServer(app);
app.use(express.static(path.join(__dirname, '../../apps/chaos-dashboard')));
const wss = new WebSocketServer({ server });

wss.on('connection', function connection(ws) {
    console.log('🌐 [Fog Network] A new local observer connected to the stream!');
    ws.on('close', () => console.log('🌐 [Fog Network] Observer disconnected.'));
});

const PORT = 3000;
server.listen(PORT, () => {
    console.log(`🚀 [System] Edge Middleware is running on http://localhost:${PORT}`);
});

// --- 2. 定義跨層合約 (The DMZ Contract) ---
const SENSOR_MAGIC = 'S'.charCodeAt(0);
const COMMAND_NR = 1;

// 🛡️ 堅不可摧的合約：即使底層換了混沌引擎，24 Bytes 的記憶體對齊依然不變！
const DATA_SIZE = 24; 

// --- IOCTL 號碼計算機 ---
const _IOC_NRBITS = 8, _IOC_TYPEBITS = 8, _IOC_SIZEBITS = 14, _IOC_DIRBITS = 2;
const _IOC_NRSHIFT = 0;
const _IOC_TYPESHIFT = _IOC_NRSHIFT + _IOC_NRBITS;
const _IOC_SIZESHIFT = _IOC_TYPESHIFT + _IOC_TYPEBITS;
const _IOC_DIRSHIFT = _IOC_SIZESHIFT + _IOC_SIZEBITS;
const _IOC_READ = 2;

function _IOR(type, nr, size) {
    return (_IOC_READ << _IOC_DIRSHIFT) | (size << _IOC_SIZESHIFT) | (type << _IOC_TYPESHIFT) | (nr << _IOC_NRSHIFT);
}

const IOCTL_GET_DATA = _IOR(SENSOR_MAGIC, COMMAND_NR, DATA_SIZE);

// --- 3. 連接底層深邊緣驅動 (Deep Edge Interface) ---
const DEVICE_PATH = '/dev/mock_elc';
let fd = null;

try {
    fd = fs.openSync(DEVICE_PATH, 'r+');
    console.log(`[System] Connected to Chaos Engine at ${DEVICE_PATH} (fd=${fd})`);
} catch (err) {
    console.error(`[Error] Failed to open ${DEVICE_PATH}. Did you insmod the V4.4 core?`);
    process.exit(1);
}

const buffer = Buffer.alloc(DATA_SIZE);

// --- 4. OT 軌道序列化 (Hex Protocol Translator) ---
function encodeHexProtocol(distance_mm, status_code) {
    const txBuffer = Buffer.alloc(6);
    txBuffer.writeUInt8(0xAA, 0); // Header
    txBuffer.writeUInt8(0x01, 1); // Command ID
    const safeDistance = Math.min(Math.max(0, distance_mm), 65535);
    txBuffer.writeUInt16BE(safeDistance, 2); 
    txBuffer.writeUInt8(status_code, 4); // Status: 0=NORMAL, 1=EMERGENCY
    
    let checksum = 0;
    for (let i = 0; i < 5; i++) checksum += txBuffer.readUInt8(i);
    txBuffer.writeUInt8(checksum & 0xFF, 5);
    return txBuffer;
}

function formatHexString(buffer) {
    return [...buffer].map(b => '0x' + b.toString(16).padStart(2, '0').toUpperCase()).join(' ');
}

// --- 5. 霧區外交廣播迴圈 (The Fog Ambassador Loop) ---
// 頻率提升到 10Hz (100ms)，以捕捉底層瞬息萬變的物理突波
const aggregationTimer = setInterval(() => {
    try {
        const ret = ioctl(fd, IOCTL_GET_DATA, buffer);
        
        if (ret === 0) {
            // 🗄️ 解碼：我們只取前兩個欄位，後面的 PM2.5 等廢棄欄位直接無視
            const distance = buffer.readInt32LE(4);
            const status_code = buffer.readInt32LE(8);

            const isLocked = (status_code === 1);
            const status_str = isLocked ? "EMERGENCY_STOP" : "NORMAL";

            // 🧠 物理狀態判定 (辨識 Chaos Engine 的三相位)
            let chaos_state = "CRUISING";
            if (isLocked) chaos_state = "CRITICAL_CRASH";
            else if (distance > 2000) chaos_state = "EMI_SPIKE";

            // 轉化為面向叢集的泛用型 JSON Payload (未來可無縫接軌 MQTT)
            const systemPayload = {
                timestamp: new Date().toISOString(),
                node_id: "edge-node-rpi5-01",
                chaos_engine: {
                    distance_mm: distance,
                    physical_state: chaos_state,
                    interlock_status: status_str
                }
            };

            // IT 軌道推播：非同步廣播給所有訂閱者
            const jsonString = JSON.stringify(systemPayload);
            wss.clients.forEach(client => {
                if (client.readyState === 1) client.send(jsonString);
            });

            // OT 軌道推播：翻譯成 Hex 封包
            const hexBuffer = encodeHexProtocol(distance, status_code);
            const hexString = formatHexString(hexBuffer);
            
            // 終端機動態展示：只在發生「突波」或「崩潰」時發出強烈警告
            if (chaos_state === "CRITICAL_CRASH") {
                console.error(`🚨 [FOG ALERT] CRITICAL CRASH! Dist: ${distance}mm. Interlock ACTIVE! OT-TX: [ ${hexString} ]`);
            } else if (chaos_state === "EMI_SPIKE") {
                console.warn(`⚡ [FOG WARN] EMI Spike Detected (${distance}mm). Defense hold...`);
            }
        }
    } catch (e) {
        console.error(`[Error] Read failed:`, e.message);
    }
}, 100);

// --- 優雅退出 (Graceful Shutdown) ---
process.on('SIGINT', () => {
    console.log('\n[System] SIGINT received. Shutting down Fog Ambassador...');
    clearInterval(aggregationTimer);
    wss.close();
    server.close();
    if (fd !== null) fs.closeSync(fd);
    setTimeout(() => process.exit(0), 500); 
});