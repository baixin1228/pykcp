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

def stress_test_client(ip):
	udp_kcp = ikcp.PyKcp("0.0.0.0", 0)
	client = udp_kcp.new_client(ip, 8888)

	send_data = {}
	for x in range(2000):
		send_data[x] = str(x)

	print("stress test")
	print("pkg_len:", len(pickle.dumps(send_data)))
	send_len = 0
	time_start = time.time_ns()
	for x in range(2):
		send_data[1000] = str(x)
		send_bytes = pickle.dumps(send_data)

		ret = None
		for i in range(400):
			send_len = send_len + len(send_bytes)
			udp_kcp.send_and_flush(client, send_bytes)
			ret = udp_kcp.recv_pkg()

		for client, data in ret:
			try:
				obj = pickle.loads(data)
				if obj[1000] != str(x):
					ip = utils.int_to_ip_str(socket.ntohl(client.nip))
					port = socket.ntohs(client.nport)
					print(f"STRESS {ip}:{port} {obj[1000]} != {str(x)}")
				now = time.time_ns()
				print(f"Speed: {int(send_len * 1000000000 / (now - time_start) / 1024 / 1024)}Mb/s")
				time_start = now
				send_len = 0
			except pickle.UnpicklingError as e:
				print(f"Error during unpickling: {e}")
	print("")

def stress_batch_test_client(ip):
	udp_kcp = ikcp.PyKcp("0.0.0.0", 0)
	client = udp_kcp.new_client(ip, 8888)
	udp_kcp.client_wndsize(client, 40960, 40960)

	send_data = {}
	for x in range(2000):
		send_data[x] = str(x)

	print("stress test, batch mode")
	print("pkg_len:", len(pickle.dumps(send_data)))
	send_len = 0
	time_start = time.time_ns()
	for x in range(10):
		send_data[1000] = str(x)
		send_bytes = pickle.dumps(send_data)

		ret = None
		for i in range(10):
			send_len = send_len + len(send_bytes)
			udp_kcp.send_and_flush(client, send_bytes)
		for i in range(10):
			ret = udp_kcp.recv_pkg()

		for client, data in ret:
			try:
				obj = pickle.loads(data)
				if obj[1000] != str(x):
					ip = utils.int_to_ip_str(socket.ntohl(client.nip))
					port = socket.ntohs(client.nport)
					print(f"STRESS {ip}:{port} {obj[1000]} != {str(x)}")
				if x % 5 == 4:
					now = time.time_ns()
					print(f"Speed: {int(send_len * 1000000000 / (now - time_start) / 1024 / 1024)}Mb/s")
					time_start = now
					send_len = 0
			except pickle.UnpicklingError as e:
				print(f"Error during unpickling: {e}")
	print("")

if __name__ == '__main__':
	if len(sys.argv) < 2:
		print("Usage: stress_client.py <ip>")
		sys.exit(1)
	stress_test_client(sys.argv[1])
	stress_batch_test_client(sys.argv[1])