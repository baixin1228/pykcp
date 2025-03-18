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

def echo_server_atomic():
	udp_kcp = ikcp.PyKcp("0.0.0.0", 8888, atomicSem = True)
	exit = False
	while not exit:
		ret = udp_kcp.recv_pkg()
		for client, data in ret:
			udp_kcp.send_and_flush(client, data)
			obj = pickle.loads(data)
			exit = obj["exit"]

if __name__ == '__main__':
	echo_server_atomic()