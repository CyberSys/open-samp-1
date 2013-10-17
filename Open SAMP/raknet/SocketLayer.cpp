/**
* @file
* @brief SocketLayer class implementation 
*
 * This file is part of RakNet Copyright 2003 Rakkarsoft LLC and Kevin Jenkins.
 *
 * Usage of Raknet is subject to the appropriate licence agreement.
 * "Shareware" Licensees with Rakkarsoft LLC are subject to the
 * shareware license found at
 * http://www.rakkarsoft.com/shareWareLicense.html which you agreed to
 * upon purchase of a "Shareware license" "Commercial" Licensees with
 * Rakkarsoft LLC are subject to the commercial license found at
 * http://www.rakkarsoft.com/sourceCodeLicense.html which you agreed
 * to upon purchase of a "Commercial license"
 * Custom license users are subject to the terms therein.
 * All other users are
 * subject to the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * Refer to the appropriate license agreement for distribution,
 * modification, and warranty rights.
*/
#include "SocketLayer.h"
#include <assert.h>
#include "MTUSize.h"

#ifdef _WIN32
#include <process.h>
typedef int socklen_t;
#else
#include <string.h> // memcpy
#include <unistd.h>
#include <fcntl.h>
#endif

#include "ExtendedOverlappedPool.h"
#ifdef __USE_IO_COMPLETION_PORTS
#include "AsynchronousFileIO.h"
#endif

bool SocketLayer::socketLayerStarted = false;
#ifdef _WIN32
WSADATA SocketLayer::winsockInfo;
#endif
SocketLayer SocketLayer::I;

// Open SA:MP
unsigned short usLocalPort = 0;

#ifdef _WIN32
extern void __stdcall ProcessNetworkPacket( unsigned int binaryAddress, unsigned short port, const char *data, int length, RakPeer *rakPeer );
#else
extern void ProcessNetworkPacket( unsigned int binaryAddress, unsigned short port, const char *data, int length, RakPeer *rakPeer );
#endif

#ifdef _WIN32
extern void __stdcall ProcessPortUnreachable( unsigned int binaryAddress, unsigned short port, RakPeer *rakPeer );
#else
extern void ProcessPortUnreachable( unsigned int binaryAddress, unsigned short port, RakPeer *rakPeer );
#endif

#ifdef _DEBUG
#include <stdio.h>
#endif



SocketLayer::SocketLayer()
{
	if ( socketLayerStarted == false )
	{
#ifdef _WIN32

		if ( WSAStartup( MAKEWORD( 2, 2 ), &winsockInfo ) != 0 )
		{
#if defined(_WIN32) && !defined(_COMPATIBILITY_1) && defined(_DEBUG)
			DWORD dwIOError = GetLastError();
			LPVOID messageBuffer;
			FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL, dwIOError, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),  // Default language
				( LPTSTR ) & messageBuffer, 0, NULL );
			// something has gone wrong here...
			printf( "WSAStartup failed:Error code - %d\n%s", dwIOError, messageBuffer );
			//Free the buffer.
			LocalFree( messageBuffer );
#endif
		}

#endif
		socketLayerStarted = true;
	}
}

SocketLayer::~SocketLayer()
{
	if ( socketLayerStarted == true )
	{
#ifdef _WIN32
		WSACleanup();
#endif

		socketLayerStarted = false;
	}
}

SOCKET SocketLayer::Connect( SOCKET writeSocket, unsigned int binaryAddress, unsigned short port )
{
	assert( writeSocket != INVALID_SOCKET );
	sockaddr_in connectSocketAddress;

	connectSocketAddress.sin_family = AF_INET;
	connectSocketAddress.sin_port = htons( port );
	connectSocketAddress.sin_addr.s_addr = binaryAddress;

	if ( connect( writeSocket, ( struct sockaddr * ) & connectSocketAddress, sizeof( struct sockaddr ) ) != 0 )
	{
#if defined(_WIN32) && !defined(_COMPATIBILITY_1) && defined(_DEBUG)
		DWORD dwIOError = GetLastError();
		LPVOID messageBuffer;
		FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, dwIOError, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),  // Default language
			( LPTSTR ) &messageBuffer, 0, NULL );
		// something has gone wrong here...
		printf( "WSAConnect failed:Error code - %d\n%s", dwIOError, messageBuffer );
		//Free the buffer.
		LocalFree( messageBuffer );
