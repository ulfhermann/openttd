/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_gui.cpp Implementation of the Network related GUIs. */

#ifdef ENABLE_NETWORK
#include "../stdafx.h"
#include "../openttd.h"
#include "../strings_func.h"
#include "../date_func.h"
#include "../fios.h"
#include "network_internal.h"
#include "network_client.h"
#include "network_gui.h"
#include "network_gamelist.h"
#include "../gui.h"
#include "network_server.h"
#include "network_udp.h"
#include "../window_func.h"
#include "../string_func.h"
#include "../gfx_func.h"
#include "../settings_type.h"
#include "../widgets/dropdown_func.h"
#include "../querystring_gui.h"
#include "../sortlist_type.h"
#include "../company_base.h"
#include "../company_func.h"

#include "table/strings.h"
#include "../table/sprites.h"


static void ShowNetworkStartServerWindow();
static void ShowNetworkLobbyWindow(NetworkGameList *ngl);
extern void SwitchToMode(SwitchMode new_mode);

static const StringID _connection_types_dropdown[] = {
	STR_NETWORK_START_SERVER_LAN_INTERNET,
	STR_NETWORK_START_SERVER_INTERNET_ADVERTISE,
	INVALID_STRING_ID
};

static const StringID _lan_internet_types_dropdown[] = {
	STR_NETWORK_SERVER_LIST_LAN,
	STR_NETWORK_SERVER_LIST_INTERNET,
	INVALID_STRING_ID
};

static StringID _language_dropdown[NETLANG_COUNT + 1] = {STR_NULL};

void SortNetworkLanguages()
{
	/* Init the strings */
	if (_language_dropdown[0] == STR_NULL) {
		for (int i = 0; i < NETLANG_COUNT; i++) _language_dropdown[i] = STR_NETWORK_LANG_ANY + i;
		_language_dropdown[NETLANG_COUNT] = INVALID_STRING_ID;
	}

	/* Sort the strings (we don't move 'any' and the 'invalid' one) */
	qsort(&_language_dropdown[1], NETLANG_COUNT - 1, sizeof(StringID), &StringIDSorter);
}

enum {
	NET_PRC__OFFSET_TOP_WIDGET          = 54,
	NET_PRC__OFFSET_TOP_WIDGET_COMPANY  = 52,
	NET_PRC__SIZE_OF_ROW                = 14,
};

/** Update the network new window because a new server is
 * found on the network.
 * @param unselect unselect the currently selected item */
void UpdateNetworkGameWindow(bool unselect)
{
	InvalidateWindowData(WC_NETWORK_WINDOW, 0, unselect ? 1 : 0);
}

/** Enum for NetworkGameWindow, referring to _network_game_window_widgets */
enum NetworkGameWindowWidgets {
	NGWW_CLOSE,         ///< Close 'X' button
	NGWW_CAPTION,       ///< Caption of the window
	NGWW_MAIN,          ///< Main panel

	NGWW_CONNECTION,    ///< Label in from of connection droplist
	NGWW_CONN_BTN,      ///< 'Connection' droplist button
	NGWW_CLIENT,        ///< Panel with editbox to set client name

	NGWW_NAME,          ///< 'Name' button
	NGWW_CLIENTS,       ///< 'Clients' button
	NGWW_MAPSIZE,       ///< 'Map size' button
	NGWW_DATE,          ///< 'Date' button
	NGWW_YEARS,         ///< 'Years' button
	NGWW_INFO,          ///< Third button in the game list panel

	NGWW_MATRIX,        ///< Panel with list of games
	NGWW_SCROLLBAR,     ///< Scrollbar of matrix

	NGWW_LASTJOINED_LABEL, ///< Label "Last joined server:"
	NGWW_LASTJOINED,    ///< Info about the last joined server

	NGWW_DETAILS,       ///< Panel with game details
	NGWW_JOIN,          ///< 'Join game' button
	NGWW_REFRESH,       ///< 'Refresh server' button
	NGWW_NEWGRF,        ///< 'NewGRF Settings' button

	NGWW_FIND,          ///< 'Find server' button
	NGWW_ADD,           ///< 'Add server' button
	NGWW_START,         ///< 'Start server' button
	NGWW_CANCEL,        ///< 'Cancel' button

	NGWW_RESIZE,        ///< Resize button
};

typedef GUIList<NetworkGameList*> GUIGameServerList;
typedef uint16 ServerListPosition;
static const ServerListPosition SLP_INVALID = 0xFFFF;

class NetworkGameWindow : public QueryStringBaseWindow {
protected:
	/* Runtime saved values */
	static Listing last_sorting;

	/* Constants for sorting servers */
	static GUIGameServerList::SortFunction * const sorter_funcs[];

	byte field;                   ///< selected text-field
	NetworkGameList *server;      ///< selected server
	NetworkGameList *last_joined; ///< the last joined server
	GUIGameServerList servers;    ///< list with game servers.
	ServerListPosition list_pos;  ///< position of the selected server

	/**
	 * (Re)build the network game list as its amount has changed because
	 * an item has been added or deleted for example
	 */
	void BuildNetworkGameList()
	{
		if (!this->servers.NeedRebuild()) return;

		/* Create temporary array of games to use for listing */
		this->servers.Clear();

		for (NetworkGameList *ngl = _network_game_list; ngl != NULL; ngl = ngl->next) {
			*this->servers.Append() = ngl;
		}

		this->servers.Compact();
		this->servers.RebuildDone();
	}

	/** Sort servers by name. */
	static int CDECL NGameNameSorter(NetworkGameList * const *a, NetworkGameList * const *b)
	{
		return strcasecmp((*a)->info.server_name, (*b)->info.server_name);
	}

	/** Sort servers by the amount of clients online on a
	 * server. If the two servers have the same amount, the one with the
	 * higher maximum is preferred. */
	static int CDECL NGameClientSorter(NetworkGameList * const *a, NetworkGameList * const *b)
	{
		/* Reverse as per default we are interested in most-clients first */
		int r = (*a)->info.clients_on - (*b)->info.clients_on;

		if (r == 0) r = (*a)->info.clients_max - (*b)->info.clients_max;
		if (r == 0) r = NGameNameSorter(a, b);

		return r;
	}

	/** Sort servers by map size */
	static int CDECL NGameMapSizeSorter(NetworkGameList * const *a, NetworkGameList * const *b)
	{
		/* Sort by the area of the map. */
		int r = ((*a)->info.map_height) * ((*a)->info.map_width) - ((*b)->info.map_height) * ((*b)->info.map_width);

		if (r == 0) r = (*a)->info.map_width - (*b)->info.map_width;
		return (r != 0) ? r : NGameClientSorter(a, b);
	}

	/** Sort servers by current date */
	static int CDECL NGameDateSorter(NetworkGameList * const *a, NetworkGameList * const *b)
	{
		int r = (*a)->info.game_date - (*b)->info.game_date;
		return (r != 0) ? r : NGameClientSorter(a, b);
	}

	/** Sort servers by the number of days the game is running */
	static int CDECL NGameYearsSorter(NetworkGameList * const *a, NetworkGameList * const *b)
	{
		int r = (*a)->info.game_date - (*a)->info.start_date - (*b)->info.game_date + (*b)->info.start_date;
		return (r != 0) ? r : NGameDateSorter(a, b);
	}

	/** Sort servers by joinability. If both servers are the
	 * same, prefer the non-passworded server first. */
	static int CDECL NGameAllowedSorter(NetworkGameList * const *a, NetworkGameList * const *b)
	{
		/* The servers we do not know anything about (the ones that did not reply) should be at the bottom) */
		int r = StrEmpty((*a)->info.server_revision) - StrEmpty((*b)->info.server_revision);

		/* Reverse default as we are interested in version-compatible clients first */
		if (r == 0) r = (*b)->info.version_compatible - (*a)->info.version_compatible;
		/* The version-compatible ones are then sorted with NewGRF compatible first, incompatible last */
		if (r == 0) r = (*b)->info.compatible - (*a)->info.compatible;
		/* Passworded servers should be below unpassworded servers */
		if (r == 0) r = (*a)->info.use_password - (*b)->info.use_password;
		/* Finally sort on the name of the server */
		if (r == 0) r = NGameNameSorter(a, b);

		return r;
	}

	/** Sort the server list */
	void SortNetworkGameList()
	{
		if (!this->servers.Sort()) return;

		/* After sorting ngl->sort_list contains the sorted items. Put these back
		 * into the original list. Basically nothing has changed, we are only
		 * shuffling the ->next pointers. While iterating, look for the
		 * currently selected server and set list_pos to its position */
		this->list_pos = SLP_INVALID;
		_network_game_list = this->servers[0];
		NetworkGameList *item = _network_game_list;
		if (item == this->server) this->list_pos = 0;
		for (uint i = 1; i != this->servers.Length(); i++) {
			item->next = this->servers[i];
			item = item->next;
			if (item == this->server) this->list_pos = i;
		}
		item->next = NULL;
	}

	/**
	 * Draw a single server line.
	 * @param cur_item  the server to draw.
	 * @param y         from where to draw?
	 * @param highlight does the line need to be highlighted?
	 */
	void DrawServerLine(const NetworkGameList *cur_item, uint y, bool highlight)
	{
		/* show highlighted item with a different colour */
		if (highlight) GfxFillRect(this->widget[NGWW_NAME].left + 1, y - 2, this->widget[NGWW_INFO].right - 1, y + 9, 10);

		DrawString(this->widget[NGWW_NAME].left + 5, this->widget[NGWW_NAME].right, y, cur_item->info.server_name, TC_BLACK);

		/* only draw details if the server is online */
		if (cur_item->online) {
			SetDParam(0, cur_item->info.clients_on);
			SetDParam(1, cur_item->info.clients_max);
			SetDParam(2, cur_item->info.companies_on);
			SetDParam(3, cur_item->info.companies_max);
			DrawString(this->widget[NGWW_CLIENTS].left, this->widget[NGWW_CLIENTS].right, y, STR_NETWORK_SERVER_LIST_GENERAL_ONLINE, TC_FROMSTRING, SA_CENTER);

			/* map size */
			if (!this->IsWidgetHidden(NGWW_MAPSIZE)) {
				SetDParam(0, cur_item->info.map_width);
				SetDParam(1, cur_item->info.map_height);
				DrawString(this->widget[NGWW_MAPSIZE].left, this->widget[NGWW_MAPSIZE].right, y, STR_NETWORK_SERVER_LIST_MAP_SIZE_SHORT, TC_FROMSTRING, SA_CENTER);
			}

			/* current date */
			if (!this->IsWidgetHidden(NGWW_DATE)) {
				YearMonthDay ymd;
				ConvertDateToYMD(cur_item->info.game_date, &ymd);
				SetDParam(0, ymd.year);
				DrawString(this->widget[NGWW_DATE].left, this->widget[NGWW_DATE].right, y, STR_JUST_INT, TC_BLACK, SA_CENTER);
			}

			/* number of years the game is running */
			if (!this->IsWidgetHidden(NGWW_YEARS)) {
				YearMonthDay ymd_cur, ymd_start;
				ConvertDateToYMD(cur_item->info.game_date, &ymd_cur);
				ConvertDateToYMD(cur_item->info.start_date, &ymd_start);
				SetDParam(0, ymd_cur.year - ymd_start.year);
				DrawString(this->widget[NGWW_YEARS].left, this->widget[NGWW_YEARS].right, y, STR_JUST_INT, TC_BLACK, SA_CENTER);
			}

			/* draw a lock if the server is password protected */
			if (cur_item->info.use_password) DrawSprite(SPR_LOCK, PAL_NONE, this->widget[NGWW_INFO].left + 5, y - 1);

			/* draw red or green icon, depending on compatibility with server */
			DrawSprite(SPR_BLOT, (cur_item->info.compatible ? PALETTE_TO_GREEN : (cur_item->info.version_compatible ? PALETTE_TO_YELLOW : PALETTE_TO_RED)), this->widget[NGWW_INFO].left + 15, y);

			/* draw flag according to server language */
			DrawSprite(SPR_FLAGS_BASE + cur_item->info.server_lang, PAL_NONE, this->widget[NGWW_INFO].left + 25, y);
		}
	}

	/**
	 * Scroll the list up or down to the currently selected server.
	 * If the server is below the currently displayed servers, it will
	 * scroll down an amount so that the server appears at the bottom.
	 * If the server is above the currently displayed servers, it will
	 * scroll up so that the server appears at the top.
	 */
	void ScrollToSelectedServer()
	{
		if (this->list_pos == SLP_INVALID) return; // no server selected
		if (this->list_pos < this->vscroll.pos) {
			/* scroll up to the server */
			this->vscroll.pos = this->list_pos;
		} else if (this->list_pos >= this->vscroll.pos + this->vscroll.cap) {
			/* scroll down so that the server is at the bottom */
			this->vscroll.pos = this->list_pos - this->vscroll.cap + 1;
		}
	}

public:
	NetworkGameWindow(const WindowDesc *desc) : QueryStringBaseWindow(NETWORK_CLIENT_NAME_LENGTH, desc)
	{
		this->widget[NGWW_CLIENTS].left = this->widget[NGWW_NAME].right + 1;
		this->widget[NGWW_MAPSIZE].left = this->widget[NGWW_NAME].right + 1;
		this->widget[NGWW_DATE].left = this->widget[NGWW_NAME].right + 1;
		this->widget[NGWW_YEARS].left = this->widget[NGWW_NAME].right + 1;

		this->widget[NGWW_CLIENTS].right = this->widget[NGWW_INFO].left - 1;
		this->widget[NGWW_MAPSIZE].right = this->widget[NGWW_INFO].left - 1;
		this->widget[NGWW_DATE].right = this->widget[NGWW_INFO].left - 1 - 20;
		this->widget[NGWW_YEARS].right = this->widget[NGWW_INFO].left - 1 - 20;

		this->widget[NGWW_NAME].display_flags &= ~RESIZE_LRTB;
		this->widget[NGWW_CLIENTS].display_flags &= ~RESIZE_LRTB;
		this->widget[NGWW_MAPSIZE].display_flags &= ~RESIZE_LRTB;
		this->widget[NGWW_DATE].display_flags &= ~RESIZE_LRTB;
		this->widget[NGWW_YEARS].display_flags &= ~RESIZE_LRTB;

		ttd_strlcpy(this->edit_str_buf, _settings_client.network.client_name, this->edit_str_size);
		this->afilter = CS_ALPHANUMERAL;
		InitializeTextBuffer(&this->text, this->edit_str_buf, this->edit_str_size, 120);
		this->SetFocusedWidget(NGWW_CLIENT);

		UpdateNetworkGameWindow(true);

		this->vscroll.cap = 11;
		this->resize.step_height = NET_PRC__SIZE_OF_ROW;

		this->field = NGWW_CLIENT;
		this->server = NULL;
		this->list_pos = SLP_INVALID;

		this->last_joined = NetworkGameListAddItem(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port));

		this->servers.SetListing(this->last_sorting);
		this->servers.SetSortFuncs(this->sorter_funcs);
		this->servers.ForceRebuild();
		this->SortNetworkGameList();

