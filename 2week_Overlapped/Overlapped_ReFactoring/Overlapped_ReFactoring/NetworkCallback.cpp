#include "stdafx.h"
#include "NetworkCallback.h"
#include "SESSION.h"



// CALLBACK, disconnect 등 콜백, 연결 끊기 함수가 있는 위치

void disconnect_client(int id)
{
	// disconnect: 서버에서 해당 클라이언트를 지운다
	auto it = clients.find(id);
	if (it != clients.end()) clients.erase(it);
}


void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED send_over, DWORD recv_flag)
{
	// send_callback 함수
	// 송신 작업이 처리가 완료된 이후에 실행되는 함수
	auto* ex = reinterpret_cast<EXP_OVER*>(send_over); // 보낸 send_over을 EXP_OVER 포인터로 바꿔서 받음
	int id = ex->client_id; // id 받아오기
	delete ex;  // send 완료 → 해제: Send의 경우 EXP_OVER를 즉석에서 만들어 보냈기 때문에 전송이 성공했다면 지워줘야함

	auto it = clients.find(id); // 받아온 id로 클라찾기: 전달받은 id값이 손상되었을 수 있음
	if (it == clients.end()) return; //  그래서 체크함

	if (err != 0 || num_bytes == 0) {
		// 에러가 나지 않았는데 아무것도 보내지 않았음 = 예약할 때부터 보내겠다고 한 게 없음 = 클라가 나갔다
		disconnect_client(id); // 연결 끊어주기
		return;
	}
	//it->second->do_recv();
}

void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED recv_over, DWORD recv_flag)
{
	// 수신 완료후 실행되는 함수

	auto* ex = reinterpret_cast<EXP_OVER*>(recv_over);		// 똑같이 cast해서 받음
	int id = ex->client_id;									// id 가져오기

	auto it = clients.find(id);
	if (it == clients.end()) return;						// id 에러 대비

	if (err != 0 || num_bytes == 0) {
		disconnect_client(id);								// 에러가 없는데 보낸 크기가 0 = 클라가 나감
		return;
	}

	SESSION* session = it->second.get();				// Iterator에서 실제 session을 가지고 옴

	std::cout << "Client[" << id << "] Sent [" << num_bytes << "bytes] : ";
	std::cout.write(session->_recv_over._buf, num_bytes);
	std::cout << std::endl;

	// 받은 데이터를 모든 Client에게 BroadCast
	for (auto& pair : clients) {
		pair.second->do_send(session->_recv_over._buf, num_bytes);			// 전달받은 데이터를 즉시 브로드캐스팅 해야함
	}
	//session->do_send(session->_recv_over._buf, num_bytes);

	session->do_recv();				// 같은 클라에게 계속 메시지를 받아야 하므로 수신 예약을 다시 건다
}