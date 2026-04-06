#include <iostream>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32")

const char* SERVER_IP = "127.0.0.1";
constexpr short SERVER_PORT = 6000;
constexpr int BUFFER_SIZE = 4096;

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
	std::wcout << L" 에러 " << lpMsgBuf << std::endl;
	while (true);
	// 디버깅 용
	LocalFree(lpMsgBuf);
}

int main()
{
	std::wcout.imbue(std::locale("korean")); // 에러 문자를 한글로 출력해요
	WSADATA wsaData{};
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	SOCKET s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, 0);

	SOCKADDR_IN server_addr{};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);
	//server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

	int result = WSAConnect(s_socket, reinterpret_cast<SOCKADDR*>(&server_addr), sizeof(server_addr), nullptr, nullptr, nullptr, nullptr);
	if (result == SOCKET_ERROR)
	{
		error_display(L"Connect Failed", WSAGetLastError());
	}

	for (;;)
	{
		char buffer[BUFFER_SIZE];
		std::cout << "Input: ";
		std::cin.getline(buffer, BUFFER_SIZE);

		WSABUF send_buffer{ static_cast<ULONG>(strlen(buffer)) + 1, buffer};
		DWORD sent_size = 0;
		WSASend(s_socket, &send_buffer, 1, &sent_size, 0, nullptr, nullptr);

		char recvBuffer[BUFFER_SIZE]{};
		WSABUF recv_buffer_wsa{ BUFFER_SIZE, recvBuffer };
		DWORD rec_flat = 0;
		DWORD recv_size = 0;

		int bytes_received = 
			WSARecv(s_socket, &recv_buffer_wsa, 1, &recv_size, &rec_flat, nullptr, nullptr);

		std::cout << "Recv from Server:" << recvBuffer << std::endl;

		if (recv_size > 0)
		{
			std::string response(buffer, bytes_received);
			std::cout << "Echoed: " << response << std::endl;
		}
		else
		{
			std::cerr << "Connection closed by server." << std::endl;
			break;
		}
	}

	WSACleanup();

}