		this->FindWindowPlacementAndResize(desc);
	}

	~NetworkGameWindow()
	{
		this->last_sorting = this->servers.GetListing();
	}

	virtual void OnPaint()
	{
		NetworkGameList *sel = this->server;
		const SortButtonState arrow = this->servers.IsDescSortOrder() ? SBS_DOWN : SBS_UP;

		if (this->servers.NeedRebuild()) {
			this->BuildNetworkGameList();
			SetVScrollCount(this, this->servers.Length());
		}
		this->SortNetworkGameList();

		/* 'Refresh' button invisible if no server selected */
		this->SetWidgetDisabledState(NGWW_REFRESH, sel == NULL);
		/* 'Join' button disabling conditions */
		this->SetWidgetDisabledState(NGWW_JOIN, sel == NULL || // no Selected Server
				!sel->online || // Server offline
				sel->info.clients_on >= sel->info.clients_max || // Server full
				!sel->info.compatible); // Revision mismatch

		/* 'NewGRF Settings' button invisible if no NewGRF is used */
		this->SetWidgetHiddenState(NGWW_NEWGRF, sel == NULL ||
				!sel->online ||
				sel->info.grfconfig == NULL);

		SetDParam(0, 0x00);
		SetDParam(1, _lan_internet_types_dropdown[_settings_client.network.lan_internet]);
		this->DrawWidgets();

		/* Edit box to set client name */
		this->DrawEditBox(NGWW_CLIENT);

		DrawString(0, this->widget[NGWW_CLIENT].left - 5, 23, STR_NETWORK_SERVER_LIST_PLAYER_NAME, TC_FROMSTRING, SA_RIGHT);

		/* Sort based on widgets: name, clients, compatibility */
		switch (this->servers.SortType()) {
			case NGWW_NAME    - NGWW_NAME: this->DrawSortButtonState(NGWW_NAME,    arrow); break;
			case NGWW_CLIENTS - NGWW_NAME: this->DrawSortButtonState(NGWW_CLIENTS, arrow); break;
			case NGWW_MAPSIZE - NGWW_NAME: if (!this->IsWidgetHidden(NGWW_MAPSIZE)) this->DrawSortButtonState(NGWW_MAPSIZE, arrow); break;
			case NGWW_DATE    - NGWW_NAME: if (!this->IsWidgetHidden(NGWW_DATE))    this->DrawSortButtonState(NGWW_DATE,    arrow); break;
			case NGWW_YEARS   - NGWW_NAME: if (!this->IsWidgetHidden(NGWW_YEARS))   this->DrawSortButtonState(NGWW_YEARS,   arrow); break;
			case NGWW_INFO    - NGWW_NAME: this->DrawSortButtonState(NGWW_INFO,    arrow); break;
		}

		uint16 y = NET_PRC__OFFSET_TOP_WIDGET + 3;

		const int max = min(this->vscroll.pos + this->vscroll.cap, (int)this->servers.Length());

		for (int i = this->vscroll.pos; i < max; ++i) {
			const NetworkGameList *ngl = this->servers[i];
			this->DrawServerLine(ngl, y, ngl == sel);
			y += NET_PRC__SIZE_OF_ROW;
		}

		/* Draw the last joined server, if any */
		if (this->last_joined != NULL) this->DrawServerLine(this->last_joined, y = this->widget[NGWW_LASTJOINED].top + 3, this->last_joined == sel);

		/* Draw the right menu */
		GfxFillRect(this->widget[NGWW_DETAILS].left + 1, 43, this->widget[NGWW_DETAILS].right - 1, 92, 157);
		if (sel == NULL) {
			DrawString(this->widget[NGWW_DETAILS].left + 1, this->widget[NGWW_DETAILS].right - 1, 58, STR_NETWORK_SERVER_LIST_GAME_INFO, TC_FROMSTRING, SA_CENTER);
		} else if (!sel->online) {
			DrawString(this->widget[NGWW_DETAILS].left + 1, this->widget[NGWW_DETAILS].right - 1, 68, sel->info.server_name, TC_ORANGE, SA_CENTER); // game name

			DrawString(this->widget[NGWW_DETAILS].left + 1, this->widget[NGWW_DETAILS].right - 1, 132, STR_NETWORK_SERVER_LIST_SERVER_OFFLINE, TC_FROMSTRING, SA_CENTER); // server offline
		} else { // show game info
			uint16 y = 100;
			const uint16 x = this->widget[NGWW_DETAILS].left + 5;

			DrawString(this->widget[NGWW_DETAILS].left + 1, this->widget[NGWW_DETAILS].right - 1, 48, STR_NETWORK_SERVER_LIST_GAME_INFO, TC_FROMSTRING, SA_CENTER);

			DrawString(this->widget[NGWW_DETAILS].left, this->widget[NGWW_DETAILS].right, 62, sel->info.server_name, TC_ORANGE, SA_CENTER); // game name
			DrawString(this->widget[NGWW_DETAILS].left, this->widget[NGWW_DETAILS].right, 74, sel->info.map_name, TC_BLACK, SA_CENTER); // map name

			SetDParam(0, sel->info.clients_on);
			SetDParam(1, sel->info.clients_max);
			SetDParam(2, sel->info.companies_on);
			SetDParam(3, sel->info.companies_max);
			DrawString(x, this->widget[NGWW_DETAILS].right, y, STR_NETWORK_SERVER_LIST_CLIENTS);
			y += 10;

			SetDParam(0, STR_NETWORK_LANG_ANY + sel->info.server_lang);
			DrawString(x, this->widget[NGWW_DETAILS].right, y, STR_NETWORK_SERVER_LIST_LANGUAGE); // server language
			y += 10;

			SetDParam(0, STR_CHEAT_SWITCH_CLIMATE_TEMPERATE_LANDSCAPE + sel->info.map_set);
			DrawString(x, this->widget[NGWW_DETAILS].right, y, STR_NETWORK_SERVER_LIST_TILESET); // tileset
			y += 10;

			SetDParam(0, sel->info.map_width);
			SetDParam(1, sel->info.map_height);
			DrawString(x, this->widget[NGWW_DETAILS].right, y, STR_NETWORK_SERVER_LIST_MAP_SIZE); // map size
			y += 10;

			SetDParamStr(0, sel->info.server_revision);
			DrawString(x, this->widget[NGWW_DETAILS].right, y, STR_NETWORK_SERVER_LIST_SERVER_VERSION); // server version
			y += 10;

			SetDParamStr(0, sel->address.GetAddressAsString());
			DrawString(x, this->widget[NGWW_DETAILS].right, y, STR_NETWORK_SERVER_LIST_SERVER_ADDRESS); // server address
			y += 10;

			SetDParam(0, sel->info.start_date);
			DrawString(x, this->widget[NGWW_DETAILS].right, y, STR_NETWORK_SERVER_LIST_START_DATE); // start date
			y += 10;

			SetDParam(0, sel->info.game_date);
			DrawString(x, this->widget[NGWW_DETAILS].right, y, STR_NETWORK_SERVER_LIST_CURRENT_DATE); // current date
			y += 10;

			y += 2;

			if (!sel->info.compatible) {
				DrawString(this->widget[NGWW_DETAILS].left + 1, this->widget[NGWW_DETAILS].right - 1, y, sel->info.version_compatible ? STR_NETWORK_SERVER_LIST_GRF_MISMATCH : STR_NETWORK_SERVER_LIST_VERSION_MISMATCH, TC_FROMSTRING, SA_CENTER); // server mismatch
			} else if (sel->info.clients_on == sel->info.clients_max) {
				/* Show: server full, when clients_on == max_clients */
				DrawString(this->widget[NGWW_DETAILS].left + 1, this->widget[NGWW_DETAILS].right - 1, y, STR_NETWORK_SERVER_LIST_SERVER_FULL, TC_FROMSTRING, SA_CENTER); // server full
			} else if (sel->info.use_password) {
				DrawString(this->widget[NGWW_DETAILS].left + 1, this->widget[NGWW_DETAILS].right - 1, y, STR_NETWORK_SERVER_LIST_PASSWORD, TC_FROMSTRING, SA_CENTER); // password warning
			}

			y += 10;
		}
	}

	virtual void OnClick(Point pt, int widget)
	{
		this->field = widget;
		switch (widget) {
			case NGWW_CANCEL: // Cancel button
				DeleteWindowById(WC_NETWORK_WINDOW, 0);
				break;

			case NGWW_CONN_BTN: // 'Connection' droplist
				ShowDropDownMenu(this, _lan_internet_types_dropdown, _settings_client.network.lan_internet, NGWW_CONN_BTN, 0, 0); // do it for widget NSSW_CONN_BTN
				break;

			case NGWW_NAME:    // Sort by name
			case NGWW_CLIENTS: // Sort by connected clients
			case NGWW_MAPSIZE: // Sort by map size
			case NGWW_DATE:    // Sort by date
			case NGWW_YEARS:   // Sort by years
			case NGWW_INFO:    // Connectivity (green dot)
				if (this->servers.SortType() == widget - NGWW_NAME) {
					this->servers.ToggleSortOrder();
					if (this->list_pos != SLP_INVALID) this->list_pos = this->servers.Length() - this->list_pos - 1;
				} else {
					this->servers.SetSortType(widget - NGWW_NAME);
					this->servers.ForceResort();
					this->SortNetworkGameList();
				}
				this->ScrollToSelectedServer();
				this->SetDirty();
				break;

			case NGWW_MATRIX: { // Matrix to show networkgames
				uint32 id_v = (pt.y - NET_PRC__OFFSET_TOP_WIDGET) / NET_PRC__SIZE_OF_ROW;

				if (id_v >= this->vscroll.cap) return; // click out of bounds
				id_v += this->vscroll.pos;

				this->server = (id_v < this->servers.Length()) ? this->servers[id_v] : NULL;
				this->list_pos = (server == NULL) ? SLP_INVALID : id_v;
				this->SetDirty();
			} break;

			case NGWW_LASTJOINED: {
				NetworkGameList *last_joined = NetworkGameListAddItem(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port));
				if (last_joined != NULL) {
					this->server = last_joined;

					/* search the position of the newly selected server */
					for (uint i = 0; i < this->servers.Length(); i++) {
						if (this->servers[i] == this->server) {
							this->list_pos = i;
							break;
						}
					}
					this->ScrollToSelectedServer();
					this->SetDirty();
				}
			} break;

			case NGWW_FIND: // Find server automatically
				switch (_settings_client.network.lan_internet) {
					case 0: NetworkUDPSearchGame(); break;
					case 1: NetworkUDPQueryMasterServer(); break;
				}
				break;

			case NGWW_ADD: // Add a server
				SetDParamStr(0, _settings_client.network.connect_to_ip);
				ShowQueryString(
					STR_JUST_RAW_STRING,
					STR_NETWORK_SERVER_LIST_ENTER_IP,
					NETWORK_HOSTNAME_LENGTH,  // maximum number of characters including '\0'
					0,                        // no limit in pixels
					this, CS_ALPHANUMERAL, QSF_ACCEPT_UNCHANGED);
				break;

			case NGWW_START: // Start server
				ShowNetworkStartServerWindow();
				break;

			case NGWW_JOIN: // Join Game
				if (this->server != NULL) {
					snprintf(_settings_client.network.last_host, sizeof(_settings_client.network.last_host), "%s", this->server->address.GetHostname());
					_settings_client.network.last_port = this->server->address.GetPort();
					ShowNetworkLobbyWindow(this->server);
				}
				break;

			case NGWW_REFRESH: // Refresh
				if (this->server != NULL) NetworkUDPQueryServer(this->server->address);
				break;

			case NGWW_NEWGRF: // NewGRF Settings
				if (this->server != NULL) ShowNewGRFSettings(false, false, false, &this->server->info.grfconfig);
				break;
		}
	}

	virtual void OnDoubleClick(Point pt, int widget)
	{
		if (widget == NGWW_MATRIX || widget == NGWW_LASTJOINED) {
			/* is the Join button enabled? */
			if (!this->IsWidgetDisabled(NGWW_JOIN)) this->OnClick(pt, NGWW_JOIN);
		}
	}

	virtual void OnDropdownSelect(int widget, int index)
	{
		switch (widget) {
			case NGWW_CONN_BTN:
				_settings_client.network.lan_internet = index;
				break;

			default:
				NOT_REACHED();
		}

		this->SetDirty();
	}

	virtual void OnMouseLoop()
	{
		if (this->field == NGWW_CLIENT) this->HandleEditBox(NGWW_CLIENT);
	}

	virtual void OnInvalidateData(int data)
	{
		switch (data) {
			/* Remove the selection */
			case 1:
				this->server = NULL;
				this->list_pos = SLP_INVALID;
				break;

			/* Reiterate the whole server list as we downloaded some files */
			case 2:
				for (NetworkGameList **iter = this->servers.Begin(); iter != this->servers.End(); iter++) {
					NetworkGameList *item = *iter;
					bool missing_grfs = false;
					for (GRFConfig *c = item->info.grfconfig; c != NULL; c = c->next) {
						if (c->status != GCS_NOT_FOUND) continue;

						const GRFConfig *f = FindGRFConfig(c->grfid, c->md5sum);
						if (f == NULL) {
							missing_grfs = true;
							continue;
						}

						c->filename  = f->filename;
						c->name      = f->name;
						c->info      = f->info;
						c->status    = GCS_UNKNOWN;
					}

					if (!missing_grfs) item->info.compatible = item->info.version_compatible;
				}
				break;
		}
		this->servers.ForceRebuild();
		this->SetDirty();
	}

	virtual EventState OnKeyPress(uint16 key, uint16 keycode)
	{
		EventState state = ES_NOT_HANDLED;

		/* handle up, down, pageup, pagedown, home and end */
		if (keycode == WKC_UP || keycode == WKC_DOWN || keycode == WKC_PAGEUP || keycode == WKC_PAGEDOWN || keycode == WKC_HOME || keycode == WKC_END) {
			if (this->servers.Length() == 0) return ES_HANDLED;
			switch (keycode) {
				case WKC_UP:
					/* scroll up by one */
					if (this->server == NULL) return ES_HANDLED;
					if (this->list_pos > 0) this->list_pos--;
					break;
				case WKC_DOWN:
					/* scroll down by one */
					if (this->server == NULL) return ES_HANDLED;
					if (this->list_pos < this->servers.Length() - 1) this->list_pos++;
					break;
				case WKC_PAGEUP:
					/* scroll up a page */
					if (this->server == NULL) return ES_HANDLED;
					this->list_pos = (this->list_pos < this->vscroll.cap) ? 0 : this->list_pos - this->vscroll.cap;
					break;
				case WKC_PAGEDOWN:
					/* scroll down a page */
					if (this->server == NULL) return ES_HANDLED;
					this->list_pos = min(this->list_pos + this->vscroll.cap, (int)this->servers.Length() - 1);
					break;
				case WKC_HOME:
					/* jump to beginning */
					this->list_pos = 0;
					break;
				case WKC_END:
					/* jump to end */
					this->list_pos = this->servers.Length() - 1;
					break;
				default: break;
			}

			this->server = this->servers[this->list_pos];

			/* scroll to the new server if it is outside the current range */
			this->ScrollToSelectedServer();

			/* redraw window */
			this->SetDirty();
			return ES_HANDLED;
		}

		if (this->field != NGWW_CLIENT) {
			if (this->server != NULL) {
				if (keycode == WKC_DELETE) { // Press 'delete' to remove servers
					NetworkGameListRemoveItem(this->server);
					this->server = NULL;
					this->list_pos = SLP_INVALID;
				}
			}
			return state;
		}

		if (this->HandleEditBoxKey(NGWW_CLIENT, key, keycode, state) == HEBR_CONFIRM) return state;

		/* The name is only allowed when it starts with a letter! */
		if (!StrEmpty(this->edit_str_buf) && this->edit_str_buf[0] != ' ') {
			strecpy(_settings_client.network.client_name, this->edit_str_buf, lastof(_settings_client.network.client_name));
		} else {
			strecpy(_settings_client.network.client_name, "Player", lastof(_settings_client.network.client_name));
		}
		return state;
	}

	virtual void OnQueryTextFinished(char *str)
	{
		if (!StrEmpty(str)) NetworkAddServer(str);
	}

	virtual void OnResize(Point delta)
	{
		this->vscroll.cap += delta.y / (int)this->resize.step_height;

		this->widget[NGWW_MATRIX].data = (this->vscroll.cap << MAT_ROW_START) + (1 << MAT_COL_START);

		SetVScrollCount(this, this->servers.Length());

		/* Additional colums in server list */
		if (this->width > NetworkGameWindow::MIN_EXTRA_COLUMNS_WIDTH + GetWidgetWidth(NGWW_MAPSIZE) +
				GetWidgetWidth(NGWW_DATE) + GetWidgetWidth(NGWW_YEARS)) {
			/* show columns 'Map size', 'Date' and 'Years' */
			this->SetWidgetsHiddenState(false, NGWW_MAPSIZE, NGWW_DATE, NGWW_YEARS, WIDGET_LIST_END);
			AlignWidgetRight(NGWW_YEARS,   NGWW_INFO);
			AlignWidgetRight(NGWW_DATE,    NGWW_YEARS);
			AlignWidgetRight(NGWW_MAPSIZE, NGWW_DATE);
			AlignWidgetRight(NGWW_CLIENTS, NGWW_MAPSIZE);
		} else if (this->width > NetworkGameWindow::MIN_EXTRA_COLUMNS_WIDTH + GetWidgetWidth(NGWW_MAPSIZE) + GetWidgetWidth(NGWW_DATE)) {
			/* show columns 'Map size' and 'Date' */
			this->SetWidgetsHiddenState(false, NGWW_MAPSIZE, NGWW_DATE, WIDGET_LIST_END);
			this->HideWidget(NGWW_YEARS);
			AlignWidgetRight(NGWW_DATE,    NGWW_INFO);
			AlignWidgetRight(NGWW_MAPSIZE, NGWW_DATE);
			AlignWidgetRight(NGWW_CLIENTS, NGWW_MAPSIZE);
		} else if (this->width > NetworkGameWindow::MIN_EXTRA_COLUMNS_WIDTH + GetWidgetWidth(NGWW_MAPSIZE)) {
			/* show column 'Map size' */
			this->ShowWidget(NGWW_MAPSIZE);
			this->SetWidgetsHiddenState(true, NGWW_DATE, NGWW_YEARS, WIDGET_LIST_END);
			AlignWidgetRight(NGWW_MAPSIZE, NGWW_INFO);
			AlignWidgetRight(NGWW_CLIENTS, NGWW_MAPSIZE);
		} else {
			/* hide columns 'Map size', 'Date' and 'Years' */
			this->SetWidgetsHiddenState(true, NGWW_MAPSIZE, NGWW_DATE, NGWW_YEARS, WIDGET_LIST_END);
			AlignWidgetRight(NGWW_CLIENTS, NGWW_INFO);
		}
		this->widget[NGWW_NAME].right = this->widget[NGWW_CLIENTS].left - 1;

		/* BOTTOM */
		int widget_width = this->widget[NGWW_FIND].right - this->widget[NGWW_FIND].left;
		int space = (this->width - 4 * widget_width - 25) / 3;

		int offset = 10;
		for (uint i = 0; i < 4; i++) {
			this->widget[NGWW_FIND + i].left  = offset;
			offset += widget_width;
			this->widget[NGWW_FIND + i].right = offset;
			offset += space;
		}
	}

	static const int MIN_EXTRA_COLUMNS_WIDTH = 550;   ///< default width of the window
};

