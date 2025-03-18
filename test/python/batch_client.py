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

def ping_test_batch(ip, deep):
	
	udp_kcp = ikcp.PyKcp("0.0.0.0", 0)
	client = udp_kcp.new_client(ip, 8888)
	udp_kcp.client_wndsize(client, 40960, 40960)
	for x in range(deep):
		if x == deep - 1:
			udp_kcp.send_and_flush(client, pickle.dumps({"time" : time.time_ns() / 1000, "exit" : True}))
		else:
			udp_kcp.send_and_flush(client, pickle.dumps({"time" : time.time_ns() / 1000, "exit" : False}))
	all_time = 0
	for x in range(deep):
		ret = udp_kcp.recv_pkg()
		for client, data in ret:
			ip = utils.int_to_ip_str(socket.ntohl(client.nip))
			port = socket.ntohs(client.nport)
			obj = pickle.loads(data)
			all_time = all_time + (time.time_ns() / 1000 - obj['time'])

	print(f"PING {ip}:{port} deep:{deep:<6} time:{int(all_time / deep)}us")

if __name__ == '__main__':
	if len(sys.argv) < 2:
		print("Usage: stress_client.py <ip>")
		sys.exit(1)
	ping_test_batch(sys.argv[1], 1)
	ping_test_batch(sys.argv[1], 10)
	ping_test_batch(sys.argv[1], 100)
	ping_test_batch(sys.argv[1], 1000)
	ping_test_batch(sys.argv[1], 10000)
	ping_test_batch(sys.argv[1], 40000)