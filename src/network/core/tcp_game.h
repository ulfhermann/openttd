/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tcp_game.h Basic functions to receive and send TCP packets for game purposes.
 */

#ifndef NETWORK_CORE_TCP_GAME_H
#define NETWORK_CORE_TCP_GAME_H

#include "os_abstraction.h"
#include "tcp.h"
#include "../network_type.h"
#include "../../core/pool_type.hpp"

#ifdef ENABLE_NETWORK

/**
 * Enum with all types of UDP packets.
 * The order of the first 4 packets MUST not be changed, as
 * it protects old clients from joining newer servers
 * (because SERVER_ERROR is the respond to a wrong revision)
 */
enum TCPPacketType {
	PACKET_SERVER_FULL,
	PACKET_SERVER_BANNED,
	PACKET_CLIENT_JOIN,
	PACKET_SERVER_ERROR,
	PACKET_CLIENT_COMPANY_INFO,
	PACKET_SERVER_COMPANY_INFO,
	PACKET_SERVER_CLIENT_INFO,
	PACKET_SERVER_NEED_GAME_PASSWORD,
	PACKET_SERVER_NEED_COMPANY_PASSWORD,
	PACKET_CLIENT_GAME_PASSWORD,
	PACKET_CLIENT_COMPANY_PASSWORD,
	PACKET_SERVER_WELCOME,
	PACKET_CLIENT_GETMAP,
	PACKET_SERVER_WAIT,
	PACKET_SERVER_MAP,
	PACKET_CLIENT_MAP_OK,
	PACKET_SERVER_JOIN,
	PACKET_SERVER_FRAME,
	PACKET_SERVER_SYNC,
	PACKET_CLIENT_ACK,
	PACKET_CLIENT_COMMAND,
	PACKET_SERVER_COMMAND,
	PACKET_CLIENT_CHAT,
	PACKET_SERVER_CHAT,
	PACKET_CLIENT_SET_PASSWORD,
	PACKET_CLIENT_SET_NAME,
	PACKET_CLIENT_QUIT,
	PACKET_CLIENT_ERROR,
	PACKET_SERVER_QUIT,
	PACKET_SERVER_ERROR_QUIT,
	PACKET_SERVER_SHUTDOWN,
	PACKET_SERVER_NEWGAME,
	PACKET_SERVER_RCON,
	PACKET_CLIENT_RCON,
	PACKET_SERVER_CHECK_NEWGRFS,
	PACKET_CLIENT_NEWGRFS_CHECKED,
	PACKET_SERVER_MOVE,
	PACKET_CLIENT_MOVE,
	PACKET_SERVER_COMPANY_UPDATE,
	PACKET_SERVER_CONFIG_UPDATE,
	PACKET_END                   ///< Must ALWAYS be on the end of this list!! (period)
};

/** Packet that wraps a command */
struct CommandPacket;

/** Status of a client */
enum ClientStatus {
	STATUS_INACTIVE,     ///< The client is not connected nor active
	STATUS_NEWGRFS_CHECK, ///< The client is checking NewGRFs
	STATUS_AUTH_GAME,    ///< The client is authorizing with game (server) password
	STATUS_AUTH_COMPANY, ///< The client is authorizing with company password
	STATUS_AUTHORIZED,   ///< The client is authorized
	STATUS_MAP_WAIT,     ///< The client is waiting as someone else is downloading the map
	STATUS_MAP,          ///< The client is downloading the map
	STATUS_DONE_MAP,     ///< The client has downloaded the map
	STATUS_PRE_ACTIVE,   ///< The client is catching up the delayed frames
	STATUS_ACTIVE,       ///< The client is active within in the game
	STATUS_END           ///< Must ALWAYS be on the end of this list!! (period)
};

class NetworkClientSocket;
typedef Pool<NetworkClientSocket, ClientIndex, 8, MAX_CLIENT_SLOTS> NetworkClientSocketPool;
extern NetworkClientSocketPool _networkclientsocket_pool;

/** Base socket handler for all TCP sockets */
class NetworkClientSocket : public NetworkClientSocketPool::PoolItem<&_networkclientsocket_pool>, public NetworkTCPSocketHandler {
/* TODO: rewrite into a proper class */
private:
	NetworkClientInfo *info;  ///< Client info related to this socket
public:
	ClientID client_id;       ///< Client identifier
	uint32 last_frame;        ///< Last frame we have executed
	uint32 last_frame_server; ///< Last frame the server has executed
	byte lag_test;            ///< Byte used for lag-testing the client

	ClientStatus status;      ///< Status of this client

	CommandPacket *command_queue; ///< The command-queue awaiting delivery

	NetworkRecvStatus CloseConnection(bool error = true);

	NetworkClientSocket(ClientID client_id = INVALID_CLIENT_ID);
	~NetworkClientSocket();

	inline void SetInfo(NetworkClientInfo *info) { assert(info != NULL && this->info == NULL); this->info = info; }
	inline NetworkClientInfo *GetInfo() const { return this->info; }

	const char *Recv_Command(Packet *p, CommandPacket *cp);
	void Send_Command(Packet *p, const CommandPacket *cp);
};

#define FOR_ALL_CLIENT_SOCKETS_FROM(var, start) FOR_ALL_ITEMS_FROM(NetworkClientSocket, clientsocket_index, var, start)
#define FOR_ALL_CLIENT_SOCKETS(var) FOR_ALL_CLIENT_SOCKETS_FROM(var, 0)

#endif /* ENABLE_NETWORK */

#endif /* NETWORK_CORE_TCP_GAME_H */
