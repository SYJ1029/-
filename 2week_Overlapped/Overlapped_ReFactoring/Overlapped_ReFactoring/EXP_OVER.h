#pragma once
#include "stdafx.h"
#include "Protocol.h"


// EXP_OVER: WSAOVERLAPPED 확장 구조체
// 전송할 때 client의 id를 받기 위한 방법

struct EXP_OVER
{
	WSAOVERLAPPED _over;		// Overlapped 구조체
	int client_id;				// 클라이언트의 ID
	WSABUF _wsabuf;				// WSABUF: 메모리 전송 시에 시스템 콜과 복사 비용을 모두 줄일 수 있게 만든 마소의 방식
	char _buf[BUFSIZE];			// 실제로 보낼 데이터

	EXP_OVER(int s_id) : client_id(s_id)
	{
		ZeroMemory(&_over, sizeof(_over));
		_wsabuf.buf = _buf;				// 미리 WSABUF의 buf에 SESSION의 _buf를 연결해놓는다
		_wsabuf.len = BUFSIZE;			// 길이는 최대치라고 생각
	}
};