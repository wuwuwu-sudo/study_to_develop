#!/bin/bash
# ==========================================================
# 停止三实例 webserver (8081 / 8082 / 8083)
# 用法: ./stop_servers.sh
# ==========================================================
cd "$(dirname "$0")"

for port in 8081 8082 8083; do
    pid_file="logs/server_${port}.pid"
    if [ -f "$pid_file" ]; then
        pid=$(cat "$pid_file")
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid"
            echo "✅ 已停止实例 ${port} (PID $pid)"
        else
            echo "ℹ️  实例 ${port} 未在运行，清理 PID 文件"
        fi
        rm -f "$pid_file"
    fi
done

echo "全部实例已停止。"
