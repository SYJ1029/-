#include <iostream>
#include <WS2tcpip.h>
using namespace std;
#pragma comment (lib, "WS2_32.LIB")
const short SERVER_PORT = 4000;
const int BUFSIZE = 4096;
int main()
{
	std::wcout.imbue(std::locale("korean")); // 에러 문자를 한글로 출력해요
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	SOCKET s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, 0);
	SOCKADDR_IN server_addr;
	ZeroMemory(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind(s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(s_socket, SOMAXCONN);
	INT addr_size = sizeof(server_addr);
	SOCKET c_socket = WSAAccept(s_socket, reinterpret_cast<sockaddr*>(&server_addr), &addr_size, 0, 0);
	for (;;) {
		char recv_buf[BUFSIZE];
		WSABUF recv_wsaBuf{ BUFSIZE, recv_buf };
		DWORD recv_byte = 0;
		DWORD recv_flag = 0;
		WSARecv(c_socket, &recv_wsaBuf, 1, &recv_byte, &recv_flag, nullptr, nullptr);
		cout << "Client Sent [" << recv_byte << "bytes] : " << recv_buf << endl;

		DWORD sent_byte = 0;
		WSABUF send_wsaBuf{ recv_byte, recv_buf };
		WSASend(c_socket, &send_wsaBuf, 1, &sent_byte, 0, nullptr, nullptr);
		cout << "Server Sent [" << sent_byte << "bytes] : " << recv_buf << endl;

	}
	WSACleanup();
}
