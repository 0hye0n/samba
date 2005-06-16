/*
 * Unix SMB/CIFS implementation.
 * SMB parameters and setup
 * Copyright (C) Andrew Tridgell 1992-1998 Modified by Jeremy Allison 1995.
 * 
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 675
 * Mass Ave, Cambridge, MA 02139, USA.
 */

#include "includes.h"
#include "system/shmem.h"
#include "system/filesys.h"

/*************************************************************************
 gets a line out of a file.
 line is of format "xxxx:xxxxxx:xxxxx:".
 lines with "#" at the front are ignored.
*************************************************************************/
int getfileline(void *vp, char *linebuf, int linebuf_size)
{
	/* Static buffers we will return. */
	FILE *fp = (FILE *)vp;
	uint8_t   c;
	uint8_t  *p;
	size_t            linebuf_len;

	if (fp == NULL)
	{
		DEBUG(0,("getfileline: Bad file pointer.\n"));
		return -1;
	}

	/*
	 * Scan the file, a line at a time.
	 */
	while (!feof(fp))
	{
		linebuf[0] = '\0';

		fgets(linebuf, linebuf_size, fp);
		if (ferror(fp))
		{
			return -1;
		}

		/*
		 * Check if the string is terminated with a newline - if not
		 * then we must keep reading and discard until we get one.
		 */

		linebuf_len = strlen(linebuf);
		if (linebuf_len == 0)
		{
			linebuf[0] = '\0';
			return 0;
		}

		if (linebuf[linebuf_len - 1] != '\n')
		{
			c = '\0';
			while (!ferror(fp) && !feof(fp))
			{
				c = fgetc(fp);
				if (c == '\n')
				{
					break;
				}
			}
		}
		else
		{
			linebuf[linebuf_len - 1] = '\0';
		}

#ifdef DEBUG_PASSWORD
		DEBUG(100, ("getfileline: got line |%s|\n", linebuf));
#endif
		if ((linebuf[0] == 0) && feof(fp))
		{
			DEBUG(4, ("getfileline: end of file reached\n"));
			return 0;
		}

		if (linebuf[0] == '#' || linebuf[0] == '\0')
		{
			DEBUG(6, ("getfileline: skipping comment or blank line\n"));
			continue;
		}

		p = (uint8_t *) strchr_m(linebuf, ':');
		if (p == NULL)
		{
			DEBUG(0, ("getfileline: malformed line entry (no :)\n"));
			continue;
		}
		return linebuf_len;
	}
	return -1;
}


/****************************************************************************
read a line from a file with possible \ continuation chars. 
Blanks at the start or end of a line are stripped.
The string will be allocated if s2 is NULL
****************************************************************************/
char *fgets_slash(char *s2,int maxlen,XFILE *f)
{
  char *s=s2;
  int len = 0;
  int c;
  BOOL start_of_line = True;

  if (x_feof(f))
    return(NULL);

  if (maxlen <2) return(NULL);

  if (!s2)
    {
      maxlen = MIN(maxlen,8);
      s = (char *)malloc(maxlen);
    }

  if (!s) return(NULL);

  *s = 0;

  while (len < maxlen-1)
    {
      c = x_getc(f);
      switch (c)
	{
	case '\r':
	  break;
	case '\n':
	  while (len > 0 && s[len-1] == ' ')
	    {
	      s[--len] = 0;
	    }
	  if (len > 0 && s[len-1] == '\\')
	    {
	      s[--len] = 0;
	      start_of_line = True;
	      break;
	    }
	  return(s);
	case EOF:
	  if (len <= 0 && !s2) 
	    SAFE_FREE(s);
	  return(len>0?s:NULL);
	case ' ':
	  if (start_of_line)
	    break;
	default:
	  start_of_line = False;
	  s[len++] = c;
	  s[len] = 0;
	}
      if (!s2 && len > maxlen-3)
	{
	  char *t;
	  
	  maxlen *= 2;
	  t = realloc_p(s, char, maxlen);
	  if (!t) {
	    DEBUG(0,("fgets_slash: failed to expand buffer!\n"));
	    SAFE_FREE(s);
	    return(NULL);
	  } else s = t;
	}
    }
  return(s);
}



/****************************************************************************
load a file into memory from a fd.
****************************************************************************/ 

