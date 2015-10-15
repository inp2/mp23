#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "common.h"


void reliablyReceive(unsigned short int myUDPport, char* destinationFile);

int main(int argc, char** argv)
{
	unsigned short int udpPort;

	if(argc != 3)
	{
		fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
		exit(1);
	}

	udpPort = (unsigned short int)atoi(argv[1]);

	reliablyReceive(udpPort, argv[2]);
	return 0;
}

void receiver_init(receiver_t *receiver, int sockfd)
{
	memset(receiver, 0, sizeof (receiver_t));
	receiver->sockfd = sockfd;
	receiver->src_addrlen = sizeof (struct sockaddr_storage);
	// sequence number starts from 0
	receiver->win_base = 1;
}

/*
   return number of bytes received
   `buf` will be filled with INORDER packets if on of following happens
   (note that unorderred packets will be buffered):
   * enough number of bytes received
   * fin packet received
*/
size_t receiver_recv(receiver_t *receiver, char *buf, size_t length)
{
	if (receiver->closed) {
		return 0;
	}

	// number of IN-ORDER packets written to buf
	size_t buflen = 0;

	for (;;) { 
		// receive a packet
		packet_t *pkt = (packet_t *)malloc(sizeof (packet_t));
		recvfrom(receiver->sockfd, pkt, sizeof (packet_t), MSG_WAITALL,
				(struct sockaddr *)&receiver->src_addr, &receiver->src_addrlen); 
		fprintf(stderr, "Received packet %lldu\n", pkt->header.seqnum);
		if (pkt->header.seqnum >= receiver->win_base) {
			// received regular packets 
			unsigned buf_idx = seq_to_buf((window_t *)receiver, pkt->header.seqnum); 
			if (receiver->buf[buf_idx] == NULL) {
				receiver->buf[buf_idx] = pkt;
			} else {
				free(pkt);
			}
		} else if (pkt->header.seqnum == 0) {
			// got fin packet
			seq_t finack = 0;
			sendto(receiver->sockfd, &finack, sizeof (seq_t), 0,
					(struct sockaddr *)&receiver->src_addr, receiver->src_addrlen);
			fprintf(stderr, "Received and acked FIN packet\n");
			receiver->closed = 1;
			goto forward;
		}
		assert(seq_to_buf((window_t *)receiver, receiver->win_base) == receiver->buf_base);

		// try to slide window and save packets to buffer 
		// i.e. iterate over buffered packets until an unacked packet is seen
		pkt = receiver->buf[receiver->buf_base];
		while (pkt != NULL) { 
			size_t pkt_size = pkt->header.size,
				   newlen = buflen + pkt_size;
			if (newlen > length) {
				// got more than the user wants
				goto forward;
			}

			memcpy(buf+buflen, pkt->payload, pkt->header.size); 
			free(pkt);
			receiver->buf[receiver->buf_base] = NULL; 
			buflen = newlen;
			fprintf(stderr, "Wrote packet %lldu to buffer\n", receiver->win_base);

			receiver->win_base++;
			receiver->buf_base = (receiver->buf_base + 1) % MAX_BUF_LEN;
			pkt = receiver->buf[receiver->buf_base];
		}

		seq_t ack = receiver->win_base - 1;
		sendto(receiver->sockfd, &ack, sizeof (seq_t), 0,
				(struct sockaddr *)&receiver->src_addr, receiver->src_addrlen);
		fprintf(stderr, "Acked packet %lldu\n", ack); 
	}

forward:
	return buflen;
}

void reliablyReceive(unsigned short int myUDPport, char* destinationFile)
{
	/*
	   receiver waits for connection and receives
	 */

	struct addrinfo hints, *servinfo, *p;
	char port_str[5];
	int sockfd;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	sprintf(port_str, "%d", myUDPport);

	if (getaddrinfo(NULL, port_str, &hints, &servinfo)) {
		fprintf(stderr, "Unable to get host address\n");
		exit(1);
	}

	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd=socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			continue;
		}
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			continue;
		}
		break;
	}
	if (p == NULL) {
		fprintf(stderr, "Unable to connect to host\n");
		exit(1);
	}


	FILE *fout;
	if ((fout=fopen(destinationFile, "wb")) != NULL) {
		size_t buflen = MB * 512;
		size_t total_bytes = 0;
		char *buf = calloc(sizeof (char), sizeof (char) * buflen);

		size_t num_received;
		receiver_t receiver;
		receiver_init(&receiver, sockfd);
		while ((num_received=receiver_recv(&receiver, buf+total_bytes, MB)) > 0) {
			total_bytes += num_received;
		}
		fwrite(buf, sizeof (char), total_bytes, fout); 
		freeaddrinfo(servinfo);
		fclose(fout);
	} else {
		fprintf(stderr, "Unable to open file %s for write\n", destinationFile);
		freeaddrinfo(servinfo);
		exit(1);
	}

}
