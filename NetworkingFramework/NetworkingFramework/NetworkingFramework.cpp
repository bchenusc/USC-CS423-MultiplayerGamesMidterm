/*
	Author: Brian Y. Chen
	Date:   4/6/2015
	
	This application is one file that contains functionality for both UDP
	and TCP send/recv as well as other functionalities pertaining to
	networking.

	IMPORTANT: This file is granted to be used in the University of Southern
	California CSCI 423 Spring 2015 midterm and is forbidden to be used 
	elsewhere unless given explicit consent from the author, Brian Y. Chen.
*/
// NativeConsoles.cpp : Defines the entry point for the console application.

//------------ Base Includes ------------ //
#include "stdafx.h"
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#pragma comment(lib, "Ws2_32.lib")

//------------ Other Includes ------------//
#include <time.h>
#include <vector>
#include <memory>
#include <string>
#include <iostream>

using std::shared_ptr;
using std::to_string;
using std::string;
using std::cout;
using std::endl;
using std::cin;


//------------ Defines ------------//
#define TCP_CHAT_BUFFER_SIZE (1024)
#define UDP_PACKET_MAX_CAPACITY (1024)


class IPv4Util
{
public:
	/* Tested and worked. */
	static u_long IPStringToLong(const string& ipAddress)
	{
		struct in_addr sa;
		{ inet_pton(AF_INET, ipAddress.c_str(), &(sa)); }
		return sa.S_un.S_addr;
	}
	/* Tested and worked. */
	static string IPLongToString(u_long ipAddress)
	{
		char str[INET_ADDRSTRLEN];
		struct sockaddr_in sa;
		sa.sin_addr.S_un.S_addr = ipAddress;
		{ inet_ntop(AF_INET, &(sa.sin_addr), str, INET_ADDRSTRLEN); }
		return string(str);
	}

	static u_short PortStringToShort(const string& portNum)
	{
		return static_cast<u_short>(std::stoul(portNum.c_str(), nullptr, 10));
	}

	static void TEST()
	{
		string local_host = "127.0.0.1";
		cout << "Old IP: " << local_host << endl;
		u_long ip = IPv4Util::IPStringToLong(local_host);
		cout << "ULong IP: " << to_string(ip) << endl;
		cout << "New IP: " << IPv4Util::IPLongToString(ip) << endl;
	}
};

//------------ TCP ------------//
struct TCPSocket
{
	TCPSocket(SOCKET s) { mSocket = s; }

	/* Connects to a socket. */
	int Connect(u_long ipAddress, u_short port)
	{
		struct sockaddr_in sockAddress;
		sockAddress.sin_family = AF_INET;
		sockAddress.sin_port = htons(port);
		sockAddress.sin_addr.S_un.S_addr = ipAddress;
		memset(sockAddress.sin_zero, 0, 8);
		return connect(mSocket, reinterpret_cast<sockaddr*>(&sockAddress), sizeof(sockAddress));
	}

	/* Server: Accepts a connection. Allocates new socket to interact w/ client. */
	shared_ptr<TCPSocket> Accept(sockaddr* address, int* length)
	{
		SOCKET returnSocket;
		returnSocket = accept(mSocket, address, length);
		if (returnSocket == INVALID_SOCKET) { return shared_ptr<TCPSocket>(); }
		return shared_ptr<TCPSocket>(new TCPSocket(returnSocket));
	}

	/* Listens to the socket. */
	int Listen() { return listen(mSocket, SOMAXCONN); }

	/*  Returns SOCKET_ERROR if failed.
	Returns # of bytes if succeed.  */
	int Send(const char* buffer, int length)
	{
		return send(mSocket, buffer, length, 0);
	}

	/*  Returns number of bytes received.
	int length: max length to receive. */
	int Receive(char* buffer, int length)
	{
		int bytes = recv(mSocket, buffer, length, 0);
		// Null terminator @ end of buffer for chat client.
		if (bytes == length) { buffer[length - 1] = '\0'; }
		// This is the more common code segment.
		else if (bytes < length && bytes > 0) { buffer[bytes] = '\0'; }
		return bytes;
	}

	/* If block == true, then non-blocking also true. */
	int SetNonBlocking(bool block)
	{
		u_long blocking = !block;
		return ioctlsocket(mSocket, FIONBIO, &blocking);
	}
	SOCKET mSocket;
};