#endif
	}

	return writeSocket;
}

#ifdef _MSC_VER
#pragma warning( disable : 4100 ) // warning C4100: <variable name> : unreferenced formal parameter
#endif
SOCKET SocketLayer::CreateBoundSocket( unsigned short port, bool blockingSocket, const char *forceHostAddress )
{
	SOCKET listenSocket;
	sockaddr_in listenerSocketAddress;
	int ret;

#ifdef __USE_IO_COMPLETION_PORTS

	if ( blockingSocket == false ) 
		listenSocket = WSASocket( AF_INET, SOCK_DGRAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED );
	else
#endif

		listenSocket = socket( AF_INET, SOCK_DGRAM, 0 );

	if ( listenSocket == INVALID_SOCKET )
	{
#if defined(_WIN32) && !defined(_COMPATIBILITY_1) && defined(_DEBUG)
		DWORD dwIOError = GetLastError();
		LPVOID messageBuffer;
		FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, dwIOError, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),  // Default language
			( LPTSTR ) & messageBuffer, 0, NULL );
		// something has gone wrong here...
		printf( "socket(...) failed:Error code - %d\n%s", dwIOError, messageBuffer );
		//Free the buffer.
		LocalFree( messageBuffer );
#endif

		return INVALID_SOCKET;
	}

	int sock_opt = 1;

	if ( setsockopt( listenSocket, SOL_SOCKET, SO_REUSEADDR, ( char * ) & sock_opt, sizeof ( sock_opt ) ) == -1 )
	{
#if defined(_WIN32) && !defined(_COMPATIBILITY_1) && defined(_DEBUG)
		DWORD dwIOError = GetLastError();
		LPVOID messageBuffer;
		FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, dwIOError, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),  // Default language
			( LPTSTR ) & messageBuffer, 0, NULL );
		// something has gone wrong here...
		printf( "setsockopt(SO_REUSEADDR) failed:Error code - %d\n%s", dwIOError, messageBuffer );
		//Free the buffer.
		LocalFree( messageBuffer );
#endif

	}

	#if defined(_WIN32) && !defined(_COMPATIBILITY_1) && defined(_DEBUG)
	// If this assert hit you improperly linked against WSock32.h
	assert(IP_DONTFRAGMENT==14);
	#endif

	// TODO - I need someone on dialup to test this with :(
	// Path MTU Detection
	/*
	if ( setsockopt( listenSocket, IPPROTO_IP, IP_DONTFRAGMENT, ( char * ) & sock_opt, sizeof ( sock_opt ) ) == -1 )
	{
#if defined(_WIN32) && !defined(_COMPATIBILITY_1) && defined(_DEBUG)
		DWORD dwIOError = GetLastError();
		LPVOID messageBuffer;
		FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, dwIOError, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),  // Default language
			( LPTSTR ) & messageBuffer, 0, NULL );
		// something has gone wrong here...
		printf( "setsockopt(IP_DONTFRAGMENT) failed:Error code - %d\n%s", dwIOError, messageBuffer );
		//Free the buffer.
		LocalFree( messageBuffer );
#endif
	}
	*/

	//Set non-blocking
#ifdef _WIN32
	unsigned long nonblocking = 1;
// http://www.die.net/doc/linux/man/man7/ip.7.html
	if ( ioctlsocket( listenSocket, FIONBIO, &nonblocking ) != 0 )
	{
		assert( 0 );
		return INVALID_SOCKET;
	}

#else
	if ( fcntl( listenSocket, F_SETFL, O_NONBLOCK ) != 0 )
	{
		assert( 0 );
		return INVALID_SOCKET;
	}

