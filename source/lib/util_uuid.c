/* 
 *  Unix SMB/CIFS implementation.
 *  UUID server routines
 *  Copyright (C) Theodore Ts'o               1996, 1997,
 *  Copyright (C) Jim McDonough                     2002.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "includes.h"

/*
 * Offset between 15-Oct-1582 and 1-Jan-70
 */
#define TIME_OFFSET_HIGH 0x01B21DD2
#define TIME_OFFSET_LOW  0x13814000

struct uuid {
        uint32   time_low;
        uint16   time_mid;
        uint16   time_hi_and_version;
        uint16   clock_seq;
        uint8    node[6];
};


static void uuid_pack(const struct uuid *uu, GUID *ptr)
{
	uint8 *out = ptr->info;

	SIVAL(out, 0, uu->time_low);
	SSVAL(out, 4, uu->time_mid);
	SSVAL(out, 6, uu->time_hi_and_version);
	SSVAL(out, 8, uu->clock_seq);
	memcpy(out+10, uu->node, 6);
}

static void uuid_unpack(const GUID in, struct uuid *uu)
{
	const uint8 *ptr = in.info;

	uu->time_low = IVAL(ptr, 0);
	uu->time_mid = SVAL(ptr, 4);
	uu->time_hi_and_version = SVAL(ptr, 6);
	uu->clock_seq = SVAL(ptr, 8);
	memcpy(uu->node, ptr+10, 6);
}

void uuid_generate_random(GUID *out)
{
	GUID tmp;
	struct uuid uu;

	generate_random_buffer(tmp.info, sizeof(tmp.info), True);
	uuid_unpack(tmp, &uu);

	uu.clock_seq = (uu.clock_seq & 0x3FFF) | 0x8000;
	uu.time_hi_and_version = (uu.time_hi_and_version & 0x0FFF) | 0x4000;
	uuid_pack(&uu, out);
}
