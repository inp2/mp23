Reliability and Congestion Control
Implement a transport protocol with properties equivalent to TCP

sender_main.c
reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer)

Transfer the first bytesToTransfer bytes of filename to the receiver at hostname:hostUDPport correctly & efficiently, even if the network drops or reorders some of your packets

receiver_main.c
reliablyTransfer(unsigned short int myUDPport, char* destinationFile)

Write what is receives to a file called destinateFile

Requires
Data written to disk by the receiver must be exactly what the sender was given

Two instances of your protocol competing with each other must converge to roughly fairly sharing the link (same throughputs +/0 10%), within 100 RTTs

TCP-Friendly: an instance of TCP competing with you must get on average at least half as much throughput as your flow

Instance of your protocol competing with TCP must get on average at least as much throughput as the TCP flow

Everything must hold in the presence of any amount of dropped packets

Protocol must utilize at least 70% of bandwidth when there is no competing traffic, and packets are not artifically dropped or reordered

Cannot use TCP in any way, use SOCK_DGRAM (UDP)
