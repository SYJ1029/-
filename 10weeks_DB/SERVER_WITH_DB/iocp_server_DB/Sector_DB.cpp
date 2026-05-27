#include <iostream>
#include <stdio.h>
#include <WS2tcpip.h>
#include <array>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <chrono>
#include <atomic>
#include <memory>
#include <concurrent_queue.h>
#include <concurrent_priority_queue.h>
#include <tbb/concurrent_unordered_map.h>
#include <sqlext.h>
#include "protocol_2026.h"

#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "odbc32.lib")
using namespace std;
using namespace std::chrono;

constexpr int BUF_SIZE = 200;
constexpr int VIEW_RANGE = 5;
constexpr int MOVE_COOL_TIME = 1000;
constexpr int EVENT_NPC_MOVE = 1;

constexpr int SECTOR_SIZE = VIEW_RANGE * 2 + 1;
constexpr int MAX_SECTORS_X = (WORLD_WIDTH + SECTOR_SIZE - 1) / SECTOR_SIZE;
constexpr int MAX_SECTORS_Y = (WORLD_HEIGHT + SECTOR_SIZE - 1) / SECTOR_SIZE;

constexpr int SECTOR_X(int x) { return x / SECTOR_SIZE; }
constexpr int SECTOR_Y(int y) { return y / SECTOR_SIZE; }
constexpr int SECTOR_ID(int sx, int sy) { return sy * MAX_SECTORS_X + sx; }

struct event_type {
	int obj_id;
	system_clock::time_point wakeup_time;
	int event_id;

	constexpr bool operator < (const event_type& other) const
	{
		return wakeup_time > other.wakeup_time;
	}
};

concurrency::concurrent_priority_queue<event_type> timer_queue;

enum IOType { IO_SEND, IO_RECV, IO_ACCEPT, IO_NPC_MOVE, IO_DB_LOGIN };
enum DB_TYPE { DB_LOGIN, DB_SAVE_POSITION };

struct DB_EVENT {
	DB_TYPE type;
	int session_id;
	int user_id;
	short x, y;
};

struct DB_RESULT {
	bool success;
	int user_id;
	short x, y;
	bool has_position;
};

concurrency::concurrent_queue<DB_EVENT> db_queue;

class EXP_OVER {
public:
	WSAOVERLAPPED m_over;
	IOType m_iotype;
	WSABUF m_wsa;
	SOCKET m_client_socket;
	char m_buff[BUF_SIZE];
	DB_RESULT m_db_result;

	EXP_OVER() : m_iotype(IO_RECV), m_client_socket(INVALID_SOCKET),
		m_db_result{ false, -1, 0, 0, false }
	{
		ZeroMemory(&m_over, sizeof(m_over));
		m_wsa.buf = m_buff;
		m_wsa.len = BUF_SIZE;
	}

	EXP_OVER(IOType iot) : m_iotype(iot), m_client_socket(INVALID_SOCKET),
		m_db_result{ false, -1, 0, 0, false }
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
	std::wcout << L" === error " << lpMsgBuf << std::endl;
	LocalFree(lpMsgBuf);
}

enum CL_STATE { CS_FREE, CS_CONNECT, CS_DB_WAIT, CS_PLAYING, CS_LOGOUT };

void request_login(int session_id, int user_id);
void request_save_position(int user_id, short x, short y);

bool is_pc(int id)
{
	return id < NPC_ID_START;
}

bool is_npc(int id)
{
	return id >= NPC_ID_START;
}

class SECTOR {
public:
	std::unordered_set<int> m_objects;
	std::mutex m_mutex;

	void add_object(int object_id)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_objects.insert(object_id);
	}

	void remove_object(int object_id)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_objects.erase(object_id);
	}
};

class SectorManager {
public:
	SectorManager()
	{
		for (auto& sector : sectors)
			sector = std::make_shared<SECTOR>();
	}

	void add_object_to_sector(int object_id, short x, short y)
	{
		sectors[sector_id(x, y)]->add_object(object_id);
	}

	void remove_object_from_sector(int object_id, short x, short y)
	{
		sectors[sector_id(x, y)]->remove_object(object_id);
	}

	void update_object_sector(int object_id, short old_x, short old_y, short new_x, short new_y)
	{
		int old_sid = sector_id(old_x, old_y);
		int new_sid = sector_id(new_x, new_y);
		if (old_sid == new_sid) return;

		sectors[old_sid]->remove_object(object_id);
		sectors[new_sid]->add_object(object_id);
	}