#endif

	// Set broadcast capable
	if ( setsockopt( listenSocket, SOL_SOCKET, SO_BROADCAST, ( char * ) & sock_opt, sizeof( sock_opt ) ) == -1 )
	{
#if defined(_WIN32) && !defined(_COMPATIBILITY_1) && defined(_DEBUG)
		DWORD dwIOError = GetLastError();
		LPVOID messageBuffer;
		FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, dwIOError, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),  // Default language
			( LPTSTR ) & messageBuffer, 0, NULL );
		// something has gone wrong here...
		printf( "setsockopt(SO_BROADCAST) failed:Error code - %d\n%s", dwIOError, messageBuffer );
		//Free the buffer.
		LocalFree( messageBuffer );
#endif

	}

	// Listen on our designated Port#
	listenerSocketAddress.sin_port = htons( port );

	// Fill in the rest of the address structure
	listenerSocketAddress.sin_family = AF_INET;

	if (forceHostAddress)
	{
		listenerSocketAddress.sin_addr.s_addr = inet_addr( forceHostAddress );
	}
	else
	{
		listenerSocketAddress.sin_addr.s_addr = INADDR_ANY;
	}	

	// bind our name to the socket
	ret = bind( listenSocket, ( struct sockaddr * ) & listenerSocketAddress, sizeof( struct sockaddr ) );

	if ( ret == SOCKET_ERROR )
	{
#if defined(_WIN32) && !defined(_COMPATIBILITY_1) && defined(_DEBUG)
		DWORD dwIOError = GetLastError();
		LPVOID messageBuffer;
		FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, dwIOError, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),  // Default language
			( LPTSTR ) & messageBuffer, 0, NULL );
		// something has gone wrong here...
		printf( "bind(...) failed:Error code - %d\n%s", dwIOError, messageBuffer );
		//Free the buffer.
		LocalFree( messageBuffer );
#endif

		return INVALID_SOCKET;
	}

	usLocalPort = port;

	return listenSocket;
}

#if !defined(_COMPATIBILITY_1)
const char* SocketLayer::DomainNameToIP( const char *domainName )
{
	struct hostent * phe = gethostbyname( domainName );

	if ( phe == 0 || phe->h_addr_list[ 0 ] == 0 )
	{
		//cerr << "Yow! Bad host lookup." << endl;
		return 0;
	}

	struct in_addr addr;

	memcpy( &addr, phe->h_addr_list[ 0 ], sizeof( struct in_addr ) );

	return inet_ntoa( addr );
}
#endif

// Start an asynchronous read using the specified socket.
#ifdef _MSC_VER
#pragma warning( disable : 4100 ) // warning C4100: <variable name> : unreferenced formal parameter
#endif
bool SocketLayer::AssociateSocketWithCompletionPortAndRead( SOCKET readSocket, unsigned int binaryAddress, unsigned short port, RakPeer *rakPeer )
{
#ifdef __USE_IO_COMPLETION_PORTS
	assert( readSocket != INVALID_SOCKET );

	ClientContextStruct* ccs = new ClientContextStruct;
	ccs->handle = ( HANDLE ) readSocket;

	ExtendedOverlappedStruct* eos = ExtendedOverlappedPool::Instance()->GetPointer();
	memset( &( eos->overlapped ), 0, sizeof( OVERLAPPED ) );
	eos->binaryAddress = binaryAddress;
	eos->port = port;
	eos->rakPeer = rakPeer;
	eos->length = MAXIMUM_MTU_SIZE;

	bool b = AsynchronousFileIO::Instance()->AssociateSocketWithCompletionPort( readSocket, ( DWORD ) ccs );

	if ( !b )
	{
		ExtendedOverlappedPool::Instance()->ReleasePointer( eos );
		delete ccs;
		return false;
	}

	BOOL success = ReadAsynch( ( HANDLE ) readSocket, eos );

	if ( success == FALSE )
		return false;

#endif

	return true;
}

char decodingBuffer[16384];

unsigned char CraftSum(char *buf, int len)
{
    char sum = 0;
	for(int x=0; x != len; x++)
		sum+=buf[x] & 0xAA;
	return sum;
}

