/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_func.h Network functions used by other parts of OpenTTD. */

#ifndef NETWORK_FUNC_H
#define NETWORK_FUNC_H

#include "core/address.h"
#include "network_type.h"
#include "../console_type.h"
#include "../gfx_type.h"
#include "../openttd.h"
#include "../company_type.h"

#ifdef ENABLE_NETWORK

extern NetworkServerGameInfo _network_game_info;
extern NetworkCompanyState *_network_company_states;

extern ClientID _network_own_client_id;
extern ClientID _redirect_console_to_client;
extern bool _network_need_advertise;
extern uint32 _network_last_advertise_frame;
extern uint8 _network_reconnect;
extern StringList _network_bind_list;
extern StringList _network_host_list;
extern StringList _network_ban_list;

byte NetworkSpectatorCount();
void NetworkUpdateClientName();
bool NetworkCompanyHasClients(CompanyID company);
const char *NetworkChangeCompanyPassword(const char *);
void NetworkReboot();
void NetworkDisconnect(bool blocking = false);
void NetworkGameLoop();
void NetworkUDPGameLoop();
void NetworkUDPCloseAll();
void ParseConnectionString(const char **company, const char **port, char *connection_string);
void NetworkStartDebugLog(NetworkAddress address);
void NetworkPopulateCompanyStats(NetworkCompanyStats *stats);

void NetworkUpdateClientInfo(ClientID client_id);
void NetworkClientConnectGame(NetworkAddress address, CompanyID join_as, const char *join_server_password = NULL, const char *join_company_password = NULL);
void NetworkClientRequestMove(CompanyID company, const char *pass = "");
void NetworkClientSendRcon(const char *password, const char *command);
void NetworkClientSendChat(NetworkAction action, DestType type, int dest, const char *msg, int64 data = 0);
bool NetworkClientPreferTeamChat(const NetworkClientInfo *cio);
bool NetworkCompanyIsPassworded(CompanyID company_id);
bool NetworkMaxCompaniesReached();
bool NetworkMaxSpectatorsReached();
void NetworkPrintClients();
void NetworkHandlePauseChange(PauseMode prev_mode, PauseMode changed_mode);

/*** Commands ran by the server ***/
void NetworkServerMonthlyLoop();
void NetworkServerYearlyLoop();
void NetworkServerChangeOwner(Owner current_owner, Owner new_owner);
void NetworkServerSendConfigUpdate();
void NetworkServerShowStatusToConsole();
bool NetworkServerStart();
void NetworkServerUpdateCompanyPassworded(CompanyID company_id, bool passworded);
bool NetworkServerChangeClientName(ClientID client_id, const char *new_name);

NetworkClientInfo *NetworkFindClientInfoFromIndex(ClientIndex index);
NetworkClientInfo *NetworkFindClientInfoFromClientID(ClientID client_id);
NetworkClientInfo *NetworkFindClientInfoFromIP(const char *ip);
const char *GetClientIP(NetworkClientInfo *ci);

void NetworkServerDoMove(ClientID client_id, CompanyID company_id);
void NetworkServerSendRcon(ClientID client_id, ConsoleColour colour_code, const char *string);
void NetworkServerSendError(ClientID client_id, NetworkErrorCode error);
void NetworkServerSendChat(NetworkAction action, DestType type, int dest, const char *msg, ClientID from_id, int64 data = 0);

void NetworkServerKickClient(ClientID client_id);
void NetworkServerBanIP(const char *banip);

void NetworkInitChatMessage();
void CDECL NetworkAddChatMessage(TextColour colour, uint8 duration, const char *message, ...) WARN_FORMAT(3, 4);
void NetworkUndrawChatMessage();
void NetworkChatMessageDailyLoop();

void NetworkAfterNewGRFScan();

#endif /* ENABLE_NETWORK */
#endif /* NETWORK_FUNC_H */
