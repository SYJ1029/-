#pragma once
#include <unordered_map>
#include <memory>
#include "Protocol.h"
#include "EXP_OVER.h"
#include "NetworkCallback.h"

// SESSION: 하나의 클라이언트를 관리하기 위한 클래스
// 클라이언트와 연결된 소켓 뿐만 아니라, 이 클라이언트를 식별하기 위한 ID, 그리고 데이터를 전달받기 위한 OVERLAPPED 구조체가 필요하다


class SESSION
{
public:
	int _id;		// 이 클라이언트의 id
	SOCKET _socket; // 클라와 연결된 소켓
	EXP_OVER _recv_over; // EXP_OVER 확장 구조

public:
	SESSION(int id, SOCKET s)
		: _id(id), _socket(s), _recv_over(id) // id, 소켓, RECV OVER 초기화하기
	{
	}

	~SESSION()
	{
		if (_socket != INVALID_SOCKET) closesocket(_socket); // 소켓이 제대로 만들어진 소켓이었다면 정상 종료처리
	}

	inline void do_recv()
	{
		DWORD recv_flag = 0;	// recv_flag: 전달받은 데이터의 flag가 들어갈 자리
		DWORD recv_bytes = 0;  // recv_bytes: 전달받은 데이터의 크기가 들어갈 자리
		ZeroMemory(&_recv_over._over, sizeof(_recv_over._over));  // recv_over를 초기화
		_recv_over._wsabuf.len = BUFSIZE;		// WSABUF에 길이를 기록한다. 여기선 최대길이를 항상 넣고 있음
		WSARecv(_socket, &_recv_over._wsabuf, 1, &recv_bytes, &recv_flag, &_recv_over._over, recv_callback);		// 수신 예약하기
	}

	inline void do_send(const char* data, DWORD num_bytes)
	{
		auto* send_ex = new EXP_OVER(_id);							// Send의 경우 새로 구조체를 만들어서 담는다
		memcpy(send_ex->_buf, data, num_bytes);						// 구조체의 내부 buf에 data 복사
		send_ex->_wsabuf.len = num_bytes;							// WSABUF에 길이를 기록한다
		DWORD send_bytes = 0;										// 전송한 크기가 들어갈 자리
		WSASend(_socket, &send_ex->_wsabuf, 1, &send_bytes, 0, &send_ex->_over, send_callback);			// 전송 예약
	}
};

extern std::unordered_map<int, std::unique_ptr<SESSION>> clients;