Listing NetworkGameWindow::last_sorting = {false, 5};
GUIGameServerList::SortFunction * const NetworkGameWindow::sorter_funcs[] = {
	&NGameNameSorter,
	&NGameClientSorter,
	&NGameMapSizeSorter,
	&NGameDateSorter,
	&NGameYearsSorter,
	&NGameAllowedSorter
};


static const Widget _network_game_window_widgets[] = {
/* TOP */
{   WWT_CLOSEBOX,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,     0,    10,     0,    13, STR_BLACK_CROSS,                  STR_TOOLTIP_CLOSE_WINDOW},         // NGWW_CLOSE
{    WWT_CAPTION,   RESIZE_RIGHT,  COLOUR_LIGHT_BLUE,    11,   449,     0,    13, STR_NETWORK_SERVER_LIST_CAPTION,          STR_NULL},                         // NGWW_CAPTION
{      WWT_PANEL,   RESIZE_RB,     COLOUR_LIGHT_BLUE,     0,   449,    14,   263, 0x0,                              STR_NULL},                         // NGWW_MAIN

{       WWT_TEXT,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,     9,    85,    23,    35, STR_NETWORK_SERVER_LIST_CONNECTION,           STR_NULL},                         // NGWW_CONNECTION
{   WWT_DROPDOWN,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,    90,   181,    22,    33, STR_NETWORK_START_SERVER_LAN_INTERNET_COMBO,   STR_NETWORK_SERVER_LIST_CONNECTION_TOOLTIP},       // NGWW_CONN_BTN

{    WWT_EDITBOX,   RESIZE_LR,     COLOUR_LIGHT_BLUE,   290,   440,    22,    33, STR_NETWORK_SERVER_LIST_PLAYER_NAME_OSKTITLE, STR_NETWORK_SERVER_LIST_ENTER_NAME_TOOLTIP},       // NGWW_CLIENT

/* LEFT SIDE */
{ WWT_PUSHTXTBTN,   RESIZE_NONE,   COLOUR_WHITE,         10,    70,    42,    53, STR_NETWORK_SERVER_LIST_GAME_NAME,            STR_NETWORK_SERVER_LIST_GAME_NAME_TOOLTIP},        // NGWW_NAME
{ WWT_PUSHTXTBTN,   RESIZE_NONE,   COLOUR_WHITE,         71,   150,    42,    53, STR_NETWORK_SERVER_LIST_CLIENTS_CAPTION,      STR_NETWORK_SERVER_LIST_CLIENTS_CAPTION_TOOLTIP},  // NGWW_CLIENTS
{ WWT_PUSHTXTBTN,   RESIZE_NONE,   COLOUR_WHITE,         71,   150,    42,    53, STR_NETWORK_SERVER_LIST_MAP_SIZE_CAPTION,     STR_NETWORK_SERVER_LIST_MAP_SIZE_CAPTION_TOOLTIP}, // NGWW_MAPSIZE
{ WWT_PUSHTXTBTN,   RESIZE_NONE,   COLOUR_WHITE,         71,   130,    42,    53, STR_NETWORK_SERVER_LIST_DATE_CAPTION,         STR_NETWORK_SERVER_LIST_DATE_CAPTION_TOOLTIP},     // NGWW_DATE
{ WWT_PUSHTXTBTN,   RESIZE_NONE,   COLOUR_WHITE,         71,   130,    42,    53, STR_NETWORK_SERVER_LIST_YEARS_CAPTION,        STR_NETWORK_SERVER_LIST_YEARS_CAPTION_TOOLTIP},    // NGWW_YEARS
{ WWT_PUSHTXTBTN,   RESIZE_LR,     COLOUR_WHITE,        151,   190,    42,    53, STR_EMPTY,                        STR_NETWORK_SERVER_LIST_INFO_ICONS_TOOLTIP},       // NGWW_INFO

{     WWT_MATRIX,   RESIZE_RB,     COLOUR_LIGHT_BLUE,    10,   190,    54,   208, (11 << 8) + 1,                    STR_NETWORK_SERVER_LIST_CLICK_GAME_TO_SELECT}, // NGWW_MATRIX
{  WWT_SCROLLBAR,   RESIZE_LRB,    COLOUR_LIGHT_BLUE,   191,   202,    42,   208, 0x0,                              STR_TOOLTIP_VSCROLL_BAR_SCROLLS_LIST}, // NGWW_SCROLLBAR
{       WWT_TEXT,   RESIZE_RTB,    COLOUR_LIGHT_BLUE,    10,   190,   211,   222, STR_NETWORK_SERVER_LIST_LAST_JOINED_SERVER,   STR_NULL},                         // NGWW_LASTJOINED_LABEL
{      WWT_PANEL,   RESIZE_RTB,    COLOUR_LIGHT_BLUE,    10,   190,   223,   236, 0x0,                              STR_NETWORK_SERVER_LIST_CLICK_TO_SELECT_LAST}, // NGWW_LASTJOINED

/* RIGHT SIDE */
{      WWT_PANEL,   RESIZE_LRB,    COLOUR_LIGHT_BLUE,   210,   440,    42,   236, 0x0,                              STR_NULL},                         // NGWW_DETAILS

{ WWT_PUSHTXTBTN,   RESIZE_LRTB,   COLOUR_WHITE,        215,   315,   215,   226, STR_NETWORK_SERVER_LIST_JOIN_GAME,            STR_NULL},                         // NGWW_JOIN
{ WWT_PUSHTXTBTN,   RESIZE_LRTB,   COLOUR_WHITE,        330,   435,   215,   226, STR_NETWORK_SERVER_LIST_REFRESH,              STR_NETWORK_SERVER_LIST_REFRESH_TOOLTIP},          // NGWW_REFRESH

{ WWT_PUSHTXTBTN,   RESIZE_LRTB,   COLOUR_WHITE,        330,   435,   197,   208, STR_INTRO_NEWGRF_SETTINGS,        STR_NULL},                         // NGWW_NEWGRF

/* BOTTOM */
{ WWT_PUSHTXTBTN,   RESIZE_TB,     COLOUR_WHITE,         10,   110,   246,   257, STR_NETWORK_SERVER_LIST_FIND_SERVER,          STR_NETWORK_SERVER_LIST_FIND_SERVER_TOOLTIP},      // NGWW_FIND
{ WWT_PUSHTXTBTN,   RESIZE_TB,     COLOUR_WHITE,        118,   218,   246,   257, STR_NETWORK_SERVER_LIST_ADD_SERVER,           STR_NETWORK_SERVER_LIST_ADD_SERVER_TOOLTIP},       // NGWW_ADD
{ WWT_PUSHTXTBTN,   RESIZE_TB,     COLOUR_WHITE,        226,   326,   246,   257, STR_NETWORK_SERVER_LIST_START_SERVER,         STR_NETWORK_SERVER_LIST_START_SERVER_TOOLTIP},     // NGWW_START
{ WWT_PUSHTXTBTN,   RESIZE_TB,     COLOUR_WHITE,        334,   434,   246,   257, STR_BUTTON_CANCEL,                 STR_NULL},                         // NGWW_CANCEL

{  WWT_RESIZEBOX,   RESIZE_LRTB,   COLOUR_LIGHT_BLUE,   438,   449,   252,   263, 0x0,                              STR_TOOLTIP_RESIZE },               // NGWW_RESIZE

{   WIDGETS_END},
};

/* Generates incorrect display_flags for widgets NGWW_NAME, and incorrect
 * display_flags and/or left/right side for the overlapping widgets
 * NGWW_CLIENTS through NGWW_YEARS.
 */
