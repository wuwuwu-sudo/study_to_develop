#!/bin/bash
# ==========================================================
# 一键启动三实例 webserver (8081 / 8082 / 8083)
# 用法: ./start_servers.sh
# 依赖: 先构建 ./webserver (make release)
# 增强: ① 配置文件存在性检查 ② 端口占用检测 ③ 启动后存活确认
# ==========================================================
cd "$(dirname "$0")"

BIN=./webserver
PORTS="8081 8082 8083"
mkdir -p logs

# ---------- 1. 检查可执行文件 ----------
if [ ! -x "$BIN" ]; then
    echo "❌ 未找到可执行文件 $BIN，请先执行: make release"
    exit 1
fi

# ---------- 2. 检查实例配置文件是否存在 ----------
for port in $PORTS; do
    cfg="config/instance_${port}.json"
    if [ ! -f "$cfg" ]; then
        echo "❌ 缺少配置文件 $cfg"
        exit 1
    fi
done

# ---------- 3. 端口占用检测函数 ----------
# 返回 0 = 被占用, 1 = 空闲
port_in_use() {
    local port=$1
    if command -v ss >/dev/null 2>&1; then
        ss -ltnH | awk '{print $4}' | grep -q ":$port$"
    elif command -v lsof >/dev/null 2>&1; then
        lsof -iTCP:"$port" -sTCP:LISTEN -P -n >/dev/null 2>&1
    elif command -v netstat >/dev/null 2>&1; then
        netstat -ltn | awk '{print $4}' | grep -q ":$port$"
    else
        # 兜底: 解析 /proc/net/tcp（十六进制端口）
        grep -qi ":$(printf '%04X' "$port")" /proc/net/tcp
    fi
}

started=0
for port in $PORTS; do
    pid_file="logs/server_${port}.pid"

    # ---------- 4. 防重复启动（PID 文件） ----------
    if [ -f "$pid_file" ]; then
        pid=$(cat "$pid_file")
        if kill -0 "$pid" 2>/dev/null; then
            echo "ℹ️  实例 ${port} 已在运行 (PID $pid)，跳过"
            continue
        fi
        rm -f "$pid_file"
    fi

    # ---------- 5. 端口占用检查 ----------
    if port_in_use "$port"; then
        echo "❌ 端口 $port 已被占用，跳过（请先排查占用进程）"
        continue
    fi

    # ---------- 6. 启动实例 ----------
    nohup "$BIN" --port "$port" --config "config/instance_${port}.json" \
        >> "logs/server_${port}.out" 2>&1 &
    echo $! > "$pid_file"
    started=$((started + 1))
    echo "✅ 已启动实例 ${port} (PID $!)"
done

# ---------- 7. 启动后存活确认 ----------
if [ "$started" -gt 0 ]; then
    echo ""
    echo "等待实例就绪..."
    sleep 1
    for port in $PORTS; do
        pid_file="logs/server_${port}.pid"
        pid=$(cat "$pid_file" 2>/dev/null)
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            code=$(curl -s -o /dev/null -w "%{http_code}" --max-time 3 \
                "http://127.0.0.1:${port}/api/shops" 2>/dev/null)
            if [ "$code" = "200" ]; then
                echo "  🟢 实例 ${port} 存活 (PID $pid, /api/shops -> HTTP $code)"
            else
                echo "  🟡 实例 ${port} 进程在但 API 未就绪 (PID $pid, HTTP ${code:-N/A})"
            fi
        else
            echo "  🔴 实例 ${port} 启动失败！请查看 logs/server_${port}.out"
        fi
    done
fi

echo ""
echo "=========================================="
echo " 三实例启动流程结束"
echo " 日志: logs/server_*.out"
echo " 健康检查: curl http://127.0.0.1:8081/api/shops"
echo "=========================================="
