/* $Id$ */

/**
 * @file tcp_game.cpp Basic functions to receive and send TCP packets for game purposes.
 */

#ifdef ENABLE_NETWORK

#include "../../stdafx.h"
#include "../../openttd.h"
#include "../../variables.h"

#include "../network_internal.h"
#include "packet.h"
#include "tcp_game.h"
#include "../../core/pool_func.hpp"

#include "table/strings.h"

/** Make very sure the preconditions given in network_type.h are actually followed */
assert_compile(MAX_CLIENT_SLOTS > MAX_CLIENTS);
assert_compile(NetworkClientSocketPool::MAX_SIZE == MAX_CLIENT_SLOTS);

NetworkClientSocketPool _networkclientsocket_pool("NetworkClientSocket");
INSTANTIATE_POOL_METHODS(NetworkClientSocket)

NetworkClientSocket::NetworkClientSocket(ClientID client_id)
{
	this->client_id         = client_id;
	this->status            = STATUS_INACTIVE;
}

NetworkClientSocket::~NetworkClientSocket()
{
	while (this->command_queue != NULL) {
		CommandPacket *p = this->command_queue->next;
		free(this->command_queue);
		this->command_queue = p;
	}

	this->client_id = INVALID_CLIENT_ID;
	this->status = STATUS_INACTIVE;
}

/**
 * Functions to help NetworkRecv_Packet/NetworkSend_Packet a bit
 *  A socket can make errors. When that happens this handles what to do.
 * For clients: close connection and drop back to main-menu
 * For servers: close connection and that is it
 * @return the new status
 * TODO: needs to be splitted when using client and server socket packets
 */
NetworkRecvStatus NetworkClientSocket::CloseConnection(bool error)
{
	/* Clients drop back to the main menu */
	if (!_network_server && _networking) {
		_switch_mode = SM_MENU;
		_networking = false;
		extern StringID _switch_mode_errorstr;
		_switch_mode_errorstr = STR_NETWORK_ERROR_LOSTCONNECTION;

		return NETWORK_RECV_STATUS_CONN_LOST;
	}

	NetworkCloseClient(this, error);
	return NETWORK_RECV_STATUS_OKAY;
}

#endif /* ENABLE_NETWORK */
