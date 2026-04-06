#include <iostream>
#include <WS2tcpip.h>
#include <unordered_map>
#include <MSWSock.h>
#include <array>
#include "Protocol.h"

#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "WS2_32.lib")
using namespace std;
constexpr int BUF_SIZE = 200;

enum IOType { IO_SEND, IO_RECV, IO_ACCEPT };


class EXP_OVER {
public:
	WSAOVERLAPPED m_over;
	IOType  m_iotype;
	WSABUF	m_wsa;
	char  m_buff[BUF_SIZE];
	EXP_OVER()
	{
		ZeroMemory(&m_over, sizeof(m_over));
		m_wsa.buf = m_buff;
		m_wsa.len = BUF_SIZE;
	}
	EXP_OVER(IOType iot) : m_iotype(iot)
	{
		ZeroMemory(&m_over, sizeof(m_over));
		m_wsa.buf = m_buff;
		m_wsa.len = BUF_SIZE;
	}
};

void error_display(const wchar_t* msg, int err_no)
{
	WCHAR* lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	std::wcout << msg;
	std::wcout << L" === 에러 " << lpMsgBuf << std::endl;
	while (true);   // 디버깅 용
	LocalFree(lpMsgBuf);
}

class SESSION;
class SESSION {
public:
	SOCKET m_client;
	int m_id;
	bool m_is_connected;
	EXP_OVER m_recv_over;
	char m_user_name[20];
	int m_prev_recv;

	short m_x, m_y;
	
	SESSION() 
	{ 
		m_is_connected = false;
		m_id = 999;
		m_client = INVALID_SOCKET;
		m_recv_over.m_iotype = IO_RECV;
		m_x = 0, m_y = 0;
		m_prev_recv = 0;
	}
	SESSION(int id, SOCKET so) : m_id(id), m_client(so)
	{
	}
	~SESSION()
	{
		if(m_is_connected)
			closesocket(m_client);
	}
	void do_recv()
	{
		DWORD recv_flag = 0;
		memset(&m_recv_over.m_over, 0, sizeof(m_recv_over.m_over));
		m_recv_over.m_iotype = IO_RECV;
		m_recv_over.m_wsa.len = BUF_SIZE;
		WSARecv(m_client, &m_recv_over.m_wsa, 1, 0, &recv_flag, &m_recv_over.m_over, nullptr);
	}
	void do_send(int num_bytes, char* mess)
	{
		EXP_OVER* o = new EXP_OVER(IO_SEND);
		o->m_wsa.len = num_bytes;
		memcpy(o->m_buff, mess, num_bytes);
		WSASend(m_client, &o->m_wsa, 1, 0, 0, &o->m_over, nullptr);
	}

	void send_login_success()
	{
		S2C_LoginResult packet;
		packet.size = sizeof(packet);
		packet.type = S2C_LOGIN_RESULT;
		packet.success = true;
		strcpy_s(packet.message, "Login Successful");
		do_send(packet.size, reinterpret_cast<char*>(&packet));
	}

	
	void send_avatar_info()
	{
		S2C_AvatarInfo packet;
		packet.size = sizeof(packet);
		packet.type = S2C_AVATAR_INFO;
		packet.playerId = m_id;
		packet.x = m_x;
		packet.y = m_y;
		do_send(packet.size, reinterpret_cast<char*>(&packet));
	}

	void send_move_packet(int mover);
	void process_packet(unsigned char* packet)
	{
		PACKET_TYPE type = static_cast<PACKET_TYPE>(packet[1]);
		C2S_Login* login_packet = nullptr;
		C2S_Move* move_packet = nullptr;
		DIRECTION dir = DIR_DOWN;

		switch (type)
		{
		case C2S_LOGIN:
			login_packet = reinterpret_cast<C2S_Login*>(packet);

			strcpy_s(m_user_name, login_packet->userName);
			cout << "Player " << m_user_name << " logged in with ID: " << m_id << endl;

			send_avatar_info();
			break;
		case C2S_MOVE:
			move_packet = reinterpret_cast<C2S_Move*>(packet);
			dir = static_cast<DIRECTION>(move_packet->dir);
			switch (dir)
			{
			case DIR_UP: 
				m_y = max(0, m_y - 1); 
				break;
			case DIR_DOWN: 
				m_y = min(WORLD_HEIGHT - 1, m_y + 1);
				break;
			case DIR_LEFT:
				m_x = max(0, m_x - 1); 
				break;
			case DIR_RIGHT: 
				m_x = min(WORLD_WIDTH - 1, m_x + 1); 
				break;
			}
			cout << "Player " << m_user_name << " moved to (" << m_x << ", " << m_y << ")" << endl;

			for(auto& cl : clients)
			{
				if (!cl.m_is_connected || cl.m_id == m_id) continue;
				cl.send_move_packet(m_id);
			}
			//send_move_packet(m_id);
			break;


		default:
			cout << "Unknown packet type from player " << m_user_name << endl;
			break;
		}
	}

	void send_add_player(int playerId);
	void send_remove_player(int playerId);
};

array<SESSION, MAX_PLAYER> clients;

void SESSION::send_add_player(int playerId)
{
	S2C_AddPlayer packet;
	packet.size = sizeof(packet);
	packet.type = S2C_ADD_PLAYER;
	packet.playerId = playerId;
	SESSION& pl = clients[playerId];

	memcpy(packet.name, pl.m_user_name, sizeof(pl.m_user_name));
	packet.x = pl.m_x;
	packet.y = pl.m_y;

	do_send(packet.size, reinterpret_cast<char*>(&packet));
}

