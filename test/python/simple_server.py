
import sys
import ikcp
import json
import time
import utils
import pickle
import socket
import ctypes
import asyncio
import platform
import threading

def simple_test_server():
	udp_kcp = ikcp.PyKcp("0.0.0.0", 8888)
	while True:
		ret = udp_kcp.recv_pkg()
		for client, data in ret:
			ip = utils.int_to_ip_str(socket.ntohl(client.nip))
			port = socket.ntohs(client.nport)
			print(f"{ip}:{port} {data.decode('utf-8')}")
			udp_kcp.send_and_flush(client, "recv ok.".encode("utf-8"))
		if data.decode('utf-8') == "exit":
			break

if __name__ == '__main__':
	simple_test_server()