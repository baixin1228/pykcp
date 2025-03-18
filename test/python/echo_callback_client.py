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

client_recv_cb_exit = False
def client_recv_cb(udp_kcp, client, data):
	global client_recv_cb_exit
	ip = utils.int_to_ip_str(socket.ntohl(client.nip))
	port = socket.ntohs(client.nport)
	obj = pickle.loads(data)
	print(f"PING {ip}:{port} {(time.time_ns() / 1000 - obj['time'])}us")
	if obj["exit"]:
		client_recv_cb_exit = True

def ping_test_client_cb(ip):
	udp_kcp = ikcp.PyKcp("0.0.0.0", 0)
	udp_kcp.set_recv_cb(client_recv_cb)
	client = udp_kcp.new_client(ip, 8888)
	for x in range(10):
		if x == 9:
			udp_kcp.send_and_flush(client, pickle.dumps({"time" : time.time_ns() / 1000, "exit" : True}))
		else:
			udp_kcp.send_and_flush(client, pickle.dumps({"time" : time.time_ns() / 1000, "exit" : False}))
		time.sleep(0.2)
	while not client_recv_cb_exit:
		time.sleep(0.03)

if __name__ == '__main__':
	if len(sys.argv) < 2:
		print("Usage: stress_client.py <ip>")
		sys.exit(1)
	ping_test_client_cb(sys.argv[1])