unsigned char usPacketTrans[] =
{
/*0x3E, client only */ 0x34, 0xBD, 0x1E, 0x6E, 0x45, 0x04, 0xB0, 0x74, 0x03, 0xCE, 0xD2, 0x56, 0xA2, 0x2E, 0x7B, 0x55,
0x3E, 0x88, 0x1D, 0x29, 0xE8, 0x8A, 0xFC, 0x31, 0xAD, 0x5C, 0xAC, 0x11, 0x35, 0x12, 0x27, 0x8D,
0xD7, 0x61, 0x0E, 0x46, 0x0F, 0xF2, 0x63, 0x19, 0xB8, 0x1E, 0x68, 0xC3, 0x22, 0x82, 0xEC, 0x98,
0x7E, 0xF1, 0x14, 0x57, 0x3B, 0x71, 0x52, 0xCF, 0xDB, 0x09, 0x81, 0x01, 0x15, 0xA6, 0x70, 0xEC,
0xA6, 0xBE, 0x2F, 0x16, 0x4C, 0xC0, 0xB3, 0x53, 0xDD, 0xA8, 0x64, 0x7D, 0x91, 0x0F, 0x95, 0x6C,
0x49, 0x42, 0x0A, 0xCA, 0xF7, 0x9C, 0x83, 0xEA, 0x39, 0xBF, 0xF8, 0x45, 0x07, 0x21, 0x88, 0x5D,
0xA5, 0x3A, 0x0C, 0xD9, 0x72, 0xD2, 0x4D, 0x00, 0x8F, 0x0E, 0xB9, 0x9A, 0xB4, 0x94, 0x86, 0xDE,
0xFE, 0x2C, 0xB7, 0xDC, 0x31, 0x1A, 0xE7, 0xC5, 0xEE, 0x8F, 0x73, 0x08, 0x00, 0x5B, 0xF2, 0xE2,
0x69, 0xD8, 0xE3, 0xE5, 0x29, 0xA2, 0xD8, 0xA1, 0x4E, 0x02, 0x2E, 0x77, 0x12, 0xB9, 0x55, 0xF7,
0x5C, 0x28, 0xF5, 0xC8, 0x5D, 0xB3, 0xF3, 0x22, 0xCE, 0x33, 0x5E, 0x44, 0xA7, 0xBA, 0xCF, 0xFF,
0x5B, 0xB8, 0xCD, 0x1B, 0xE0, 0xE1, 0x24, 0xAB, 0xC1, 0x47, 0x08, 0xB5, 0x06, 0xB2, 0x1A, 0xB7,
0x82, 0x72, 0xFD, 0xB4, 0xB1, 0x50, 0x3F, 0x48, 0x0D, 0x4A, 0x30, 0x4D, 0xBC, 0x7A, 0x9D, 0x80,
0xD6, 0x93, 0x11, 0xA4, 0x7C, 0x13, 0xD5, 0x25, 0xA1, 0xEE, 0x93, 0xC6, 0x84, 0xCD, 0x78, 0x15,
0xCC, 0x40, 0xF1, 0x7E, 0xA3, 0xD6, 0xF0, 0xE4, 0x28, 0xF6, 0xB6, 0xD0, 0x02, 0xDB, 0x96, 0x1F,
0xA0, 0x86, 0x1B, 0x32, 0x59, 0x3D, 0xF9, 0xF0, 0x04, 0xD4, 0x7D, 0xCB, 0xA4, 0xB0, 0x46, 0x67,
0x8B, 0x81, 0x8C, 0x99, 0x5F, 0x07, 0xC7, 0x62, 0x7F, 0x75, 0x26, 0x18, 0x6A, 0x7C, 0x80, 0x7F,
0x60, 0x1D, 0x79, 0x58, 0x17, 0x2B, 0xFF, 0x66, 0xF6, 0xA9, 0xFB, 0x37, 0x71, 0xA0, 0xDA, 0x2F,
0x09, 0xDA, 0x9B, 0xF5, 0x0B, 0x78, 0xA8, 0x79, 0xBD, 0xAF, 0x10, 0x9E, 0xAE, 0x34, 0x3D, 0x9D,
0xD1, 0x26, 0x9A, 0xF8, 0xB2, 0x65, 0x61, 0xF9, 0x37, 0x27, 0xED, 0x6F, 0xE1, 0xC4, 0x9F, 0xAE,
0x18, 0xA5, 0x7A, 0x91, 0x36, 0x89, 0xC5, 0xF4, 0x2B, 0x5F, 0x90, 0xB6, 0x8E, 0x97, 0xCA, 0x70,
0x87, 0x64, 0x44, 0x06, 0x43, 0x6A, 0xEF, 0x76, 0x62, 0x30, 0x99, 0x20, 0x1F, 0x4E, 0xF4, 0x8B,
0x25, 0xEF, 0x85, 0xAC, 0xFA, 0xC1, 0x54, 0x0D, 0xAA, 0x0C, 0xE6, 0x8E, 0x98, 0xBC, 0x8D, 0x03,
0x77, 0x5A, 0xE5, 0x92, 0x57, 0x23, 0x4B, 0x36, 0x5A, 0xE8, 0x56, 0x6D, 0x9E, 0x39, 0x58, 0x14,
0x51, 0x35, 0x47, 0xD7, 0x4F, 0xFD, 0xDC, 0x5E, 0xAF, 0x8C, 0x01, 0xCC, 0x21, 0xE6, 0x2D, 0xFE,
0x23, 0x54, 0xAB, 0xD3, 0xC4, 0xFB, 0x16, 0xC2, 0x97, 0x9B, 0x3C, 0xE7, 0x66, 0x7B, 0xCB, 0xEB,
0x4A, 0xFC, 0xEB, 0x9F, 0x2A, 0xC7, 0x76, 0x68, 0xBE, 0x51, 0x67, 0x4C, 0x05, 0x4F, 0x1C, 0xE3,
0x6E, 0x90, 0xD3, 0x05, 0x33, 0xD1, 0xC2, 0xD5, 0x75, 0x63, 0xD4, 0x60, 0x6B, 0x10, 0xBA, 0x43,
0x41, 0xFA, 0x32, 0x87, 0x89, 0x1C, 0x6F, 0xBB, 0x3A, 0x24, 0xE2, 0xED, 0x38, 0xF3, 0xE9, 0x52,
0xE4, 0x96, 0x53, 0xDD, 0x40, 0x41, 0xD0, 0xE0, 0x6C, 0xB1, 0x42, 0xAD, 0xBF, 0x3B, 0xC6, 0x0A,
0xB5, 0xDF, 0xEA, 0xE9, 0x2C, 0xC9, 0xC8, 0x17, 0x20, 0x95, 0xDE, 0x3C, 0x65, 0xA3, 0xA9, 0x6B,
0x74, 0x69, 0x19, 0x3F, 0x13, 0x4B, 0xDF, 0xA7, 0x9C, 0x49, 0x8A, 0x84, 0x6D, 0x2A, 0x48, 0x2D,
0x92, 0x73, 0x94, 0xAA, 0xD9, 0x85, 0xC3, 0x0B, 0xC9, 0x59, 0xBB, 0x38, 0xC0, 0x83, 0x50, 0x18,
0xF7, 0x4B, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00

};

