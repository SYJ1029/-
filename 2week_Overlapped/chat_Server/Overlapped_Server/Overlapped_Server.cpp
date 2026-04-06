#include <iostream>
#include <memory>
#include <unordered_map>
#include <WS2tcpip.h>

using namespace std;
#pragma comment(lib, "WS2_32.LIB")

const short SERVER_PORT = 4000;
const int BUFSIZE = 256;

class SESSION;
struct EXP_OVER;

void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED send_over, DWORD recv_flag);
void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED recv_over, DWORD recv_flag);

struct EXP_OVER
{
	WSAOVERLAPPED _over;
	int client_id;
	WSABUF _wsabuf;
	char _buf[BUFSIZE];

	EXP_OVER(int s_id) : client_id(s_id)
	{
		ZeroMemory(&_over, sizeof(_over));
		_wsabuf.buf = _buf;
		_wsabuf.len = BUFSIZE;
	}
};

class SESSION
{
public:
	int _id;
	SOCKET _socket;
	EXP_OVER _recv_over;

public:
	SESSION(int id, SOCKET s)
		: _id(id), _socket(s), _recv_over(id)
	{}

	~SESSION()
	{
		if (_socket != INVALID_SOCKET) closesocket(_socket);
	}

	void do_recv()
	{
		DWORD recv_flag = 0;
		DWORD recv_bytes = 0;
		ZeroMemory(&_recv_over._over, sizeof(_recv_over._over));
		_recv_over._wsabuf.len = BUFSIZE;
		WSARecv(_socket, &_recv_over._wsabuf, 1, &recv_bytes, &recv_flag, &_recv_over._over, recv_callback);
	}

	void do_send(const char* data, DWORD num_bytes)
	{
		auto* send_ex = new EXP_OVER(_id);
		memcpy(send_ex->_buf, data, num_bytes);
		send_ex->_wsabuf.len = num_bytes;
		DWORD send_bytes = 0;
		WSASend(_socket, &send_ex->_wsabuf, 1, &send_bytes, 0, &send_ex->_over, send_callback);
	}
};

unordered_map<int, unique_ptr<SESSION>> clients;

void disconnect_client(int id)
{
	auto it = clients.find(id);
	if (it != clients.end()) clients.erase(it);
}

void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED send_over, DWORD recv_flag)
{
	auto* ex = reinterpret_cast<EXP_OVER*>(send_over);
	int id = ex->client_id;
	delete ex;  // send 완료 → 해제

	auto it = clients.find(id);
	if (it == clients.end()) return;

	if (err != 0 || num_bytes == 0) {
		disconnect_client(id);
		return;
	}
	//it->second->do_recv();
}

void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED recv_over, DWORD recv_flag)
{
	auto* ex = reinterpret_cast<EXP_OVER*>(recv_over);
	int id = ex->client_id;

	auto it = clients.find(id);
	if (it == clients.end()) return;

	if (err != 0 || num_bytes == 0) {
		disconnect_client(id);
		return;
	}

	SESSION* session = it->second.get();

	cout << "Client[" << id << "] Sent [" << num_bytes << "bytes] : ";
	cout.write(session->_recv_over._buf, num_bytes);
	cout << endl;

	// 받은 데이터를 모든 Client에게 BroadCast
	for (auto& pair : clients) {
		pair.second->do_send(session->_recv_over._buf, num_bytes);
	}
	//session->do_send(session->_recv_over._buf, num_bytes);

	session->do_recv();
}

int main()
{
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);

	SOCKET s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);

	SOCKADDR_IN server_addr;
	ZeroMemory(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	bind(s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(s_socket, SOMAXCONN);

	INT addr_size = sizeof(server_addr);

	for (int i = 1;; ++i) {
		SOCKET c_socket = WSAAccept(s_socket, reinterpret_cast<sockaddr*>(&server_addr), &addr_size, 0, 0);
		clients.emplace(i, make_unique<SESSION>(i, c_socket));
		clients[i]->do_recv();
	}

	clients.clear();
	closesocket(s_socket);
	WSACleanup();
}