#include <netdb.h>
#include <string.h>
#include <printf.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <stdio.h>

// servname as the port server listen on.
#define PORT "80"

// how many pending connections queue will hold waiting for accept.
#define BACKLOG 10

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
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main() {
    /*
     * prepare addrinfo hints for creating socket for server.
     * prepare addrinfo servinfo for holding one or more results.
     */
    struct addrinfo hints, *servinfo;

    /*
     * correctly initialized hints by:
     * on input, certain fields must be initialized to 0,
     *
     * indicate socket intended for bind, namespace address format,
     * style of communication, and protocol to carry out communication.
     */
    memset(&hints, 0, sizeof hints);
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;

    /*
     * call getaddrinfo to obtain one or more sets of socket address,
     * and associated information to be used to create the socket for the service.
     *
     * And do error handling.
     */
    int rv;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        // error control
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    /*
     * prepare a type of pointer to addrinfo to hold the one meaningful server-socket we want.
     * prepare a server-socket-fd to hold the file descriptor of that server-socket.
     * prepare an int of 1 for turning on socket option
     *
     * loop through to condition out a meaningful server-socket by:
     * successfully create fd or not,
     * can setsockopt or not, if not we should exit(1),
     * successfully bind or not,
     * when condition all pass, we find our server-socket and bound it, break loop,
     *
     * or loop through addrinfo but could not create valid socket and of course nothing to bind,
     * now the p is NULL.
     */
    struct addrinfo *p;
    int servsockfd;
    int yes = 1;

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((servsockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            // error control.
            perror("server: socket");
            continue;
        }

        if (setsockopt(servsockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            // error control.
            perror("setsockopt");
            exit(1);
        }

        if (bind(servsockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(servsockfd);
            // error control.
            perror("server: bind");
            continue;
        }

        break;
    }

    /*
     * free servinfo that had served its purpose.
     */
    freeaddrinfo(servinfo);

    /*
     * check for a scenario that loop through completely with no error,
     * and no valid socket to create and to bind, so p is NULL.
     */
    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    /*
     * listen on socket listen for socket connections and limit the queue of incoming connections
     */
    if (listen(servsockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    /*
     * reaping zombie processes that appear as the fork()ed child processes exit.
     */
    struct sigaction sa;
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connections...\n");

    /*
     * a loop that runs indefinitely to accept connection, receive and send data,
     * and accept the next pending connection and so on.
     *
     * prepare a struct sockaddr_storage that is large enough,
     * to hold both ipv4 and ipv6 address of the connecting socket of client.
     * prepare a socklen_t to hold the size of so say socket address.
     *
     * prepare an int client-socket-fd to hold the return value of accept().
     * accept connection with server-socket-fd, pointer to sockaddr_storage type cast to pointer to sockaddr,
     * and the length set to socklen_t,
     * then obtain the return value fd to store in clisockfd.
     *
     * checking for client-sock-fd creation error.
     */
    struct sockaddr_storage clisockaddr;
    socklen_t address_len;

    int clisockfd;

    char s[INET6_ADDRSTRLEN];

    while(1){
        address_len = sizeof clisockaddr;
        clisockfd = accept(servsockfd, (struct sockaddr *)&clisockaddr, &address_len);

        if (clisockfd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(clisockaddr.ss_family,
                  get_in_addr((struct sockaddr *)&clisockaddr),
                  s, sizeof s);
        printf("server: got connection from %s\n", s);

        if (!fork()) { // this is the child process
            close(servsockfd); // child doesn't need the listener
            if (send(clisockfd, "Hello, world!", 13, 0) == -1)
                perror("send");
            close(clisockfd);
            exit(0);
        }
        close(clisockfd);  // parent doesn't need this
    }
}
