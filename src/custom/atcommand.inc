// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

/**
 * Custom Atcommands
 * Place the body of custom atcommands in this file.
 * Format:
 *	ACMD_FUNC(command_name)
 *	{
 *		<code>;
 *	}
 **/

#include <ctime>
#include <cstring>

#include "../common/nullpo.hpp"
#include "../map/atcommand.hpp"
#include "../map/clif.hpp"
#include "../map/pc.hpp"

ACMD_FUNC(hatredreset)
{
	nullpo_retr(-1, sd);

	pc_resethate(sd);
	clif_displaymessage(fd, "Reset 'Hatred' state~");
	return 0;
}

ACMD_FUNC(afk)
{
	nullpo_retr(-1, sd);

	char mes[100];
	bool has_reason = false;

	if (pc_isdead(sd)) {
		clif_displaymessage(fd, "You can't use @afk if you are dead.");
		return -1;
	}

	if (sd->state.vending || sd->state.buyingstore) {
		clif_displaymessage(fd, "You can't use @afk while vending.");
		return -1;
	}

	if (map_getmapflag(sd->m, MF_PVP) || map_getmapflag(sd->m, MF_GVG)) {
		clif_displaymessage(fd, "You can't use @afk in PVP or GVG maps.");
		return -1;
	}

	if (message && *message && sscanf(message, "%99[^\n]", mes) >= 1) {
		has_reason = true;
	}

	// If Auto Attack is active, go straight to offline battle.
	// Do NOT create chat room here.
	if (sd->sc.getSCE(SC_AUTOATTACK)) {
		clif_displaymessage(fd, "AFK mode enabled. Switching to offline auto battle...");

		// Inline battleoffline logic directly here
		sd->state.autotrade = 1;
		clif_authfail_fd(fd, 15);
		return 0;
	}

	// Normal AFK mode
	sd->state.autotrade = 1;

	// Only create chat room if player typed a reason
	if (has_reason) {
		chat_createpcchat(sd, mes, "", 1, 1);
	}

	clif_displaymessage(fd, "AFK mode enabled.");
	clif_authfail_fd(fd, 15);
	return 0;
}

ACMD_FUNC(vipstatus)
{
	time_t now = time(nullptr);
	char h_line[30];

	nullpo_retr(-1, sd);

	memset(h_line, '=', sizeof(h_line) - 1);
	h_line[sizeof(h_line) - 1] = '\0';

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	atcmd_output[sizeof(atcmd_output) - 1] = '\0';

	if (sd->vip.time <= now) {
		clif_displaymessage(fd, h_line);
		clif_displaymessage(fd, "Account status: Normal (Non-VIP)");
		clif_displaymessage(fd, h_line);
		return 0;
	}

	int year, month, day, hour, minute, second;
	char timestr[21];

	clif_displaymessage(fd, h_line);
	clif_displaymessage(fd, "Account status: VIP");

	split_time((int)(sd->vip.time - now), &year, &month, &day, &hour, &minute, &second);
	sprintf(atcmd_output, msg_txt(sd, 705), year, month, day, hour, minute, second);
	clif_displaymessage(fd, atcmd_output);

	timestamp2string(timestr, 20, sd->vip.time, "%Y-%m-%d %H:%M:%S");
	sprintf(atcmd_output, msg_txt(sd, 707), timestr);
	clif_displaymessage(fd, atcmd_output);

	clif_displaymessage(fd, h_line);
	clif_displaymessage(fd, "VIP Benefits:");
	clif_displaymessage(fd, "        1. Exclusive Commands");
	clif_displaymessage(fd, "                ~ @vipstatus");
	clif_displaymessage(fd, "        2. Storage Slot Increase by 300");
	clif_displaymessage(fd, "        3. Gives an additional 5% Drop Rate");
	clif_displaymessage(fd, "        4. Increased BaseExp & Job Exp by 50%");
	clif_displaymessage(fd, "        5. Reduced Refine Cost");
	clif_displaymessage(fd, "        6. VIP Exclusive Buffs");
	clif_displaymessage(fd, "        7. Increase Weight Limit +500");

	return 0;
}

/*==========================================
 * @seeitems by [Geezy]
 * allows players to toggle visibility of items dropped on the floor
 *------------------------------------------*/
ACMD_FUNC(seeitems)
{
	nullpo_retr(-1, sd);

	if (sd != nullptr) {
		if (sd->sc.option & OPTION_IGNORE_DROP) {
			sd->sc.option &= ~OPTION_IGNORE_DROP;
			clif_displaymessage(fd, "You will see items on the floor.");
		} else {
			sd->sc.option |= OPTION_IGNORE_DROP;
			clif_displaymessage(fd, "You will not see items on the floor.");
		}
		clif_refresh(sd);
	}
	return 0;
}