NWidgetPart _nested_network_game_widgets[] = {
	/* TOP */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_LIGHT_BLUE, NGWW_CLOSE),
		NWidget(WWT_CAPTION, COLOUR_LIGHT_BLUE, NGWW_CAPTION), SetMinimalSize(439, 14), SetDataTip(STR_NETWORK_SERVER_LIST_CAPTION, STR_NULL), // XXX Add default caption tooltip!
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_LIGHT_BLUE, NGWW_MAIN),
		NWidget(NWID_SPACER), SetMinimalSize(0, 8), SetResize(1, 0),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetMinimalSize(9, 0),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_SPACER), SetMinimalSize(0,1), // Text is one pixel further down
				NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, NGWW_CONNECTION), SetMinimalSize(77, 13), SetDataTip(STR_NETWORK_SERVER_LIST_CONNECTION, STR_NULL),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(4, 0),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_DROPDOWN, COLOUR_LIGHT_BLUE, NGWW_CONN_BTN), SetMinimalSize(92, 12),
									SetDataTip(STR_NETWORK_START_SERVER_LAN_INTERNET_COMBO, STR_NETWORK_SERVER_LIST_CONNECTION_TOOLTIP),
				NWidget(NWID_SPACER), SetMinimalSize(0,2), // Text ends two pixels further down
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(108, 0), SetFill(1,0), SetResize(1,0),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_EDITBOX, COLOUR_LIGHT_BLUE, NGWW_CLIENT), SetMinimalSize(151, 12),
									SetDataTip(STR_NETWORK_SERVER_LIST_PLAYER_NAME_OSKTITLE, STR_NETWORK_SERVER_LIST_ENTER_NAME_TOOLTIP),
				NWidget(NWID_SPACER), SetMinimalSize(0,2), // Text ends two pixels further down
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(9, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 6), SetResize(1, 0),
		NWidget(NWID_HORIZONTAL),
			/* LEFT SIDE */
			NWidget(NWID_SPACER), SetMinimalSize(10, 0), SetResize(0, 1),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NGWW_NAME), SetMinimalSize(61, 12), SetResize(1, 0),
									SetDataTip(STR_NETWORK_SERVER_LIST_GAME_NAME, STR_NETWORK_SERVER_LIST_GAME_NAME_TOOLTIP),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NGWW_CLIENTS), SetMinimalSize(20, 12),
									SetDataTip(STR_NETWORK_SERVER_LIST_CLIENTS_CAPTION, STR_NETWORK_SERVER_LIST_CLIENTS_CAPTION_TOOLTIP),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NGWW_MAPSIZE), SetMinimalSize(20, 12),
									SetDataTip(STR_NETWORK_SERVER_LIST_MAP_SIZE_CAPTION, STR_NETWORK_SERVER_LIST_MAP_SIZE_CAPTION_TOOLTIP),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NGWW_DATE), SetMinimalSize(20, 12), SetDataTip(STR_NETWORK_SERVER_LIST_DATE_CAPTION, STR_NETWORK_SERVER_LIST_DATE_CAPTION_TOOLTIP),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NGWW_YEARS), SetMinimalSize(20, 12), SetDataTip(STR_NETWORK_SERVER_LIST_YEARS_CAPTION, STR_NETWORK_SERVER_LIST_YEARS_CAPTION_TOOLTIP),
					NWidget(NWID_SPACER), SetMinimalSize(0, 0), SetFill(0, 0), SetResize(1, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NGWW_INFO), SetMinimalSize(40, 12), SetDataTip(STR_EMPTY, STR_NETWORK_SERVER_LIST_INFO_ICONS_TOOLTIP),
				EndContainer(),
				NWidget(WWT_MATRIX, COLOUR_LIGHT_BLUE, NGWW_MATRIX), SetMinimalSize(181, 155), SetResize(1,1),
									SetDataTip((11 << 8) + 1, STR_NETWORK_SERVER_LIST_CLICK_GAME_TO_SELECT),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2), SetResize(1, 0),
				NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, NGWW_LASTJOINED_LABEL), SetMinimalSize(181, 12), SetFill(1,0),
									SetDataTip(STR_NETWORK_SERVER_LIST_LAST_JOINED_SERVER, STR_NULL), SetResize(1, 0),
				NWidget(WWT_PANEL, COLOUR_LIGHT_BLUE, NGWW_LASTJOINED), SetMinimalSize(181, 14), SetFill(1,0), SetResize(1, 0),
									SetDataTip(0x0, STR_NETWORK_SERVER_LIST_CLICK_TO_SELECT_LAST),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_SCROLLBAR, COLOUR_LIGHT_BLUE, NGWW_SCROLLBAR), SetMinimalSize(12, 165),
				NWidget(NWID_SPACER), SetMinimalSize(0,28),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(7, 0), SetResize(0, 1),
			/* RIGHT SIDE */
			NWidget(WWT_PANEL, COLOUR_LIGHT_BLUE, NGWW_DETAILS),
				NWidget(NWID_SPACER), SetMinimalSize(0, 155), SetResize(0, 1),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SPACER), SetMinimalSize(120, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NGWW_NEWGRF), SetMinimalSize(106, 12), SetDataTip(STR_INTRO_NEWGRF_SETTINGS, STR_NULL),
					NWidget(NWID_SPACER), SetMinimalSize(5, 0),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 6),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SPACER), SetMinimalSize(5, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NGWW_JOIN), SetMinimalSize(101, 12), SetDataTip(STR_NETWORK_SERVER_LIST_JOIN_GAME, STR_NULL),
					NWidget(NWID_SPACER), SetMinimalSize(14, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NGWW_REFRESH), SetMinimalSize(106, 12), SetDataTip(STR_NETWORK_SERVER_LIST_REFRESH, STR_NETWORK_SERVER_LIST_REFRESH_TOOLTIP),
					NWidget(NWID_SPACER), SetMinimalSize(5, 0),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 10),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(9, 0), SetResize(0, 1),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 9), SetResize(1, 0),
		/* BOTTOM */
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetMinimalSize(10, 0),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NGWW_FIND), SetMinimalSize(101, 12), SetDataTip(STR_NETWORK_SERVER_LIST_FIND_SERVER, STR_NETWORK_SERVER_LIST_FIND_SERVER_TOOLTIP),
					NWidget(NWID_SPACER), SetMinimalSize(7, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NGWW_ADD), SetMinimalSize(101, 12), SetDataTip(STR_NETWORK_SERVER_LIST_ADD_SERVER, STR_NETWORK_SERVER_LIST_ADD_SERVER_TOOLTIP),
					NWidget(NWID_SPACER), SetMinimalSize(7, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NGWW_START), SetMinimalSize(101, 12), SetDataTip(STR_NETWORK_SERVER_LIST_START_SERVER, STR_NETWORK_SERVER_LIST_START_SERVER_TOOLTIP),
					NWidget(NWID_SPACER), SetMinimalSize(7, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NGWW_CANCEL), SetMinimalSize(101, 12), SetDataTip(STR_BUTTON_CANCEL, STR_NULL),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0,6),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(3, 0), SetResize(1, 0),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_SPACER), SetMinimalSize(0, 6),
				NWidget(WWT_RESIZEBOX, COLOUR_LIGHT_BLUE, NGWW_RESIZE),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static const WindowDesc _network_game_window_desc(
	WDP_CENTER, WDP_CENTER, 450, 264, 780, 264,
	WC_NETWORK_WINDOW, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_DEF_WIDGET | WDF_STD_BTN | WDF_UNCLICK_BUTTONS | WDF_RESIZABLE,
	_network_game_window_widgets, _nested_network_game_widgets, lengthof(_nested_network_game_widgets)
);

void ShowNetworkGameWindow()
{
	static bool first = true;
	DeleteWindowById(WC_NETWORK_WINDOW, 0);

	/* Only show once */
	if (first) {
		first = false;
		/* add all servers from the config file to our list */
		for (char **iter = _network_host_list.Begin(); iter != _network_host_list.End(); iter++) {
			NetworkAddServer(*iter);
  }
	}

	new NetworkGameWindow(&_network_game_window_desc);
}

enum {
	NSSWND_START = 64,
	NSSWND_ROWSIZE = 12
};

/** Enum for NetworkStartServerWindow, referring to _network_start_server_window_widgets */
enum NetworkStartServerWidgets {
	NSSW_CLOSE,             ///< Close 'X' button
	NSSW_CAPTION,
	NSSW_BACKGROUND,
	NSSW_GAMENAME_LABEL,
	NSSW_GAMENAME,          ///< Background for editbox to set game name
	NSSW_SETPWD,            ///< 'Set password' button
	NSSW_SELECT_MAP_LABEL,
	NSSW_SELMAP,            ///< 'Select map' list
	NSSW_SCROLLBAR,
	NSSW_CONNTYPE_LABEL,
	NSSW_CONNTYPE_BTN,      ///< 'Connection type' droplist button
	NSSW_CLIENTS_LABEL,
	NSSW_CLIENTS_BTND,      ///< 'Max clients' downarrow
	NSSW_CLIENTS_TXT,       ///< 'Max clients' text
	NSSW_CLIENTS_BTNU,      ///< 'Max clients' uparrow
	NSSW_COMPANIES_LABEL,
	NSSW_COMPANIES_BTND,    ///< 'Max companies' downarrow
	NSSW_COMPANIES_TXT,     ///< 'Max companies' text
	NSSW_COMPANIES_BTNU,    ///< 'Max companies' uparrow
	NSSW_SPECTATORS_LABEL,
	NSSW_SPECTATORS_BTND,   ///< 'Max spectators' downarrow
	NSSW_SPECTATORS_TXT,    ///< 'Max spectators' text
	NSSW_SPECTATORS_BTNU,   ///< 'Max spectators' uparrow

	NSSW_LANGUAGE_LABEL,
	NSSW_LANGUAGE_BTN,      ///< 'Language spoken' droplist button
	NSSW_START,             ///< 'Start' button
	NSSW_LOAD,              ///< 'Load' button
	NSSW_CANCEL,            ///< 'Cancel' button
};

struct NetworkStartServerWindow : public QueryStringBaseWindow {
	byte field;                  ///< Selected text-field
	FiosItem *map;               ///< Selected map
	byte widget_id;              ///< The widget that has the pop-up input menu

	NetworkStartServerWindow(const WindowDesc *desc) : QueryStringBaseWindow(NETWORK_NAME_LENGTH, desc)
	{
		ttd_strlcpy(this->edit_str_buf, _settings_client.network.server_name, this->edit_str_size);

		_saveload_mode = SLD_NEW_GAME;
		BuildFileList();
		this->vscroll.cap = 12;
		this->vscroll.count = _fios_items.Length() + 1;

		this->afilter = CS_ALPHANUMERAL;
		InitializeTextBuffer(&this->text, this->edit_str_buf, this->edit_str_size, 160);
		this->SetFocusedWidget(NSSW_GAMENAME);

		this->field = NSSW_GAMENAME;

		this->FindWindowPlacementAndResize(desc);
	}

	virtual void OnPaint()
	{
		int y = NSSWND_START;
		const FiosItem *item;

		/* draw basic widgets */
		SetDParam(1, _connection_types_dropdown[_settings_client.network.server_advertise]);
		SetDParam(2, _settings_client.network.max_clients);
		SetDParam(3, _settings_client.network.max_companies);
		SetDParam(4, _settings_client.network.max_spectators);
		SetDParam(5, STR_NETWORK_LANG_ANY + _settings_client.network.server_lang);
		this->DrawWidgets();

		/* editbox to set game name */
		this->DrawEditBox(NSSW_GAMENAME);

		/* if password is set, draw red '*' next to 'Set password' button */
		if (!StrEmpty(_settings_client.network.server_password)) DrawString(408, this->width - 2, 23, "*", TC_RED);

		/* draw list of maps */
		GfxFillRect(this->widget[NSSW_SELMAP].left + 1, this->widget[NSSW_SELMAP].top + 1, this->widget[NSSW_SELMAP].right - 1, this->widget[NSSW_SELMAP].bottom - 1, 0xD7);  // black background of maps list

		for (uint pos = this->vscroll.pos; pos < _fios_items.Length() + 1; pos++) {
			item = _fios_items.Get(pos - 1);
			if (item == this->map || (pos == 0 && this->map == NULL)) {
				GfxFillRect(this->widget[NSSW_SELMAP].left + 1, y - 1, this->widget[NSSW_SELMAP].right - 1, y + 10, 155); // show highlighted item with a different colour
			}

			if (pos == 0) {
				DrawString(this->widget[NSSW_SELMAP].left + 4, this->widget[NSSW_SELMAP].right - 4, y, STR_NETWORK_START_SERVER_SERVER_RANDOM_GAME, TC_DARK_GREEN);
			} else {
				DrawString(this->widget[NSSW_SELMAP].left + 4, this->widget[NSSW_SELMAP].right - 4, y, item->title, _fios_colours[item->type] );
			}
			y += NSSWND_ROWSIZE;

			if (y >= this->vscroll.cap * NSSWND_ROWSIZE + NSSWND_START) break;
		}
	}

	virtual void OnClick(Point pt, int widget)
	{
		this->field = widget;
		switch (widget) {
			case NSSW_CLOSE:  // Close 'X'
			case NSSW_CANCEL: // Cancel button
				ShowNetworkGameWindow();
				break;

			case NSSW_SETPWD: // Set password button
				this->widget_id = NSSW_SETPWD;
				SetDParamStr(0, _settings_client.network.server_password);
				ShowQueryString(STR_JUST_RAW_STRING, STR_NETWORK_START_SERVER_SET_PASSWORD, 20, 250, this, CS_ALPHANUMERAL, QSF_NONE);
				break;

			case NSSW_SELMAP: { // Select map
				int y = (pt.y - NSSWND_START) / NSSWND_ROWSIZE;

				y += this->vscroll.pos;
				if (y >= this->vscroll.count) return;

				this->map = (y == 0) ? NULL : _fios_items.Get(y - 1);
				this->SetDirty();
			} break;

			case NSSW_CONNTYPE_BTN: // Connection type
				ShowDropDownMenu(this, _connection_types_dropdown, _settings_client.network.server_advertise, NSSW_CONNTYPE_BTN, 0, 0); // do it for widget NSSW_CONNTYPE_BTN
				break;

			case NSSW_CLIENTS_BTND:    case NSSW_CLIENTS_BTNU:    // Click on up/down button for number of clients
			case NSSW_COMPANIES_BTND:  case NSSW_COMPANIES_BTNU:  // Click on up/down button for number of companies
			case NSSW_SPECTATORS_BTND: case NSSW_SPECTATORS_BTNU: // Click on up/down button for number of spectators
				/* Don't allow too fast scrolling */
				if ((this->flags4 & WF_TIMEOUT_MASK) <= WF_TIMEOUT_TRIGGER) {
					this->HandleButtonClick(widget);
					this->SetDirty();
					switch (widget) {
						default: NOT_REACHED();
						case NSSW_CLIENTS_BTND: case NSSW_CLIENTS_BTNU:
							_settings_client.network.max_clients    = Clamp(_settings_client.network.max_clients    + widget - NSSW_CLIENTS_TXT,    2, MAX_CLIENTS);
							break;
						case NSSW_COMPANIES_BTND: case NSSW_COMPANIES_BTNU:
							_settings_client.network.max_companies  = Clamp(_settings_client.network.max_companies  + widget - NSSW_COMPANIES_TXT,  1, MAX_COMPANIES);
							break;
						case NSSW_SPECTATORS_BTND: case NSSW_SPECTATORS_BTNU:
							_settings_client.network.max_spectators = Clamp(_settings_client.network.max_spectators + widget - NSSW_SPECTATORS_TXT, 0, MAX_CLIENTS);
							break;
					}
				}
				_left_button_clicked = false;
				break;

			case NSSW_CLIENTS_TXT:    // Click on number of clients
				this->widget_id = NSSW_CLIENTS_TXT;
				SetDParam(0, _settings_client.network.max_clients);
				ShowQueryString(STR_JUST_INT, STR_NETWORK_START_SERVER_NUMBER_OF_CLIENTS,    4, 50, this, CS_NUMERAL, QSF_NONE);
				break;

			case NSSW_COMPANIES_TXT:  // Click on number of companies
				this->widget_id = NSSW_COMPANIES_TXT;
				SetDParam(0, _settings_client.network.max_companies);
				ShowQueryString(STR_JUST_INT, STR_NETWORK_START_SERVER_NUMBER_OF_COMPANIES,  3, 50, this, CS_NUMERAL, QSF_NONE);
				break;

			case NSSW_SPECTATORS_TXT: // Click on number of spectators
				this->widget_id = NSSW_SPECTATORS_TXT;
				SetDParam(0, _settings_client.network.max_spectators);
				ShowQueryString(STR_JUST_INT, STR_NETWORK_START_SERVER_NUMBER_OF_SPECTATORS, 4, 50, this, CS_NUMERAL, QSF_NONE);
				break;

			case NSSW_LANGUAGE_BTN: { // Language
				uint sel = 0;
				for (uint i = 0; i < lengthof(_language_dropdown) - 1; i++) {
					if (_language_dropdown[i] == STR_NETWORK_LANG_ANY + _settings_client.network.server_lang) {
						sel = i;
						break;
					}
				}
				ShowDropDownMenu(this, _language_dropdown, sel, NSSW_LANGUAGE_BTN, 0, 0);
			} break;

			case NSSW_START: // Start game
				_is_network_server = true;

				if (this->map == NULL) { // start random new game
					ShowGenerateLandscape();
				} else { // load a scenario
					const char *name = FiosBrowseTo(this->map);
					if (name != NULL) {
						SetFiosType(this->map->type);
						_file_to_saveload.filetype = FT_SCENARIO;
						strecpy(_file_to_saveload.name, name, lastof(_file_to_saveload.name));
						strecpy(_file_to_saveload.title, this->map->title, lastof(_file_to_saveload.title));

						delete this;
						SwitchToMode(SM_START_SCENARIO);
					}
				}
				break;

			case NSSW_LOAD: // Load game
				_is_network_server = true;
				/* XXX - WC_NETWORK_WINDOW (this window) should stay, but if it stays, it gets
				 * copied all the elements of 'load game' and upon closing that, it segfaults */
				delete this;
				ShowSaveLoadDialog(SLD_LOAD_GAME);
				break;
		}
	}

