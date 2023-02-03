#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define PORT "3490"  // the port users will be connecting to

#define BACKLOG 10   // how many pending connections queue will hold

// Handle zombie processes
void sigchld_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        // IPv4
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    // IPv6
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// modify response to change Stockholm to Linkoping and Smiley to Trolly
// must change all occurences
void modifyResponse(char *res) {
    char *stockholm = "Stockholm";
    char *linkoping = "Linkoping";
    char *smiley = "Smiley";
    char *trolly = "Trolly";

    // replace all occurences of stockholm with linkoping
    char *pos = strstr(res, stockholm);
    while (pos != NULL) {
        memcpy(pos, linkoping, strlen(linkoping));
        pos = strstr(pos + strlen(linkoping), stockholm);
    }

    // replace all occurences of smiley with trolly
    pos = strstr(res, smiley);
    while (pos != NULL) {
        memcpy(pos, trolly, strlen(trolly));
        pos = strstr(pos + strlen(trolly), smiley);
    }
}

// get host from request
int parseRequest(char *request, char *host, char *port, char *path) {
    // request form :
    // GET http://www.example.com/favicon.ico HTTP/1.1
    // Host: www.example.com
    // Proxy-Connection: keep-alive
    // User-Agent: Mozilla/5.0 (X11; CrOS x86_64 14541.0.0) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/109.0.0.0 Safari/537.36
    // Accept: image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8
    // Referer: http://www.example.com/
    // Accept-Encoding: gzip, deflate
    // Accept-Language: fr-FR,fr;q=0.9,en-US;q=0.8,en;q=0.7

    // host is in the second line
    char *line1 = strtok(request, "\r");
    char *line2 = strtok(NULL, "\r");

    // // debug print
    // printf("line1: %s\n", line1);
    // printf("line2: %s\n", line2);

    // get the host
    char *host_prefix = strtok(line2, " ");
    char *host_maybe_port = strtok(NULL, " ");

    // get the port
    char *portStart = strstr(host_maybe_port, ":");
    if (portStart == NULL) {
        // if we are unable to find the port, use the default port
        strcpy(port, "80");
        strcpy(host, host_maybe_port);
    } else {
        // if we found the port
        char *host_found = strtok(host_maybe_port, ":");
        char *port_found = strtok(NULL, ":");
        strcpy(port, port_found);
        strcpy(host, host_found);
    }

    // get the path
    char *temp = strtok(line1, "/");
    char *temp2 = strtok(NULL, "/");
    char *path_with_suffix = strtok(NULL, " ");
    char *path_found = strtok(path_with_suffix, " ");
    strcpy(path, path_found);

    // // debug print
    // printf("host: %s, port: %s, path: %s\n", host, port, path);
    // printf("path_with_suffix: %s, path_found: %s\n", path_with_suffix, path_found);
}

// transferRequest takes the request buffer sent by the client and sends it to the server
// it then receives the response from the server and sends it back to the client
void transferRequest(char *request, char *host, char *port, char *path, char *res, int sockfd_client) {
    printf("[TRANSFER] Transfering request to : \n");
    printf("[TRANSFER] host: %s, port: %s, path: %s\n", host, port, path);

    // create a socket
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        // if we are unable to create a socket
        perror("client: socket");
        exit(1);
    }

    // get the address info for the server
    struct addrinfo hints, *servinfo, *p;
    int rv;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
        // if we are unable to get the address info
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return;
    }

    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            // if we are unable to connect
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        // if we were unable to connect
        fprintf(stderr, "client: failed to connect\n");
        return;
    }

    // send the request to the server
    if (send(sockfd, request, strlen(request), 0) == -1) {
        // if we are unable to send the request
        perror("send");
        exit(1);
    }

    const int BUF_SIZE = 1000000;
    char response[BUF_SIZE];
    int total_bytes = 0;
    int numbytes;

    // receive the response from the server and send it to the client at the same time
    while ((numbytes = recv(sockfd, response, BUF_SIZE-1, 0)) > 0) {
        // if we are able to receive the response
        total_bytes += numbytes;

        // debug
        printf("response: %s\n", response);

        modifyResponse(response);

        // send the response to the client
        if (send(sockfd_client, response, strlen(response), 0) == -1) {
            // if we are unable to send the response to the client
            perror("send");
            exit(1);
        }
    }

    response[numbytes] = '\0';

    if (numbytes == -1) {
        perror("recv");
        exit(1);
    }

    response[total_bytes] = '\0';

    // response now contains the full response from the server

    strcpy(res, response);

    // close the socket
    close(sockfd);
}

int main(void)
{
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p; // server info
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size; // size of their_addr
    struct sigaction sa; // for signal handling
    int yes=1; // for setsockopt() SO_REUSEADDR, below
    char s[INET6_ADDRSTRLEN]; // for printing IP address
    int rv; // return value

    memset(&hints, 0, sizeof hints); // make sure the struct is empty
    hints.ai_family = AF_UNSPEC; // don't care IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        // if we are unable to get the address info
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv)); // error message
        return 1;
    }

    // servinfo now points to a linked list of 1 or more struct addrinfos
    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        // create a socket
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            // if we are unable to create a socket
            perror("server: socket");
            continue;
        }

        // lose the pesky "address already in use" error message
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            // if we are unable to set the socket options, we exit
            perror("setsockopt");
            exit(1);
        }

        // bind the socket to the port we passed in to getaddrinfo()
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            // if we are unable to bind the socket
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break; // if we get here, we must have connected successfully
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  {
        // if we were unable to bind
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        // if we are unable to listen
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask); // block all signals
    sa.sa_flags = SA_RESTART; // restart functions if interrupted by handler
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        // if we are unable to set the signal handler
        perror("sigaction");
        exit(1);
    }

    // if we get here, we must have connected successfully, server is ready
    printf("server: listening on port 3490...\n");

    while(1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size); // accept a connection
        if (new_fd == -1) {
            // if we are unable to accept a connection
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr),
            s, sizeof s); // convert IP address to string and store in s
        printf("server: got connection from %s\n", s);

        if (!fork()) { // this is the child process
            close(sockfd); // child doesn't need the listener
            
            // print the request
            char buf[1000];
            int numbytes;
            if ((numbytes = recv(new_fd, buf, 1000-1, 0)) == -1) {
                perror("recv");
                exit(1);
            }
            buf[numbytes] = '\0';

            // transfer the request to the requested server
            // copy buf to a new string
            char *request = (char *)malloc(1000);
            strcpy(request, buf);

            char host[1000];
            char port[1000];
            char path[1000];

            parseRequest(buf, host, port, path);

            char *res = (char *)malloc(2000);

            printf("[FORK %s] server : request is %s:%s/%s...\n", path, host, port, path);

            // if path is favicon.ico, send back a 404
            if (strcmp(path, "favicon.ico") == 0) {
                strcpy(res, "HTTP/1.1 404 Not Found\r\n\r");
                printf("favicon.ico requested, sending 404\r");
            } else {
                transferRequest(request, host, port, path, res, new_fd);
                printf("[FORK %s] server : sending response to client...\n", path);
            }

            // send back res to client
            if (send(new_fd, res, strlen(res), 0) == -1) {
                perror("send");
                printf("error sending back response");
                exit(1);
            }

            printf("[FORK %s] server : sent response to client.\n", path);

            close(new_fd);
            exit(0);
        }
        close(new_fd); // parent doesn't need this
    }

    return 0; // we should never get here
}