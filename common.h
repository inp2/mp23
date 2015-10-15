#include <limits.h>
#include <assert.h>
#include <arpa/inet.h>

#define MAX_PAYLOAD 1472
#define MAX_WINDOW 64 
#define MAX_BUF_LEN (1024 * 128)
//#define MAX_BUF_LEN 100

#define KB 1024
#define MB (1024 * 1024)

#define RECV_TIMEOUT 120000 

//#define SS_THRESH 2048

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b)) 
#endif 

typedef unsigned long long seq_t;

typedef struct {
	seq_t seqnum;
	size_t size; 	
} header_t;

#define PAYLOAD_SIZE (MAX_PAYLOAD - sizeof (header_t))

typedef struct {
	header_t header;
	char payload[PAYLOAD_SIZE];
} packet_t; 

typedef struct {
	unsigned buf_base;
	seq_t win_base; 
} window_t;

typedef struct {
	int acked;
	packet_t *pkt;
} segment_t;

typedef struct {
	// window_t
	unsigned buf_base;
	seq_t win_base;
	// end window_t

	seq_t prev_ack;
	unsigned dup_acks;
	

	int in_ss; // flag for being in slow start
	// used to increment window size in cong. avoidance phase
	size_t frac;
	size_t ssthresh;
	segment_t *segments[MAX_BUF_LEN];
	seq_t min_to_send;	// minimum seq num to send
	size_t win_size; 
	int sockfd;
	struct addrinfo *dest; 
} sender_t;

typedef struct {
	// window_t
	unsigned buf_base;
	seq_t win_base;
	// end window_t

	packet_t *buf[MAX_BUF_LEN]; 
	int sockfd;
	struct sockaddr_storage src_addr; 
	socklen_t src_addrlen;

	int closed;
} receiver_t;

unsigned seq_to_buf(window_t* win, seq_t seq)
{ 
	return (win->buf_base + (seq - win->win_base)) % MAX_BUF_LEN;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
