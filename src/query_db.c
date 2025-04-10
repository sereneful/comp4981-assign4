/* query_db.c */
#include "server.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int main(void)
{
    DBM  *db;
    datum key;

    db = dbm_open("requests_db", O_RDONLY, 0);
    if(db == NULL)
    {
        perror("dbm_open");
        exit(EXIT_FAILURE);
    }

    for(key = dbm_firstkey(db); key.dptr != NULL; key = dbm_nextkey(db))
    {
        /* Declare value in the loop to limit its scope */
        datum value = dbm_fetch(db, key);
        printf("Key: %s\n", key.dptr);
        if(value.dptr)
        {
            printf("Value: %s\n\n", value.dptr);
        }
    }

    dbm_close(db);
    return 0;
}