	std::vector<int> get_objects_in_adjacent_sectors(short x, short y)
	{
		std::vector<int> result;
		int sx = SECTOR_X(x);
		int sy = SECTOR_Y(y);
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				int nsx = sx + dx;
				int nsy = sy + dy;
				if (nsx < 0 || nsx >= MAX_SECTORS_X || nsy < 0 || nsy >= MAX_SECTORS_Y)
					continue;

				auto& sector = sectors[SECTOR_ID(nsx, nsy)];
				std::lock_guard<std::mutex> lock(sector->m_mutex);
				result.insert(result.end(), sector->m_objects.begin(), sector->m_objects.end());
			}
		}
		return result;
	}

private:
	int sector_id(short x, short y)
	{
		return SECTOR_ID(SECTOR_X(x), SECTOR_Y(y));
	}

	std::array<std::shared_ptr<SECTOR>, MAX_SECTORS_X* MAX_SECTORS_Y> sectors;
};

class SESSION {
public:
	SOCKET m_client;
	int m_id;
	int m_db_user_id;
	CL_STATE m_state;
	EXP_OVER m_recv_over;
	int m_prev_recv;
	char m_username[MAX_NAME_LEN];
	short m_x, m_y;
	int m_move_time;
	bool m_is_npc;
	std::atomic<bool> m_active_npc;
	system_clock::time_point m_last_npc_move_time;
	std::unordered_set<int> m_visible_objects;
	std::mutex m_visible_mutex;

	SESSION()
		: m_client(INVALID_SOCKET), m_id(-1), m_db_user_id(-1), m_state(CS_FREE), m_prev_recv(0),
		  m_x(0), m_y(0), m_move_time(0), m_is_npc(false), m_active_npc(false)
	{
		m_username[0] = 0;
		m_recv_over.m_iotype = IO_RECV;
		m_last_npc_move_time = system_clock::now();
	}

	SESSION(SOCKET s, int id)
		: m_client(s), m_id(id), m_db_user_id(-1), m_state(CS_CONNECT), m_prev_recv(0),
		  m_move_time(0), m_is_npc(false), m_active_npc(false)
	{
		m_recv_over.m_iotype = IO_RECV;
		m_x = rand() % WORLD_WIDTH;
		m_y = rand() % WORLD_HEIGHT;
		m_username[0] = 0;
		m_last_npc_move_time = system_clock::now();
	}

	~SESSION()
	{
		if (m_client != INVALID_SOCKET)
			closesocket(m_client);
	}

	bool can_see(short x, short y) const
	{
		return abs(m_x - x) <= VIEW_RANGE && abs(m_y - y) <= VIEW_RANGE;
	}

	bool can_send() const
	{
		return (m_id < MAX_PLAYERS) && m_client != INVALID_SOCKET;
	}

	void do_recv()
	{
		DWORD recv_flag = 0;
		memset(&m_recv_over.m_over, 0, sizeof(m_recv_over.m_over));
		m_recv_over.m_wsa.len = BUF_SIZE - m_prev_recv;
		m_recv_over.m_wsa.buf = m_recv_over.m_buff + m_prev_recv;
		WSARecv(m_client, &m_recv_over.m_wsa, 1, 0, &recv_flag, &m_recv_over.m_over, nullptr);
	}

	void do_send(int num_bytes, char* data)
	{
		if (!can_send()) return;

		EXP_OVER* over = new EXP_OVER(IO_SEND);
		over->m_wsa.len = num_bytes;
		memcpy(over->m_buff, data, num_bytes);
		WSASend(m_client, &over->m_wsa, 1, 0, 0, &over->m_over, nullptr);
	}

	void send_login_success()
	{
		S2C_LoginResult packet;
		packet.size = sizeof(packet);
		packet.type = S2C_LOGIN_RESULT;
		packet.success = true;
		strcpy_s(packet.message, "Login successful.");
		do_send(packet.size, reinterpret_cast<char*>(&packet));
	}

