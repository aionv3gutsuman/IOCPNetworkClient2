#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

struct PER_IO_DATA {
	WSAOVERLAPPED overlapped;
	WSABUF buffer;
	char data[1024];
	DWORD bytes;
	int operation; // 0=recv, 1=send
	SOCKET sock;
};

int main() {
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);

	// IOCP 用ソケット
	SOCKET sock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(5000);
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	printf("Connecting...\n");
	if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
		printf("connect failed: %d\n", WSAGetLastError());
		return 1;
	}

	printf("Connected to server\n");

	// IOCP 作成
	HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	// ソケットを IOCP に関連付け
	CreateIoCompletionPort((HANDLE)sock, iocp, (ULONG_PTR)sock, 0);

	// 最初の recv を投げる
	PER_IO_DATA* recvIo = new PER_IO_DATA();
	ZeroMemory(&recvIo->overlapped, sizeof(recvIo->overlapped));
	recvIo->buffer.buf = recvIo->data;
	recvIo->buffer.len = sizeof(recvIo->data);
	recvIo->operation = 0;
	recvIo->sock = sock;

	DWORD flags = 0;
	WSARecv(sock, &recvIo->buffer, 1, NULL, &flags, &recvIo->overlapped, NULL);

	// 最初の送信
	const char* msg = "Hello from IOCP client!";
	PER_IO_DATA* sendIo = new PER_IO_DATA();
	ZeroMemory(&sendIo->overlapped, sizeof(sendIo->overlapped));
	strcpy(sendIo->data, msg);
	sendIo->buffer.buf = sendIo->data;
	sendIo->buffer.len = (ULONG)strlen(msg);
	sendIo->operation = 1;
	sendIo->sock = sock;

	WSASend(sock, &sendIo->buffer, 1, NULL, 0, &sendIo->overlapped, NULL);

	// 完了通知ループ
	while (true) {
		DWORD bytes;
		ULONG_PTR key;
		PER_IO_DATA* io;

		BOOL ok = GetQueuedCompletionStatus(
			iocp, &bytes, &key, (LPOVERLAPPED*)&io, INFINITE);

		if (!ok || bytes == 0) {
			printf("Disconnected or error\n");
			closesocket(sock);
			break;
		}

		if (io->operation == 0) {
			// recv 完了
			printf("Received from server: %.*s\n", bytes, io->data);

			// 次の recv を投げる
			ZeroMemory(&io->overlapped, sizeof(io->overlapped));
			io->buffer.buf = io->data;
			io->buffer.len = sizeof(io->data);
			io->operation = 0;

			DWORD flags = 0;
			WSARecv(sock, &io->buffer, 1, NULL, &flags, &io->overlapped, NULL);
		}
		else if (io->operation == 1) {
			// send 完了 → 特に何もしない
		}
	}

	WSACleanup();
	return 0;
}