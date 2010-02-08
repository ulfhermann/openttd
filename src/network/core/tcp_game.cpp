/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tcp_game.cpp Basic functions to receive and send TCP packets for game purposes.
 */

#ifdef ENABLE_NETWORK

#include "../../stdafx.h"

#include "../network.h"
#include "../network_internal.h"
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

	if (_redirect_console_to_client == this->client_id) _redirect_console_to_client = INVALID_CLIENT_ID;
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