	void send_login_fail()
	{
		S2C_LoginResult packet;
		packet.size = sizeof(packet);
		packet.type = S2C_LOGIN_RESULT;
		packet.success = false;
		strcpy_s(packet.message, "Login failed.");
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

	void send_add_object(int object_id);
	void send_remove_object(int object_id);
	void send_move_object(int object_id);
	bool process_packet(unsigned char* p);
	void do_move(DIRECTION dir);
	void do_random_move();
	void wake_up();
};

tbb::concurrent_unordered_map<int, std::atomic<std::shared_ptr<SESSION>>> clients;
SectorManager sector_manager;

SOCKET g_server;
HANDLE g_iocp;
std::atomic<int> player_index = 1;

std::shared_ptr<SESSION> get_session(int id)
{
	auto iter = clients.find(id);
	if (iter == clients.end()) return nullptr;
	return iter->second.load();
}

void request_login(int session_id, int user_id)
{
	DB_EVENT ev;
	ev.type = DB_LOGIN;
	ev.session_id = session_id;
	ev.user_id = user_id;
	ev.x = 0;
	ev.y = 0;
	db_queue.push(ev);
}

void request_save_position(int user_id, short x, short y)
{
	DB_EVENT ev;
	ev.type = DB_SAVE_POSITION;
	ev.session_id = -1;
	ev.user_id = user_id;
	ev.x = x;
	ev.y = y;
	db_queue.push(ev);
}

void SESSION::send_add_object(int object_id)
{
	if (!can_send()) return;

	std::shared_ptr<SESSION> obj = get_session(object_id);
	if (nullptr == obj) return;
	if (obj->m_state != CS_PLAYING) return;

	{
		std::lock_guard<std::mutex> lock(m_visible_mutex);
		if (m_visible_objects.count(object_id) > 0) return;
		m_visible_objects.insert(object_id);
	}

	S2C_AddPlayer packet;
	packet.size = sizeof(packet);
	packet.type = S2C_ADD_PLAYER;
	packet.playerId = object_id;
	strcpy_s(packet.username, obj->m_username);
	packet.x = obj->m_x;
	packet.y = obj->m_y;
	do_send(packet.size, reinterpret_cast<char*>(&packet));
}

void SESSION::send_remove_object(int object_id)
{
	if (!can_send()) return;

	{
		std::lock_guard<std::mutex> lock(m_visible_mutex);
		if (m_visible_objects.count(object_id) == 0) return;
		m_visible_objects.erase(object_id);
	}

	S2C_RemovePlayer packet;
	packet.size = sizeof(packet);
	packet.type = S2C_REMOVE_PLAYER;
	packet.playerId = object_id;
	do_send(packet.size, reinterpret_cast<char*>(&packet));
}

void SESSION::send_move_object(int object_id)
{
	if (!can_send()) return;

	std::shared_ptr<SESSION> obj = get_session(object_id);
	if (nullptr == obj) return;
	if (obj->m_state != CS_PLAYING) return;

	S2C_MovePlayer packet;
	packet.size = sizeof(packet);
	packet.type = S2C_MOVE_PLAYER;
	packet.playerId = object_id;
	packet.x = obj->m_x;
	packet.y = obj->m_y;
	packet.move_time = obj->m_move_time;
	do_send(packet.size, reinterpret_cast<char*>(&packet));
}

void add_object_to_viewer_if_needed(std::shared_ptr<SESSION> viewer, int object_id)
{
	if (nullptr == viewer || !viewer->can_send()) return;
	viewer->send_add_object(object_id);
}

void update_player_view(int player_id)
{
	std::shared_ptr<SESSION> player = get_session(player_id);
	if (nullptr == player || !player->can_send()) return;

	std::unordered_set<int> old_view;
	{
		std::lock_guard<std::mutex> lock(player->m_visible_mutex);
		old_view = player->m_visible_objects;
	}

	std::unordered_set<int> new_view;
	for (int object_id : sector_manager.get_objects_in_adjacent_sectors(player->m_x, player->m_y)) {
		if (object_id == player_id) continue;

		std::shared_ptr<SESSION> obj = get_session(object_id);
		if (nullptr == obj) continue;
		if (obj->m_state != CS_PLAYING) continue;
		if (player->can_see(obj->m_x, obj->m_y))
			new_view.insert(object_id);
	}

	player->send_move_object(player_id);

	for (int object_id : new_view) {
		std::shared_ptr<SESSION> obj = get_session(object_id);
		if (nullptr == obj) continue;

		if (old_view.count(object_id) == 0) {
			player->send_add_object(object_id);
			if (is_pc(object_id))
				obj->send_add_object(player_id);
			else
				obj->wake_up();
		}
		else if (is_pc(object_id)) {
			obj->send_move_object(player_id);
		}
	}

	for (int object_id : old_view) {
		if (new_view.count(object_id) != 0) continue;

		player->send_remove_object(object_id);
		if (is_pc(object_id)) {
			std::shared_ptr<SESSION> other = get_session(object_id);
			if (nullptr != other)
				other->send_remove_object(player_id);
		}
	}
}

bool SESSION::process_packet(unsigned char* p)
{
	PACKET_TYPE type = *reinterpret_cast<PACKET_TYPE*>(&p[1]);
	switch (type) {
	case C2S_LOGIN:
	{
		C2S_Login* packet = reinterpret_cast<C2S_Login*>(p);
		if (m_state != CS_CONNECT) return false;
		m_state = CS_DB_WAIT;
		request_login(m_id, packet->user_id);
		break;
	}
	case C2S_MOVE:
	{
		if (m_state != CS_PLAYING) return false;
		C2S_Move* packet = reinterpret_cast<C2S_Move*>(p);
		m_move_time = packet->move_time;
		do_move(packet->dir);
		break;
	}
	default:
		cout << "Unknown packet type received from player[" << m_id << "].\n";
		return false;
	}
	return true;
}

void SESSION::do_move(DIRECTION dir)
{
	short old_x = m_x;
	short old_y = m_y;

	switch (dir) {
	case UP: if (m_y > 0) --m_y; break;
	case DOWN: if (m_y < WORLD_HEIGHT - 1) ++m_y; break;
	case LEFT: if (m_x > 0) --m_x; break;
	case RIGHT: if (m_x < WORLD_WIDTH - 1) ++m_x; break;
	}

	sector_manager.update_object_sector(m_id, old_x, old_y, m_x, m_y);
	update_player_view(m_id);
	if (m_db_user_id != -1)
		request_save_position(m_db_user_id, m_x, m_y);
}

void SESSION::do_random_move()
{
	short old_x = m_x;
	short old_y = m_y;
	std::unordered_set<int> old_viewers;
	for (int object_id : sector_manager.get_objects_in_adjacent_sectors(old_x, old_y)) {
		if (!is_pc(object_id)) continue;

		std::shared_ptr<SESSION> player = get_session(object_id);
		if (nullptr == player || !player->can_send()) continue;
		if (player->can_see(old_x, old_y))
			old_viewers.insert(object_id);
	}

	switch (rand() % 4) {
	case 0: if (m_y > 0) --m_y; break;
	case 1: if (m_y < WORLD_HEIGHT - 1) ++m_y; break;
	case 2: if (m_x > 0) --m_x; break;
	case 3: if (m_x < WORLD_WIDTH - 1) ++m_x; break;
	}

	sector_manager.update_object_sector(m_id, old_x, old_y, m_x, m_y);
	m_last_npc_move_time = system_clock::now();

	std::unordered_set<int> new_viewers;
	for (int object_id : sector_manager.get_objects_in_adjacent_sectors(m_x, m_y)) {
		if (!is_pc(object_id)) continue;

		std::shared_ptr<SESSION> player = get_session(object_id);
		if (nullptr == player || !player->can_send()) continue;
		if (!player->can_see(m_x, m_y)) continue;
		new_viewers.insert(object_id);

		bool already_visible = false;
		{
			std::lock_guard<std::mutex> lock(player->m_visible_mutex);
			already_visible = player->m_visible_objects.count(m_id) > 0;
		}

		if (already_visible)
			player->send_move_object(m_id);
		else
			player->send_add_object(m_id);
	}

	for (int player_id : old_viewers) {
		if (new_viewers.count(player_id) != 0) continue;

		std::shared_ptr<SESSION> player = get_session(player_id);
		if (nullptr != player)
			player->send_remove_object(m_id);
	}
}

void SESSION::wake_up()
{
	bool expected = false;
	if (!m_active_npc.compare_exchange_strong(expected, true))
		return;

	event_type ev;
	ev.obj_id = m_id;
	ev.event_id = EVENT_NPC_MOVE;
	ev.wakeup_time = system_clock::now() + milliseconds(MOVE_COOL_TIME);
	timer_queue.push(ev);
}

void send_login_fail(SOCKET client, const char* message)
{
	S2C_LoginResult packet;
	packet.size = sizeof(packet);
	packet.type = S2C_LOGIN_RESULT;
	packet.success = false;
	strcpy_s(packet.message, message);
	WSABUF wsa_buf;
	wsa_buf.buf = reinterpret_cast<char*>(&packet);
	wsa_buf.len = packet.size;
	WSASend(client, &wsa_buf, 1, 0, 0, nullptr, nullptr);
}

int get_new_player_id()
{
	for (;;) {
		int id = player_index++;
		if (id >= NPC_ID_START) return -1;

		auto iter = clients.find(id);
		if (iter == clients.end()) return id;

		std::shared_ptr<SESSION> old = iter->second.load();
		if (nullptr == old || old->m_state == CS_FREE || old->m_state == CS_LOGOUT)
			return id;
	}
}

void disconnect(int key)
{
	std::shared_ptr<SESSION> cl = get_session(key);
	if (nullptr == cl || cl->m_id >= NPC_ID_START) return;

	cl->m_state = CS_LOGOUT;
	sector_manager.remove_object_from_sector(key, cl->m_x, cl->m_y);

	std::unordered_set<int> visible;
	{
		std::lock_guard<std::mutex> lock(cl->m_visible_mutex);
		visible = cl->m_visible_objects;
		cl->m_visible_objects.clear();
	}

	for (int object_id : visible) {
		if (!is_pc(object_id)) continue;
		std::shared_ptr<SESSION> other = get_session(object_id);
		if (nullptr != other)
			other->send_remove_object(key);
	}

	if (cl->m_client != INVALID_SOCKET) {
		closesocket(cl->m_client);
		cl->m_client = INVALID_SOCKET;
	}
	clients[key].store(nullptr);
}

void process_npc_move(int npc_id)
{
	std::shared_ptr<SESSION> npc = get_session(npc_id);
	if (nullptr == npc || npc->m_id < NPC_ID_START || npc->m_state != CS_PLAYING) return;

	npc->do_random_move();

	bool has_nearby_player = false;
	for (int object_id : sector_manager.get_objects_in_adjacent_sectors(npc->m_x, npc->m_y)) {
		if (!is_pc(object_id)) continue;
		std::shared_ptr<SESSION> player = get_session(object_id);
		if (nullptr != player && player->can_send() && player->can_see(npc->m_x, npc->m_y)) {
			has_nearby_player = true;
			break;
		}
	}

	if (has_nearby_player) {
		event_type ev;
		ev.obj_id = npc_id;
		ev.event_id = EVENT_NPC_MOVE;
		ev.wakeup_time = system_clock::now() + milliseconds(MOVE_COOL_TIME);
		timer_queue.push(ev);
	}
	else {
		npc->m_active_npc = false;
	}
}

bool sql_success(SQLRETURN retcode)
{
	return retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO;
}

void db_error(SQLSMALLINT handle_type, SQLHANDLE handle)
{
	SQLSMALLINT rec = 0;
	SQLINTEGER native_error;
	SQLWCHAR state[SQL_SQLSTATE_SIZE + 1];
	SQLWCHAR message[1000];

	while (SQLGetDiagRecW(handle_type, handle, ++rec, state, &native_error,
		message, 1000, NULL) == SQL_SUCCESS)
		wcout << L"[" << state << L"] " << message << L" (" << native_error << L")\n";
}

bool connect_database(SQLHENV& henv, SQLHDBC& hdbc)
{
	SQLRETURN retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
	if (!sql_success(retcode)) return false;

	retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
	if (!sql_success(retcode)) {
		db_error(SQL_HANDLE_ENV, henv);
		return false;
	}

	retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
	if (!sql_success(retcode)) return false;

	SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);
	retcode = SQLConnectW(hdbc, (SQLWCHAR*)L"2026_Game_Test", SQL_NTS,
		(SQLWCHAR*)NULL, 0, NULL, 0);
	if (!sql_success(retcode)) {
		db_error(SQL_HANDLE_DBC, hdbc);
		return false;
	}
	return true;
}