char *fd_load(int fd, size_t *size)
{
	struct stat sbuf;
	char *p;

	if (fstat(fd, &sbuf) != 0) return NULL;

	p = (char *)malloc(sbuf.st_size+1);
	if (!p) return NULL;

	if (read(fd, p, sbuf.st_size) != sbuf.st_size) {
		SAFE_FREE(p);
		return NULL;
	}
	p[sbuf.st_size] = 0;

	if (size) *size = sbuf.st_size;

	return p;
}

/****************************************************************************
load a file into memory
****************************************************************************/
char *file_load(const char *fname, size_t *size)
{
	int fd;
	char *p;

	if (!fname || !*fname) return NULL;
	
	fd = open(fname,O_RDONLY);
	if (fd == -1) return NULL;

	p = fd_load(fd, size);

	close(fd);

	return p;
}


/*******************************************************************
mmap (if possible) or read a file
********************************************************************/
void *map_file(char *fname, size_t size)
{
	size_t s2 = 0;
	void *p = NULL;
#ifdef HAVE_MMAP
	int fd;
	fd = open(fname, O_RDONLY, 0);
	if (fd == -1) {
		DEBUG(2,("Failed to load %s - %s\n", fname, strerror(errno)));
		return NULL;
	}
	p = mmap(NULL, size, PROT_READ, MAP_SHARED|MAP_FILE, fd, 0);
	close(fd);
	if (p == MAP_FAILED) {
		DEBUG(1,("Failed to mmap %s - %s\n", fname, strerror(errno)));
		return NULL;
	}
#endif
	if (!p) {
		p = file_load(fname, &s2);
		if (!p) return NULL;
		if (s2 != size) {
			DEBUG(1,("incorrect size for %s - got %d expected %d\n",
				 fname, s2, size));
			if (p) free(p);
			return NULL;
		}
	}

	return p;
}


/****************************************************************************
parse a buffer into lines
****************************************************************************/
static char **file_lines_parse(char *p, size_t size, int *numlines)
{
	int i;
	char *s, **ret;

	if (!p) return NULL;

	for (s = p, i=0; s < p+size; s++) {
		if (s[0] == '\n') i++;
	}

	ret = malloc_array_p(char *, i+2);
	if (!ret) {
		SAFE_FREE(p);
		return NULL;
	}	
	memset(ret, 0, sizeof(ret[0])*(i+2));
	if (numlines) *numlines = i;

	ret[0] = p;
	for (s = p, i=0; s < p+size; s++) {
		if (s[0] == '\n') {
			s[0] = 0;
			i++;
			ret[i] = s+1;
		}
		if (s[0] == '\r') s[0] = 0;
	}

	return ret;
}


/****************************************************************************
load a file into memory and return an array of pointers to lines in the file
must be freed with file_lines_free(). 
****************************************************************************/
char **file_lines_load(const char *fname, int *numlines)
{
	char *p;
	size_t size;

	p = file_load(fname, &size);
	if (!p) return NULL;

	return file_lines_parse(p, size, numlines);
}

/****************************************************************************
load a fd into memory and return an array of pointers to lines in the file
must be freed with file_lines_free(). If convert is true calls unix_to_dos on
the list.
****************************************************************************/
char **fd_lines_load(int fd, int *numlines)
{
	char *p;
	size_t size;

	p = fd_load(fd, &size);
	if (!p) return NULL;

	return file_lines_parse(p, size, numlines);
}


/****************************************************************************
free lines loaded with file_lines_load
****************************************************************************/
void file_lines_free(char **lines)
{
	if (!lines) return;
	SAFE_FREE(lines[0]);
	SAFE_FREE(lines);
}


/****************************************************************************
take a lislist of lines and modify them to produce a list where \ continues
a line
****************************************************************************/
void file_lines_slashcont(char **lines)
{
	int i, j;

	for (i=0; lines[i];) {
		int len = strlen(lines[i]);
		if (lines[i][len-1] == '\\') {
			lines[i][len-1] = ' ';
			if (lines[i+1]) {
				char *p = &lines[i][len];
				while (p < lines[i+1]) *p++ = ' ';
				for (j = i+1; lines[j]; j++) lines[j] = lines[j+1];
			}
		} else {
			i++;
		}
	}
}

/*
  save a lump of data into a file. Mostly used for debugging 
*/
BOOL file_save(const char *fname, void *packet, size_t length)
{
	int fd;
	fd = open(fname, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (fd == -1) {
		return False;
	}
	if (write(fd, packet, length) != (size_t)length) {
		return False;
	}
	close(fd);
	return True;
}

/*
  see if a file exists
*/
BOOL file_exists(const char *path)
{
	struct stat st;
	return (stat(path, &st) == 0);
}