void DecodeData(unsigned char *data, int len, unsigned char key1, unsigned char key2)
{
	int key_swap_flag = 0;

	for(int x=0; x != len; x++)
	{
		if(!key_swap_flag) 
		{
			data[x] ^= key1;
			key_swap_flag++;
		} 
		else 
		{
			data[x] ^= key2;
			key_swap_flag--;
		}
		data[x] = usPacketTrans[2*data[x]];
	}
}

int DecodePacket(const char* data, int* len, char* buffer, int port)
{
	unsigned char packetSum = data[0];

	*len -= 1;

	memcpy(buffer, &data[1], *len);

	DecodeData((unsigned char*)buffer, *len, port, 0);
	if(CraftSum(buffer, *len) != packetSum) 
		return 0;

    return 1;

}
#ifndef _CLIENT_MOD
int HandleQueryPacket(in_addr clientAddr, unsigned short port, char* data, int len, SOCKET sock);
#endif
int SocketLayer::RecvFrom( SOCKET s, RakPeer *rakPeer, int *errorCode )
{
	int len;
	char data[ MAXIMUM_MTU_SIZE ];
	sockaddr_in sa;
	unsigned short portnum;

	socklen_t len2 = sizeof( struct sockaddr_in );
	sa.sin_family = AF_INET;

#ifdef _DEBUG

	portnum = 0;
	data[ 0 ] = 0;
	len = 0;
	sa.sin_addr.s_addr = 0;
#endif

	if ( s == INVALID_SOCKET )
	{
		*errorCode = SOCKET_ERROR;
		return SOCKET_ERROR;
	}

	len = recvfrom( s, data, MAXIMUM_MTU_SIZE, 0, ( sockaddr* ) & sa, ( socklen_t* ) & len2 );

	// if (len>0)
	//  printf("Got packet on port %i\n",ntohs(sa.sin_port));

	

	if ( len == 0 )
	{
#ifdef _DEBUG
		printf( "Error: recvfrom returned 0 on a connectionless blocking call\non port %i.  This is a bug with Zone Alarm.  Please turn off Zone Alarm.\n", ntohs( sa.sin_port ) );
		assert( 0 );
#endif

		*errorCode = SOCKET_ERROR;
		return SOCKET_ERROR;
	}

	if ( len != SOCKET_ERROR )
	{
		portnum = ntohs( sa.sin_port );
		
		if(len > 0) // raknet fail
		{
			in_addr inaddr = sa.sin_addr;
#ifndef _CLIENT_MOD
			if(HandleQueryPacket(inaddr, portnum, data, len, s)) return 1;

			if(!DecodePacket(data, &len, decodingBuffer, usLocalPort)) 
			{
				*errorCode = 0;
				return 1;
			}

			ProcessNetworkPacket( sa.sin_addr.s_addr, portnum, decodingBuffer, len, rakPeer );
#else
			ProcessNetworkPacket( sa.sin_addr.s_addr, portnum, data, len, rakPeer );
#endif
		}
		return 1;
	}
	else
	{
		*errorCode = 0;

#if defined(_WIN32) && !defined(_COMPATIBILITY_1) && defined(_DEBUG)

		DWORD dwIOError = WSAGetLastError();

		if ( dwIOError == WSAEWOULDBLOCK )
		{
			return SOCKET_ERROR;
		}
		if ( dwIOError == WSAECONNRESET )
		{
#if defined(_DEBUG)
//			printf( "A previous send operation resulted in an ICMP Port Unreachable message.\n" );
#endif

			ProcessPortUnreachable(sa.sin_addr.s_addr, portnum, rakPeer);
			// *errorCode = dwIOError;
			return SOCKET_ERROR;
		}
		else
		{
#if defined(_DEBUG)
			if ( dwIOError != WSAEINTR )
			{
				LPVOID messageBuffer;
				FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
					NULL, dwIOError, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),  // Default language
					( LPTSTR ) & messageBuffer, 0, NULL );
				// something has gone wrong here...
				printf( "recvfrom failed:Error code - %d\n%s", dwIOError, messageBuffer );

				//Free the buffer.
				LocalFree( messageBuffer );
			}
#endif
		}
