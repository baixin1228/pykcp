#include "ikcp.h"

#include <map>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <semaphore>
#include <functional>
#include <shared_mutex>
#include <condition_variable>

#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <pybind11/stl.h>
#include <pybind11/pybind11.h>
#include <pybind11/functional.h>

namespace py = pybind11;
using namespace std;

void signal_handler(int signal) {
	if (signal == SIGINT)
	{
		cout << "Received SIGINT, stopping loop..." << endl;
		exit(0);
	}
}

void module_init() {
	signal(SIGINT, signal_handler);
}

class SpinLock {
public:
	SpinLock() : flag(false) {}

	void lock() {
		while (flag.exchange(true, std::memory_order_acquire)) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
			// x86 PAUSE instruction
			asm volatile("pause" ::: "memory");
#elif defined(__arm__) || defined(__aarch64__) || defined(__APPLE__)
			// ARM, ARM64, and Apple M series WFE instruction
			asm volatile("wfe" ::: "memory");
#else
			// Fallback to yield
			std::this_thread::yield();
#endif
		}
	}

	void unlock() {
		flag.store(false, std::memory_order_release);
#if defined(__arm__) || defined(__aarch64__) || defined(__APPLE__)
		// ARM, ARM64, and Apple M series SEV instruction
		asm volatile("sev" ::: "memory");
#endif
	}

private:
	std::atomic<bool> flag;
};

class AtomicSemaphore {
public:
	AtomicSemaphore(int count = 0) : count(count) {}

	void notify() {
		count.fetch_add(1, memory_order_release);
	}

	void wait() {
		while (true) {
			int expected = count.load(memory_order_relaxed);
			if (expected > 0 && count.compare_exchange_weak(expected, expected - 1, memory_order_acquire, memory_order_relaxed)) {
				break;
			}
			this_thread::yield();
		}
	}

private:
	atomic<int> count;
};

class SemaphoreProxy {
public:
	SemaphoreProxy(int count = 0, bool _atomicSem = false) : semaphore(count), atomicSemaphore(count) {
		atomicSem = _atomicSem;
	}

	void notify() {
		if (atomicSem)
			atomicSemaphore.notify();
		else
			semaphore.release();
	}

	void wait() {
		if (atomicSem)
			atomicSemaphore.wait();
		else
			semaphore.acquire();
	}

private:
	bool atomicSem;
	counting_semaphore<999> semaphore;
	AtomicSemaphore atomicSemaphore;
};

struct KcpClient;

class PyKcp {
public:
	PyKcp(string ip, uint16_t port, int32_t time_out, bool atomicSem);
	~PyKcp();
	uint64_t getTimeMs();
	uint32_t getBoottimeMs(shared_ptr<KcpClient> client);
	shared_ptr<KcpClient> findOrNewClient(uint32_t nip, uint16_t nport);
	static int kcpOutputCallback(const char *buf, int len, 
		ikcpcb *kcp, void *user);
	int kcpOutput(const char *buf, int len,
		ikcpcb *kcp, void *user);
	void updateLoop();

	void set_create_cb(const function<bool(PyKcp *, shared_ptr<KcpClient> client)> callback);
	void set_clean_cb(const function<void(PyKcp *, shared_ptr<KcpClient> client)> callback);
	void set_recv_cb(const function<void(PyKcp *, shared_ptr<KcpClient> client, py::bytes)> callback);
	void recvLoop();
	shared_ptr<KcpClient> new_client(string ip, uint16_t hport);
	int client_wndsize(shared_ptr<KcpClient> client, int sndwnd, int rcvsnd);
	int client_nodelay(shared_ptr<KcpClient> client, int nodelay, int interval, int resend, int nc);
	py::list recv_pkg();
	int send_pkg(shared_ptr<KcpClient>  client, py::bytes bytes);
	void flush(shared_ptr<KcpClient>  client);
	int send_and_flush(shared_ptr<KcpClient>  client, py::bytes bytes);

private:
	int sockfd = -1;
	bool exit = false;
	char recvBuffer[2048];
	SemaphoreProxy semaphore;
	uint64_t timeOutMs;
	SpinLock kcp_lock;
	
