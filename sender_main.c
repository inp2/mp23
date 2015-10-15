#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h> 
#include <sys/time.h>

#include "common.h"


void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer);

int main(int argc, char** argv)
{
	unsigned short int udpPort;
	unsigned long long int numBytes;
	
	if(argc != 5)
	{
		fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
		exit(1);
	}
	udpPort = (unsigned short int)atoi(argv[2]);
	numBytes = atoll(argv[4]);
	
	reliablyTransfer(argv[1], udpPort, argv[3], numBytes);
		return 0;
} 

// create and prepare a segment given a buffer
// `buf` is the WHOLE buffer that a sender is asked to sent
segment_t *get_segment(seq_t seqnum, char *buf, size_t buflen, size_t processed)
{ 
	segment_t *seg = (segment_t *)malloc(sizeof (segment_t)); 
	seg->pkt = (packet_t *)malloc(sizeof (packet_t)); 

	seg->acked = 0;

	size_t seg_size = (PAYLOAD_SIZE + processed) <= buflen ?
		PAYLOAD_SIZE :
		buflen - processed;
	
	seg->pkt->header.size = seg_size;
	seg->pkt->header.seqnum = seqnum; 
	memcpy(seg->pkt->payload, buf+processed, seg_size);

	return seg;
}

// resend segment
void sender_resend(sender_t *sender, seq_t resend_seq)
{
	unsigned idx = seq_to_buf((window_t *)sender, resend_seq);
	segment_t *seg = sender->segments[idx];
	assert(seg != NULL);
	assert(!seg->acked);
	fprintf(stderr, "RE-sending packet %lldu\n", seg->pkt->header.seqnum);
	sendto(sender->sockfd, seg->pkt, sizeof (packet_t), 0,
			sender->dest->ai_addr, sender->dest->ai_addrlen); 
}

// enter cong. avoidance
void sender_enter_ca(sender_t *sender)
{
	sender->in_ss = 0;
	sender->frac = 0;
}


// deal with 3 dup acks
void sender_process_dup3(sender_t *sender, seq_t ack)
{
	// fast recovery when getting 3 dup acks 
	fprintf(stderr, "Got 3 dup acks (%lldu)\n", ack);
	sender_resend(sender, ack+1);
	sender->win_size = (sender->win_size + 1)/2;
	sender->ssthresh = sender->win_size;
	sender_enter_ca(sender);
} 

// reliably send `buf`
void sender_send(sender_t *sender, char *buf, size_t buflen)
{ 
	seq_t sq;
	size_t bytes_processed = 0; 

	fd_set fdset; 
	FD_ZERO(&fdset);
	struct timeval nowait;

	for (;;) { 
		seq_t min = sender->min_to_send,
			  max = sender->win_base + sender->win_size;
		// send as many packets as possible
		for (sq = min; sq < max && bytes_processed < buflen; sq++) { 
			sender->min_to_send++;
			// TODO: `buf_idx` can be calculated more efficiently
			unsigned buf_idx = seq_to_buf((window_t *)sender, sq);
			segment_t *seg = sender->segments[buf_idx];
//			if (seg != NULL) {
//				assert(seg->pkt->header.seqnum == sq);
//			} else { 

			assert(bytes_processed < buflen);
			seg = get_segment(sq, buf, buflen, bytes_processed); 
			// buffer unacked segment
			sender->segments[buf_idx] = seg;
			bytes_processed += seg->pkt->header.size;
			assert(bytes_processed <= buflen);
			fprintf(stderr, "Sending packet %lldu\n", seg->pkt->header.seqnum);
			sendto(sender->sockfd, seg->pkt, sizeof (packet_t), MSG_DONTWAIT,
					sender->dest->ai_addr, sender->dest->ai_addrlen);

//			}
			// poll socket in case of acks coming 
			// (it takes some time to process packets when the window is big)
			// and it's better to fill in a hole (dup acks) asap
			nowait.tv_sec = 0;
			nowait.tv_usec = 0;
			FD_SET(sender->sockfd, &fdset);
			if (select(sender->sockfd+1, &fdset, NULL, NULL, &nowait)) {
				break;
			} 

		}

		if (bytes_processed == buflen && sender->segments[sender->buf_base] == NULL) {
			// done
			break;
		} 
		// repond to ack
		seq_t ack;
		ssize_t rv = recvfrom(sender->sockfd, &ack, sizeof (seq_t), 0,
				NULL, NULL);

		if (rv > 0) {
			fprintf(stderr, "Received ack %lldu\n", ack);
			if (ack == sender->prev_ack) {
				sender->dup_acks++;
			} else {
				sender->prev_ack = ack;
				sender->dup_acks = 0;
			}
		}

		// react to ack
		if (rv < 0) {
			// timeout
			fprintf(stderr, "Packet %lldu timeout\n", sender->win_base);
			sender->ssthresh = sender->win_size / 2;
			sender->win_size = 1;
			sender->in_ss = 1;
			sender_resend(sender, sender->win_base);
		} else if (sender->dup_acks == 3) {
			sender_process_dup3(sender, ack);
		} else if (ack >= sender->win_base) {
			// acks are accumulative
			// try to slide window 
			for (sq = sender->win_base; sq <= ack; sq++) { 
				// TODO `buf_idx` can be calculated more efficiently
				unsigned buf_idx = seq_to_buf((window_t *)sender, sq);
				segment_t *seg = sender->segments[buf_idx];
				if (seg == NULL) {
					break;
				}
				free(seg->pkt);
				free(seg);
				sender->segments[buf_idx] = NULL;
			}
			sender->buf_base = seq_to_buf((window_t *)sender, sq);
			sender->win_base = sq;
			assert(sender->buf_base == seq_to_buf((window_t *)sender, sender->win_base));
		}

		// increment win size if poss.
		if (sender->win_size < MAX_WINDOW) {
			if (sender->in_ss) {
				sender->win_size++;
				if (sender->win_size >= sender->ssthresh) {
					sender_enter_ca(sender);
				}
			} else if (++sender->frac == sender->win_size) {
				// cong. avoidance
				sender->win_size++;
				sender->frac = 0;
			}
		}



#ifdef FIXEDWIN
		sender->win_size = FIXEDWIN;
#endif
	} 
}

