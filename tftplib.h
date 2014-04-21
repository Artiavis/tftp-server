/*
 * Jeffrey Rabinowitz
 * Network Centric Programming Assignment 3
 * Trivial File Transfer Protocol Server
 * April 5, 2014
 *
 */

#include <arpa/inet.h>
#include <ctype.h>
//#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


// DECLARE DEFINITIONS HERE

/* Datagram length for data transfer is capped at 516 bytes */
#define DGRM_LEN 516
/* The "data" block of a datagram is exactly 512 bytes */
#define DATA_LEN 512
/* Define an arbitrary timeout limit for connections */
#define TIMEOUT_LIMIT 10

#define STRINGIFY(x) STRINGIFY2(x) // stringify macros for printing macro numbers
#define STRINGIFY2(x) #x

// DECLARE STRUCTS HERE


/*
 * The following structure is used to represent an active connection.
 * It contains references to the file it is in the midst of transferring
 * as well as the file descriptor which it is transferring over.
 * In order to ensure a reliable datagram flow, a reference is kept to its
 * own block number so that the server can move in lockstep with the client.
 *
 * A timeout counter is kept in order to ensure that a connection is aborted
 * if too many timeouts occur. A timeout occurs whenever the I/O multiplexing
 * server times out, in which case it iterates over all connections, increments
 * their timeout counters, and then discards connections which have timed out
 * too many times. 
 *
 * Ordinarily, all reads are 512 bytes, and if a packet gets lost, one can rewind
 * by 512 bytes on the openfile_fd and reread the lost data.
 * However, in the edge case where the final DATA packet of a transfer gets dropped
 * and a retransmit is necessary, the amount of data is most likely less than 512
 * bytes. Therefore, on all transfers, keep track of the amount of data sent on the
 * last transmit, in case a timeout occurs and the data must be retransmitted.
 */ 
struct _Conn {
	int conn_fd; // connection fd for identifying a connection
	FILE* openfile; // file for reading/writing 
	uint16_t block_num; // block number identifier
	unsigned char timeout_counter; // counter for identifying timeout conditions
	uint16_t prev_read_size; // measure prev read size in case need to retransmit

	/* Need to hold a reference to previous client in order to retransmit.
	 * There's no concern that client will be null because this struct is populated
	 * based off an incoming request.
	 */
	struct sockaddr_in client;
	socklen_t addr_size;

	LIST_ENTRY(_Conn) connections;
};

typedef struct _Conn Conn;

LIST_HEAD(listhead, _Conn) list;

// DECLARE FUNCTIONS HERE

/*
 * Parses an identified RRQ/WRQ packet for its arguments as in the following
 * format specification:
 *
 * Returns -1 on a parsing error and 0 on successful parsing.
 *
 *          2 bytes    string   1 byte     string   1 byte
 *         -----------------------------------------------
 *  RRQ/  | 01/02 |  Filename  |   0  |    Mode    |   0  |
 *  WRQ    -----------------------------------------------
 *  
 */
int parse_readwrite_request(char* source_buff, char* filename, char* mode);

/*
 * Formats the error specified by error_code into buf
 *
 *          2 bytes  2 bytes        string    1 byte
 *         ----------------------------------------
 *  ERROR | 05    |  ErrorCode |   ErrMsg   |   0  |
 *         ----------------------------------------
 *   Error Codes
 *   
 *     Value     Meaning
 *   
 *     0         Not defined, see error message (if any).
 *     1         File not found.
 *     2         Access violation.
 *     3         Disk full or allocation exceeded.
 *     4         Illegal TFTP operation.
 *     5         Unknown transfer ID.
 *     6         File already exists.
 *     7         No such user.
 */
void format_error(char* buff, uint16_t error_code);


/*
 * Respond to an ACK packet sent by a RRQ client by transmitting the next portion
 * of the file being read in a DATA packet, or to a timeout by retransmitting the
 * previous chunk.
 */
void format_data(Conn* conn, char* buff, int retransmit);

/* 
 * Allocate and create a new Conn connection object, and attach it to the existing list. 
 * Given the name of a file and
 * whether it should be opened for reading or writing, either attempt to open
 * an existing file for reading, or attempt to open a new file for writing.
 * (Writing functionality has not been implemented.)
 * 
 * On success, return a new Conn structure with its conn_fd, openfile_fd initialized
 * and its other fields set to 0. 
 *
 * On error, return NULL. This could occur for any number of reasons, including 
 * inability to allocate memory and inability to open files.
 */
Conn* create_new_conn(struct listhead* head,
	struct sockaddr_in client, char* filename, unsigned char READWRITE);

/* Set to zero and deallocate an existing Conn connection object. */
void destroy_conn(Conn* conn);

/*
 * Call this to register all the file desciptors across all connections.
 * They will be registered to readset. ndfs is set in this function as well.
 * To use this, copy the file descriptor of the main listening socket into
 * an int to use for ndfs. The max file descriptor as required by 
 * select will be returned.
 */
void register_connections(struct listhead* head, fd_set* readset, int* ndfs);

/*
 * Send the message in buff to the address stored in conn.
 */
void transmit_msg(Conn* conn, char* buff);

/* Traverse the list of Conn structures and increment their timeouts. Then,
 * if necessary, clean up any connections which have timed out.
 */
void timeout_connections(struct listhead* head);

/*
 * Iterate over the list of open connections and test whether there is a
 * packet waiting for each connection. If so, handle appropriately.
 */
void respond_to_conns(struct listhead* head, fd_set* readset);

/*
 * These are the "main" functions of the program. They're called to handle
 * all incoming packet on any port, whether the "main" port or an ephemeral
 * port only being used for transfers. 
 *
 * On a given fd, for a given client, read and reply to the message as
 * appropriate. If an error was received on an open connection,
 * which should be a rare occurence, look up the node and delete it.
 *
 * Or, for a given connection, read the corresponding file descriptor and
 * handle its response.
 */
void respond_msg_onmain(int fd, struct listhead* head);

void respond_msg_onconn(Conn* conn);