void SESSION::send_move_packet(int mover)
{
	S2C_AvatarInfo packet;
	packet.size = sizeof(packet);
	packet.type = S2C_AVATAR_INFO;
	packet.playerId = mover;
	packet.x = clients[mover].m_x;
	packet.y = clients[mover].m_y;
	do_send(packet.size, reinterpret_cast<char*>(&packet));
}

void SESSION::send_remove_player(int playerId)
{
	S2C_RemovePlayer packet;
	packet.size = sizeof(packet);
	packet.type = S2C_REMOVE_PLAYER;
	packet.playerId = playerId;
	do_send(packet.size, reinterpret_cast<char*>(&packet));
}

void send_login_fail(SOCKET& sock, const char* message)
{
	S2C_LoginResult packet;
	packet.size = sizeof(packet);
	packet.type = S2C_LOGIN_RESULT;
	packet.success = false;
	strcpy_s(packet.message, message);
	WSABUF wsa_buf;
	wsa_buf.buf = reinterpret_cast<char*>(&packet);
	wsa_buf.len = sizeof(packet);
	WSASend(sock, &wsa_buf, 1, 0, 0, nullptr, nullptr);
}

int main()
{
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	SOCKET server = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
	bind(server, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(server, SOMAXCONN);
	HANDLE h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	CreateIoCompletionPort((HANDLE)server, h_iocp, 0, 0);

	SOCKET client_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	EXP_OVER accept_over(IO_ACCEPT);
	AcceptEx(server, client_socket, &accept_over.m_buff, 0,
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
		NULL, &accept_over.m_over);

	for (int playerIndex = 0;;) {
		DWORD num_bytes;
		ULONG_PTR key;
		LPOVERLAPPED over;
		GetQueuedCompletionStatus(h_iocp, &num_bytes, &key, &over, INFINITE);
		if (over == nullptr) {
			error_display(L"GQCS Errror: ", WSAGetLastError());
			if(key != 0) {
				exit(-1);
			}
			cout << "client[" << key << "] disconnected." << endl;
			clients[key].m_is_connected = false;
			for(auto& cl : clients)
			{
				if (!cl.m_is_connected || cl.m_id == key) continue;
				cl.send_remove_player(key);
			}

			closesocket(clients[key].m_client);
			clients[key].m_client = INVALID_SOCKET;

			

			continue;
		}
		EXP_OVER* exp_over = reinterpret_cast<EXP_OVER*>(over);
		switch (exp_over->m_iotype) {
		case IO_ACCEPT:
			cout << "Client connected." << endl;
			playerIndex = -1;
			for(int i = 0; i < MAX_PLAYER; ++i)
				if (!clients[i].m_is_connected) {
					playerIndex = i;
					break;
				}

			if(-1 == playerIndex) {
				cout << "Max player limit reached. Connection refused." << endl;
				send_login_fail(client_socket, "Server is Full");
				closesocket(client_socket);
			}
			else
			{
				CreateIoCompletionPort((HANDLE)client_socket, h_iocp, playerIndex, 0);
				clients[playerIndex].m_is_connected = true;
				clients[playerIndex].m_client = client_socket;
				clients[playerIndex].m_x = 0;
				clients[playerIndex].m_y = 0;

				clients[playerIndex].m_id = playerIndex;
				clients[playerIndex].send_login_success();
				clients[playerIndex].m_prev_recv = 0;

				for(auto& other : clients)
				{
					if (!other.m_is_connected || other.m_id == playerIndex) continue;
					other.send_add_player(playerIndex);
				}
				clients[playerIndex].do_recv();
			}

			client_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
			AcceptEx(server, client_socket, &accept_over.m_buff, 0,
				sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
				NULL, &accept_over.m_over);
			break;
		case IO_RECV:
		{
			cout << "Received message." << endl;
			int player_index = static_cast<int>(key);
			cout << "Client[" << player_index << "] sent: " << endl;

			SESSION& cl = clients[player_index];

			// 재조립
			unsigned char* p = reinterpret_cast<unsigned char*>(exp_over->m_buff);
			int data_size = num_bytes + cl.m_prev_recv;
			while(data_size > 0)
			{
				int packet_size = p[0];
				if (data_size < packet_size) break; // 패킷이 완성되지 않음
				// 패킷 처리
				cl.process_packet(p);
				// 다음 패킷으로 이동
				p += packet_size;
				data_size -= packet_size;
			}

			if(data_size > 0)
			{
				// 남은 데이터는 다음 recv 때 처리
				memmove(cl.m_recv_over.m_buff, p, data_size);
				cl.m_prev_recv = data_size;
			}

			cl.process_packet(reinterpret_cast<unsigned char*>(exp_over->m_buff));
			cl.do_recv();


			for (auto& cl : clients)
			{
				//if (cl.first == client_id) continue;
				cl.do_send(num_bytes, clients[player_index].m_recv_over.m_buff);
			}

		}
		break;
		case IO_SEND: {
			cout << "Message sent to client[" << key << "]\n" << endl;
			EXP_OVER* o = reinterpret_cast<EXP_OVER*>(over);
			delete o;
		}
					break;
		default:
			cout << "Unknown IO type." << endl;
			exit(-1);
			break;
		}
	}

	closesocket(server);
	WSACleanup();
}
