#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <ctype.h>
#include "tdb.h"

/* a tdb tool for manipulating a tdb database */

static TDB_CONTEXT *tdb;

static int print_rec(TDB_CONTEXT *tdb, TDB_DATA key, TDB_DATA dbuf, void *state);

static void print_asc(unsigned char *buf,int len)
{
	int i;
	for (i=0;i<len;i++)
		printf("%c",isprint(buf[i])?buf[i]:'.');
}

static void print_data(unsigned char *buf,int len)
{
	int i=0;
	if (len<=0) return;
	printf("[%03X] ",i);
	for (i=0;i<len;) {
		printf("%02X ",(int)buf[i]);
		i++;
		if (i%8 == 0) printf(" ");
		if (i%16 == 0) {      
			print_asc(&buf[i-16],8); printf(" ");
			print_asc(&buf[i-8],8); printf("\n");
			if (i<len) printf("[%03X] ",i);
		}
	}
	if (i%16) {
		int n;
		
		n = 16 - (i%16);
		printf(" ");
		if (n>8) printf(" ");
		while (n--) printf("   ");
		
		n = i%16;
		if (n > 8) n = 8;
		print_asc(&buf[i-(i%16)],n); printf(" ");
		n = (i%16) - n;
		if (n>0) print_asc(&buf[i-n],n); 
		printf("\n");    
	}
}

static void help(void)
{
	printf("
tdbtool: 
  create    dbname     : create a database
  open      dbname     : open an existing database
  erase                : erase the database
  dump                 : dump the database as strings
  insert    key  data  : insert a record
  store     key  data  : store a record (replace)
  show      key        : show a record by key
  delete    key        : delete a record by key
  free                 : print the database freelist
");
}

static void terror(char *why)
{
	printf("%s\n", why);
}

static void create_tdb(void)
{
	char *tok = strtok(NULL, " ");
	if (!tok) {
		help();
		return;
	}
	if (tdb) tdb_close(tdb);
	tdb = tdb_open(tok, 0, TDB_CLEAR_IF_FIRST,
		       O_RDWR | O_CREAT | O_TRUNC, 0600);
}

static void open_tdb(void)
{
	char *tok = strtok(NULL, " ");
	if (!tok) {
		help();
		return;
	}
	if (tdb) tdb_close(tdb);
	tdb = tdb_open(tok, 0, 0, O_RDWR, 0600);
}

static void insert_tdb(void)
{
	char *k = strtok(NULL, " ");
	char *d = strtok(NULL, " ");
	TDB_DATA key, dbuf;

	if (!k || !d) {
		help();
		return;
	}

	key.dptr = k;
	key.dsize = strlen(k);
	dbuf.dptr = d;
	dbuf.dsize = strlen(d);

	if (tdb_store(tdb, key, dbuf, TDB_INSERT) == -1) {
		terror("insert failed");
	}
}

static void store_tdb(void)
{
	char *k = strtok(NULL, " ");
	char *d = strtok(NULL, " ");
	TDB_DATA key, dbuf;

	if (!k || !d) {
		help();
		return;
	}

	key.dptr = k;
	key.dsize = strlen(k);
	dbuf.dptr = d;
	dbuf.dsize = strlen(d);

	if (tdb_store(tdb, key, dbuf, TDB_REPLACE) == -1) {
		terror("store failed");
	}
}

static void show_tdb(void)
{
	char *k = strtok(NULL, " ");
	TDB_DATA key, dbuf;

	if (!k) {
		help();
		return;
	}

	key.dptr = k;
	key.dsize = strlen(k)+1;

	dbuf = tdb_fetch(tdb, key);
	if (!dbuf.dptr) terror("fetch failed");
	/* printf("%s : %*.*s\n", k, (int)dbuf.dsize, (int)dbuf.dsize, dbuf.dptr); */
	print_rec(tdb, key, dbuf, NULL);
}

static void delete_tdb(void)
{
	char *k = strtok(NULL, " ");
	TDB_DATA key;

	if (!k) {
		help();
		return;
	}

	key.dptr = k;
	key.dsize = strlen(k);

	if (tdb_delete(tdb, key) != 0) {
		terror("delete failed");
	}
}

static int print_rec(TDB_CONTEXT *tdb, TDB_DATA key, TDB_DATA dbuf, void *state)
{
	printf("\nkey %d bytes\n", key.dsize);
	print_data(key.dptr, key.dsize);
	printf("data %d bytes\n", dbuf.dsize);
	print_data(dbuf.dptr, dbuf.dsize);
	return 0;
}

static int total_bytes;

static int traverse_fn(TDB_CONTEXT *tdb, TDB_DATA key, TDB_DATA dbuf, void *state)
{
	total_bytes += dbuf.dsize;
	return 0;
}

static void info_tdb(void)
{
	int count;
	total_bytes = 0;
	count = tdb_traverse(tdb, traverse_fn, NULL);
	printf("%d records totalling %d bytes\n", count, total_bytes);
}

static char *getline(char *prompt)
{
	static char line[1024];
	char *p;
	fputs(prompt, stdout);
	line[0] = 0;
	p = fgets(line, sizeof(line)-1, stdin);
	if (p) p = strchr(p, '\n');
	if (p) *p = 0;
	return p?line:NULL;
}

static int do_delete_fn(TDB_CONTEXT *tdb, TDB_DATA key, TDB_DATA dbuf,
                     void *state)
{
    return tdb_delete(tdb, key);
}

int main(int argc, char *argv[])
{
    char *line;
    char *tok;

    if (argv[1]) {
	static char tmp[1024];
        sprintf(tmp, "open %s", argv[1]);
        tok=strtok(tmp," ");
        open_tdb();
    }

    while ((line = getline("tdb> "))) {

        /* Shell command */
        
        if (line[0] == '!') {
            system(line + 1);
            continue;
        }
        
        if ((tok = strtok(line," "))==NULL) {
            continue;
        }
        if (strcmp(tok,"create") == 0) {
            create_tdb();
            continue;
        } else if (strcmp(tok,"open") == 0) {
            open_tdb();
            continue;
        }
            
        /* all the rest require a open database */
        if (!tdb) {
            terror("database not open");
            help();
            continue;
        }
            
        if (strcmp(tok,"insert") == 0) {
            insert_tdb();
        } else if (strcmp(tok,"store") == 0) {
            store_tdb();
        } else if (strcmp(tok,"show") == 0) {
            show_tdb();
        } else if (strcmp(tok,"erase") == 0) {
            tdb_traverse(tdb, do_delete_fn, NULL);
        } else if (strcmp(tok,"delete") == 0) {
            delete_tdb();
        } else if (strcmp(tok,"dump") == 0) {
            tdb_traverse(tdb, print_rec, NULL);
        } else if (strcmp(tok,"info") == 0) {
            info_tdb();
		} else if (strcmp(tok, "free") == 0) {
			tdb_printfreelist(tdb);
        } else {
            help();
        }
    }

    if (tdb) tdb_close(tdb);

    return 0;
}