bool login_player(SQLHDBC hdbc, int user_id, DB_RESULT& result)
{
	SQLHSTMT hstmt;
	SQLRETURN retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
	if (!sql_success(retcode)) return false;

	SQLWCHAR query[100];
	swprintf_s(query, L"EXEC dbo.login_player @UserId = %d", user_id);
	retcode = SQLExecDirectW(hstmt, query, SQL_NTS);
	if (!sql_success(retcode)) {
		db_error(SQL_HANDLE_STMT, hstmt);
		SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
		return false;
	}

	SQLINTEGER db_user_id = -1;
	SQLSMALLINT user_x = 0;
	SQLSMALLINT user_y = 0;
	SQLLEN cb_user_id = 0;
	SQLLEN cb_x = 0;
	SQLLEN cb_y = 0;

	SQLBindCol(hstmt, 1, SQL_C_LONG, &db_user_id, sizeof(db_user_id), &cb_user_id);
	SQLBindCol(hstmt, 3, SQL_C_SSHORT, &user_x, sizeof(user_x), &cb_x);
	SQLBindCol(hstmt, 4, SQL_C_SSHORT, &user_y, sizeof(user_y), &cb_y);

	retcode = SQLFetch(hstmt);
	if (sql_success(retcode)) {
		result.success = true;
		result.user_id = db_user_id;
		result.has_position = cb_x != SQL_NULL_DATA && cb_y != SQL_NULL_DATA;
		result.x = user_x;
		result.y = user_y;
	}

	SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
	return result.success;
}

