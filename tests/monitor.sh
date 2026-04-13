#!/bin/bash
LOG_FILE="burn_in_health.csv"

# 寫入 CSV 標頭
echo "Timestamp,Node_RAM_MB,Sys_Free_RAM_MB,Disk_Usage" > $LOG_FILE

echo "🕵️ 開始背景監控系統健康度... (按 Ctrl+C 停止，或放入背景執行)"

while true; do
    TIME=$(date '+%Y-%m-%d %H:%M:%S')
    
    # 抓取 node adapter.js 的實際記憶體使用量 (MB)
    NODE_PID=$(pgrep -f "node adapter.js")
    if [ -z "$NODE_PID" ]; then
        NODE_RAM="0 (CRASHED)"
    else
        NODE_RAM=$(ps -p $NODE_PID -o rss= | awk '{printf "%.2f", $1/1024}')
    fi

    # 抓取系統剩餘記憶體
    SYS_FREE=$(free -m | awk '/Mem:/{print $7}')
    
    # 抓取 SD 卡使用率
    DISK_USAGE=$(df -h / | awk 'NR==2{print $5}')

    # 寫入 CSV
    echo "$TIME, $NODE_RAM, $SYS_FREE, $DISK_USAGE" >> $LOG_FILE
    
    # 每 60 秒紀錄一次
    sleep 60
done