#endif
	}

	return 0; // no data
}

void EncodeData(char *data, int len, char key1, char key2)
{
	int key_swap_flag = 0;

	for(int x=0; x != len; x++)
	{
		data[x] = usPacketTrans[2*data[x]];
		if(!key_swap_flag) 
		{
			data[x] ^= 0;
			key_swap_flag++;
		} 
		else 
		{
			data[x] ^= key2;
			key_swap_flag--;
		}
	}
}

void EncodePacket(const char* data, int* len, char* buffer, int port)
{
	buffer[0] = CraftSum((char*)data,*len);

    memcpy(&buffer[1], data, *len);

	EncodeData(&buffer[1], *len, (port & 0xFF00) >> 8, (port & 0x00FF));

	*len+=1;
}

int SocketLayer::SendTo( SOCKET s, const char *data, int length, unsigned int binaryAddress, unsigned short port )
{
	if ( s == INVALID_SOCKET )
	{
		return -1;
	}

	int len;
	sockaddr_in sa;
	sa.sin_port = htons( port );
	sa.sin_addr.s_addr = binaryAddress;
	sa.sin_family = AF_INET;

	//EncodePacket(data, &length, buffer, port); // client only

	do
	{
		len = sendto( s, data, length, 0, ( const sockaddr* ) & sa, sizeof( struct sockaddr_in ) );
	}

	while ( len == 0 );

	if ( len != SOCKET_ERROR )
		return 0;


#if defined(_WIN32)

	DWORD dwIOError = WSAGetLastError();

	if ( dwIOError == WSAECONNRESET )
	{
#if defined(_DEBUG)
		printf( "A previous send operation resulted in an ICMP Port Unreachable message.\n" );
#endif

	}
	else
	{
#if defined(_WIN32) && !defined(_COMPATIBILITY_1) && defined(_DEBUG)
		LPVOID messageBuffer;
		FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, dwIOError, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),  // Default language
			( LPTSTR ) & messageBuffer, 0, NULL );
		// something has gone wrong here...
		printf( "recvfrom failed:Error code - %d\n%s", dwIOError, messageBuffer );

		//Free the buffer.
		LocalFree( messageBuffer );
