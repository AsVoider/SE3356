/*
 * FILE: rdt_receiver.cc
 * DESCRIPTION: Reliable data transfer receiver.
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
#include <map>
#include <vector>

#include "rdt_struct.h"
#include "rdt_receiver.h"

static const u_int32_t payload_size_bytes = 1;
static const u_int32_t seq_bytes = 3;
static const u_int32_t checksum_bytes = 4;
static const u_int32_t payload_bytes = RDT_PKTSIZE - checksum_bytes - payload_size_bytes - seq_bytes;

static const u_int32_t payload_size_offset = 7;
static const u_int32_t seq_offset = 4;
static const u_int32_t checksum_offset = 0;
static const u_int32_t payload_offset = 8;

static const u_int32_t ack_offset = 4;
static const u_int32_t window_size = 10;

/*max seq which has received*/
u_int32_t mx_rev = 0;

/*map: save which index is filled with packet*/
std::map<u_int32_t, bool> valid_map;

/*buffer: save further packet*/
packet *packet_buffer;

/*'record': save index and payload length*/
std::vector<std::pair<u_int32_t, u_int32_t>> record;

/*tcp checksum method*/
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

/*send and then free ack_num packet*/
void send_ack(u_int32_t ack_num) {

    packet *pkt = (packet *)malloc(sizeof(packet));
    //memcpy(pkt->data, 0, RDT_PKTSIZE);
    memset(pkt->data, 0, RDT_PKTSIZE);
    //printf("send ack here1\n");
    pkt->data[ack_offset] = (char)(ack_num / (256 * 256));
    pkt->data[ack_offset + 1] = (char)((ack_num / 256) % 256);
    pkt->data[ack_offset + 2] = (char)(ack_num % 256);
    
    u_int32_t checksum_pkt = checksum(pkt);
    memcpy(pkt->data, &checksum_pkt, 4);
    Receiver_ToLowerLayer(pkt);
    free(pkt);
}

/* receiver initialization, called once at the very beginning */
void Receiver_Init()
{
    for (u_int32_t i = 0; i < window_size; i++) {
        valid_map[i] = false;
    }
    packet_buffer = (packet *)malloc(sizeof(packet) * window_size);
    fprintf(stdout, "At %.2fs: receiver initializing ...\n", GetSimulationTime());
}

/* receiver finalization, called once at the very end.
   you may find that you don't need it, in which case you can leave it blank.
   in certain cases, you might want to use this opportunity to release some 
   memory you allocated in Receiver_init(). */
void Receiver_Final()
{
    free(packet_buffer);
    fprintf(stdout, "At %.2fs: receiver finalizing ...\n", GetSimulationTime());
}

/* event handler, called when a packet is passed from the lower layer at the 
   receiver */
void Receiver_FromLowerLayer1(struct packet *pkt)
{
    /* 1-byte header indicating the size of the payload */
    int header_size = 1;

    /* construct a message and deliver to the upper layer */
    struct message *msg = (struct message*) malloc(sizeof(struct message));
    ASSERT(msg!=NULL);

    msg->size = pkt->data[0];

    /* sanity check in case the packet is corrupted */
    if (msg->size<0) msg->size=0;
    if (msg->size>RDT_PKTSIZE-header_size) msg->size=RDT_PKTSIZE-header_size;

    msg->data = (char*) malloc(msg->size);
    ASSERT(msg->data!=NULL);
    memcpy(msg->data, pkt->data+header_size, msg->size);
    Receiver_ToUpperLayer(msg);

    /* don't forget to free the space */
    if (msg->data!=NULL) free(msg->data);
    if (msg!=NULL) free(msg);
}

/* My First GBN, without window in receiver*/
void Receiver_FromLowerLayerMy(struct packet *pkt) {
    /*get seq number*/
    u_int32_t seq = 0;
    seq = ((unsigned char)pkt->data[seq_offset]) * 256 * 256 + ((unsigned char)pkt->data[seq_offset + 1]) * 256 + ((unsigned char)pkt->data[seq_offset + 2]);
    /*get checksum in packet*/
    u_int32_t checksum_pkt = 0;
    memcpy(&checksum_pkt, pkt->data + checksum_offset, checksum_bytes);
    /*get payload length*/
    u_int32_t leng = pkt->data[payload_size_offset];

    /*calculate checksum*/
    u_int32_t now_checksum = checksum(pkt);
    // printf("rece now check is %d, check is %d\n", now_checksum, checksum_pkt);
    
    if (now_checksum != checksum_pkt)
        return;
    
    if (seq != mx_rev + 1) {
        // printf("rev here\n");
        send_ack(mx_rev);
    } else {
        // printf("rev here1\n");
        /*transform packet into message*/
        mx_rev = seq;
        message *pkt2msg = (message *)malloc(sizeof(message));
        pkt2msg->size = leng;
        pkt2msg->data = (char*)malloc(leng);
        memcpy(pkt2msg->data, pkt->data + payload_offset, leng);
        Receiver_ToUpperLayer(pkt2msg);
        //printf("rev here2\n");
        send_ack(mx_rev);
        if (pkt2msg->data)
            free(pkt2msg->data);
        if (pkt2msg)
            free(pkt2msg);
    }
    // printf("rev here\n");
}

