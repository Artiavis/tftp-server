/*
 * Jeffrey Rabinowitz
 * Network Centric Programming Assignemnt 3
 * Trivial File Transfer Protocol Server
 * April 6, 2014
 *
 */

#include "tftplib.h"

int main(int argc, char** argv) {

    /* Check arguments */
    if (argc != 2) 
       fprintf(stderr, "Usage: %s <PORT>\n", argv[0]),exit(0);

    // Attempt to parse argument as a number
    long arg_port = strtol(argv[1], NULL, 10);

    // If cannot be parsed, exit immediately
    if (arg_port == 0)
        printf("%s: Could not interpet port number\n",argv[0]),exit(1);
    else if(arg_port == LONG_MAX || arg_port == LONG_MIN)
        perror(argv[0]),exit(1);

    /* PROCEED TO BEGIN TFTP SERVER */

    char buff[DGRM_LEN], *temp;
    struct sockaddr_in serveraddr, clientaddr;
    socklen_t addr_size;
    int listenfd, nfds;
    unsigned short tftp_port;
    fd_set readset; // used for multiplexing
    struct timeval timeout; // used for timing out

    tftp_port = (unsigned short) arg_port;

    if ( (listenfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    	perror("Couldn't open socket!");
    	exit(-1);
    }

    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short) tftp_port);

    addr_size = sizeof(struct sockaddr_in);

    if (bind(listenfd, (struct sockaddr*) &serveraddr, sizeof(serveraddr)) < 0) {
        perror("couldn't bind socket");
        exit(-1);
    }

    // Declare a list of connections
    LIST_INIT(&list);

    for ( ; ; ) { // zoidberg

        // sleep(1);
        
        // Create select file descriptor list
        FD_ZERO(&readset);

        // Set all file descriptors
        nfds = listenfd;
        register_connections(&list, &readset, &nfds);

        /* Reset timeout structure to time out after 1 second */
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        addr_size = sizeof(struct sockaddr_in);
        memset(&clientaddr, 0, addr_size);

        // TODO FIX THIS
        nfds = select(nfds, &readset, NULL, NULL, &timeout);

        // If 0 is returned, a timeout occurred, otherwise there are readable files
        if (nfds == 0) {
#ifdef DEBUG
            puts("No incoming requests!");
#endif
            timeout_connections(&list);
        } else if (nfds > 0) {
#ifdef DEBUG
            puts("Some incoming requests!");
#endif
            // Check whether connections are available to respond
            respond_to_conns(&list, &readset);

            // If main port is available, respond on it
            if (FD_ISSET(listenfd, &readset)) {
#ifdef DEBUG
            puts("Incoming request on main port!");
#endif            
                respond_msg_onmain(listenfd, &list);
            }
        }
    }

	// This never gets reached
	exit(0); 
}