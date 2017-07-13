#include <stdio.h>
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <netinet/in.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <netdb.h>
#include <pthread.h>

#define BACKLOG 10
#define MAX_REQUEST 1024
#define MAX_URL 1024
#define MAX_OBJECT 500000
#define WEBPORT 80

struct args {
    int id;
    int connfd;
    char buffer[MAX_REQUEST];
    char ip[20];
};

struct req {
    char request[MAX_REQUEST];
    char method[16];
    char path[MAX_URL];
    char version[16];
    char host[MAX_URL];
    char page[MAX_URL];
};

struct cache {
    char URL[MAX_URL];
    char object[MAX_OBJECT];
};

void *handleConnection(void *args);

FILE *proxyLog; //log
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct cache mycache[10]; //5MB 캐시
int cacheIndex = 0;

int main(int argc, char *argv[]) {
    int proxyfd;
    struct sockaddr_in proxy_addr;

    int port_no = atoi(argv[1]);
    int threadID = 0;

    if (argc < 2) {
        perror("<Usage> ./proxy <port_no>");
        exit(0);
    }
    //socket
    if((proxyfd = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(0);
    }

    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_port = htons(port_no);
    proxy_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    //bind
    if(bind(proxyfd, (struct sockaddr *)&proxy_addr, sizeof(struct sockaddr)) == -1) {
        perror("bind");
        exit(0);
    }
    if(listen(proxyfd, BACKLOG) == -1) {
        perror("listen");
        exit(0);
    }

    //accept loop
    while(1) {
        int new_fd;
        struct sockaddr_in cli_addr;
        struct args* myargs;
	    socklen_t sin_size = sizeof(struct sockaddr_in);
        myargs = (struct args*) malloc(sizeof(struct args));

	    char *temp;

        proxyLog = fopen("proxy.log", "a");

        if((new_fd = accept(proxyfd, (struct sockaddr *) &cli_addr, &sin_size)) == -1) {
            perror("accept");
            exit(0);
        }
        myargs->connfd = new_fd;

        getpeername(new_fd, (struct sockaddr *)&cli_addr, &sin_size);
        temp = inet_ntoa(cli_addr.sin_addr);
    	strcpy(myargs->ip, temp);

        pthread_t thread;
        threadID++;
        myargs->id = threadID;

        //Thread 생성
        pthread_create(&thread, NULL, handleConnection, myargs);
        pthread_detach(thread);
    }
    close(proxyfd);
    return 0;
}

void *handleConnection(void *args) {

    int i, j;

    struct args* myarg = (struct args*) args;
    struct req* myreq;
    int bytesRead = 0;
    char *temp;
    char host[MAX_URL], page[MAX_URL];

    int chunkRead;
    char data[1024];
    char reqBuffer[MAX_REQUEST];
    int totalBytesWritten = 0;
    int chunkWritten = 0;

    char totalData[500000];


    //get Request Message
    myreq = (struct req*) malloc(sizeof(struct req));
    printf("thread %d created\n",myarg->id);
    memset(myarg->buffer, 0, MAX_REQUEST);
    bytesRead = read(myarg->connfd, myarg->buffer, sizeof(myarg->buffer));

    if(strcmp(myarg->buffer, "") == 0){
	    printf("There is no data\n");
	    pthread_exit(NULL);
    }

	strncpy(myreq->request, myarg->buffer, MAX_REQUEST - 1);
	strncpy(myreq->method, strtok_r(myarg->buffer, " ", &temp), 15);
	strncpy(myreq->path, strtok_r(NULL, " ", &temp), MAX_URL - 1);
	strncpy(myreq->version, strtok_r(NULL, "\r\n", &temp), 15);
	sscanf(myreq->path, "http://%99[^/]%99[^\n]", host, page);
	strncpy(myreq->page, page,MAX_URL - 1);
	strncpy(myreq->host, host,MAX_URL - 1);
	printf("method : %s\n path : %s\nversion : %s\npage : %s\nhost : ",myreq->method,myreq->path,myreq->version,myreq->page,myreq->host);


    int serv_sock;
    struct sockaddr_in serv_addr;
    struct hostent *server;


    //Server와 소켓연걸
	bzero(reqBuffer, MAX_REQUEST);
	serv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	server = gethostbyname(host);
	if (server == NULL) {
		printf("No host\n");
		exit(0);
	}
	memset((char *)&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	memmove((char *)&serv_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
	serv_addr.sin_port = htons(WEBPORT);
	if (connect(serv_sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) == -1){
		printf("Error Connecting\n");
		exit(0);
	}

	strcat(reqBuffer, myreq->method);
	strcat(reqBuffer, " ");
	strcat(reqBuffer, myreq->page);
	strcat(reqBuffer, " ");
	strcat(reqBuffer, myreq->version);
	strcat(reqBuffer, "\r\n");
	strcat(reqBuffer, "host: ");
	strcat(reqBuffer, host);
	strcat(reqBuffer, "\r\n");
	strcat(reqBuffer, "\r\n");

    //Find Cache
    pthread_mutex_lock(&mutex);

    int cacheFlag = 0;

    if(cacheIndex > 0) {
        if(cacheIndex < 10) {
            for(i = 0; i < cacheIndex; i++) {
                if(strcmp(myreq->path, mycache[i].URL) == 0) {
                    if((chunkWritten = write(myarg->connfd, mycache[i].object, sizeof(mycache[i].object))) < 0) {
                        perror("write");
                        exit(1);
                    }
                    cacheFlag = 1;
                    strcpy(totalData, mycache[i].object);
                    for(j = i; j < cacheIndex - 1; j++) {
                        mycache[j] = mycache[j + 1];
                    }
                    strcpy(mycache[cacheIndex - 1].object, totalData);
                    strcpy(mycache[cacheIndex - 1].URL, myreq->path);
                    break;
                }
            }
        }
        else {
            for(i = 0; i < 10; i++) {
                if(strcmp(myreq->path, mycache[i].URL) == 0) {
                    if((chunkWritten = write(myarg->connfd, mycache[i].object, sizeof(mycache[i].object))) < 0) {
                        perror("write");
                        exit(1);
                    }
                    cacheFlag = 1;
                    strcpy(totalData, mycache[i].object);
                    for(j = i; j < 9; j++) {
                        mycache[j] = mycache[j + 1];
                    }
                    strcpy(mycache[9].object, totalData);
                    strcpy(mycache[9].URL, myreq->path);
                    break;
                }
            }
        }
    }


    //Server에서 Response message를 받아 clinet에 전송하고 Cache에 저장
    if(!cacheFlag) {
        bzero(totalData, 500000);

        chunkRead = write(serv_sock, reqBuffer, strlen(reqBuffer));

    	while ((chunkRead = read(serv_sock, data, sizeof(data))) > 0) {
    		chunkWritten = write(myarg->connfd, data, chunkRead);
    		totalBytesWritten += chunkWritten;
            strcat(totalData, data);
    	}

        if(totalBytesWritten <= MAX_OBJECT) {
            if(cacheIndex < 10) {
                strcpy(mycache[cacheIndex].URL, myreq->path);
                strcpy(mycache[cacheIndex].object, totalData);
                cacheIndex++;
            }
            else {
                for(i = 0; i < 9; i++) {
                    mycache[i] = mycache[i + 1];
                }
                strcpy(mycache[9].URL, myreq->path);
                strcpy(mycache[9].object, totalData);
            }

         }
    }

    for(i = 0; i < 10; i++) {
        printf("proxy : %s\n", mycache[i].URL);
    }

    //로그 생성
    time_t result;
    result = time(NULL);
    struct tm* brokentime = localtime(&result);


    char *servIP = inet_ntoa(serv_addr.sin_addr);

    proxyLog = fopen("proxy.log", "a");
    if(cacheFlag) {
        fprintf(proxyLog,"%s %s %s %d\n",asctime(brokentime), myarg->ip, myreq->path, totalBytesWritten);
        printf("%s %s %s %d\n",asctime(brokentime), myarg->ip, myreq->path, totalBytesWritten);
    }
    else {
        fprintf(proxyLog,"%s %s %s %d\n",asctime(brokentime), servIP, myreq->path, totalBytesWritten);
        printf("%s %s %s %d\n",asctime(brokentime), servIP, myreq->path, totalBytesWritten);
    }

    fclose(proxyLog);
    pthread_mutex_unlock(&mutex);
    printf("completed sending %s at %d bytes at %s\n------------------------------------------------------------------------------------\n", myreq->page, totalBytesWritten, asctime(brokentime));
    close(serv_sock);
    close(myarg->connfd);
    printf("Thread %d exits\n", myarg->id);
    pthread_exit(NULL);
}