struct TCPSocketUtil
{
	static shared_ptr<TCPSocket> CreateSocket(u_short port)
	{
		SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (s == INVALID_SOCKET) {
			TCPSocketUtil::TCPLastError();
		}

		if (port != 0) {
			struct sockaddr_in sockAddress;
			sockAddress.sin_family = AF_INET;
			sockAddress.sin_port = htons(port);
			memset(sockAddress.sin_zero, 0, 8);
			sockAddress.sin_addr.S_un.S_addr = INADDR_ANY;
			// If the binding failed
			if (bind(s, reinterpret_cast<sockaddr*>(&sockAddress), sizeof(sockAddress)) == SOCKET_ERROR) {
				return shared_ptr<TCPSocket>();
			}
		}

		return shared_ptr<TCPSocket>(new TCPSocket(s));
	}
	static int TCPLastError() { return WSAGetLastError(); }
	static int TCPStartup()
	{
		struct WSAData data;
		return WSAStartup(MAKEWORD(1, 1), &data);
	}
	static int TCPCleanup() { return WSACleanup(); }
	static int Select(fd_set* readfds, fd_set* writefds, timeval* time)
	{
		return select(0, readfds, writefds, nullptr, time);
	}
	static void TCPAddToSet(shared_ptr<TCPSocket>& socket, fd_set* set)
	{
		FD_SET(socket->mSocket, set);
	}
	static int TCPInitSet(fd_set* set)
	{
		return FD_ZERO(set);
	}
	static int TCPIsInSet(shared_ptr<TCPSocket>& socket, fd_set* set)
	{
		return FD_ISSET(socket->mSocket, set);
	}
	static void TCPRemoveFromSet(shared_ptr<TCPSocket>& socket, fd_set* set)
	{
		FD_CLR(socket->mSocket, set);
	}
};

struct TCPChatClient
{
	TCPChatClient(u_long ipAddress, u_short portNum)
	{
		if (TCPSocketUtil::TCPStartup() != 0) { return; }
		mSocket = TCPSocketUtil::CreateSocket(0);
		if (mSocket.get() == nullptr) { return; }
		mIPAddress = ipAddress;
		mPort = portNum;
	}

	int Run()
	{
		// Connect to the server.
		if (mSocket->Connect(mIPAddress, mPort) == SOCKET_ERROR)
		{
			cout << "Failed to connect to server." << endl;
			return 0;
		}
		cout << "Connected to server port: " << to_string(mPort) << endl;
		// Turn off blocking.
		if (mSocket->SetNonBlocking(false) == SOCKET_ERROR)
		{
			cout << "Set non blocking failed." << endl;
			return 0;
		}
		char buffer[1024];

		// Send out a packet
		string sendData = "login:";
		for (uint32_t i = 0; i < 100; ++i)
		{
			if (i < 10)
				sendData.append("0");
			sendData.append(to_string(i));
		}
		int sendBytes = mSocket->Send(sendData.c_str(), sendData.size());
		cout << "Client Send: " << sendData << endl;

		uint32_t password = -1; // Start from -1 because when you increment, then
								// psw received is actually 0.

		while (true)
		{
			// TODO : Functionality
			// Option 1 - Send (char* confirmation, size)
			// Option 2 - Receive();
			int bytes = mSocket->Receive(buffer, 1024);
			if (bytes > -1)
				password++;
			if (bytes == 3)
				break; // YOU HAVE FOUND THE PASSWORD.
		}
		/* Retrieve the image size from the server. */
		string passString = "";
		if (password < 10) // god so hacky.
		{
			passString.append("0");
		}
		passString.append(to_string(password));
		cout << "Password found is: " << passString << endl;

		sendData = "image:" + passString;
		sendBytes = mSocket->Send(sendData.c_str(), sendData.size());

		// The copy pasta is real.
		while (true)
		{
			memset(buffer, 0, 1024);
			int bytes = mSocket->Receive(buffer, 1024);
			if (bytes == 9)
			{
				cout << "Image size as string is: " << buffer << endl;
				string imageSize(buffer);
				for (uint32_t i = 0; i < imageSize.size(); ++i)
				{
					if (imageSize[i] != 0)
					{
						// Substring from i
						imageSize = imageSize.substr(i);
						return std::stoi(imageSize);
					}
				}

				return -1; // Not reached.
			}
		}

	}
	void Send(char* inData, unsigned bytes)
	{
		// TODO : Manipulate the data if you need to.
		int sendBytes = mSocket->Send(inData, bytes);
		cout << "TCP Client - Sent " << to_string(sendBytes) << " bytes to the server." << endl;
	}
	void Receive(char* outData)
	{
		int bytes = mSocket->Receive(outData, TCP_CHAT_BUFFER_SIZE);
		if (bytes > 0)
		{
			//cout << "Client - Received " << bytes << " bytes from the server." << endl;
			// TODO : Do something with the data if you need to.

		}
	}

