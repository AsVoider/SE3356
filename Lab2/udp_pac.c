#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_mbuf.h>
#include <rte_lcore.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

struct rte_ether_addr addr;
static uint8_t dst_mac[6] = {0x00, 0x50, 0x56, 0xc0, 0x00, 0x02};

#define CAL_IPV4(a, b, c, d) (((a & 0xff) << 24) + ((b & 0xff) << 16) + ((c & 0xff) << 8) + (d & 0xff))

static inline int port_init(uint16_t port, struct rte_mempool *mbuf_pool) {
    struct rte_eth_conf port_conf;
    // const uint16_t rx_rings = 1, tx_rings = 1;
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;
    int retval;
    // uint16_t q;
    struct rte_eth_dev_info dev_info;
    struct rte_eth_txconf txconf;

    if (!rte_eth_dev_is_valid_port(port)) {
        return -1;
    }

    memset(&port_conf, 0, sizeof(struct rte_eth_conf));

    retval = rte_eth_dev_info_get(port, &dev_info);
    if (retval != 0) {
        printf("Error during getting device (port %u) info: %s\n",
                port, strerror(-retval));
        return retval;
    }

     if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE) {
        port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;
    }

    retval = rte_eth_dev_configure(port, 1, 1, &port_conf);
    if (retval != 0) {
        return retval;
    }

    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval != 0) {
        return retval;
    }

    retval = rte_eth_rx_queue_setup(port, 0, nb_rxd, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (retval < 0) {
        return retval;
    }

    txconf = dev_info.default_txconf;
    txconf.offloads = port_conf.txmode.offloads;
    retval = rte_eth_tx_queue_setup(port, 0, nb_txd, rte_eth_dev_socket_id(port), &txconf);
    if (retval < 0) {
        return retval;
    }

    retval = rte_eth_dev_start(port);
    if (retval < 0) {
        return retval;
    }

    retval = rte_eth_macaddr_get(port, &addr);
    if (retval != 0) {
        return retval;
    }

    printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
               " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
            port, RTE_ETHER_ADDR_BYTES(&addr));

    retval = rte_eth_promiscuous_enable(port);
    if (retval != 0) {
        return retval;
    }
    return 0;
}

static void set_udp_pac(struct rte_mbuf *this_buf) {
    char *data2pkt = "Good Bye World.";
    uint32_t dataLength = 16;

    struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(this_buf, struct rte_ether_hdr *);
    struct rte_ipv4_hdr *ip_hdr = rte_pktmbuf_mtod_offset(this_buf, struct rte_ipv4_hdr *,
        sizeof(struct rte_ether_hdr));
    struct rte_udp_hdr *udp_hdr = rte_pktmbuf_mtod_offset(this_buf, struct rte_udp_hdr *, 
        sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));

    struct rte_ether_addr mac_addr;
    rte_eth_macaddr_get(0, &mac_addr);
    eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
    eth_hdr->src_addr = mac_addr;
    memcpy(eth_hdr->dst_addr.addr_bytes, dst_mac, 6);

    ip_hdr->version_ihl = RTE_IPV4_VHL_DEF;
    ip_hdr->type_of_service = 0;
    ip_hdr->total_length = (dataLength + sizeof(struct rte_udp_hdr) + sizeof(struct rte_ipv4_hdr)) << 8;
    ip_hdr->packet_id = 0;
    ip_hdr->fragment_offset = 0;
    ip_hdr->time_to_live = 255;
    ip_hdr->next_proto_id = 17;
    ip_hdr->src_addr = CAL_IPV4(10, 137, 168, 192);
    ip_hdr->dst_addr = CAL_IPV4(1, 137, 168, 192);
    ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);

    udp_hdr->src_port = rte_cpu_to_be_16(9000);
    udp_hdr->dst_port = rte_cpu_to_be_16(9000);
    udp_hdr->dgram_len = (dataLength + sizeof(struct rte_udp_hdr)) << 8;
    udp_hdr->dgram_cksum = rte_ipv4_udptcp_cksum(ip_hdr, udp_hdr);

    char *data_ptr = rte_pktmbuf_mtod_offset(this_buf, char *, sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + 
        sizeof(struct rte_udp_hdr));
    memcpy(data_ptr, data2pkt, dataLength);

}

static void lcore_main(struct rte_mbuf **bufs, int nums) {
    uint16_t port;
    RTE_ETH_FOREACH_DEV(port)
        if (rte_eth_dev_socket_id(port) >= 0 &&
                rte_eth_dev_socket_id(port) !=
                        (int)rte_socket_id())
            printf("WARNING, port %u is on remote NUMA node to "
                    "polling thread.\n\tPerformance will "
                    "not be optimal.\n", port);
    printf("\nCore %u forwarding packets. [Ctrl+C to quit]\n",
            rte_lcore_id());

    port = 0;
    int pkts = nums;
    int pkt_left = pkts;
    while (pkt_left > 0) {
        int burst_sz = pkt_left;
        for (int id = 0; id < burst_sz; id++) {
            set_udp_pac(bufs[id]);
        }
        int num_send = rte_eth_tx_burst(port, 0, bufs, burst_sz);
        pkt_left -= num_send;
    }
    printf("send end\n");
}

int main (int argc, char *argv[]) {
    struct rte_mempool *mbuf_pool;
    uint32_t nb_ports;
    uint16_t portid;

    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
    }

    argc -= ret;
    argv += ret;

    nb_ports = rte_eth_dev_count_avail();
    printf("num port init: %d\n", nb_ports);
    // if (nb_ports < 2 || nb_ports & 1) {
    //     rte_exit(EXIT_FAILURE, "Error: number of ports must be even\n");
    // }
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (!mbuf_pool) {
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
    }

    printf("mbufpool success\n");

    RTE_ETH_FOREACH_DEV(portid)
        if (port_init(portid, mbuf_pool) != 0) {
            rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n", portid);
        }

    if (rte_lcore_count() > 1) {
        printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");
    }

    struct rte_mbuf *bufs[10];
    for (uint32_t id = 0; id < 10; id++) {
        bufs[id] = rte_pktmbuf_alloc(mbuf_pool);
        if (!bufs[id]) {
            rte_exit(EXIT_FAILURE, "Cannot alloc mbuf\n");
        }
        bufs[id]->pkt_len = 58;
        bufs[id]->data_len = 58;
    }

    printf("bufalloc success\n");

    lcore_main(bufs, 10);

    printf("cleaning begin\n");
    for (uint32_t i = 0; i < 10; i++) {
        rte_pktmbuf_free(bufs[i]);
        printf("clean %d buf\n", i);
    }

    rte_eal_cleanup();
    return 0;
}