	function<bool(PyKcp *, shared_ptr<KcpClient> client)> mOnCreate;
	function<void(PyKcp *, shared_ptr<KcpClient> client)> mOnClean;
	function<void(PyKcp *, shared_ptr<KcpClient> client, py::bytes)> mOnRecv;

	map<uint64_t, shared_ptr<KcpClient>> clients;
	shared_mutex client_lock;
	thread *recvThread;
	thread *updateThread;
};

struct KcpClient{
	ikcpcb *kcp;
	PyKcp *pyKcp;
	uint32_t nextUpdate;
	uint32_t nip;
	uint16_t nport;
	uint64_t startTimeMs;
	uint64_t lastTimeMs;
public:
	~KcpClient()
	{
		if (kcp)
		{
			if(0)
				cout << "ikcp_release" << endl;
			ikcp_release(kcp);
		}
	}
};

PyKcp::PyKcp(string ip, uint16_t port, int32_t time_out = 6, bool atomicSem = false) : semaphore(0, atomicSem), timeOutMs(time_out * 1000), kcp_lock()
{
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
		throw runtime_error("socket create fail.");

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 100000;

	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
		throw runtime_error("Failed to set socket options.");

	sockaddr_in bindAddr;
	memset(&bindAddr, 0, sizeof(bindAddr));
	bindAddr.sin_family = AF_INET;
	bindAddr.sin_addr.s_addr = inet_addr(ip.c_str());
	bindAddr.sin_port = htons(port);

	if (bind(sockfd, (struct sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
		close(sockfd);
		throw invalid_argument("bind fail, invalid addr.");
	}

	recvThread = new thread(&PyKcp::recvLoop, this);
	updateThread = new thread(&PyKcp::updateLoop, this);
}

PyKcp::~PyKcp()
{
	exit = true;
	if(recvThread)
	{
		recvThread->join();
		delete recvThread;
	}
	if(updateThread)
	{
		updateThread->join();
		delete updateThread;
	}

	if(sockfd != -1)
		close(sockfd);
}

void PyKcp::set_create_cb(const function<bool(PyKcp *, shared_ptr<KcpClient> client)> callback)
{
	mOnCreate = callback;
}

void PyKcp::set_clean_cb(const function<void(PyKcp *, shared_ptr<KcpClient> client)> callback)
{
	mOnClean = callback;
}

void PyKcp::set_recv_cb(const function<void(PyKcp *, shared_ptr<KcpClient> client, py::bytes)> callback)
{
	mOnRecv = callback;
}

uint64_t PyKcp::getTimeMs()
{
	auto now = chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	return chrono::duration_cast<chrono::milliseconds>(duration).count();
}

uint32_t PyKcp::getBoottimeMs(shared_ptr<KcpClient> client)
{
	uint64_t time_ms = getTimeMs();
	if(client->startTimeMs == 0)
		client->startTimeMs = time_ms;

	return time_ms - client->startTimeMs;
}

shared_ptr<KcpClient> PyKcp::findOrNewClient(uint32_t nip, uint16_t nport)
{
	shared_ptr<KcpClient> client;

	uint64_t client_id = (nip << 16) + nport;

	client_lock.lock_shared();
	bool empty = clients.find(client_id) == clients.end();
	client_lock.unlock_shared();
	if (empty)
	{
		client = make_shared<KcpClient>();
		client->pyKcp = this;
		client->nip = nip;
		client->nport = nport;

		client->kcp = ikcp_create(0x55, client.get());
		ikcp_wndsize(client->kcp, 64, 64);
		/* fastest: ikcp_nodelay(kcp, 1, 20, 2, 1)
		*  nodelay: 0:disable(default), 1:enable
		*  interval: internal update timer interval in millisec, default is 100ms
		*  resend: 0:disable fast resend(default), 1:enable fast resend
		*  nc: 0:normal congestion control(default), 1:disable congestion control
		*/
		ikcp_nodelay(client->kcp, 1, 20, 1, 1);
		/* extreme settings */
		client->kcp->rx_minrto = 10;

		if (mOnCreate)
		{
			py::gil_scoped_acquire acquire;
			if(mOnCreate(this, client) == false)
				return nullptr;
		}

		ikcp_setoutput(client->kcp, kcpOutputCallback);
		/* Ensure that flush can be invoked successfully immediately. */
		ikcp_update(client->kcp, getBoottimeMs(client));
		client_lock.lock();
		clients[client_id] = client;
		client_lock.unlock();
	} else {
		client_lock.lock_shared();
		client = clients[client_id];
		client_lock.unlock_shared();
	}

	/* update time */
	client->lastTimeMs = getTimeMs();

	return client;
}

shared_ptr<KcpClient> PyKcp::new_client(string ip, uint16_t hport)
{
	return findOrNewClient(inet_addr(ip.c_str()), htons(hport));
}

int PyKcp::client_wndsize(shared_ptr<KcpClient> client, int sndwnd, int rcvsnd)
{
	return ikcp_wndsize(client->kcp, sndwnd, rcvsnd);
}

int PyKcp::client_nodelay(shared_ptr<KcpClient> client, int nodelay, int interval, int resend, int nc)
{
	return ikcp_nodelay(client->kcp, nodelay, interval, resend, nc);
}

int PyKcp::kcpOutputCallback(const char *buf, int len, 
	ikcpcb *kcp, void *user)
{
	KcpClient* client = static_cast<KcpClient*>(user);
	return client->pyKcp->kcpOutput(buf, len, kcp, user);
}

int PyKcp::kcpOutput(const char *buf, int len,
	ikcpcb *kcp, void *user)
{
	sockaddr_in clientAddr;
	KcpClient* client = static_cast<KcpClient*>(user);

	memset(&clientAddr, 0, sizeof(clientAddr));
	clientAddr.sin_family = AF_INET;
	clientAddr.sin_addr.s_addr = client->nip;
	clientAddr.sin_port = client->nport;

	ssize_t bytes_sent = sendto(sockfd, buf, len, 0,
			(struct sockaddr*)&clientAddr, sizeof(clientAddr));
	return bytes_sent;
}

void PyKcp::recvLoop()
{
	ssize_t recv_len;
	sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	while(!exit)
	{
		recv_len = recvfrom(sockfd, recvBuffer, sizeof(recvBuffer), 0, (struct sockaddr*)&client_addr, &client_addr_len);
		if (recv_len < 0)
			continue;

		shared_ptr<KcpClient> client = findOrNewClient(client_addr.sin_addr.s_addr, client_addr.sin_port);
		if (client == nullptr)
			continue;

		kcp_lock.lock();
		ikcp_input(client->kcp, recvBuffer, recv_len);
		ssize_t size = ikcp_peeksize(client->kcp);
		kcp_lock.unlock();
		if(size > 0)
		{
			if (mOnRecv)
			{
				char *buf = new char[size];
				kcp_lock.lock();
				ssize_t size_r = ikcp_recv(client->kcp, buf, size);
				kcp_lock.unlock();
				if(size != size_r)
					throw runtime_error("ikcp_peeksize != ikcp_recv.");

				py::gil_scoped_acquire acquire;
				mOnRecv(this, client, py::bytes(buf, size));
			} else
				semaphore.notify();
		}
	}
}

void PyKcp::updateLoop()
{
	uint64_t now_ms;
	uint32_t boot_ms;
	uint32_t min_sleep;
	vector<int> clear_clients;

	while(!exit)
	{
		min_sleep = 50;
		now_ms = getTimeMs();
		client_lock.lock_shared();
		for (auto it = clients.begin(); it != clients.end(); ++it) {
			shared_ptr<KcpClient> client = it->second;
			boot_ms = getBoottimeMs(client);

			if (now_ms - client->lastTimeMs > timeOutMs)
			{
				if (mOnClean)
				{
					py::gil_scoped_acquire acquire;
					mOnClean(this, client);
				}

				clear_clients.push_back(it->first);
				if(1)
					cout << "client timeout. key:" << it->first << " clear_clients size:" << clear_clients.size() << endl;
				continue;
			}

			if(client->nextUpdate == 0)
			{
				kcp_lock.lock();
				client->nextUpdate = ikcp_check(client->kcp, boot_ms);
				kcp_lock.unlock();
				if(client->nextUpdate - boot_ms < min_sleep)
					min_sleep = client->nextUpdate - boot_ms;
			}

			if(client->nextUpdate <= boot_ms)
			{
				kcp_lock.lock();
				ikcp_update(client->kcp, boot_ms);
				kcp_lock.unlock();
				client->nextUpdate = 0;
			}
		}
		client_lock.unlock_shared();

		for (int value : clear_clients) {
			client_lock.lock();
			clients.erase(value);
			client_lock.unlock();
		}
		clear_clients.clear();

		this_thread::sleep_for(chrono::milliseconds(min_sleep));
	}
}

py::list PyKcp::recv_pkg() {
	py::list bytes_list;
	ssize_t size;

	while(bytes_list.size() == 0 && !mOnRecv)
	{
		client_lock.lock_shared();
		for (const auto& pair : clients) {
			shared_ptr<KcpClient> client = pair.second;
			kcp_lock.lock();
			size = ikcp_peeksize(client->kcp);
			kcp_lock.unlock();
			if(size <= 0)
				continue;

			char *buf = new char[size];
			kcp_lock.lock();
			ssize_t size_r = ikcp_recv(client->kcp, buf, size);
			kcp_lock.unlock();
			if(size != size_r)
				throw runtime_error("ikcp_peeksize != ikcp_recv.");

			bytes_list.append(py::make_tuple(client, py::bytes(buf, size)));
		}
		client_lock.unlock_shared();

		if(bytes_list.size() == 0)
		{
			/* release GIL lock to sleep */
			py::gil_scoped_release release;
			semaphore.wait();
			py::gil_scoped_acquire acquire;
		}
	}

	return bytes_list;
}

int PyKcp::send_pkg(shared_ptr<KcpClient> client, py::bytes bytes) {
	char *buf;
	Py_ssize_t size;
	if (!PyBytes_Check(bytes.ptr())) return -1;
	if (PyBytes_AsStringAndSize(bytes.ptr(), &buf, &size) == -1) return -1;
	kcp_lock.lock();
	int ret = ikcp_send(client->kcp, buf, size);
	kcp_lock.unlock();
	return ret;
}

void PyKcp::flush(shared_ptr<KcpClient> client) {
	kcp_lock.lock();
	ikcp_flush(client->kcp);
	kcp_lock.unlock();
}

int PyKcp::send_and_flush(shared_ptr<KcpClient> client, py::bytes bytes) {
	int ret = send_pkg(client, bytes);
	if(ret < 0) return -1;
	flush(client);
	return ret;
}

PYBIND11_MODULE(ikcp, m) {
	module_init();

	py::class_<KcpClient, shared_ptr<KcpClient>>(m, "KcpClient")
		.def_readwrite("kcp", &KcpClient::kcp)
		.def_readwrite("pyKcp", &KcpClient::pyKcp)
		.def_readwrite("nextUpdate", &KcpClient::nextUpdate)
		.def_readwrite("nip", &KcpClient::nip)
		.def_readwrite("nport", &KcpClient::nport);

	py::class_<PyKcp>(m, "PyKcp")
		.def(py::init<string, uint16_t, uint32_t, bool>(), py::arg("ip"), py::arg("port"), py::arg("timeout") = 6, py::arg("atomicSem") = false)
		.def("new_client", &PyKcp::new_client, "Create a client.")
		.def("client_wndsize", &PyKcp::client_wndsize, "Change kcp window size.")
		.def("client_nodelay", &PyKcp::client_nodelay, "Change kcp nodelay params.")
		.def("set_create_cb", &PyKcp::set_create_cb, "Set a callback function that is called when the client is created.")
		.def("set_clean_cb", &PyKcp::set_clean_cb, "Set a callback function that is called when the client is cleaned up.")
		.def("set_recv_cb", &PyKcp::set_recv_cb, "Set a callback function for data reception. The recv_pkg will become invalid.")
		.def("recv_pkg", &PyKcp::recv_pkg, "Receive data.")
		.def("send_pkg", &PyKcp::send_pkg, "Send data.")
		.def("flush", &PyKcp::flush, "The same as kcp flush.")
		.def("send_and_flush", &PyKcp::send_and_flush, "Send and flush.");
}