	shared_ptr<TCPSocket> mSocket;
	u_long mIPAddress;
	u_short mPort;
};

//------------ UDP ------------//
/* Most likely won't be using Packet Buffer, but just in case we need
   to implement one. */
struct PacketBuffer
{
	PacketBuffer()
	{
		mBufferCapacity = UDP_PACKET_MAX_CAPACITY;
		mBuffer = new char[mBufferCapacity];
	}

	~PacketBuffer()
	{
		delete[] mBuffer;
	}

	void ResetBuffer()
	{
		mBufferHead = 0;
		mBufferCapacity = UDP_PACKET_MAX_CAPACITY;
	}

	/* How to use:
	1. Have PacketBuffer mOutBuffer in your UDPChatClient
	2. Also store your ip, port, and sockaddr_in mConnectedTo.
	3. Every frame (client), reset mOutBuffer.
	4. Then call mSocket->ReceiveFrom(mOutBuffer, (sockaddr*)mConnectedTo)
	5. Then you can look inside of the buffer and do things.
	6. One way to do that is use a strncmp("compare me", mBuffer, size);
	*/

	bool WriteData(const void* inData, size_t inLength) {
		if (inLength + mBufferHead <= mBufferCapacity) {
			memcpy(mBuffer + mBufferHead, inData, inLength);
			mBufferHead += inLength;
			return true;
		}
		else {
			return false;
		}
	}

	bool WriteString(const std::string& inString) {
		size_t length = inString.size();
		if (length < 65535) {
			uint16_t l = static_cast<uint16_t> (length);
			WriteData(&l, 2);
			return WriteData(&inString[0], length);
		}
		return false;
	}

	bool ReadData(void* outData, size_t inLength) {
		if (inLength + mBufferHead <= mBufferCapacity) {
			memcpy(outData, mBuffer + mBufferHead, inLength);
			mBufferHead += inLength;

			return true;
		}
		else {
			return false;
		}
	}

	bool ReadString(std::string& outString) {
		uint16_t length;
		if (ReadData(&length, 2)) {
			outString.resize(length);
			return ReadData(&outString[0], length);
		}
		return false;
	}

	// memory allocated for buffer
	char* mBuffer;
	// maximum data that will fit in the buffer
	uint32_t mBufferCapacity;
	// current location to read and write.
	uint32_t mBufferHead;

};

struct UDPSocket
{
	UDPSocket(SOCKET s) { mSocket = s;}

	int Recv(void* inData, uint32_t length)
	{
		return recv(mSocket, reinterpret_cast<char*>(inData), length, 0);
	}

	// Sends data to the address- returns how many bytes were sent, or an error code.
	int SendTo(const void* inData, uint32_t inLength, const sockaddr& inToAddress) {
		return sendto(mSocket, reinterpret_cast<const char*>(inData), inLength, 0, &inToAddress, sizeof(inToAddress));
	}

	int RecvFrom(void* outData, uint32_t inLength, sockaddr& outFromAddress) {
		int sizeOfOut = sizeof(outFromAddress);
		return recvfrom(mSocket, reinterpret_cast<char*>(outData), inLength, 0, &outFromAddress, &sizeOfOut);
	}

	bool Close() {
		bool error = closesocket(mSocket) == NOERROR;
		return error;
	}

	int SetNonBlocking(bool inShouldBlock) {
		// If inShouldBlock = true then set non-blocking to true;
		u_long blocking = !inShouldBlock;
		return ioctlsocket(mSocket, FIONBIO, &blocking);
	}

	SOCKET mSocket;
	struct sockaddr_in mServerAddr;
	bool   mBroadcast;
};

struct UDPSocketUtil
{
	static shared_ptr<UDPSocket> CreateSocket(uint16_t portNum)
	{
		SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (s == INVALID_SOCKET)
		{
			cout << "Invalid Socket UDP" << endl;
			return shared_ptr<UDPSocket>();
		}

		struct sockaddr_in ownAddr;
		ownAddr.sin_family = AF_INET;
		ownAddr.sin_port = htons(portNum);
		ownAddr.sin_addr.S_un.S_addr = INADDR_ANY;
		memset(ownAddr.sin_zero, 0, 8);

		int bindResult = bind(s, reinterpret_cast<const sockaddr*>(&ownAddr), 
			sizeof(sockaddr));
		if (bindResult == SOCKET_ERROR)
		{
			cout << "Sad Face. " << endl;
			return shared_ptr<UDPSocket>();
		}

		return shared_ptr<UDPSocket>(new UDPSocket(s));
	}

