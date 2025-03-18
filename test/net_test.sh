#!/bin/bash

# 检查是否以 root 权限运行
if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run with root privileges (use sudo)"
    exit 1
fi

# 检查 uuidgen 命令是否存在
if ! command -v uuidgen &> /dev/null; then
    echo "错误: uuidgen 命令未找到，请安装 uuid-runtime 包。"
    if [ -f /etc/debian_version ]; then
        echo "对于基于 Debian 或 Ubuntu 的系统，请运行: sudo apt install uuid-runtime"
    elif [ -f /etc/redhat-release ]; then
        echo "对于基于 Red Hat 或 CentOS 的系统，请运行: sudo yum install util-linux"
    fi
    exit 1
fi

# 定义网络命名空间和接口名称
SERVER_NS="server_ns_$(uuidgen | cut -c -4)"
CLIENT_NS="client_ns_$(uuidgen | cut -c -4)"
VETH_SERVER="veth_$(uuidgen | cut -c -4)"
VETH_CLIENT="veth_$(uuidgen | cut -c -4)"
SERVER_IP="192.168.45.1/24"
CLIENT_IP="192.168.45.2/24"

# 定义丢包率和延时
LOSS_RATE="10%"
DELAY_TIME="20ms"

# 定义服务端和客户端程序路径
CLIENT_PROGRAM=$1
SERVER_PROGRAM=$3

# 检查程序路径
if [ ! -x "$SERVER_PROGRAM" ] || [ ! -x "$CLIENT_PROGRAM" ]; then
    echo "Error: Server or client program not found or not executable"
    exit 1
fi

# 清理函数
cleanup() {
    echo "Cleaning up..."
    ip netns exec $CLIENT_NS tc qdisc del dev $VETH_CLIENT root 2>/dev/null || true
    ip link delete $VETH_SERVER 2>/dev/null || true
    ip link delete $VETH_CLIENT 2>/dev/null || true
    ip netns delete $SERVER_NS 2>/dev/null || true
    ip netns delete $CLIENT_NS 2>/dev/null || true
}
trap cleanup EXIT

# 创建网络命名空间
ip netns add $SERVER_NS || { echo "Failed to create $SERVER_NS"; exit 1; }
ip netns add $CLIENT_NS || { echo "Failed to create $CLIENT_NS"; exit 1; }

# 创建虚拟网络接口对
ip link add $VETH_SERVER type veth peer name $VETH_CLIENT || { echo "Failed to create veth pair"; exit 1; }

# 将虚拟网络接口分配到不同的网络命名空间
ip link set $VETH_SERVER netns $SERVER_NS || { echo "Failed to move $VETH_SERVER"; exit 1; }
ip link set $VETH_CLIENT netns $CLIENT_NS || { echo "Failed to move $VETH_CLIENT"; exit 1; }

# 配置网络接口的 IP 地址
ip netns exec $SERVER_NS ip addr add $SERVER_IP dev $VETH_SERVER || { echo "Failed to set IP for server"; exit 1; }
ip netns exec $CLIENT_NS ip addr add $CLIENT_IP dev $VETH_CLIENT || { echo "Failed to set IP for client"; exit 1; }

# 启用网络接口
ip netns exec $SERVER_NS ip link set $VETH_SERVER up || { echo "Failed to bring up $VETH_SERVER"; exit 1; }
ip netns exec $CLIENT_NS ip link set $VETH_CLIENT up || { echo "Failed to bring up $VETH_CLIENT"; exit 1; }

if [ "$5" == "delay" ]; then
    # 模拟丢包和延时（在客户端网络命名空间）
    ip netns exec $CLIENT_NS tc qdisc add dev $VETH_CLIENT root netem loss $LOSS_RATE delay $DELAY_TIME || { echo "Failed to set netem"; exit 1; }
fi

# 运行服务端程序
eval "ip netns exec $SERVER_NS $SERVER_PROGRAM $4 &"
SERVER_PID=$!
echo "Server PID: $SERVER_PID"

# 等待服务端启动
sleep 1

# 运行客户端程序
eval "ip netns exec $CLIENT_NS $CLIENT_PROGRAM $2"
exit_code=$?

# 停止服务端程序
kill -9 $SERVER_PID 2>/dev/null || true

# 清理在 trap 中自动执行
echo "Test completed"
exit $exit_code