void save_player_position(SQLHDBC hdbc, int user_id, short x, short y)
{
	SQLHSTMT hstmt;
	SQLRETURN retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
	if (!sql_success(retcode)) return;

	SQLWCHAR query[120];
	swprintf_s(query, L"EXEC dbo.set_player_position @UserId = %d, @X = %d, @Y = %d",
		user_id, x, y);
	retcode = SQLExecDirectW(hstmt, query, SQL_NTS);
	if (!sql_success(retcode))
		db_error(SQL_HANDLE_STMT, hstmt);

	SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
}

void db_thread()
{
	SQLHENV henv = SQL_NULL_HENV;
	SQLHDBC hdbc = SQL_NULL_HDBC;
	bool connected = connect_database(henv, hdbc);

	for (;;) {
		DB_EVENT ev;
		if (!db_queue.try_pop(ev)) {
			this_thread::sleep_for(milliseconds(1));
			continue;
		}

		if (ev.type == DB_LOGIN) {
			EXP_OVER* db_over = new EXP_OVER(IO_DB_LOGIN);
			if (connected)
				login_player(hdbc, ev.user_id, db_over->m_db_result);
			PostQueuedCompletionStatus(g_iocp, 1, ev.session_id, &db_over->m_over);
		}
		else if (connected) {
			save_player_position(hdbc, ev.user_id, ev.x, ev.y);
		}
	}
}

