#include "tftplib.h"

int parse_readwrite_request(char* source_buff, char* filename, char* mode) {
	
	int length;

    // Check the filename
    if (sscanf(source_buff, "%" STRINGIFY(DATA_LEN) "s", filename) != 1) {
        return -1;
    }

    length = strlen(filename);
    if ( !(length > 0 && length < DATA_LEN)) {
        return -1;
    }

    // Point to the next string
    source_buff += (length + 1);

    // Check the mode
    if (sscanf(source_buff, "%" STRINGIFY(DATA_LEN) "s", mode) != 1) {
        return -1;
    }

    return 0;
}



Conn* create_new_conn(struct listhead* head,
	struct sockaddr_in client, char* filename, unsigned char READWRITE) {

	FILE* file;
	int fd;
	struct sockaddr_in server;
	socklen_t length;
	Conn* new_conn;

	// Open file for reading
	if ( (file = fopen(filename, "r")) == NULL)
		return NULL;

	// Open a new socket on any port
	if ( (fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		fclose(file);
		return NULL;
	}

	bzero(&server, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = htons(0); // use any unused port

	if ( (bind(fd, (struct sockaddr*) &server, sizeof(server))) < 0) {
		fclose(file);
		close(fd);
		return NULL;
	}

	// Allocate new connection object
	new_conn = calloc(1, sizeof(Conn));
	if (new_conn == NULL) {
		fclose(file);
		close(fd);
		return NULL;
	}

#ifdef DEBUG
	getsockname(fd, (struct sockaddr*) &server, &length);
	// printf("Port number of new connection is %d!\n", ntohs(server.sin_port));
	printf("Client connection port is %d\n", ntohs(client.sin_port));
#endif

	new_conn->client = client;
	new_conn->addr_size = sizeof(struct sockaddr_in);

	new_conn->conn_fd = fd;
	new_conn->openfile = file;

	LIST_INSERT_HEAD(head, new_conn, connections);

	return new_conn;
}

void destroy_conn(Conn* conn) {

	fclose(conn->openfile);
	close(conn->conn_fd);

	LIST_REMOVE(conn, connections);

	bzero(conn, sizeof(Conn));

	free(conn);
	conn = NULL;
}

void register_connections(struct listhead* head, fd_set* readset, int* ndfs) {

	Conn* temp_ptr;
	int current_max = 0;
	int temp_fd;

	FD_SET(*ndfs, readset);

	// copy value
	current_max = *ndfs;

	for (temp_ptr = head->lh_first; temp_ptr != NULL; temp_ptr = temp_ptr->connections.le_next) {
		temp_fd = temp_ptr->conn_fd;
		FD_SET(temp_fd, readset);
		current_max = MAX(current_max, temp_fd);
	}

	*ndfs = (1 + current_max);
}

void respond_to_conns(struct listhead* head, fd_set* readset) {

	char buff[DGRM_LEN];
	Conn* temp_ptr;

	for (temp_ptr = head->lh_first; temp_ptr != NULL; temp_ptr = temp_ptr->connections.le_next) {

		if ( FD_ISSET(temp_ptr->conn_fd, readset) ) {
#ifdef DEBUG
            printf("Continuing transfer of fd %d at block %d!\n", temp_ptr->conn_fd, temp_ptr->block_num);
#endif       
			respond_msg_onconn(temp_ptr);
		}
	}
}

void respond_msg_onmain(int fd, struct listhead* head) {

	Conn* conn;
	char readbuff[DGRM_LEN], filebuff[DGRM_LEN], modebuff[DGRM_LEN], *temp;
	struct sockaddr_in client;
	socklen_t addr_size;
	uint16_t opcode; 

	bzero(&client, sizeof(client));

	addr_size = sizeof(addr_size);

    recvfrom(fd, readbuff, DGRM_LEN, 0, 
            (struct sockaddr*) &client, &addr_size);

    temp = readbuff;

    // Read the opcode
    memcpy(&opcode, temp, sizeof(uint16_t));
    opcode = ntohs(opcode);
    temp += 2;

    switch (opcode) {
        case 1: // RRQ
            if (parse_readwrite_request(temp, filebuff, modebuff) == -1) {
            	bzero(readbuff, sizeof(readbuff));
            	format_error(readbuff, 1);
            	sendto(fd, readbuff, DGRM_LEN, 0, (struct sockaddr*) &client, addr_size);
            	return;
            }
            break;

        case 2: // WRQ
        case 3: // DATA
        case 4: // ACK
#ifdef DEBUG
        	puts("Received an ACK on main! Uh oh!");
        	return;
#endif
        case 5: // ERROR
        default:
            // These packets shouldn't be received on main, simply ignore
            return;
    }

    // If successful, send the first packet from the new connection
    conn = create_new_conn(head, client, filebuff, 1);
    if (conn != NULL) {
    	bzero(readbuff, sizeof(readbuff));
    	format_data(conn, readbuff, 0);
#ifdef DEBUG
	// printf("Successfully created new connection with fd %d!\n", conn->conn_fd);
#endif
    	transmit_msg(conn, readbuff);
    } 
    // otherwise send an error packet
    else {
    	bzero(readbuff, sizeof(readbuff));
    	format_error(readbuff, 1);
    	sendto(fd, readbuff, DGRM_LEN, 0, (struct sockaddr*) &client, addr_size);
    }
}

void respond_msg_onconn(Conn* conn) {

	char readbuff[DGRM_LEN], *temp;
	uint16_t opcode, blocknum;
	int n; // unused, would be meant to measure recvfrom packet size
	/* This pair of variables is used in case a packet comes in from the wrong port 
	 * as per the specifications of the single non-critical error condition */
	struct sockaddr_in client;
	socklen_t addr_size;

	addr_size = sizeof(client);

	n = recvfrom(conn->conn_fd, readbuff, sizeof(readbuff), 0, 
		(struct sockaddr*) &(client), &(addr_size));

	// This is a non-critical error, return error to originating host
	if ( client.sin_port != conn->client.sin_port) {
    	bzero(readbuff, sizeof(readbuff));
    	format_error(readbuff, 0);
    	sendto(conn->conn_fd, readbuff, DGRM_LEN, 0, 
    		(struct sockaddr*) &client, addr_size);
		return;
	}

    temp = readbuff;

    // Read the opcode
    memcpy(&opcode, temp, sizeof(uint16_t));
    opcode = ntohs(opcode);
    temp += 2;

    // receiving DATA only happens on write, don't need to implement
    switch (opcode) {
    	case 3: // DATA
    		destroy_conn(conn);
    		return;
/*
 *         2 bytes     2 bytes
 *         ---------------------
 *        | Opcode |   Block #  |
 *         ---------------------
 */
		case 4: // ACK
			memcpy(&blocknum, temp, sizeof(uint16_t));
			blocknum = ntohs(blocknum);

			/* 
			 * If this ACK is for a read which was less than 
			 * 512 bytes, it signals the ACK of the final
			 * transmission and the connection can be closed.
			 * Otherwise, continue transmission.
			 */
			if (conn->prev_read_size < DATA_LEN) {
				
				destroy_conn(conn);
				return;

			} else {
				bzero(readbuff, sizeof(readbuff));
				// If the block numbers match, send the next block
				// Otherwise retransmit
				format_data(conn, readbuff, (blocknum != conn->block_num));
				transmit_msg(conn, readbuff);
				return;
			}

		case 5: // ERROR
			destroy_conn(conn);
			return;

		default:
			destroy_conn(conn);
			return;
    }
}

void timeout_connections(struct listhead* head) {

	char buff[DGRM_LEN];
	Conn* temp_ptr;

	for (temp_ptr = head->lh_first; temp_ptr != NULL; temp_ptr = temp_ptr->connections.le_next) {

		// If the timeout has been exceeded, terminate node
		if (temp_ptr->timeout_counter > TIMEOUT_LIMIT) {

#ifdef DEBUG
			printf("Destroying connection held by fd %d!\n", temp_ptr->conn_fd);
#endif

			destroy_conn(temp_ptr);

		// Else increment the timeout counter and retransmit
		} else {

			temp_ptr->timeout_counter++;
			bzero(buff, sizeof(buff));

			format_data(temp_ptr, buff, 1);
			transmit_msg(temp_ptr, buff);

		}
	}
}

void transmit_msg(Conn* conn, char* buff) {

	/* 
	 * Use prev_read_size and not a flat amount because the final
	 * packet transmission needs to be less than 516 and the 
	 * protocol will infer the final packet from the size 
	 */
	sendto(conn->conn_fd, buff, conn->prev_read_size + 4, 0, 
		(struct sockaddr*) &(conn->client), conn->addr_size);

}

void format_data(Conn* conn, char* buff, int retransmit) {


	uint16_t code;
	size_t n;

	// Write opcode
	code = htons(3);
	memcpy(buff, &code, sizeof(uint16_t));
	buff += 2;

	if (retransmit == 0)
		conn->block_num++;

	// Write block number
	code = htons(conn->block_num);
	memcpy(buff, &code, sizeof(uint16_t));
	buff += 2;

	// Either copy next segment or recopy old segment
	if (retransmit == 0) {

		n = fread(buff, 1, DATA_LEN, conn->openfile);
		conn->prev_read_size = n;

	} else if (retransmit == 1) {

		n = fseek(conn->openfile, -(conn->prev_read_size), SEEK_CUR);

		n = fread(buff, 1, conn->prev_read_size, conn->openfile);
		conn->prev_read_size = n;

	}

#ifdef DEBUG
	printf("First ten chars: %*.*s\n", 10, 10, buff);
#endif
}

void format_error(char* buff, uint16_t error_code) {
  
    uint16_t error;
    char* error_message;
    
    // Copy opcode into buffer  
    error = htons(5);
    memcpy(buff, &error, sizeof(uint16_t));

    // Copy error code into buffer
    buff += 2;
    error = htons(error_code);
    memcpy(buff, &error, sizeof(uint16_t));

    // Write a basic string into message
    buff += 2;
    switch (error_code) {
        case 1:
            error_message = "File not found!";
            break;
        case 2:
        	error_message = "Access violation!";
        	break;
    	case 3:
    		error_message = "Disk full or allocation exceeded!";
    		break;
		case 4:
			error_message = "Illegal TFTP operation!";
			break;
		case 5:
			error_message = "Unknown transfer ID!";
			break;
		case 6:
			error_message = "File already exists!";
			break;
		case 7:
			error_message = "No such user!";
			break;
        default:
            error_message = "Some other error happened!";
            break;
    }

    strcpy(buff, error_message);
}