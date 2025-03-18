import sys
import ikcp
import json
import time
import pickle
import socket
import ctypes
import asyncio
import platform
import threading

def ip_str_to_int(ip_str):
		packed_ip = socket.inet_aton(ip_str)
		return int.from_bytes(packed_ip, 'big')

def int_to_ip_str(int_val):
		packed_ip = int_val.to_bytes(4, 'big')
		return socket.inet_ntoa(packed_ip)