void worker_thread()
{
	for (;;) {
		DWORD num_bytes;
		ULONG_PTR long_key;
		LPOVERLAPPED over;
		BOOL ret = GetQueuedCompletionStatus(g_iocp, &num_bytes, &long_key, &over, INFINITE);
		int key = static_cast<int>(long_key);
		EXP_OVER* exp_over = reinterpret_cast<EXP_OVER*>(over);

		if (TRUE != ret) {
			error_display(L"GQCS Error: ", WSAGetLastError());
			if (key >= 0) disconnect(key);
			continue;
		}

		switch (exp_over->m_iotype) {
		case IO_ACCEPT:
		{
			int my_id = get_new_player_id();
			if (-1 == my_id) {
				send_login_fail(exp_over->m_client_socket, "Server is full.");
				closesocket(exp_over->m_client_socket);
			}
			else {
				CreateIoCompletionPort((HANDLE)exp_over->m_client_socket, g_iocp, my_id, 0);
				std::shared_ptr<SESSION> new_pl = std::make_shared<SESSION>(exp_over->m_client_socket, my_id);
				clients[my_id] = new_pl;
				new_pl->do_recv();
			}

			exp_over->m_client_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
			AcceptEx(g_server, exp_over->m_client_socket, &exp_over->m_buff, 0,
				sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
				NULL, &exp_over->m_over);
			break;
		}
		case IO_RECV:
		{
			if (0 == num_bytes) {
				disconnect(key);
				break;
			}

			std::shared_ptr<SESSION> cl = get_session(key);
			if (nullptr == cl) break;

			unsigned char* p = reinterpret_cast<unsigned char*>(exp_over->m_buff);
			int data_size = num_bytes + cl->m_prev_recv;
			while (data_size > 0) {
				int packet_size = p[0];
				if (packet_size > data_size) break;
				if (!cl->process_packet(p)) {
					disconnect(key);
					break;
				}
				p += packet_size;
				data_size -= packet_size;
			}

			if (data_size > 0) {
				memmove(cl->m_recv_over.m_buff, p, data_size);
				cl->m_prev_recv = data_size;
			}
			else {
				cl->m_prev_recv = 0;
			}
			cl->do_recv();
			break;
		}
		case IO_SEND:
			delete exp_over;
			break;
		case IO_NPC_MOVE:
			delete exp_over;
			process_npc_move(key);
			break;
		case IO_DB_LOGIN:
		{
			std::shared_ptr<SESSION> cl = get_session(key);
			if (nullptr == cl || cl->m_state != CS_DB_WAIT) {
				delete exp_over;
				break;
			}

			if (!exp_over->m_db_result.success) {
				cl->m_state = CS_LOGOUT;
				cl->send_login_fail();
				delete exp_over;
				break;
			}

			cl->m_db_user_id = exp_over->m_db_result.user_id;
			sprintf_s(cl->m_username, "%d", cl->m_db_user_id);
			if (exp_over->m_db_result.has_position) {
				cl->m_x = exp_over->m_db_result.x;
				cl->m_y = exp_over->m_db_result.y;
			}
			else {
				request_save_position(cl->m_db_user_id, cl->m_x, cl->m_y);
			}

			cl->m_state = CS_PLAYING;
			sector_manager.add_object_to_sector(cl->m_id, cl->m_x, cl->m_y);
			cl->send_login_success();
			cl->send_avatar_info();
			update_player_view(cl->m_id);
			delete exp_over;
			break;
		}
		default:
			cout << "Unknown IO type.\n";
			break;
		}
	}
}

