/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tcp.cpp Basic functions to receive and send TCP packets.
 */

#ifdef ENABLE_NETWORK

#include "../../stdafx.h"
#include "../../debug.h"

#include "tcp.h"

NetworkTCPSocketHandler::NetworkTCPSocketHandler(SOCKET s) :
		NetworkSocketHandler(),
		packet_queue(NULL), packet_recv(NULL),
		sock(s), writable(false)
{
}

NetworkTCPSocketHandler::~NetworkTCPSocketHandler()
{
	this->CloseConnection();

	if (this->sock != INVALID_SOCKET) closesocket(this->sock);
	this->sock = INVALID_SOCKET;
}

NetworkRecvStatus NetworkTCPSocketHandler::CloseConnection(bool error)
{
	this->writable = false;
	NetworkSocketHandler::CloseConnection(error);

	/* Free all pending and partially received packets */
	while (this->packet_queue != NULL) {
		Packet *p = this->packet_queue->next;
		delete this->packet_queue;
		this->packet_queue = p;
	}
	delete this->packet_recv;
	this->packet_recv = NULL;

	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * This function puts the packet in the send-queue and it is send as
 * soon as possible. This is the next tick, or maybe one tick later
 * if the OS-network-buffer is full)
 * @param packet the packet to send
 */
void NetworkTCPSocketHandler::Send_Packet(Packet *packet)
{
	Packet *p;
	assert(packet != NULL);

	packet->PrepareToSend();

	/* Locate last packet buffered for the client */
	p = this->packet_queue;
	if (p == NULL) {
		/* No packets yet */
		this->packet_queue = packet;
	} else {
		/* Skip to the last packet */
		while (p->next != NULL) p = p->next;
		p->next = packet;
	}
}

/**
 * Sends all the buffered packets out for this client. It stops when:
 *   1) all packets are send (queue is empty)
 *   2) the OS reports back that it can not send any more
 *      data right now (full network-buffer, it happens ;))
 *   3) sending took too long
 * @param closing_down Whether we are closing down the connection.
 */
bool NetworkTCPSocketHandler::Send_Packets(bool closing_down)
{
	ssize_t res;
	Packet *p;

	/* We can not write to this socket!! */
	if (!this->writable) return false;
	if (!this->IsConnected()) return false;

	p = this->packet_queue;
	while (p != NULL) {
		res = send(this->sock, (const char*)p->buffer + p->pos, p->size - p->pos, 0);
		if (res == -1) {
			int err = GET_LAST_ERROR();
			if (err != EWOULDBLOCK) {
				/* Something went wrong.. close client! */
				if (!closing_down) {
					DEBUG(net, 0, "send failed with error %d", err);
					this->CloseConnection();
				}
				return false;
			}
			return true;
		}
		if (res == 0) {
			/* Client/server has left us :( */
			if (!closing_down) this->CloseConnection();
			return false;
		}

		p->pos += res;

		/* Is this packet sent? */
		if (p->pos == p->size) {
			/* Go to the next packet */
			this->packet_queue = p->next;
			delete p;
			p = this->packet_queue;
		} else {
			return true;
		}
	}

	return true;
}

/**
 * Receives a packet for the given client
 * @param status the variable to store the status into
 * @return the received packet (or NULL when it didn't receive one)
 */
Packet *NetworkTCPSocketHandler::Recv_Packet()
{
	ssize_t res;

	if (!this->IsConnected()) return NULL;

	if (this->packet_recv == NULL) {
		this->packet_recv = new Packet(this);
	}

	Packet *p = this->packet_recv;

	/* Read packet size */
	if (p->pos < sizeof(PacketSize)) {
		while (p->pos < sizeof(PacketSize)) {
		/* Read the size of the packet */
			res = recv(this->sock, (char*)p->buffer + p->pos, sizeof(PacketSize) - p->pos, 0);
			if (res == -1) {
				int err = GET_LAST_ERROR();
				if (err != EWOULDBLOCK) {
					/* Something went wrong... (104 is connection reset by peer) */
					if (err != 104) DEBUG(net, 0, "recv failed with error %d", err);
					this->CloseConnection();
					return NULL;
				}
				/* Connection would block, so stop for now */
				return NULL;
			}
			if (res == 0) {
				/* Client/server has left */
				this->CloseConnection();
				return NULL;
			}
			p->pos += res;
		}

		/* Read the packet size from the received packet */
		p->ReadRawPacketSize();

		if (p->size > SEND_MTU) {
			this->CloseConnection();
			return NULL;
		}
	}

	/* Read rest of packet */
	while (p->pos < p->size) {
		res = recv(this->sock, (char*)p->buffer + p->pos, p->size - p->pos, 0);
		if (res == -1) {
			int err = GET_LAST_ERROR();
			if (err != EWOULDBLOCK) {
				/* Something went wrong... (104 is connection reset by peer) */
				if (err != 104) DEBUG(net, 0, "recv failed with error %d", err);
				this->CloseConnection();
				return NULL;
			}
			/* Connection would block */
			return NULL;
		}
		if (res == 0) {
			/* Client/server has left */
			this->CloseConnection();
			return NULL;
		}

		p->pos += res;
	}

	/* Prepare for receiving a new packet */
	this->packet_recv = NULL;

	p->PrepareToRead();
	return p;
}

bool NetworkTCPSocketHandler::IsPacketQueueEmpty()
{
	return this->packet_queue == NULL;
}

#endif /* ENABLE_NETWORK */