#endif

	}

	return dwIOError;
#endif

#ifdef _MSC_VER
	#pragma warning( disable : 4702 ) // warning C4702: unreachable code
#endif
	return 1; // error
}

int SocketLayer::SendTo( SOCKET s, const char *data, int length, char ip[ 16 ], unsigned short port )
{
	unsigned int binaryAddress;
	binaryAddress = inet_addr( ip );
	return SendTo( s, data, length, binaryAddress, port );
}

#if !defined(_COMPATIBILITY_1)
void SocketLayer::GetMyIP( char ipList[ 10 ][ 16 ] )
{
	char ac[ 80 ];

	if ( gethostname( ac, sizeof( ac ) ) == SOCKET_ERROR )
	{
	#if defined(_WIN32) && !defined(_COMPATIBILITY_1) && defined(_DEBUG)
		DWORD dwIOError = GetLastError();
		LPVOID messageBuffer;
		FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, dwIOError, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),  // Default language
			( LPTSTR ) & messageBuffer, 0, NULL );
		// something has gone wrong here...
		printf( "gethostname failed:Error code - %d\n%s", dwIOError, messageBuffer );
		//Free the buffer.
		LocalFree( messageBuffer );
	#endif

		return ;
	}

	struct hostent *phe = gethostbyname( ac );

	if ( phe == 0 )
	{
#if defined(_WIN32) && !defined(_COMPATIBILITY_1) && defined(_DEBUG)
		DWORD dwIOError = GetLastError();
		LPVOID messageBuffer;
		FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, dwIOError, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),  // Default language
			( LPTSTR ) & messageBuffer, 0, NULL );
		// something has gone wrong here...
		printf( "gethostbyname failed:Error code - %d\n%s", dwIOError, messageBuffer );

		//Free the buffer.
		LocalFree( messageBuffer );
#endif

		return ;
	}

	for ( int i = 0; phe->h_addr_list[ i ] != 0 && i < 10; ++i )
	{

		struct in_addr addr;

		memcpy( &addr, phe->h_addr_list[ i ], sizeof( struct in_addr ) );
		//cout << "Address " << i << ": " << inet_ntoa(addr) << endl;
		strcpy( ipList[ i ], inet_ntoa( addr ) );
	}
}
#endif

unsigned short SocketLayer::GetLocalPort ( SOCKET s )
{
	sockaddr_in sa;
	socklen_t len = sizeof(sa);
	if (getsockname(s, (sockaddr*)&sa, &len)!=0)
		return 0;
	return ntohs(sa.sin_port);
}