void timer_thread()
{
	for (;;) {
		event_type ev;
		if (timer_queue.try_pop(ev)) {
			auto now = system_clock::now();
			if (ev.wakeup_time <= now) {
				if (ev.event_id == EVENT_NPC_MOVE) {
					EXP_OVER* move_over = new EXP_OVER(IO_NPC_MOVE);
					PostQueuedCompletionStatus(g_iocp, 1, ev.obj_id, &move_over->m_over);
				}
			}
			else {
				timer_queue.push(ev);
				this_thread::sleep_for(milliseconds(1));
			}
		}
		else {
			this_thread::sleep_for(milliseconds(1));
		}
	}
}

void InitializeNPC()
{
	cout << "NPC initialize begin.\n";
	for (int i = NPC_ID_START; i < NPC_ID_START + MAX_NPCS; ++i) {
		std::shared_ptr<SESSION> npc = std::make_shared<SESSION>();
		npc->m_id = i;
		npc->m_is_npc = true;
		npc->m_state = CS_PLAYING;
		npc->m_client = INVALID_SOCKET;
		npc->m_x = rand() % WORLD_WIDTH;
		npc->m_y = rand() % WORLD_HEIGHT;
		npc->m_move_time = 0;
		npc->m_active_npc = false;
		npc->m_last_npc_move_time = system_clock::now();
		sprintf_s(npc->m_username, "NPC%d", i);
		clients[i] = npc;
		sector_manager.add_object_to_sector(i, npc->m_x, npc->m_y);
	}
	cout << "NPC initialize end.\n";
}

int main()
{
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);

	g_server = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
	::bind(g_server, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(g_server, SOMAXCONN);

	InitializeNPC();

	g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	CreateIoCompletionPort((HANDLE)g_server, g_iocp, -1, 0);

	EXP_OVER accept_over(IO_ACCEPT);
	accept_over.m_client_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	AcceptEx(g_server, accept_over.m_client_socket, &accept_over.m_buff, 0,
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
		NULL, &accept_over.m_over);

	vector<thread> worker_threads;
	thread timer_th(timer_thread);
	thread db_th(db_thread);
	unsigned int hw_threads = thread::hardware_concurrency();
	int num_threads = hw_threads > 1 ? static_cast<int>(hw_threads - 1) : 1;
	for (int i = 0; i < num_threads; ++i)
		worker_threads.emplace_back(worker_thread);

	for (auto& th : worker_threads)
		th.join();
	timer_th.join();
	db_th.join();

	closesocket(g_server);
	WSACleanup();
}