void Receiver_FromLowerLayerMy1(struct packet *pkt) {
    // printf("rev here\n");
    /*get seq number*/
    u_int32_t seq = 0;
    seq = ((unsigned char)pkt->data[seq_offset]) * 256 * 256 + ((unsigned char)pkt->data[seq_offset + 1]) * 256 + ((unsigned char)pkt->data[seq_offset + 2]);
    
    /*get checksum*/
    u_int32_t checksum_pkt = 0;
    memcpy(&checksum_pkt, pkt->data + checksum_offset, checksum_bytes);
    
    /*get data length*/
    u_int32_t leng = pkt->data[payload_size_offset];

    //printf("rev lengg is %d, paybytes is %d\n", leng, payload_bytes);
    if (leng > payload_bytes)
        return;

    /*calculate checksum*/
    u_int32_t now_checksum = checksum(pkt);
    // printf("rece now check is %d, check is %d\n", now_checksum, checksum_pkt);
    // printf("rece seq is %d, pktseq is %d\n", mx_rev, seq);
    if (now_checksum != checksum_pkt)
        return;
    
    /*if seq in window, save it to buffer*/
    if (seq > mx_rev + 1 && seq <= mx_rev + window_size) {
        u_int32_t valid = (seq - 1) % window_size;
        if (!valid_map[valid]) {
            memcpy(packet_buffer[valid].data, pkt->data, RDT_PKTSIZE);
            valid_map[valid] = true;
            // printf("insert valid is %d, seq is %d\n", valid, seq);
        }
        send_ack(mx_rev);
    } else if (seq == mx_rev + 1) {
        /*save the received to buffer*/
        u_int32_t total_size = 0;
        message *pkt2msg = (message *)malloc(sizeof(message));
        u_int32_t valid = (seq - 1) % window_size;
        if (!valid_map[valid]) {
            memcpy(packet_buffer[valid].data, pkt->data, RDT_PKTSIZE);
            valid_map[valid] = true;
        }
        /*while loop: get the packet information, then save it*/
        while (1) {
            /*tricks mx_rev will be added one more time in each while loop*/
            mx_rev += 1;
            u_int32_t i = (mx_rev - 1) % window_size;
            if (valid_map[i] == true) {
                //printf("leng is %d, i is %d\n", leng, i);
                pkt = &packet_buffer[i];
                seq = ((unsigned char)pkt->data[seq_offset]) * 256 * 256 + ((unsigned char)pkt->data[seq_offset + 1]) * 256 + ((unsigned char)pkt->data[seq_offset + 2]);
                leng = pkt->data[payload_size_offset];
                total_size += leng;
                /*'record' to save index and payload length*/
                record.emplace_back(std::pair<u_int32_t, u_int32_t>(i, leng));
                valid_map[i] = false;
                //printf("fault?\n");
            } else {
                /*here minus one*/
                mx_rev -= 1;
                send_ack(mx_rev);
                break;
            }
        }
        pkt2msg->data = (char *)malloc(total_size);
        
        /*assemble packets into one message*/
        u_int32_t cursor = 0;
        for (auto &rec : record) {
            //printf("i is %d, leng is %d, total is %d, cursor is %d\n", rec.first, rec.second, total_size, cursor);
            memcpy(pkt2msg->data + cursor, packet_buffer[rec.first].data + payload_offset, rec.second);

            cursor += rec.second;
        }
        record.clear();

        pkt2msg->size = total_size;
        
        Receiver_ToUpperLayer(pkt2msg);
        
        if (pkt2msg->data)
            free(pkt2msg->data);
        if (pkt2msg)
            free(pkt2msg);
    } else {
        send_ack(mx_rev);
    }

    /*if the one at the beginning of window*/
    
}

void Receiver_FromLowerLayer(struct packet *pkt) {
    //Receiver_FromLowerLayerMy(pkt);
    Receiver_FromLowerLayerMy1(pkt);
}