void sender_close(sender_t *sender)
{
	// send fin and get an ack
	packet_t fin;
	fin.header.seqnum = 0;
	seq_t ack = ~0;
	fprintf(stderr, "Sent FIN, waiting for FIN_ACK\n"); 
	int retry = 0;
	do {
		sendto(sender->sockfd, &fin, sizeof (packet_t), 0,
				sender->dest->ai_addr, sender->dest->ai_addrlen);
		recvfrom(sender->sockfd, &ack, sizeof(seq_t), 0,
				NULL, NULL);
	} while(ack != 0 && retry++ < 4);
	fprintf(stderr, "Got fin-ack. Closing connection.\n");
}

void sender_init(sender_t *sender, int sockfd, struct addrinfo *dest)
{
	// set socket receive timer
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = RECV_TIMEOUT;
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void *)&tv, sizeof (struct timeval));

	memset((void *)sender, 0, sizeof (sender_t));
	sender->sockfd = sockfd;
	sender->dest = dest;
	sender->min_to_send = 1; 
	sender->win_base = 1;
	sender->win_size = 1;
	// begin with slow start
	sender->in_ss = 1;
	//sender->ssthresh = 65
	sender->ssthresh = ~0; // inf

#ifdef FIXEDWIN
	sender->win_size = FIXEDWIN;
#endif
}

void reliablyTransfer(char* hostname,
		unsigned short int hostUDPport,
		char* filename,
		unsigned long long int bytesToTransfer)
{ 
	/*
	   sender initiates connection and sends packets
	 */
	struct addrinfo hints, *servinfo, *p;
	char port_str[5];
	int sockfd;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	sprintf(port_str, "%d", hostUDPport);

	if (getaddrinfo(hostname, port_str, &hints, &servinfo)) {
		fprintf(stderr, "Unable to get host address\n");
		exit(1);
	}

	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd=socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			continue;
		}
		break;
	}

	if (p == NULL) {
		fprintf(stderr, "Unable to connect to host\n");
		exit(1);
	}

	FILE *fin;
	if ((fin=fopen(filename, "rb")) != NULL) {
		size_t num_to_send; 
		size_t buflen = 1024 * 1024 * 16;
		char *buf = (char *)malloc(sizeof (char) * buflen);
		sender_t sender;
		sender_init(&sender, sockfd, p);
		unsigned long long int bytes_sent = 0; 
		// FIXME don't send more than asked

		while ((num_to_send=fread(buf, sizeof (char), buflen, fin)) > 0) {
			if (bytes_sent + num_to_send <= bytesToTransfer) {
				sender_send(&sender, buf, num_to_send);
				bytes_sent += num_to_send;
			} else { 
				sender_send(&sender, buf, bytesToTransfer-bytes_sent);
				break;
			}
		}
		sender_close(&sender);
		freeaddrinfo(servinfo);
		fclose(fin);
	} else {
		fprintf(stderr, "Unable to open file %s\n", filename);
		freeaddrinfo(servinfo);
		exit(1);
	}
}