	static int UDPStartup()
	{
		struct WSAData data;
		return WSAStartup(MAKEWORD(1, 1), &data);
	}
	static int UDPCleanup()
	{
		return WSACleanup();
	}
};

struct UDPChatClient
{
	UDPChatClient(u_long ipAddress, uint16_t portNum, bool broadcast)
	{
		if (UDPSocketUtil::UDPStartup() != 0) { return; }
		mOwnSocket = UDPSocketUtil::CreateSocket(0);

		/* Create a server connection representation. */
		mServerAddr.sin_family = AF_INET;
		mServerAddr.sin_port = htons(portNum);
		mServerAddr.sin_addr.S_un.S_addr = ipAddress;
		memset(mServerAddr.sin_zero, 0, 8);

		if (broadcast)
		{
			const char bValue = '1';
			if (setsockopt(mOwnSocket->mSocket, SOL_SOCKET, SO_BROADCAST, 
						   &bValue, sizeof (bValue)) == -1)
			{
				cout << "Failed to Broadcast" << endl;
				return;
			}
		}
	
		if (mOwnSocket.get() == nullptr) { return; }
		mIPAddress = ipAddress;
		mPort = portNum;
		mFrameID = 0;
	}

	~UDPChatClient()
	{
		UDPSocketUtil::UDPCleanup();
	}

	u_long Run()
	{
		// TODO : Functionality
		// Option 1 - Send (char* confirmation, size)
		// Option 2 - Receive();
		// Connect to the server.

		//mOwnSocket->SetNonBlocking(false);
		cout << "Connected to server port: " << to_string(mPort) << endl;

		char buffer[UDP_PACKET_MAX_CAPACITY];

		// Send out a packet
		string sendData = "where";
		if (false)
		{
			sendData = "UDP BROADCAST MESSAGE!";
		}

		/* ------- SENDING -------- */
		int bytesSent = 0;
		
		// Wrap this
		bytesSent = mOwnSocket->SendTo(sendData.c_str(), sendData.size(),
			*reinterpret_cast<sockaddr*>(&mServerAddr));

		cout << "UDP Chat client sent: " << to_string(bytesSent) << "bytes."  << endl;
		/* ------------------------ */

		struct timeval tim;
		tim.tv_sec = 5; // Seconds till confirm dropped packet.

		// Clear descriptors
		//fd_set readfds;

		//FD_ZERO(&readfds);
		//FD_SET(mOwnSocket->mSocket, &readfds);
		

		int bytesReceived = 0;
		while (true)
		{
			memset(buffer, 0, UDP_PACKET_MAX_CAPACITY);
			bytesReceived = mOwnSocket->RecvFrom(buffer, UDP_PACKET_MAX_CAPACITY,
				*reinterpret_cast<sockaddr*>(&mServerAddr));
			if (bytesReceived > 0)
			{
				cout << "Byte received from server: " << bytesReceived << " " << buffer << endl;
				cout << "From: " << IPv4Util::IPLongToString(mServerAddr.sin_addr.S_un.S_addr) << endl;
				return mServerAddr.sin_addr.S_un.S_addr;
			}
		}
	}

	uint32_t mFrameID;
	shared_ptr<UDPSocket> mOwnSocket;
	struct sockaddr_in mServerAddr;
	u_long mIPAddress;
	uint16_t mPort;
};


int _tmain(int argc, _TCHAR* argv[])
{
	bool broadcast = false;

	/* Test sending via TCP */
	string local_host = "127.0.0.1";

	if (broadcast)
	{
		//local_host = "127.255.255.255";
	}


	u_long ip = IPv4Util::IPStringToLong(local_host);
	uint16_t tcpport = IPv4Util::PortStringToShort("4001");
	uint16_t udpport = IPv4Util::PortStringToShort("4000");

	cout << "Sending to: " << local_host << endl;
	UDPChatClient udpClient(ip, udpport, broadcast);
	u_long part1Address = udpClient.Run();
	
	TCPChatClient tcpClient(part1Address, tcpport);
	int password = tcpClient.Run();
	cout << "Image Size is: " << password << endl;

	while (true){};
	return 0;
}

