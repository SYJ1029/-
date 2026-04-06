#pragma once

constexpr short PORT = 3500;
constexpr int WORLD_WIDTH = 8;
constexpr int WORLD_HEIGHT = 8;
constexpr int MAX_PLAYER = 10;
constexpr int MAX_NAMELENGTH = 20;
enum PACKET_TYPE
{
	C2S_LOGIN = 1,
	C2S_MOVE,
	S2C_LOGIN_RESULT,
	S2C_AVATAR_INFO,
	S2C_ADD_PLAYER,
	S2C_MOVE_PLAYER,
	S2C_REMOVE_PLAYER,

	C2S_LOGOUT,
};

enum DIRECTION : unsigned char
{
	DIR_UP = 1,
	DIR_DOWN,
	DIR_LEFT,
	DIR_RIGHT,
};

#pragma pack(push, 1)
struct C2S_Login
{
	unsigned char size;
	PACKET_TYPE type;
	char userName[20];
};

struct C2S_Move
{
	unsigned char size;
	PACKET_TYPE type;
	DIRECTION dir;
};


struct S2C_LoginResult
{
	unsigned char size;
	PACKET_TYPE type;
	bool success;
	char message[50];
};

struct S2C_AvatarInfo
{
	unsigned char size;
	PACKET_TYPE type;
	int playerId;
	short x;
	short y;
};

struct S2C_AddPlayer
{
	unsigned char size;
	PACKET_TYPE type;
	int playerId;
	short x;
	short y;
	char name[MAX_NAMELENGTH];
};


struct S2C_RemovePlayer
{
	unsigned char size;
	PACKET_TYPE type;
	int playerId;
};

struct S2C_MovePlayer
{
	unsigned char size;
	PACKET_TYPE type;
	int playerId;
	short x;
	short y;
};

#pragma pack(pop)