	virtual void OnDropdownSelect(int widget, int index)
	{
		switch (widget) {
			case NSSW_CONNTYPE_BTN:
				_settings_client.network.server_advertise = (index != 0);
				break;
			case NSSW_LANGUAGE_BTN:
				_settings_client.network.server_lang = _language_dropdown[index] - STR_NETWORK_LANG_ANY;
				break;
			default:
				NOT_REACHED();
		}

		this->SetDirty();
	}

	virtual void OnMouseLoop()
	{
		if (this->field == NSSW_GAMENAME) this->HandleEditBox(NSSW_GAMENAME);
	}

	virtual EventState OnKeyPress(uint16 key, uint16 keycode)
	{
		EventState state = ES_NOT_HANDLED;
		if (this->field == NSSW_GAMENAME) {
			if (this->HandleEditBoxKey(NSSW_GAMENAME, key, keycode, state) == HEBR_CONFIRM) return state;

			strecpy(_settings_client.network.server_name, this->text.buf, lastof(_settings_client.network.server_name));
		}

		return state;
	}

	virtual void OnQueryTextFinished(char *str)
	{
		if (str == NULL) return;

		if (this->widget_id == NSSW_SETPWD) {
			strecpy(_settings_client.network.server_password, str, lastof(_settings_client.network.server_password));
		} else {
			int32 value = atoi(str);
			this->InvalidateWidget(this->widget_id);
			switch (this->widget_id) {
				default: NOT_REACHED();
				case NSSW_CLIENTS_TXT:    _settings_client.network.max_clients    = Clamp(value, 2, MAX_CLIENTS); break;
				case NSSW_COMPANIES_TXT:  _settings_client.network.max_companies  = Clamp(value, 1, MAX_COMPANIES); break;
				case NSSW_SPECTATORS_TXT: _settings_client.network.max_spectators = Clamp(value, 0, MAX_CLIENTS); break;
			}
		}

		this->SetDirty();
	}
};

static const Widget _network_start_server_window_widgets[] = {
/* Window decoration and background panel */
{   WWT_CLOSEBOX,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,     0,    10,     0,    13, STR_BLACK_CROSS,                    STR_TOOLTIP_CLOSE_WINDOW },            // NSSW_CLOSE
{    WWT_CAPTION,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,    11,   419,     0,    13, STR_NETWORK_START_SERVER_CAPTION,      STR_NULL},                             // NSSW_CAPTION
{      WWT_PANEL,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,     0,   419,    14,   243, 0x0,                                STR_NULL},                             // NSSW_BACKGROUND

/* Set game name and password widgets */
{       WWT_TEXT,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,    10,    90,    22,    34, STR_NETWORK_START_SERVER_NEW_GAME_NAME,          STR_NULL},                             // NSSW_GAMENAME_LABEL
{    WWT_EDITBOX,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   100,   272,    22,    33, STR_NETWORK_START_SERVER_NEW_GAME_NAME_OSKTITLE, STR_NETWORK_START_SERVER_NEW_GAME_NAME_TOOLTIP},        // NSSW_GAMENAME
{ WWT_PUSHTXTBTN,   RESIZE_NONE,   COLOUR_WHITE,        285,   405,    22,    33, STR_NETWORK_START_SERVER_SET_PASSWORD,           STR_NETWORK_START_SERVER_PASSWORD_TOOLTIP},             // NSSW_SETPWD

/* List of playable scenarios */
{       WWT_TEXT,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,    10,   110,    43,    55, STR_NETWORK_START_SERVER_SELECT_MAP,             STR_NULL},                             // NSSW_SELECT_MAP_LABEL
{      WWT_INSET,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,    10,   271,    62,   216, STR_NULL,                           STR_NETWORK_START_SERVER_SELECT_MAP_TOOLTIP},           // NSSW_SELMAP
{  WWT_SCROLLBAR,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   259,   270,    63,   215, 0x0,                                STR_TOOLTIP_VSCROLL_BAR_SCROLLS_LIST}, // NSSW_SCROLLBAR

/* Combo/selection boxes to control Connection Type / Max Clients / Max Companies / Max Observers / Language */
{       WWT_TEXT,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   280,   410,    63,    75, STR_NETWORK_SERVER_LIST_CONNECTION,             STR_NULL},                             // NSSW_CONNTYPE_LABEL
{   WWT_DROPDOWN,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   280,   410,    77,    88, STR_NETWORK_START_SERVER_LAN_INTERNET_COMBO,     STR_NETWORK_SERVER_LIST_CONNECTION_TOOLTIP},           // NSSW_CONNTYPE_BTN

{       WWT_TEXT,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   280,   410,    95,   107, STR_NETWORK_START_SERVER_NUMBER_OF_CLIENTS,      STR_NULL},                             // NSSW_CLIENTS_LABEL
{     WWT_IMGBTN,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   280,   291,   109,   120, SPR_ARROW_DOWN,                     STR_NETWORK_START_SERVER_NUMBER_OF_CLIENTS_TOOLTIP},    // NSSW_CLIENTS_BTND
{ WWT_PUSHTXTBTN,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   292,   397,   109,   120, STR_NETWORK_START_SERVER_CLIENTS_SELECT,         STR_NETWORK_START_SERVER_NUMBER_OF_CLIENTS_TOOLTIP},    // NSSW_CLIENTS_TXT
{     WWT_IMGBTN,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   398,   410,   109,   120, SPR_ARROW_UP,                       STR_NETWORK_START_SERVER_NUMBER_OF_CLIENTS_TOOLTIP},    // NSSW_CLIENTS_BTNU

{       WWT_TEXT,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   280,   410,   127,   139, STR_NETWORK_START_SERVER_NUMBER_OF_COMPANIES,    STR_NULL},                             // NSSW_COMPANIES_LABEL
{     WWT_IMGBTN,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   280,   291,   141,   152, SPR_ARROW_DOWN,                     STR_NETWORK_START_SERVER_NUMBER_OF_COMPANIES_TOOLTIP},  // NSSW_COMPANIES_BTND
{ WWT_PUSHTXTBTN,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   292,   397,   141,   152, STR_NETWORK_START_SERVER_COMPANIES_SELECT,       STR_NETWORK_START_SERVER_NUMBER_OF_COMPANIES_TOOLTIP},  // NSSW_COMPANIES_TXT
{     WWT_IMGBTN,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   398,   410,   141,   152, SPR_ARROW_UP,                       STR_NETWORK_START_SERVER_NUMBER_OF_COMPANIES_TOOLTIP},  // NSSW_COMPANIES_BTNU

{       WWT_TEXT,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   280,   410,   159,   171, STR_NETWORK_START_SERVER_NUMBER_OF_SPECTATORS,   STR_NULL},                             // NSSW_SPECTATORS_LABEL
{     WWT_IMGBTN,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   280,   291,   173,   184, SPR_ARROW_DOWN,                     STR_NETWORK_START_SERVER_NUMBER_OF_SPECTATORS_TOOLTIP}, // NSSW_SPECTATORS_BTND
{ WWT_PUSHTXTBTN,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   292,   397,   173,   184, STR_NETWORK_START_SERVER_SPECTATORS_SELECT,      STR_NETWORK_START_SERVER_NUMBER_OF_SPECTATORS_TOOLTIP}, // NSSW_SPECTATORS_TXT
{     WWT_IMGBTN,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   398,   410,   173,   184, SPR_ARROW_UP,                       STR_NETWORK_START_SERVER_NUMBER_OF_SPECTATORS_TOOLTIP}, // NSSW_SPECTATORS_BTNU

{       WWT_TEXT,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   280,   410,   191,   203, STR_NETWORK_START_SERVER_LANGUAGE_SPOKEN,        STR_NULL},                             // NSSW_LANGUAGE_LABEL
{   WWT_DROPDOWN,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   280,   410,   205,   216, STR_NETWORK_START_SERVER_LANGUAGE_COMBO,         STR_NETWORK_START_SERVER_LANGUAGE_TOOLTIP},             // NSSW_LANGUAGE_BTN

/* Buttons Start / Load / Cancel */
{ WWT_PUSHTXTBTN,   RESIZE_NONE,   COLOUR_WHITE,         40,   140,   224,   235, STR_NETWORK_START_SERVER_START_GAME,             STR_NETWORK_START_SERVER_START_GAME_TOOLTIP},           // NSSW_START
{ WWT_PUSHTXTBTN,   RESIZE_NONE,   COLOUR_WHITE,        150,   250,   224,   235, STR_NETWORK_START_SERVER_LOAD_GAME,              STR_NETWORK_START_SERVER_LOAD_GAME_TOOLTIP},            // NSSW_LOAD
{ WWT_PUSHTXTBTN,   RESIZE_NONE,   COLOUR_WHITE,        260,   360,   224,   235, STR_BUTTON_CANCEL,                   STR_NULL},                             // NSSW_CANCEL

{   WIDGETS_END},
};

static const NWidgetPart _nested_network_start_server_window_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_LIGHT_BLUE, NSSW_CLOSE),
		NWidget(WWT_CAPTION, COLOUR_LIGHT_BLUE, NSSW_CAPTION), SetDataTip(STR_NETWORK_START_SERVER_CAPTION, STR_NULL),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_LIGHT_BLUE, NSSW_BACKGROUND),
		NWidget(NWID_SPACER), SetMinimalSize(0, 8),
		/* Set game name and password widgets. */
		NWidget(NWID_HORIZONTAL), SetPIP(10, 0, 14),
			NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, NSSW_GAMENAME_LABEL), SetMinimalSize(81, 13), SetDataTip(STR_NETWORK_START_SERVER_NEW_GAME_NAME, STR_NULL),
			NWidget(WWT_EDITBOX, COLOUR_LIGHT_BLUE, NSSW_GAMENAME), SetMinimalSize(173, 12), SetPadding(0, 0, 1, 9),
												SetDataTip(STR_NETWORK_START_SERVER_NEW_GAME_NAME_OSKTITLE, STR_NETWORK_START_SERVER_NEW_GAME_NAME_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NSSW_SETPWD), SetMinimalSize(121, 12), SetPadding(0, 0, 1, 12),
												SetDataTip(STR_NETWORK_START_SERVER_SET_PASSWORD, STR_NETWORK_START_SERVER_PASSWORD_TOOLTIP),
		EndContainer(),
		NWidget(NWID_HORIZONTAL), SetPIP(10, 8, 9),
			NWidget(NWID_VERTICAL),
				/* List of playable scenarios. */
				NWidget(NWID_SPACER), SetMinimalSize(0, 8),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, NSSW_SELECT_MAP_LABEL), SetMinimalSize(101, 13), SetDataTip(STR_NETWORK_START_SERVER_SELECT_MAP, STR_NULL),
					NWidget(NWID_SPACER), SetFill(true, false),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 6),
				NWidget(WWT_INSET, COLOUR_LIGHT_BLUE, NSSW_SELMAP), SetDataTip(STR_NULL, STR_NETWORK_START_SERVER_SELECT_MAP_TOOLTIP),
					NWidget(NWID_HORIZONTAL),
						NWidget(NWID_SPACER), SetMinimalSize(249, 155),
						NWidget(WWT_SCROLLBAR, COLOUR_LIGHT_BLUE, NSSW_SCROLLBAR), SetPadding(1, 1, 1, 0),
					EndContainer(),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_VERTICAL),
				/* Combo/selection boxes to control Connection Type / Max Clients / Max Companies / Max Observers / Language */
				NWidget(NWID_SPACER), SetMinimalSize(0, 28),
				NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, NSSW_CONNTYPE_LABEL), SetMinimalSize(131, 13), SetDataTip(STR_NETWORK_SERVER_LIST_CONNECTION, STR_NULL),
				NWidget(NWID_SPACER), SetMinimalSize(0, 1),
				NWidget(WWT_DROPDOWN, COLOUR_LIGHT_BLUE, NSSW_CONNTYPE_BTN), SetMinimalSize(131, 12),
												SetDataTip(STR_NETWORK_START_SERVER_LAN_INTERNET_COMBO, STR_NETWORK_SERVER_LIST_CONNECTION_TOOLTIP),

				NWidget(NWID_SPACER), SetMinimalSize(0, 6),
				NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, NSSW_CLIENTS_LABEL), SetMinimalSize(131, 13), SetDataTip(STR_NETWORK_START_SERVER_NUMBER_OF_CLIENTS, STR_NULL),
				NWidget(NWID_SPACER), SetMinimalSize(0, 1),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_IMGBTN, COLOUR_LIGHT_BLUE, NSSW_CLIENTS_BTND), SetMinimalSize(12, 12),
												SetDataTip(SPR_ARROW_DOWN, STR_NETWORK_START_SERVER_NUMBER_OF_CLIENTS_TOOLTIP),
					NWidget(WWT_PUSHTXTBTN, COLOUR_LIGHT_BLUE, NSSW_CLIENTS_TXT), SetMinimalSize(106, 12),
												SetDataTip(STR_NETWORK_START_SERVER_CLIENTS_SELECT, STR_NETWORK_START_SERVER_NUMBER_OF_CLIENTS_TOOLTIP),
					NWidget(WWT_IMGBTN, COLOUR_LIGHT_BLUE, NSSW_CLIENTS_BTNU), SetMinimalSize(13, 12),
												SetDataTip(SPR_ARROW_UP, STR_NETWORK_START_SERVER_NUMBER_OF_CLIENTS_TOOLTIP),
				EndContainer(),

				NWidget(NWID_SPACER), SetMinimalSize(0, 6),
				NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, NSSW_COMPANIES_LABEL), SetMinimalSize(131, 13), SetDataTip(STR_NETWORK_START_SERVER_NUMBER_OF_COMPANIES, STR_NULL),
				NWidget(NWID_SPACER), SetMinimalSize(0, 1),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_IMGBTN, COLOUR_LIGHT_BLUE, NSSW_COMPANIES_BTND), SetMinimalSize(12, 12),
												SetDataTip(SPR_ARROW_DOWN, STR_NETWORK_START_SERVER_NUMBER_OF_COMPANIES_TOOLTIP),
					NWidget(WWT_PUSHTXTBTN, COLOUR_LIGHT_BLUE, NSSW_COMPANIES_TXT), SetMinimalSize(106, 12),
												SetDataTip(STR_NETWORK_START_SERVER_COMPANIES_SELECT, STR_NETWORK_START_SERVER_NUMBER_OF_COMPANIES_TOOLTIP),
					NWidget(WWT_IMGBTN, COLOUR_LIGHT_BLUE, NSSW_COMPANIES_BTNU), SetMinimalSize(13, 12),
												SetDataTip(SPR_ARROW_UP, STR_NETWORK_START_SERVER_NUMBER_OF_COMPANIES_TOOLTIP),
				EndContainer(),

				NWidget(NWID_SPACER), SetMinimalSize(0, 6),
				NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, NSSW_SPECTATORS_LABEL), SetMinimalSize(131, 13), SetDataTip(STR_NETWORK_START_SERVER_NUMBER_OF_SPECTATORS, STR_NULL),
				NWidget(NWID_SPACER), SetMinimalSize(0, 1),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_IMGBTN, COLOUR_LIGHT_BLUE, NSSW_SPECTATORS_BTND), SetMinimalSize(12, 12),
												SetDataTip(SPR_ARROW_DOWN, STR_NETWORK_START_SERVER_NUMBER_OF_SPECTATORS_TOOLTIP),
					NWidget(WWT_PUSHTXTBTN, COLOUR_LIGHT_BLUE, NSSW_SPECTATORS_TXT), SetMinimalSize(106, 12),
												SetDataTip(STR_NETWORK_START_SERVER_SPECTATORS_SELECT, STR_NETWORK_START_SERVER_NUMBER_OF_SPECTATORS_TOOLTIP),
					NWidget(WWT_IMGBTN, COLOUR_LIGHT_BLUE, NSSW_SPECTATORS_BTNU), SetMinimalSize(13, 12),
												SetDataTip(SPR_ARROW_UP, STR_NETWORK_START_SERVER_NUMBER_OF_SPECTATORS_TOOLTIP),
				EndContainer(),

				NWidget(NWID_SPACER), SetMinimalSize(0, 6),
				NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, NSSW_LANGUAGE_LABEL), SetMinimalSize(131, 13), SetDataTip(STR_NETWORK_START_SERVER_LANGUAGE_SPOKEN, STR_NULL),
				NWidget(NWID_SPACER), SetMinimalSize(0, 1),
				NWidget(WWT_DROPDOWN, COLOUR_LIGHT_BLUE, NSSW_LANGUAGE_BTN), SetMinimalSize(131, 12),
												SetDataTip(STR_NETWORK_START_SERVER_LANGUAGE_COMBO, STR_NETWORK_START_SERVER_LANGUAGE_TOOLTIP),
			EndContainer(),
		EndContainer(),

		/* Buttons Start / Load / Cancel. */
		NWidget(NWID_SPACER), SetMinimalSize(0, 7),
		NWidget(NWID_HORIZONTAL), SetPIP(40, 9, 59),
			NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NSSW_START), SetMinimalSize(101, 12), SetDataTip(STR_NETWORK_START_SERVER_START_GAME, STR_NETWORK_START_SERVER_START_GAME_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NSSW_LOAD), SetMinimalSize(101, 12), SetDataTip(STR_NETWORK_START_SERVER_LOAD_GAME, STR_NETWORK_START_SERVER_LOAD_GAME_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NSSW_CANCEL), SetMinimalSize(101, 12), SetDataTip(STR_BUTTON_CANCEL, STR_NULL),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 8),
	EndContainer(),
};

