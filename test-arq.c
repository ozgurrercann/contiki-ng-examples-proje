#include "contiki.h"
#include "net/ipv6/simple-udp.h"
#include "sys/log.h"
#include <string.h>

#define LOG_MODULE "ARQ-Test"
#define LOG_LEVEL LOG_LEVEL_INFO

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

static struct simple_udp_connection server_conn;
static struct simple_udp_connection client_conn;

static uint16_t expected_block = 1;
static struct etimer rt_timer;

struct firmware_packet {
  uint16_t block_number;
  uint8_t data[64];
};

static void server_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr, uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr, uint16_t receiver_port,
         const uint8_t *data, uint16_t datalen);

static void client_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr, uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr, uint16_t receiver_port,
         const uint8_t *data, uint16_t datalen);

static void bridge_to_server(const void *data, uint16_t len) {
  uip_ipaddr_t client_ip;
  memset(&client_ip, 0, sizeof(client_ip));
  server_rx_callback(&server_conn, &client_ip, UDP_CLIENT_PORT, &client_ip, UDP_SERVER_PORT, data, len);
}

static void bridge_to_client(const void *data, uint16_t len) {
  uip_ipaddr_t server_ip;
  memset(&server_ip, 0, sizeof(server_ip));
  client_rx_callback(&client_conn, &server_ip, UDP_SERVER_PORT, &server_ip, UDP_CLIENT_PORT, data, len);
}

static void server_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr, uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr, uint16_t receiver_port,
         const uint8_t *data, uint16_t datalen)
{
  if(datalen == sizeof(uint16_t)) {
    uint16_t requested_block = *(uint16_t *)data;
    if(requested_block < expected_block) return;
    struct firmware_packet pkt;
    pkt.block_number = requested_block;
    memset(pkt.data, 0xAA, sizeof(pkt.data));
    LOG_INFO("[Server] Sending block %d to client.\n", requested_block);
    bridge_to_client(&pkt, sizeof(pkt));
  }
}

static void client_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr, uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr, uint16_t receiver_port,
         const uint8_t *data, uint16_t datalen)
{
  if(datalen < sizeof(uint16_t)) return;
  struct firmware_packet *pkt = (struct firmware_packet *)data;
  if(pkt->block_number == expected_block) {
    LOG_INFO("[Client] ACK: Received block %d successfully.\n", pkt->block_number);
    expected_block++;
    etimer_stop(&rt_timer);
    etimer_set(&rt_timer, CLOCK_SECOND * 1);
  }
}

PROCESS(arq_simulation_process, "Stop-and-Wait ARQ Simulation");
AUTOSTART_PROCESSES(&arq_simulation_process);

PROCESS_THREAD(arq_simulation_process, ev, data)
{
  PROCESS_BEGIN();
  simple_udp_register(&server_conn, UDP_SERVER_PORT, NULL, UDP_CLIENT_PORT, server_rx_callback);
  simple_udp_register(&client_conn, UDP_CLIENT_PORT, NULL, UDP_SERVER_PORT, client_rx_callback);
  LOG_INFO("[Server] Server started. Firmware host is active.\n");
  LOG_INFO("[Client] Client started. Ready to receive firmware blocks...\n");
  etimer_set(&rt_timer, CLOCK_SECOND * 1);
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&rt_timer));
    if(expected_block > 10) {
      LOG_INFO("SUCCESS: All 10 firmware blocks transferred securely via Stop-and-Wait ARQ!\n");
      break;
    }
    LOG_INFO("[Client] Timeout/Request: Requesting block %d...\n", expected_block);
    bridge_to_server(&expected_block, sizeof(expected_block));
    etimer_set(&rt_timer, CLOCK_SECOND * 2);
  }
  PROCESS_END();
}