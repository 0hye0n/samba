/*
   Unix SMB/CIFS implementation.
   SWAT language handling
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   Created by Ryo Kawahara <rkawa@lbe.co.jp> 
*/

#include "includes.h"
#include "../web/swat_proto.h"

/*
  during a file download we first check to see if there is a language
  specific file available. If there is then use that, otherwise 
  just open the specified file
*/
int web_open(const char *fname, int flags, mode_t mode)
{
	char *p = NULL;
	char *lang = lang_tdb_current();
	int fd;
	if (lang) {
		asprintf(&p, "lang/%s/%s", lang, fname);
		if (p) {
			fd = sys_open(p, flags, mode);
			free(p);
			if (fd != -1) {
				return fd;
			}
		}
	}

	/* fall through to default name */
	return sys_open(fname, flags, mode);
}


/*
  choose from a list of languages. The list can be comma or space
  separated
  Keep choosing until we get a hit 
  Changed to habdle priority -- Simo
*/
void web_set_lang(const char *lang_string)
{
	char **lang_list, **count;
	float *pri;
	int lang_num, i;

	/* build the lang list */
	lang_list = str_list_make(lang_string, ", \t\r\n");
	if (!lang_list) return;
	
	/* sort the list by priority */
	lang_num = 0;
	count = lang_list;
	while (*count && **count) {
		count++;
		lang_num++;
	}
	pri = (float *)malloc(sizeof(float) * lang_num);
	for (i = 0; i < lang_num; i++) {
		char *pri_code;
		if ((pri_code=strstr(lang_list[i], ";q="))) {
			*pri_code = '\0';
			pri_code += 3;
			pri[i] = strtof(pri_code, NULL);
		} else {
			pri[i] = 1;
		}
		if (i != 0) {
			int l;
			for (l = i; l > 0; l--) {
				if (pri[l] > pri[l-1]) {
					char *tempc;
					int tempf;
					tempc = lang_list[l];
					tempf = pri[l];
					lang_list[l] = lang_list[l-1];
					pri[i] = pri[l-1];
					lang_list[l-1] = tempc;
					pri[l-1] = tempf;
				}
			}
		 }
	}

	for (i = 0; i < lang_num; i++) {
		if (lang_tdb_init(lang_list[i])) return;
	}
	
	/* it's not an error to not initialise - we just fall back to 
	   the default */
}
