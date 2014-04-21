# Trivial TFTP Server with Binary File Transfer Protocol Capability

Jeffrey Rabinowitz
Network Centric Programming
April 5 2014

-------------------------------------------------------------------------------

## Assignemnt 

Implement the RRQ function for binary mode in an interactive server. This server
should eventually be able to serve multiple connections concurrently using
I/O multiplexing instead of threads or processes. (No threads or processes allowed.)
Compare the differences in program design and performance to using threads.

-------------------------------------------------------------------------------

## Plan of Action

In order to implement a concurrent I/O multiplexing server with timeouts, it is
necessary to maintain a list of connections and upon a timeout, destroy the list.
The strategy I will employ here is to share a "global" timeout across all connections
such that if the I/O call times out, all connections are closed and cleaned up.
This iterative process can thus be summarized as:

0a. Open listening port
0b. Initialize empty list of active connections and a multiplexing object
1.  Multiplex on all active connections, plus the listening port
2a. If connection(s) are received, handle them
2b. If no connections are received, increment timeout counters and retransmit all
3.  Clean up any timed out or finished connections
4.  Go to (1) 

The identity of an incoming packet should attempt to be determind by whether it's
familiar or unfamiliar. If the packet is arriving on the primary listening port's
file descriptor, it does not represent a prior connection, and should be handled
appropriately. If it is from a prior connection, the appropriate response should
be generated. 

### Packets Arriving on Primary Port

If a packet arrives on the primary port, it can only be interpreted as a new request.
If the request is a WRQ, it should be ignored and an error packet returned. If the request
is an ERROR, DATA, or ACK packet, it can safely be ignored, although it would be courteous
to respond with an ERROR packet. If the request is a RRQ, a new connection should be created
and a packet sent.

#### Handling a RRQ

If a RRQ arrives, a new connection should object should attempt to be created. On success,
send the appropriate packet and add the connection object to a list of connection objects.

### Packets Arriving on Other Ports

If a packet arrives on another port, because this server only implements the RRQ protocol,
that packet can only be legitimately an ACK or an ERROR. If it is an ERROR, the connection
should be found and terminated. If it is an ACK, the connection should be found and the
appropriate response generated. 

#### Sending Final Packet

If a final packet gets sent (one with 0 bytes of data), the server should still keep the
connection alive until it either times out or a confirmation is received. This is considered
Normal Termination.

-------------------------------------------------------------------------------

## Gotchas

I sometimes forgot to call addr_size = sizeof(struct sockaddr_in) before calling recvfrom,
in which case I wouldn't always get the port number.