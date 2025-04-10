/* worker.c */
#include "worker.h"
#include "server.h"
#include "utils.h"
#include <arpa/inet.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Local function prototype */
static int handle_client_request(int clientSock, void **libHandle, time_t *libLastMod);

__attribute__((noreturn)) void worker_loop(int workerSock)
{
    void  *libHandle;
    time_t libLastMod;

    libHandle = dlopen("libhttp.so", RTLD_NOW);
    if(!libHandle)
    {
        fprintf(stderr, "Worker: Failed to load shared library: %s\n", dlerror());
        exit(EXIT_FAILURE);
    }
    libLastMod = get_last_mod_time("libhttp.so");

    while(1)
    {
        { /* Begin inner block to narrow scope of clientFd and ret */
            int clientFd = recv_fd(workerSock);
            int ret;
            if(clientFd < 0)
            {
                perror("Worker: Failed to receive client FD");
                continue; /* Continue the while loop */
            }
            ret = handle_client_request(clientFd, &libHandle, &libLastMod);
            if(ret != 0)
            {
                fprintf(stderr, "Worker: Error processing client request\n");
            }
            if(send_fd(workerSock, clientFd) < 0)
            {
                perror("Worker: Failed to return client FD");
            }
            close(clientFd);
        } /* End inner block; clientFd and ret go out of scope here */
    }
    /* Removed unreachable dlclose(libHandle); */
}

static int handle_client_request(int clientSock, void **libHandle, time_t *libLastMod)
{
    char    reqBuf[BUF_SIZE];
    ssize_t bytesRead;
    time_t  currMod;
    void (*httpHandler)(int, const char *);

    memset(reqBuf, 0, sizeof(reqBuf));
    bytesRead = read(clientSock, reqBuf, BUF_SIZE);
    if(bytesRead < 0)
    {
        perror("Worker: read error");
        return 1;
    }

    currMod = get_last_mod_time("libhttp.so");
    if(currMod > *libLastMod)
    {
        printf("Worker: Reloading updated shared library...\n");
        if(*libHandle)
        {
            dlclose(*libHandle);
        }
        *libHandle = dlopen("libhttp.so", RTLD_NOW);
        if(!*libHandle)
        {
            fprintf(stderr, "Worker: Reload error: %s\n", dlerror());
            return 1;
        }
        *libLastMod = currMod;
    }
    httpHandler = (void (*)(int, const char *))dlsym(*libHandle, "handle_http_request");
    if(!httpHandler)
    {
        fprintf(stderr, "Worker: dlsym error: %s\n", dlerror());
        return 1;
    }

    /* Delegate the entire HTTP request processing to the shared library */
    httpHandler(clientSock, reqBuf);
    return 0;
}
