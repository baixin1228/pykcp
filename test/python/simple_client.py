
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

def simple_test_client(ip):
	udp_kcp = ikcp.PyKcp("0.0.0.0", 0)
	client = udp_kcp.new_client(ip, 8888)
	for x in range(10):
		if x == 9:
			udp_kcp.send_and_flush(client, "exit".encode("utf-8"))
		else:
			udp_kcp.send_and_flush(client, "send ok.".encode("utf-8"))
		ret = udp_kcp.recv_pkg()
		for client, data in ret:
			ip = utils.int_to_ip_str(socket.ntohl(client.nip))
			port = socket.ntohs(client.nport)
			print(f"{ip}:{port} {data.decode('utf-8')}")
		time.sleep(0.1)

if __name__ == '__main__':
	if len(sys.argv) < 2:
		print("Usage: stress_client.py <ip>")
		sys.exit(1)
	simple_test_client(sys.argv[1])