#include "stdafx.h"
#include <iostream>
#include "EXP_OVER.h"
#include "SESSION.h"

using namespace std;



int main()
{
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);

	SOCKET s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);		// 소켓 만들기

	SOCKADDR_IN server_addr;							// IP와 포트번호 등등을 담는 구조체
	ZeroMemory(&server_addr, sizeof(server_addr));		//초기화
	server_addr.sin_family = AF_INET;					// ipv4? ipv6?
	server_addr.sin_port = htons(SERVER_PORT);			// 포트번호
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);	// 연결을 허락할 주소 - INADDR_ANY: 난 다 받을거임

	bind(s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));		// 작성한 SOCKADDR_IN을 소켓에 연결
	listen(s_socket, SOMAXCONN);			// listen 소켓으로 만들기

	INT addr_size = sizeof(server_addr);	// 아까 만든 server_addr의 사이즈 기록하기

	for (int i = 1;; ++i) {
		SOCKET c_socket = WSAAccept(s_socket, reinterpret_cast<sockaddr*>(&server_addr), &addr_size, 0, 0);		// Accpet!
		// 이 Accept는 동기식이다

		clients.emplace(i, make_unique<SESSION>(i, c_socket));		// Accept 성공하면 client에 추가하기
		clients[i]->do_recv();				// 수신을 시작하며 콜백 구조로 넘김
	}

	clients.clear();
	closesocket(s_socket);
	WSACleanup();
}