static const WindowDesc _network_start_server_window_desc(
	WDP_CENTER, WDP_CENTER, 420, 244, 420, 244,
	WC_NETWORK_WINDOW, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS,
	_network_start_server_window_widgets, _nested_network_start_server_window_widgets, lengthof(_nested_network_start_server_window_widgets)
);

static void ShowNetworkStartServerWindow()
{
	DeleteWindowById(WC_NETWORK_WINDOW, 0);

	new NetworkStartServerWindow(&_network_start_server_window_desc);
}

/** Enum for NetworkLobbyWindow, referring to _network_lobby_window_widgets */
enum NetworkLobbyWindowWidgets {
	NLWW_CLOSE,      ///< Close 'X' button
	NLWW_CAPTION,    ///< Titlebar
	NLWW_BACKGROUND, ///< Background panel
	NLWW_TEXT,       ///< Heading text
	NLWW_HEADER,     ///< Header above list of companies
	NLWW_MATRIX,     ///< List of companies
	NLWW_SCROLLBAR,  ///< Scroll bar
	NLWW_DETAILS,    ///< Company details
	NLWW_JOIN,       ///< 'Join company' button
	NLWW_NEW,        ///< 'New company' button
	NLWW_SPECTATE,   ///< 'Spectate game' button
	NLWW_REFRESH,    ///< 'Refresh server' button
	NLWW_CANCEL,     ///< 'Cancel' button
};

struct NetworkLobbyWindow : public Window {
	CompanyID company;       ///< Select company
	NetworkGameList *server; ///< Selected server
	NetworkCompanyInfo company_info[MAX_COMPANIES];

	NetworkLobbyWindow(const WindowDesc *desc, NetworkGameList *ngl) :
			Window(desc), company(INVALID_COMPANY), server(ngl)
	{
		this->vscroll.cap = 10;

		this->FindWindowPlacementAndResize(desc);
	}

	CompanyID NetworkLobbyFindCompanyIndex(byte pos)
	{
		/* Scroll through all this->company_info and get the 'pos' item that is not empty */
		for (CompanyID i = COMPANY_FIRST; i < MAX_COMPANIES; i++) {
			if (!StrEmpty(this->company_info[i].company_name)) {
				if (pos-- == 0) return i;
			}
		}

		return COMPANY_FIRST;
	}

	virtual void OnPaint()
	{
		const NetworkGameInfo *gi = &this->server->info;
		int y = NET_PRC__OFFSET_TOP_WIDGET_COMPANY, pos;

		/* Join button is disabled when no company is selected and for AI companies*/
		this->SetWidgetDisabledState(NLWW_JOIN, this->company == INVALID_COMPANY || GetLobbyCompanyInfo(this->company)->ai);
		/* Cannot start new company if there are too many */
		this->SetWidgetDisabledState(NLWW_NEW, gi->companies_on >= gi->companies_max);
		/* Cannot spectate if there are too many spectators */
		this->SetWidgetDisabledState(NLWW_SPECTATE, gi->spectators_on >= gi->spectators_max);

		/* Draw window widgets */
		SetDParamStr(0, gi->server_name);
		this->DrawWidgets();

		SetVScrollCount(this, gi->companies_on);

		/* Draw company list */
		pos = this->vscroll.pos;
		while (pos < gi->companies_on) {
			byte company = NetworkLobbyFindCompanyIndex(pos);
			bool income = false;
			if (this->company == company) {
				GfxFillRect(11, y - 1, 154, y + 10, 10); // show highlighted item with a different colour
			}

			DrawString(13, 135, y, this->company_info[company].company_name, TC_BLACK);
			if (this->company_info[company].use_password != 0) DrawSprite(SPR_LOCK, PAL_NONE, 135, y);

			/* If the company's income was positive puts a green dot else a red dot */
			if (this->company_info[company].income >= 0) income = true;
			DrawSprite(SPR_BLOT, income ? PALETTE_TO_GREEN : PALETTE_TO_RED, 145, y);

			pos++;
			y += NET_PRC__SIZE_OF_ROW;
			if (pos >= this->vscroll.pos + this->vscroll.cap) break;
		}

		/* Draw info about selected company when it is selected in the left window */
		GfxFillRect(174, 39, 403, 75, 157);
		DrawString(this->widget[NLWW_DETAILS].left + 10, this->widget[NLWW_DETAILS].right - 10, 50, STR_NETWORK_GAME_LOBBY_COMPANY_INFO, TC_FROMSTRING, SA_CENTER);
		if (this->company != INVALID_COMPANY && !StrEmpty(this->company_info[this->company].company_name)) {
			const uint x = this->widget[NLWW_DETAILS].left + 10;
			y = 80;

			SetDParam(0, gi->clients_on);
			SetDParam(1, gi->clients_max);
			SetDParam(2, gi->companies_on);
			SetDParam(3, gi->companies_max);
			DrawString(x, this->widget[NLWW_DETAILS].right, y, STR_NETWORK_SERVER_LIST_CLIENTS);
			y += 10;

			SetDParamStr(0, this->company_info[this->company].company_name);
			DrawString(x, this->widget[NLWW_DETAILS].right, y, STR_NETWORK_GAME_LOBBY_COMPANY_NAME);
			y += 10;

			SetDParam(0, this->company_info[this->company].inaugurated_year);
			DrawString(x, this->widget[NLWW_DETAILS].right, y, STR_NETWORK_GAME_LOBBY_INAUGURATION_YEAR); // inauguration year
			y += 10;

			SetDParam(0, this->company_info[this->company].company_value);
			DrawString(x, this->widget[NLWW_DETAILS].right, y, STR_NETWORK_GAME_LOBBY_VALUE); // company value
			y += 10;

			SetDParam(0, this->company_info[this->company].money);
			DrawString(x, this->widget[NLWW_DETAILS].right, y, STR_NETWORK_GAME_LOBBY_CURRENT_BALANCE); // current balance
			y += 10;

			SetDParam(0, this->company_info[this->company].income);
			DrawString(x, this->widget[NLWW_DETAILS].right, y, STR_NETWORK_GAME_LOBBY_LAST_YEARS_INCOME); // last year's income
			y += 10;

			SetDParam(0, this->company_info[this->company].performance);
			DrawString(x, this->widget[NLWW_DETAILS].right, y, STR_NETWORK_GAME_LOBBY_PERFORMANCE); // performance
			y += 10;

			SetDParam(0, this->company_info[this->company].num_vehicle[0]);
			SetDParam(1, this->company_info[this->company].num_vehicle[1]);
			SetDParam(2, this->company_info[this->company].num_vehicle[2]);
			SetDParam(3, this->company_info[this->company].num_vehicle[3]);
			SetDParam(4, this->company_info[this->company].num_vehicle[4]);
			DrawString(x, this->widget[NLWW_DETAILS].right, y, STR_NETWORK_GAME_LOBBY_VEHICLES); // vehicles
			y += 10;

			SetDParam(0, this->company_info[this->company].num_station[0]);
			SetDParam(1, this->company_info[this->company].num_station[1]);
			SetDParam(2, this->company_info[this->company].num_station[2]);
			SetDParam(3, this->company_info[this->company].num_station[3]);
			SetDParam(4, this->company_info[this->company].num_station[4]);
			DrawString(x, this->widget[NLWW_DETAILS].right, y, STR_NETWORK_GAME_LOBBY_STATIONS); // stations
			y += 10;

			SetDParamStr(0, this->company_info[this->company].clients);
			DrawString(x, this->widget[NLWW_DETAILS].right, y, STR_NETWORK_GAME_LOBBY_PLAYERS); // players
		}
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case NLWW_CLOSE:    // Close 'X'
			case NLWW_CANCEL:   // Cancel button
				ShowNetworkGameWindow();
				break;

			case NLWW_MATRIX: { // Company list
				uint32 id_v = (pt.y - NET_PRC__OFFSET_TOP_WIDGET_COMPANY) / NET_PRC__SIZE_OF_ROW;

				if (id_v >= this->vscroll.cap) break;

				id_v += this->vscroll.pos;
				this->company = (id_v >= this->server->info.companies_on) ? INVALID_COMPANY : NetworkLobbyFindCompanyIndex(id_v);
				this->SetDirty();
			} break;

			case NLWW_JOIN:     // Join company
				/* Button can be clicked only when it is enabled */
				NetworkClientConnectGame(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port), this->company);
				break;

			case NLWW_NEW:      // New company
				NetworkClientConnectGame(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port), COMPANY_NEW_COMPANY);
				break;

			case NLWW_SPECTATE: // Spectate game
				NetworkClientConnectGame(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port), COMPANY_SPECTATOR);
				break;

			case NLWW_REFRESH:  // Refresh
				NetworkTCPQueryServer(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port)); // company info
				NetworkUDPQueryServer(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port)); // general data
				/* Clear the information so removed companies don't remain */
				memset(this->company_info, 0, sizeof(this->company_info));
				break;
		}
	}

	virtual void OnDoubleClick(Point pt, int widget)
	{
		if (widget == NLWW_MATRIX) {
			/* is the Join button enabled? */
			if (!this->IsWidgetDisabled(NLWW_JOIN)) this->OnClick(pt, NLWW_JOIN);
		}
	}
};

static const Widget _network_lobby_window_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,     0,    10,     0,    13, STR_BLACK_CROSS,             STR_TOOLTIP_CLOSE_WINDOW },        // NLWW_CLOSE
{    WWT_CAPTION,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,    11,   419,     0,    13, STR_NETWORK_GAME_LOBBY_CAPTION,      STR_NULL},                         // NLWW_CAPTION
{      WWT_PANEL,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,     0,   419,    14,   234, 0x0,                         STR_NULL},                         // NLWW_BACKGROUND
{       WWT_TEXT,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,    10,   419,    22,    34, STR_NETWORK_GAME_LOBBY_PREPARE_TO_JOIN, STR_NULL},                         // NLWW_TEXT

/* company list */
{      WWT_PANEL,   RESIZE_NONE,   COLOUR_WHITE,         10,   155,    38,    49, 0x0,                         STR_NULL},                         // NLWW_HEADER
{     WWT_MATRIX,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,    10,   155,    50,   190, (10 << 8) + 1,               STR_NETWORK_GAME_LOBBY_COMPANY_LIST_TOOLTIP},     // NLWW_MATRIX
{  WWT_SCROLLBAR,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   156,   167,    38,   190, 0x0,                         STR_TOOLTIP_VSCROLL_BAR_SCROLLS_LIST}, // NLWW_SCROLLBAR

/* company info */
{      WWT_PANEL,   RESIZE_NONE,   COLOUR_LIGHT_BLUE,   173,   404,    38,   190, 0x0,                         STR_NULL},                         // NLWW_DETAILS

/* buttons */
{ WWT_PUSHTXTBTN,   RESIZE_NONE,   COLOUR_WHITE,         10,   151,   200,   211, STR_NETWORK_GAME_LOBBY_JOIN_COMPANY,    STR_NETWORK_GAME_LOBBY_JOIN_COMPANY_TOOLTIP},     // NLWW_JOIN
{ WWT_PUSHTXTBTN,   RESIZE_NONE,   COLOUR_WHITE,         10,   151,   215,   226, STR_NETWORK_GAME_LOBBY_NEW_COMPANY,     STR_NETWORK_GAME_LOBBY_NEW_COMPANY_TOOLTIP},      // NLWW_NEW
{ WWT_PUSHTXTBTN,   RESIZE_NONE,   COLOUR_WHITE,        158,   268,   200,   211, STR_NETWORK_GAME_LOBBY_SPECTATE_GAME,   STR_NETWORK_GAME_LOBBY_SPECTATE_GAME_TOOLTIP},    // NLWW_SPECTATE
{ WWT_PUSHTXTBTN,   RESIZE_NONE,   COLOUR_WHITE,        158,   268,   215,   226, STR_NETWORK_SERVER_LIST_REFRESH,         STR_NETWORK_SERVER_LIST_REFRESH_TOOLTIP},          // NLWW_REFRESH
{ WWT_PUSHTXTBTN,   RESIZE_NONE,   COLOUR_WHITE,        278,   388,   200,   211, STR_BUTTON_CANCEL,            STR_NULL},                         // NLWW_CANCEL

{   WIDGETS_END},
};

