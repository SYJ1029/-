#include <iostream>
#include <WS2tcpip.h>
#include <array>
#include <thread>
#include <mutex>
#include "protocol.h"

#pragma comment(lib, "WS2_32.lib")
using namespace std;

constexpr int BUF_SIZE = 200;

class SESSION;
std::array<SESSION, MAX_PLAYERS> clients;
std::mutex g_mutex; // 클라이언트 배열 접근 보호

class SESSION {
public:
	SOCKET m_client;
	int m_id;
	bool m_is_connected;
	char m_recv_buff[BUF_SIZE];
	int m_prev_recv;
	char m_username[MAX_NAME_LEN];
	short m_x, m_y;

	SESSION() {
		m_is_connected = false;
		m_id = 999;
		m_client = INVALID_SOCKET;
		m_x = 0; m_y = 0;
		m_prev_recv = 0;
	}
	~SESSION()
	{
		if (m_is_connected)
			closesocket(m_client);
	}
	void do_send(int num_bytes, char* mess)
	{
		send(m_client, mess, num_bytes, 0);
	}
	void send_avatar_info()
	{
		S2C_AvatarInfo packet;
		packet.size = sizeof(S2C_AvatarInfo);
		packet.type = S2C_AVATAR_INFO;
		packet.playerId = m_id;
		packet.x = m_x;
		packet.y = m_y;
		do_send(packet.size, reinterpret_cast<char*>(&packet));
	}
	void send_move_packet(int mover);
	void send_add_player(int player_id);
	void send_login_success()
	{
		S2C_LoginResult packet;
		packet.size = sizeof(S2C_LoginResult);
		packet.type = S2C_LOGIN_RESULT;
		packet.success = true;
		strncpy_s(packet.message, "Login successful.", sizeof(packet.message));
		do_send(packet.size, reinterpret_cast<char*>(&packet));
	}
	void send_remove_player(int player_id)
	{
		S2C_RemovePlayer packet;
		packet.size = sizeof(S2C_RemovePlayer);
		packet.type = S2C_REMOVE_PLAYER;
		packet.playerId = player_id;
		do_send(packet.size, reinterpret_cast<char*>(&packet));
	}
	void process_packet(unsigned char* p);
};

void disconnect(int key)
{
	// 호출 전 g_mutex가 이미 잠겨있어야 함
	if (false == clients[key].m_is_connected) return;
	cout << "Player[" << key << "] (" << clients[key].m_username << ") Disconnected.\n";
	clients[key].m_is_connected = false;
	for (auto& cl : clients)
		if (true == cl.m_is_connected)
			cl.send_remove_player(key);
	closesocket(clients[key].m_client);
	clients[key].m_client = INVALID_SOCKET;
}

void SESSION::send_add_player(int player_id)
{
	S2C_AddPlayer packet;
	packet.size = sizeof(S2C_AddPlayer);
	packet.type = S2C_ADD_PLAYER;
	packet.playerId = player_id;
	SESSION& pl = clients[player_id];
	memcpy(packet.username, pl.m_username, sizeof(packet.username));
	packet.x = pl.m_x;
	packet.y = pl.m_y;
	do_send(packet.size, reinterpret_cast<char*>(&packet));
}

void SESSION::process_packet(unsigned char* p)
{
	PACKET_TYPE type = *reinterpret_cast<PACKET_TYPE*>(&p[1]);
	switch (type) {
	case C2S_LOGIN: {
		C2S_Login* packet = reinterpret_cast<C2S_Login*>(p);
		std::lock_guard<std::mutex> lock(g_mutex);
		strncpy_s(m_username, packet->username, MAX_NAME_LEN);
		cout << "Player[" << m_id << "] logged in as " << m_username << endl;

		for (auto& other : clients) {
			if (false == other.m_is_connected) continue;
			if (other.m_id == m_id) continue;
			other.send_add_player(m_id);
			clients[m_id].send_add_player(other.m_id);
		}

		send_avatar_info();
	}
				  break;
	case C2S_MOVE: {
		C2S_Move* packet = reinterpret_cast<C2S_Move*>(p);
		DIRECTION dir = packet->dir;
		std::lock_guard<std::mutex> lock(g_mutex);
		switch (dir) {
		case UP: m_y = max(0, m_y - 1); break;
		case DOWN: m_y = min(WORLD_HEIGHT - 1, m_y + 1); break;
		case LEFT: m_x = max(0, m_x - 1); break;
		case RIGHT: m_x = min(WORLD_WIDTH - 1, m_x + 1); break;
		}
		for (auto& cl : clients)
			if (true == cl.m_is_connected)
				cl.send_move_packet(m_id);
	}
				 break;
	default:
		break;
	}
}

void SESSION::send_move_packet(int mover)
{
	S2C_MovePlayer packet;
	packet.size = sizeof(S2C_MovePlayer);
	packet.type = S2C_MOVE_PLAYER;
	packet.playerId = mover;
	packet.x = clients[mover].m_x;
	packet.y = clients[mover].m_y;
	do_send(packet.size, reinterpret_cast<char*>(&packet));
}

void send_login_fail(SOCKET client, const char* message)
{
	S2C_LoginResult packet;
	packet.size = sizeof(S2C_LoginResult);
	packet.type = S2C_LOGIN_RESULT;
	packet.success = false;
	strncpy_s(packet.message, message, sizeof(packet.message));
	send(client, reinterpret_cast<char*>(&packet), packet.size, 0);
}

void client_thread(int player_index)
{
	SESSION& cl = clients[player_index];

	for (;;) {
		int received = recv(cl.m_client, cl.m_recv_buff + cl.m_prev_recv,
			BUF_SIZE - cl.m_prev_recv, 0);
		if (received <= 0) {
			std::lock_guard<std::mutex> lock(g_mutex);
			disconnect(player_index);
			return;
		}

		unsigned char* p = reinterpret_cast<unsigned char*>(cl.m_recv_buff);
		int data_size = received + cl.m_prev_recv;
		while (data_size > 0) {
			int packet_size = p[0];
			if (packet_size > data_size) break;
			cl.process_packet(p);
			p += packet_size;
			data_size -= packet_size;
		}
		if (data_size > 0) {
			memmove(cl.m_recv_buff, p, data_size);
			cl.m_prev_recv = data_size;
		}
		else {
			cl.m_prev_recv = 0;
		}
	}
}

int main()
{
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
	SOCKADDR_IN server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
	bind(server, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(server, SOMAXCONN);

	cout << "MultiThread I/O Server Started on port " << PORT << endl;

	for (;;) {
		SOCKADDR_IN client_addr;
		int addr_len = sizeof(client_addr);
		SOCKET client_socket = accept(server, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
		if (client_socket == INVALID_SOCKET) {
			cout << "accept failed: " << WSAGetLastError() << endl;
			continue;
		}

		int player_index = -1;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			for (int i = 0; i < MAX_PLAYERS; ++i)
				if (!clients[i].m_is_connected) {
					player_index = i;
					break;
				}
			if (-1 == player_index) {
				send_login_fail(client_socket, "Server is full.");
				closesocket(client_socket);
				continue;
			}
			clients[player_index].m_is_connected = true;
			clients[player_index].m_client = client_socket;
			clients[player_index].m_x = 0;
			clients[player_index].m_y = 0;
			clients[player_index].m_id = player_index;
			clients[player_index].m_prev_recv = 0;
			clients[player_index].send_login_success();
		}

		// 클라이언트 전용 쓰레드 생성
		std::thread t(client_thread, player_index);
		t.detach();
	}

	closesocket(server);
	WSACleanup();
}
