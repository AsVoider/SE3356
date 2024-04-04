/*
 * FILE: rdt_sender.cc
 * DESCRIPTION: Reliable data transfer sender.
 * NOTE: This implementation assumes there is no packet loss, corruption, or 
 *       reordering.  You will need to enhance it to deal with all these 
 *       situations.  In this implementation, the packet format is laid out as 
 *       the following:
 *       
 *       |<-  1 byte  ->|<-             the rest            ->|
 *       | payload size |<-             payload             ->|
 *
 *       The first byte of each packet indicates the size of the payload
 *       (excluding this single-byte header)
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>

#include "rdt_struct.h"
#include "rdt_sender.h"

static const u_int32_t window_size = 10;
/*init window idx : start and end*/
u_int32_t window_start_idx = 0;
u_int32_t window_end_idx = 9;
static const double time_out = 0.3;

static const u_int32_t payload_size_bytes = 1;
static const u_int32_t seq_bytes = 3;
static const u_int32_t checksum_bytes = 4;
static const u_int32_t payload_bytes = RDT_PKTSIZE - checksum_bytes - payload_size_bytes - seq_bytes;

static const u_int32_t payload_size_offset = 7;
static const u_int32_t seq_offset = 4;
static const u_int32_t checksum_offset = 0;
static const u_int32_t payload_offset = 8;
u_int32_t mx_send = 0;

/*sender buffer*/
std::vector<struct packet> pkt_q;

/*ack packet offset*/
static const u_int32_t ack_offset = 4;

/*use in fromupperlayer, just start send packets*/
bool first_send = true;

/*tcp checksum*/
static short checksum(struct packet *pkt) {
    long sum = 0;
    unsigned short *pointer = (unsigned short *)(pkt->data);
    for (u_int32_t begin = 4; begin < RDT_PKTSIZE; begin += 2) {
        pointer = (unsigned short *)(&(pkt->data[begin]));
        sum += *pointer;
    }
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return ~sum;
}



/* sender initialization, called once at the very beginning */
void Sender_Init()
{
    first_send = true;
    fprintf(stdout, "At %.2fs: sender initializing ...\n", GetSimulationTime());
}

/* sender finalization, called once at the very end.
   you may find that you don't need it, in which case you can leave it blank.
   in certain cases, you might want to take this opportunity to release some 
   memory you allocated in Sender_init(). */
void Sender_Final()
{
    first_send = true;
    fprintf(stdout, "At %.2fs: sender finalizing ...\n", GetSimulationTime());
}

/* event handler, called when a message is passed from the upper layer at the 
   sender */
void Sender_FromUpperLayer1(struct message *msg)
{
    /* 1-byte header indicating the size of the payload */
    int header_size = 1;

    /* maximum payload size */
    int maxpayload_size = RDT_PKTSIZE - header_size;

    /* split the message if it is too big */

    /* reuse the same packet data structure */
    packet pkt;

    /* the cursor always points to the first unsent byte in the message */
    int cursor = 0;

    while (msg->size-cursor > maxpayload_size) {
	/* fill in the packet */
	pkt.data[0] = maxpayload_size;
	memcpy(pkt.data+header_size, msg->data+cursor, maxpayload_size);

	/* send it out through the lower layer */
	Sender_ToLowerLayer(&pkt);

	/* move the cursor */
	cursor += maxpayload_size;
    }

    /* send out the last packet */
    if (msg->size > cursor) {
	/* fill in the packet */
	pkt.data[0] = msg->size-cursor;
	memcpy(pkt.data+header_size, msg->data+cursor, pkt.data[0]);

	/* send it out through the lower layer */
	Sender_ToLowerLayer(&pkt);
    }
}

void Sender_FromUpperLayerMy(struct message *msg) {
    packet pkt;
    /*divide message into packets and save them to buffer*/
    for (auto idx = 0; idx < msg->size; idx += payload_bytes) {
        u_int32_t seq_ = pkt_q.size() + 1;
        pkt.data[seq_offset] = (unsigned char)(seq_ / (256 * 256));
        pkt.data[seq_offset + 1] = (unsigned char)((seq_ / 256) % 256);
        pkt.data[seq_offset + 2] = (unsigned char)(seq_ % 256);
        u_int32_t pkt_length = payload_bytes <= (uint32_t)msg->size - idx ? payload_bytes : (uint32_t)msg->size - idx;
        memcpy(pkt.data + payload_size_offset, &pkt_length, payload_size_bytes);
        memcpy(pkt.data + payload_offset, msg->data + idx, pkt_length);
        u_int32_t checksum_pkt = (u_int32_t)checksum(&pkt);
        memcpy(pkt.data + checksum_offset, &checksum_pkt, checksum_bytes);
        pkt_q.emplace_back(pkt);
    }
    //printf("size is %ld\n", pkt_q.size());

    /*start send in this func, only once*/
    if (first_send) {
        //printf("first send here\n");
        first_send = false;
        Sender_StartTimer(time_out);
        for (auto i = window_start_idx; i <= window_end_idx && i < pkt_q.size(); i++) {
            Sender_ToLowerLayer(&pkt_q[i]);
            mx_send = (i + 1) > mx_send ? i + 1 : mx_send;
        }
    }
}

void Sender_FromUpperLayer(struct message *msg) {
    Sender_FromUpperLayerMy(msg);
}

void Sender_FromLowerLayerMy(struct packet *pkt) {
    /*get seq*/
    uint32_t seq = ((unsigned char)pkt->data[seq_offset]) * 256 * 256 + ((unsigned char)pkt->data[seq_offset + 1]) * 256 + ((unsigned char)pkt->data[seq_offset + 2]);

    /*get checksum*/
    uint32_t ack_checksum = 0;
    memcpy(&ack_checksum, pkt->data + checksum_offset, checksum_bytes);

    /*calculate checksum*/
    uint32_t now_checksum = (uint32_t)checksum(pkt);
    // printf("sender now check is %d, check is %d\n", now_checksum, ack_checksum);
    if (now_checksum != ack_checksum)
        return;
    
    /*if seq > window start, move the window*/
    // printf("sender seq is %d, window start is %d\n", seq, window_start_idx);
    if (seq > window_start_idx) {
        window_start_idx = seq;
        window_end_idx = window_start_idx + window_size - 1;
        if (window_start_idx > mx_send) {
            // printf("sender rev time\n");
            // Sender_StartTimer(time_out);
            for (uint32_t start = window_start_idx; start <= window_end_idx && start < pkt_q.size(); start++) {
                if (start == window_start_idx)
                    Sender_StartTimer(time_out);
                Sender_ToLowerLayer(&pkt_q[start]);
                mx_send = (start + 1) > mx_send ? start + 1 : mx_send;
            }
        }
    }

    // printf("sender from lower end\n");
}

/* event handler, called when a packet is passed from the lower layer at the 
   sender */
void Sender_FromLowerLayer(struct packet *pkt) {
    Sender_FromLowerLayerMy(pkt);
}

void Sender_TimeoutMy() {
    /*think if not in loop*/
    // printf("sender timeout \n");
    /*resend all packets in window*/
    for(uint32_t start = window_start_idx; start <= window_end_idx && start < pkt_q.size(); start++) {
        if (window_start_idx == start)
            Sender_StartTimer(time_out);
        Sender_ToLowerLayer(&pkt_q[start]);
        mx_send = (start + 1) > mx_send ? start + 1 : mx_send;
    }
} 

/* event handler, called when the timer expires */
void Sender_Timeout() {
    Sender_TimeoutMy();
}
