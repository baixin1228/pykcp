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

recv_cb_exit = False
def server_recv_cb(udp_kcp, client, data):
	global recv_cb_exit
	udp_kcp.send_and_flush(client, data)
	obj = pickle.loads(data)
	if obj["exit"]:
		recv_cb_exit = True

def echo_server_cb():
	udp_kcp = ikcp.PyKcp("0.0.0.0", 8888)
	udp_kcp.set_recv_cb(server_recv_cb)
	while not recv_cb_exit:
		time.sleep(0.06)


if __name__ == '__main__':
	echo_server_cb()