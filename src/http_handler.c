/* http_handler.c */
#include "server.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void handle_http_request(int clientSock, const char *req);

/* Local helper prototypes */
static int serve_file(int clientSock, const char *method, const char *path);
static int handle_post(int clientSock, const char *req);

/*
 * handle_http_request - Process an HTTP request.
 * @clientSock: Client socket descriptor.
 * @req:        The HTTP request string.
 *
 * Supports GET, HEAD, and POST.
 * GET and HEAD requests serve files from the "./www" directory.
 * POST requests store the request body in an ndbm database under a unique key.
 */
void handle_http_request(int clientSock, const char *req)
{
    char method[MAX_METHOD_LENGTH];
    char path[MAX_PATH_LENGTH];
    int  ret;

    memset(method, 0, sizeof(method));
    memset(path, 0, sizeof(path));

    /* Parse the request line (method and path) */
    if(sscanf(req, "%7s %255s", method, path) != 2)
    {
        send(clientSock, HTTP_405_MSG, strlen(HTTP_405_MSG), 0);
        return;
    }

    if((strncmp(method, "GET", 3) == 0) || (strncmp(method, "HEAD", 4) == 0))
    {
        ret = serve_file(clientSock, method, path);
        if(ret != 0)
        {
            /* Error response already sent in serve_file */
        }
    }
    else if(strncmp(method, "POST", 4) == 0)
    {
        ret = handle_post(clientSock, req);
        if(ret != 0)
        {
            /* Error response already sent in handle_post */
        }
    }
    else
    {
        send(clientSock, HTTP_405_MSG, strlen(HTTP_405_MSG), 0);
    }
}

/* Serve files for GET and HEAD requests */
static int serve_file(int clientSock, const char *method, const char *path)
{
    char  fullPath[MAX_FULLPATH_LENGTH];
    FILE *fp;
    long  fileSize;
    char  header[BUF_SIZE];

    /* Construct full file path: "./www" + requested path */
    snprintf(fullPath, sizeof(fullPath), "./www%s", path);

    /* Use mode "rbe" if supported to set O_CLOEXEC */
    fp = fopen(fullPath, "rbe");
    if(fp == NULL)
    {
        send(clientSock, HTTP_404_MSG, strlen(HTTP_404_MSG), 0);
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* Prepare and send HTTP header */
    snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\nContent-Type: text/html\r\n\r\n", fileSize);
    send(clientSock, header, strlen(header), 0);

    /* For GET requests, send file contents; for HEAD, send header only */
    if(strncmp(method, "HEAD", 4) != 0)
    {
        size_t readBytes;
        char   fileBuffer[BUF_SIZE];
        while((readBytes = fread(fileBuffer, 1, BUF_SIZE, fp)) > 0)
        {
            send(clientSock, fileBuffer, readBytes, 0);
        }
    }
    fclose(fp);
    return 0;
}

/* Handle POST request by storing the body in an ndbm database */
static int handle_post(int clientSock, const char *req)
{
    DBM        *db;
    datum       key;
    datum       value;
    const char  dbFile_const[] = "requests_db";
    const char *dbFile         = dbFile_const; /* dbFile is a pointer to const */
    char        uniqueKey[UNIQUE_KEY_SIZE];
    char       *body;
    time_t      now;
    int         ret;

    /* Locate the separation between header and body */
    body = strstr(req, "\r\n\r\n");
    if(body == NULL)
    {
        send(clientSock, HTTP_405_MSG, strlen(HTTP_405_MSG), 0);
        return 1;
    }
    body += POST_DELIM_OFFSET;

    /* Create a unique key for this POST request using the current timestamp */
    now = time(NULL);
    snprintf(uniqueKey, sizeof(uniqueKey), "post_%ld", now);

    /* Disable the cast qualifier warning for the call to dbm_open */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
    db = dbm_open((char *)dbFile, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
#pragma clang diagnostic pop

    if(db == NULL)
    {
        perror("dbm_open");
        send(clientSock, "HTTP/1.1 500 Internal Server Error\r\n\r\n", INTERNAL_ERROR_MSG_LENGTH, 0);
        return 1;
    }

    key.dptr    = uniqueKey;
    key.dsize   = (datum_length)(strlen(uniqueKey) + 1);
    value.dptr  = body;
    value.dsize = (datum_length)(strlen(body) + 1);
    ret         = dbm_store(db, key, value, DBM_INSERT);
    if(ret != 0)
    {
        perror("dbm_store");
        dbm_close(db);
        send(clientSock, "HTTP/1.1 500 Internal Server Error\r\n\r\n", INTERNAL_ERROR_MSG_LENGTH, 0);
        return 1;
    }
    dbm_close(db);

    /* Send a success response */
    send(clientSock, HTTP_OK_MSG, strlen(HTTP_OK_MSG), 0);
    return 0;
}
