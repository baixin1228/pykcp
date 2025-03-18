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

def on_client_create(udp_kcp, client):
	udp_kcp.client_wndsize(client, 40960, 40960)
	ip = utils.int_to_ip_str(socket.ntohl(client.nip))
	port = socket.ntohs(client.nport)
	print(f"new client {ip}:{port}")
	return True

def stress_batch_server():
	udp_kcp = ikcp.PyKcp("0.0.0.0", 8888)
	udp_kcp.set_create_cb(on_client_create)
	while True:
		ret = udp_kcp.recv_pkg()
		for client, data in ret:
			udp_kcp.send_and_flush(client, data)

if __name__ == '__main__':
	stress_batch_server()