static const NWidgetPart _nested_network_lobby_window_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_LIGHT_BLUE, NLWW_CLOSE),
		NWidget(WWT_CAPTION, COLOUR_LIGHT_BLUE, NLWW_CAPTION), SetDataTip(STR_NETWORK_GAME_LOBBY_CAPTION, STR_NULL),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_LIGHT_BLUE, NLWW_BACKGROUND),
		NWidget(WWT_TEXT, COLOUR_LIGHT_BLUE, NLWW_TEXT), SetDataTip(STR_NETWORK_GAME_LOBBY_PREPARE_TO_JOIN, STR_NULL), SetMinimalSize(410, 13), SetPadding(8, 0, 0, 10),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
		NWidget(NWID_HORIZONTAL), SetPIP(10, 0, 15),
			/* Company list. */
			NWidget(NWID_VERTICAL),
				NWidget(WWT_PANEL, COLOUR_WHITE, NLWW_HEADER), SetMinimalSize(146, 12), SetFill(false, false), EndContainer(),
				NWidget(WWT_MATRIX, COLOUR_LIGHT_BLUE, NLWW_MATRIX), SetMinimalSize(146, 141), SetDataTip((10 << 8) + 1, STR_NETWORK_GAME_LOBBY_COMPANY_LIST_TOOLTIP),
			EndContainer(),
			NWidget(WWT_SCROLLBAR, COLOUR_LIGHT_BLUE, NLWW_SCROLLBAR),
			NWidget(NWID_SPACER), SetMinimalSize(5, 0),
			/* Company info. */
			NWidget(WWT_PANEL, COLOUR_LIGHT_BLUE, NLWW_DETAILS), SetMinimalSize(232, 153), SetFill(false, false), EndContainer(),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 9),
		/* Buttons. */
		NWidget(NWID_HORIZONTAL), SetPIP(10, 0, 31),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NLWW_JOIN), SetMinimalSize(142, 12), SetDataTip(STR_NETWORK_GAME_LOBBY_JOIN_COMPANY, STR_NETWORK_GAME_LOBBY_JOIN_COMPANY_TOOLTIP),
				NWidget(NWID_SPACER), SetMinimalSize(0, 3),
				NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NLWW_NEW), SetMinimalSize(142, 12), SetDataTip(STR_NETWORK_GAME_LOBBY_NEW_COMPANY, STR_NETWORK_GAME_LOBBY_NEW_COMPANY_TOOLTIP),
			EndContainer(),
			NWidget(NWID_VERTICAL), SetPadding(0, 0, 0, 6),
				NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NLWW_SPECTATE), SetMinimalSize(111, 12), SetDataTip(STR_NETWORK_GAME_LOBBY_SPECTATE_GAME, STR_NETWORK_GAME_LOBBY_SPECTATE_GAME_TOOLTIP),
				NWidget(NWID_SPACER), SetMinimalSize(0, 3),
				NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NLWW_REFRESH), SetMinimalSize(111, 12), SetDataTip(STR_NETWORK_SERVER_LIST_REFRESH, STR_NETWORK_SERVER_LIST_REFRESH_TOOLTIP),
			EndContainer(),
			NWidget(NWID_VERTICAL), SetPadding(0, 0, 0, 9),
				NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NLWW_CANCEL), SetMinimalSize(111, 12), SetDataTip(STR_BUTTON_CANCEL, STR_NULL),
				NWidget(NWID_SPACER), SetMinimalSize(0, 15),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 8),
	EndContainer(),
};

static const WindowDesc _network_lobby_window_desc(
	WDP_CENTER, WDP_CENTER, 420, 235, 420, 235,
	WC_NETWORK_WINDOW, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS,
	_network_lobby_window_widgets, _nested_network_lobby_window_widgets, lengthof(_nested_network_lobby_window_widgets)
);

/* Show the networklobbywindow with the selected server
 * @param ngl Selected game pointer which is passed to the new window */
static void ShowNetworkLobbyWindow(NetworkGameList *ngl)
{
	DeleteWindowById(WC_NETWORK_WINDOW, 0);

	NetworkTCPQueryServer(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port)); // company info
	NetworkUDPQueryServer(NetworkAddress(_settings_client.network.last_host, _settings_client.network.last_port)); // general data

	new NetworkLobbyWindow(&_network_lobby_window_desc, ngl);
}

/**
 * Get the company information of a given company to fill for the lobby.
 * @param company the company to get the company info struct from.
 * @return the company info struct to write the (downloaded) data to.
 */
NetworkCompanyInfo *GetLobbyCompanyInfo(CompanyID company)
{
	NetworkLobbyWindow *lobby = dynamic_cast<NetworkLobbyWindow*>(FindWindowById(WC_NETWORK_WINDOW, 0));
	return (lobby != NULL && company < MAX_COMPANIES) ? &lobby->company_info[company] : NULL;
}

/* The window below gives information about the connected clients
 *  and also makes able to give money to them, kick them (if server)
 *  and stuff like that. */

extern void DrawCompanyIcon(CompanyID cid, int x, int y);

/* Every action must be of this form */
typedef void ClientList_Action_Proc(byte client_no);

/* Max 10 actions per client */
#define MAX_CLIENTLIST_ACTION 10

enum {
	CLNWND_OFFSET = 16,
	CLNWND_ROWSIZE = 10
};

/** Widget numbers of the client list window. */
enum ClientListWidgets {
	CLW_CLOSE,
	CLW_CAPTION,
	CLW_STICKY,
	CLW_PANEL,
};

static const Widget _client_list_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,  COLOUR_GREY,     0,    10,     0,    13, STR_BLACK_CROSS,          STR_TOOLTIP_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_GREY,    11,   237,     0,    13, STR_NETWORK_COMPANY_LIST_CLIENT_LIST,  STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS},
{  WWT_STICKYBOX,   RESIZE_NONE,  COLOUR_GREY,   238,   249,     0,    13, STR_NULL,                 STR_TOOLTIP_STICKY},

{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,     0,   249,    14,    14 + CLNWND_ROWSIZE + 1, 0x0, STR_NULL},
{   WIDGETS_END},
};

static const NWidgetPart _nested_client_list_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY, CLW_CLOSE),
		NWidget(WWT_CAPTION, COLOUR_GREY, CLW_CAPTION), SetDataTip(STR_NETWORK_COMPANY_LIST_CLIENT_LIST, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, COLOUR_GREY, CLW_STICKY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, CLW_PANEL), SetMinimalSize(250, CLNWND_ROWSIZE + 2), EndContainer(),
};

static const Widget _client_list_popup_widgets[] = {
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,     0,   99,     0,     0,     0, STR_NULL},
{   WIDGETS_END},
};

static const NWidgetPart _nested_client_list_popup_widgets[] = {
	NWidget(WWT_PANEL, COLOUR_GREY, 0), SetMinimalSize(100, 1), EndContainer(),
};

static const WindowDesc _client_list_desc(
	WDP_AUTO, WDP_AUTO, 250, 1, 250, 1,
	WC_CLIENT_LIST, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_STICKY_BUTTON,
	_client_list_widgets, _nested_client_list_widgets, lengthof(_nested_client_list_widgets)
);

/* Finds the Xth client-info that is active */
static NetworkClientInfo *NetworkFindClientInfo(byte client_no)
{
	NetworkClientInfo *ci;

	FOR_ALL_CLIENT_INFOS(ci) {
		if (client_no == 0) return ci;
		client_no--;
	}

	return NULL;
}

/* Here we start to define the options out of the menu */
static void ClientList_Kick(byte client_no)
{
	const NetworkClientInfo *ci = NetworkFindClientInfo(client_no);

	if (ci == NULL) return;

	NetworkServerKickClient(ci->client_id);
}

static void ClientList_Ban(byte client_no)
{
	NetworkClientInfo *ci = NetworkFindClientInfo(client_no);

	if (ci == NULL) return;

	NetworkServerBanIP(GetClientIP(ci));
}

static void ClientList_GiveMoney(byte client_no)
{
	if (NetworkFindClientInfo(client_no) != NULL) {
		ShowNetworkGiveMoneyWindow(NetworkFindClientInfo(client_no)->client_playas);
	}
}

static void ClientList_SpeakToClient(byte client_no)
{
	if (NetworkFindClientInfo(client_no) != NULL) {
		ShowNetworkChatQueryWindow(DESTTYPE_CLIENT, NetworkFindClientInfo(client_no)->client_id);
	}
}

static void ClientList_SpeakToCompany(byte client_no)
{
	if (NetworkFindClientInfo(client_no) != NULL) {
		ShowNetworkChatQueryWindow(DESTTYPE_TEAM, NetworkFindClientInfo(client_no)->client_playas);
	}
}

static void ClientList_SpeakToAll(byte client_no)
{
	ShowNetworkChatQueryWindow(DESTTYPE_BROADCAST, 0);
}

static void ClientList_None(byte client_no)
{
	/* No action ;) */
}



struct NetworkClientListPopupWindow : Window {
	int sel_index;
	int client_no;
	char action[MAX_CLIENTLIST_ACTION][50];
	ClientList_Action_Proc *proc[MAX_CLIENTLIST_ACTION];

	NetworkClientListPopupWindow(int x, int y, const Widget *widgets, int client_no) :
			Window(x, y, 150, 100, WC_TOOLBAR_MENU, widgets),
			sel_index(0), client_no(client_no)
	{
		/*
		 * Fill the actions this client has.
		 * Watch is, max 50 chars long!
		 */

		const NetworkClientInfo *ci = NetworkFindClientInfo(client_no);

		int i = 0;
		if (_network_own_client_id != ci->client_id) {
			GetString(this->action[i], STR_NETWORK_CLIENTLIST_SPEAK_TO_CLIENT, lastof(this->action[i]));
			this->proc[i++] = &ClientList_SpeakToClient;
		}

		if (Company::IsValidID(ci->client_playas) || ci->client_playas == COMPANY_SPECTATOR) {
			GetString(this->action[i], STR_NETWORK_CLIENTLIST_SPEAK_TO_COMPANY, lastof(this->action[i]));
			this->proc[i++] = &ClientList_SpeakToCompany;
		}
		GetString(this->action[i], STR_NETWORK_CLIENTLIST_SPEAK_TO_ALL, lastof(this->action[i]));
		this->proc[i++] = &ClientList_SpeakToAll;

		if (_network_own_client_id != ci->client_id) {
			/* We are no spectator and the company we want to give money to is no spectator and money gifts are allowed */
			if (Company::IsValidID(_local_company) && Company::IsValidID(ci->client_playas) && _settings_game.economy.give_money) {
				GetString(this->action[i], STR_NETWORK_CLIENTLIST_GIVE_MONEY, lastof(this->action[i]));
				this->proc[i++] = &ClientList_GiveMoney;
			}
		}

		/* A server can kick clients (but not himself) */
		if (_network_server && _network_own_client_id != ci->client_id) {
			GetString(this->action[i], STR_NETWORK_CLIENTLIST_KICK, lastof(this->action[i]));
			this->proc[i++] = &ClientList_Kick;

			seprintf(this->action[i], lastof(this->action[i]), "Ban"); // XXX GetString?
			this->proc[i++] = &ClientList_Ban;
		}

		if (i == 0) {
			GetString(this->action[i], STR_NETWORK_CLIENTLIST_NONE, lastof(this->action[i]));
			this->proc[i++] = &ClientList_None;
		}

		/* Calculate the height */
		int h = ClientListPopupHeight();

		/* Allocate the popup */
		this->widget[0].bottom = this->widget[0].top + h;
		this->widget[0].right = this->widget[0].left + 150;

		this->flags4 &= ~WF_WHITE_BORDER_MASK;

		this->FindWindowPlacementAndResize(150, h + 1);
	}

	/**
	 * An action is clicked! What do we do?
	 */
	void HandleClientListPopupClick(byte index)
	{
		/* A click on the Popup of the ClientList.. handle the command */
		if (index < MAX_CLIENTLIST_ACTION && this->proc[index] != NULL) {
			this->proc[index](this->client_no);
		}
	}

	/**
	 * Finds the amount of actions in the popup and set the height correct
	 */
	uint ClientListPopupHeight()
	{
		int num = 0;

		/* Find the amount of actions */
		for (int i = 0; i < MAX_CLIENTLIST_ACTION; i++) {
			if (this->action[i][0] == '\0') continue;
			if (this->proc[i] == NULL) continue;
			num++;
		}

		num *= CLNWND_ROWSIZE;

		return num + 1;
	}


	virtual void OnPaint()
	{
		this->DrawWidgets();

		/* Draw the actions */
		int sel = this->sel_index;
		int y = 1;
		for (int i = 0; i < MAX_CLIENTLIST_ACTION; i++, y += CLNWND_ROWSIZE) {
			if (this->action[i][0] == '\0') continue;
			if (this->proc[i] == NULL) continue;

			TextColour colour;
			if (sel-- == 0) { // Selected item, highlight it
				GfxFillRect(1, y, 150 - 2, y + CLNWND_ROWSIZE - 1, 0);
				colour = TC_WHITE;
			} else {
				colour = TC_BLACK;
			}

			DrawString(4, this->width - 4, y, this->action[i], colour);
		}
	}

	virtual void OnMouseLoop()
	{
		/* We selected an action */
		int index = (_cursor.pos.y - this->top) / CLNWND_ROWSIZE;

		if (_left_button_down) {
			if (index == -1 || index == this->sel_index) return;

			this->sel_index = index;
			this->SetDirty();
		} else {
			if (index >= 0 && _cursor.pos.y >= this->top) {
				HandleClientListPopupClick(index);
			}

			DeleteWindowById(WC_TOOLBAR_MENU, 0);
		}
	}
};

/**
 * Show the popup (action list)
 */
static void PopupClientList(int client_no, int x, int y)
{
	static Widget *generated_client_list_popup_widgets = NULL;

	DeleteWindowById(WC_TOOLBAR_MENU, 0);

	if (NetworkFindClientInfo(client_no) == NULL) return;

	const Widget *wid = InitializeWidgetArrayFromNestedWidgets(
		_nested_client_list_popup_widgets, lengthof(_nested_client_list_popup_widgets),
		_client_list_popup_widgets, &generated_client_list_popup_widgets
	);

	new NetworkClientListPopupWindow(x, y, wid, client_no);
}

/**
 * Main handle for clientlist
 */
struct NetworkClientListWindow : Window
{
	int selected_item;
	int selected_y;

	NetworkClientListWindow(const WindowDesc *desc, WindowNumber window_number) :
			Window(desc, window_number),
			selected_item(-1),
			selected_y(0)
	{
		this->FindWindowPlacementAndResize(desc);
	}

	/**
	 * Finds the amount of clients and set the height correct
	 */
	bool CheckClientListHeight()
	{
		int num = 0;
		const NetworkClientInfo *ci;

		/* Should be replaced with a loop through all clients */
		FOR_ALL_CLIENT_INFOS(ci) {
			if (ci->client_playas != COMPANY_INACTIVE_CLIENT) num++;
		}

		num *= CLNWND_ROWSIZE;

		/* If height is changed */
		if (this->height != CLNWND_OFFSET + num + 1) {
			/* XXX - magic unfortunately; (num + 2) has to be one bigger than heigh (num + 1) */
			this->SetDirty();
			this->widget[3].bottom = this->widget[3].top + num + 2;
			this->height = CLNWND_OFFSET + num + 1;
			this->SetDirty();
			return false;
		}
		return true;
	}

