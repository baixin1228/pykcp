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

def stress_server():
	udp_kcp = ikcp.PyKcp("0.0.0.0", 8888)
	while True:
		ret = udp_kcp.recv_pkg()
		for client, data in ret:
			udp_kcp.send_and_flush(client, data)

if __name__ == '__main__':
	stress_server()