	virtual void OnPaint()
	{
		NetworkClientInfo *ci;
		int i = 0;

		/* Check if we need to reset the height */
		if (!this->CheckClientListHeight()) return;

		this->DrawWidgets();

		int y = CLNWND_OFFSET;

		FOR_ALL_CLIENT_INFOS(ci) {
			TextColour colour;
			if (this->selected_item == i++) { // Selected item, highlight it
				GfxFillRect(1, y, 248, y + CLNWND_ROWSIZE - 1, 0);
				colour = TC_WHITE;
			} else {
				colour = TC_BLACK;
			}

			if (ci->client_id == CLIENT_ID_SERVER) {
				DrawString(4, 81, y, STR_NETWORK_SERVER, colour);
			} else {
				DrawString(4, 81, y, STR_NETWORK_CLIENT, colour);
			}

			/* Filter out spectators */
			if (Company::IsValidID(ci->client_playas)) DrawCompanyIcon(ci->client_playas, 64, y + 1);

			DrawString(81, this->width - 2, y, ci->client_name, colour);

			y += CLNWND_ROWSIZE;
		}
	}

	virtual void OnClick(Point pt, int widget)
	{
		/* Show the popup with option */
		if (this->selected_item != -1) {
			PopupClientList(this->selected_item, pt.x + this->left, pt.y + this->top);
		}
	}

	virtual void OnMouseOver(Point pt, int widget)
	{
		/* -1 means we left the current window */
		if (pt.y == -1) {
			this->selected_y = 0;
			this->selected_item = -1;
			this->SetDirty();
			return;
		}
		/* It did not change.. no update! */
		if (pt.y == this->selected_y) return;

		/* Find the new selected item (if any) */
		this->selected_y = pt.y;
		if (pt.y > CLNWND_OFFSET) {
			this->selected_item = (pt.y - CLNWND_OFFSET) / CLNWND_ROWSIZE;
		} else {
			this->selected_item = -1;
		}

		/* Repaint */
		this->SetDirty();
	}
};

void ShowClientList()
{
	AllocateWindowDescFront<NetworkClientListWindow>(&_client_list_desc, 0);
}


static NetworkPasswordType pw_type;


void ShowNetworkNeedPassword(NetworkPasswordType npt)
{
	StringID caption;

	pw_type = npt;
	switch (npt) {
		default: NOT_REACHED();
		case NETWORK_GAME_PASSWORD:    caption = STR_NETWORK_NEED_GAME_PASSWORD_CAPTION; break;
		case NETWORK_COMPANY_PASSWORD: caption = STR_NETWORK_NEED_COMPANY_PASSWORD_CAPTION; break;
	}
	ShowQueryString(STR_EMPTY, caption, 20, 180, FindWindowById(WC_NETWORK_STATUS_WINDOW, 0), CS_ALPHANUMERAL, QSF_NONE);
}

/* Vars needed for the join-GUI */
NetworkJoinStatus _network_join_status;
uint8 _network_join_waiting;
uint32 _network_join_bytes;
uint32 _network_join_bytes_total;

/** Widgets used for the join status window. */
enum NetworkJoinStatusWidgets {
	NJSW_CAPTION,    ///< Caption of the window
	NJSW_BACKGROUND, ///< Background
	NJSW_CANCELOK,   ///< Cancel/OK button
};

struct NetworkJoinStatusWindow : Window {
	NetworkJoinStatusWindow(const WindowDesc *desc) : Window(desc)
	{
		this->parent = FindWindowById(WC_NETWORK_WINDOW, 0);
		this->FindWindowPlacementAndResize(desc);
	}

	virtual void OnPaint()
	{
		uint8 progress; // used for progress bar
		this->DrawWidgets();

		DrawString(this->widget[NJSW_BACKGROUND].left + 2, this->widget[NJSW_BACKGROUND].right - 2, 35, STR_NETWORK_CONNECTING_1 + _network_join_status, TC_FROMSTRING, SA_CENTER);
		switch (_network_join_status) {
			case NETWORK_JOIN_STATUS_CONNECTING: case NETWORK_JOIN_STATUS_AUTHORIZING:
			case NETWORK_JOIN_STATUS_GETTING_COMPANY_INFO:
				progress = 10; // first two stages 10%
				break;
			case NETWORK_JOIN_STATUS_WAITING:
				SetDParam(0, _network_join_waiting);
				DrawString(this->widget[NJSW_BACKGROUND].left + 2, this->widget[NJSW_BACKGROUND].right - 2, 46, STR_NETWORK_CONNECTING_WAITING, TC_FROMSTRING, SA_CENTER);
				progress = 15; // third stage is 15%
				break;
			case NETWORK_JOIN_STATUS_DOWNLOADING:
				SetDParam(0, _network_join_bytes);
				SetDParam(1, _network_join_bytes_total);
				DrawString(this->widget[NJSW_BACKGROUND].left + 2, this->widget[NJSW_BACKGROUND].right - 2, 46, STR_NETWORK_CONNECTING_DOWNLOADING, TC_FROMSTRING, SA_CENTER);
				/* Fallthrough */
			default: // Waiting is 15%, so the resting receivement of map is maximum 70%
				progress = 15 + _network_join_bytes * (100 - 15) / _network_join_bytes_total;
		}

		/* Draw nice progress bar :) */
		DrawFrameRect(20, 18, (int)((this->width - 20) * progress / 100), 28, COLOUR_MAUVE, FR_NONE);
	}

	virtual void OnClick(Point pt, int widget)
	{
		if (widget == NJSW_CANCELOK) { // Disconnect button
			NetworkDisconnect();
			SwitchToMode(SM_MENU);
			ShowNetworkGameWindow();
		}
	}

	virtual void OnQueryTextFinished(char *str)
	{
		if (StrEmpty(str)) {
			NetworkDisconnect();
			ShowNetworkGameWindow();
		} else {
			SEND_COMMAND(PACKET_CLIENT_PASSWORD)(pw_type, str);
		}
	}
};

static const Widget _network_join_status_window_widget[] = {
{    WWT_CAPTION,   RESIZE_NONE,  COLOUR_GREY,      0,   249,     0,    13, STR_NETWORK_CONNECTING_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS}, // NJSW_CAPTION
{      WWT_PANEL,   RESIZE_NONE,  COLOUR_GREY,      0,   249,    14,    84, 0x0,                    STR_NULL},                        // NJSW_BACKGROUND
{ WWT_PUSHTXTBTN,   RESIZE_NONE,  COLOUR_WHITE,    75,   175,    69,    80, STR_NETWORK_CONNECTION_DISCONNECT, STR_NULL},                        // NJSW_CANCELOK
{   WIDGETS_END},
};

static const NWidgetPart _nested_network_join_status_window_widgets[] = {
	NWidget(WWT_CAPTION, COLOUR_GREY, NJSW_CAPTION), SetDataTip(STR_NETWORK_CONNECTING_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	NWidget(WWT_PANEL, COLOUR_GREY, NJSW_BACKGROUND),
		NWidget(WWT_PUSHTXTBTN, COLOUR_WHITE, NJSW_CANCELOK), SetMinimalSize(101, 12), SetPadding(55, 74, 4, 75), SetDataTip(STR_NETWORK_CONNECTION_DISCONNECT, STR_NULL),
	EndContainer(),
};

static const WindowDesc _network_join_status_window_desc(
	WDP_CENTER, WDP_CENTER, 250, 85, 250, 85,
	WC_NETWORK_STATUS_WINDOW, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_DEF_WIDGET | WDF_MODAL,
	_network_join_status_window_widget, _nested_network_join_status_window_widgets, lengthof(_nested_network_join_status_window_widgets)
);

void ShowJoinStatusWindow()
{
	DeleteWindowById(WC_NETWORK_STATUS_WINDOW, 0);
	new NetworkJoinStatusWindow(&_network_join_status_window_desc);
}


/** Enum for NetworkGameWindow, referring to _network_game_window_widgets */
enum NetworkCompanyPasswordWindowWidgets {
	NCPWW_CLOSE,                    ///< Close 'X' button
	NCPWW_CAPTION,                  ///< Caption of the whole window
	NCPWW_BACKGROUND,               ///< The background of the interface
	NCPWW_LABEL,                    ///< Label in front of the password field
	NCPWW_PASSWORD,                 ///< Input field for the password
	NCPWW_SAVE_AS_DEFAULT_PASSWORD, ///< Toggle 'button' for saving the current password as default password
	NCPWW_CANCEL,                   ///< Close the window without changing anything
	NCPWW_OK,                       ///< Safe the password etc.
};

struct NetworkCompanyPasswordWindow : public QueryStringBaseWindow {
	NetworkCompanyPasswordWindow(const WindowDesc *desc, Window *parent) : QueryStringBaseWindow(lengthof(_settings_client.network.default_company_pass), desc)
	{
		this->parent = parent;
		this->afilter = CS_ALPHANUMERAL;
		InitializeTextBuffer(&this->text, this->edit_str_buf, this->edit_str_size, 0);
		this->SetFocusedWidget(NCPWW_PASSWORD);

		this->FindWindowPlacementAndResize(desc);
	}

	void OnOk()
	{
		if (this->IsWidgetLowered(NCPWW_SAVE_AS_DEFAULT_PASSWORD)) {
			snprintf(_settings_client.network.default_company_pass, lengthof(_settings_client.network.default_company_pass), "%s", this->edit_str_buf);
		}

		/* empty password is a '*' because of console argument */
		if (StrEmpty(this->edit_str_buf)) snprintf(this->edit_str_buf, this->edit_str_size, "*");
		char *password = this->edit_str_buf;
		NetworkChangeCompanyPassword(1, &password);
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();
		this->DrawEditBox(4);
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case NCPWW_OK:
				this->OnOk();

			/* FALL THROUGH */
			case NCPWW_CANCEL:
				delete this;
				break;

			case NCPWW_SAVE_AS_DEFAULT_PASSWORD:
				this->ToggleWidgetLoweredState(NCPWW_SAVE_AS_DEFAULT_PASSWORD);
				this->SetDirty();
				break;
		}
	}

	virtual void OnMouseLoop()
	{
		this->HandleEditBox(4);
	}

	virtual EventState OnKeyPress(uint16 key, uint16 keycode)
	{
		EventState state = ES_NOT_HANDLED;
		switch (this->HandleEditBoxKey(4, key, keycode, state)) {
			default: break;

			case HEBR_CONFIRM:
				this->OnOk();
				/* FALL THROUGH */

			case HEBR_CANCEL:
				delete this;
				break;
		}
		return state;
	}

	virtual void OnOpenOSKWindow(int wid)
	{
		ShowOnScreenKeyboard(this, wid, NCPWW_CANCEL, NCPWW_OK);
	}
};

static const Widget _ncp_window_widgets[] = {
{   WWT_CLOSEBOX,  RESIZE_NONE,  COLOUR_GREY,   0,  10,  0, 13, STR_BLACK_CROSS,                   STR_TOOLTIP_CLOSE_WINDOW},
{    WWT_CAPTION,  RESIZE_NONE,  COLOUR_GREY,  11, 299,  0, 13, STR_COMPANY_PASSWORD_CAPTION,      STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS},
{      WWT_PANEL,  RESIZE_NONE,  COLOUR_GREY,   0, 299, 14, 50, 0x0,                               STR_NULL},
{       WWT_TEXT,  RESIZE_NONE,  COLOUR_GREY,   5, 100, 19, 30, STR_COMPANY_VIEW_PASSWORD,              STR_NULL},
{    WWT_EDITBOX,  RESIZE_NONE,  COLOUR_GREY, 101, 294, 19, 30, STR_COMPANY_VIEW_SET_PASSWORD,          STR_NULL},
{    WWT_TEXTBTN,  RESIZE_NONE,  COLOUR_GREY, 101, 294, 35, 46, STR_COMPANY_PASSWORD_MAKE_DEFAULT, STR_COMPANY_PASSWORD_MAKE_DEFAULT_TOOLTIP},
{ WWT_PUSHTXTBTN,  RESIZE_NONE,  COLOUR_GREY,   0, 149, 51, 62, STR_BUTTON_CANCEL,                  STR_COMPANY_PASSWORD_CANCEL},
{ WWT_PUSHTXTBTN,  RESIZE_NONE,  COLOUR_GREY, 150, 299, 51, 62, STR_BUTTON_OK,                      STR_COMPANY_PASSWORD_OK},
{   WIDGETS_END},
};

static const NWidgetPart _nested_ncp_window_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY, NCPWW_CLOSE),
		NWidget(WWT_CAPTION, COLOUR_GREY, NCPWW_CAPTION), SetDataTip(STR_COMPANY_PASSWORD_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, NCPWW_BACKGROUND),
		NWidget(NWID_SPACER), SetMinimalSize(0, 5),
		NWidget(NWID_HORIZONTAL), SetPIP(5, 0, 5),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_TEXT, COLOUR_GREY, NCPWW_LABEL), SetMinimalSize(96, 12), SetDataTip(STR_COMPANY_VIEW_PASSWORD, STR_NULL),
				NWidget(NWID_SPACER), SetFill(false, true),
			EndContainer(),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_EDITBOX, COLOUR_GREY, NCPWW_PASSWORD), SetMinimalSize(194, 12), SetDataTip(STR_COMPANY_VIEW_SET_PASSWORD, STR_NULL),
				NWidget(NWID_SPACER), SetMinimalSize(0, 4),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, NCPWW_SAVE_AS_DEFAULT_PASSWORD), SetMinimalSize(194, 12),
											SetDataTip(STR_COMPANY_PASSWORD_MAKE_DEFAULT, STR_COMPANY_PASSWORD_MAKE_DEFAULT_TOOLTIP),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 4),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, NCPWW_CANCEL), SetMinimalSize(150, 12), SetDataTip(STR_BUTTON_CANCEL, STR_COMPANY_PASSWORD_CANCEL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, NCPWW_OK), SetMinimalSize(150, 12), SetDataTip(STR_BUTTON_OK, STR_COMPANY_PASSWORD_OK),
	EndContainer(),
};

static const WindowDesc _ncp_window_desc(
	WDP_AUTO, WDP_AUTO, 300, 63, 300, 63,
	WC_COMPANY_PASSWORD_WINDOW, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS | WDF_STICKY_BUTTON,
	_ncp_window_widgets, _nested_ncp_window_widgets, lengthof(_nested_ncp_window_widgets)
);

void ShowNetworkCompanyPasswordWindow(Window *parent)
{
	DeleteWindowById(WC_COMPANY_PASSWORD_WINDOW, 0);

	new NetworkCompanyPasswordWindow(&_ncp_window_desc, parent);
}

#endif /* ENABLE_NETWORK */
