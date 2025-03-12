/*!>
 *  ____  ____      _    ____ ___ _   _  ___  
 *  |  _ \|  _ \    / \  / ___|_ _| \ | |/ _ \ 
 *  | | | | |_) |  / _ \| |  _ | ||  \| | | | |
 *  | |_| |  _ <  / ___ \ |_| || || |\  | |_| |
 *  |____/|_| \_\/_/   \_\____|___|_| \_|\___/ 
 *
 * Dragino_gw_fwd -- An opensource lora gateway forward 
 *
 * See http://www.dragino.com for more information about
 * the lora gateway project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 *
 * Maintainer: skerlan
 *
 */

/*!>!
 * \file
 * \brief Example of robust MQTT reconnect with a dedicated thread, using printf().
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <semaphore.h>
#include <time.h>

#include <MQTTPacket.h>

#include "fwd.h"

#include "service.h"
#include "mqtt_service.h"

#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>  // Needed for IFF_LOOPBACK flag

#include <stdio.h>
#include "gwcfg.h" // Ensure this is included to access the loaded structure


#include <pthread.h>
#include "loragw_hal.h"
#include "jitqueue.h"





DECLARE_GW;  /* Provided by Dragino's codebase */

static int  payload_deal(mqttsession_s* session, struct lgw_pkt_rx_s* p);
static void mqtt_init(serv_s* serv);
static void mqtt_push_up(void* arg);       /* Thread: sends uplinks */
static void check_internet(void* arg);     /* Thread: monitors connectivity */


// Define a struct to hold MQTT downlink messages
typedef struct {
    char topicName[128];
    char payload[512];  // Buffer to store raw JSON
    int payloadlen;
} downlink_message_t;

// Extract an integer value from JSON manually
int extract_int_value(const char *json, const char *key) {
    char *pos = strstr(json, key);
    if (!pos) return -1;

    int value;
    if (sscanf(pos + strlen(key) + 2, "%d", &value) == 1) {
        return value;
    }
    return -1;
}

// Extract a boolean value from JSON
bool extract_bool_value(const char *json, const char *key) {
    char *pos = strstr(json, key);
    if (!pos) return false;
    return strstr(pos, "true") ? true : false;
}

// Extract a string value (HEX payload) from JSON
int extract_string_value(const char *json, const char *key, char *output, int max_len) {
    char *pos = strstr(json, key);
    if (!pos) return -1;

    pos = strchr(pos, ':');
    if (!pos) return -1;
    
    pos = strchr(pos, '"');
    if (!pos) return -1;
    pos++;

    char *end = strchr(pos, '"');
    if (!end) return -1;

    int len = end - pos;
    if (len >= max_len) return -1;

    strncpy(output, pos, len);
    output[len] = '\0';
    return len;
}

// Convert HEX string to binary
int hex2bin(const char *hex, uint8_t *bin, int bin_size) {
    int len = strlen(hex);
    if (len % 2 != 0 || len / 2 > bin_size) return -1;

    for (int i = 0; i < len / 2; i++) {
        sscanf(&hex[i * 2], "%2hhx", &bin[i]);
    }
    return len / 2;
}


// Worker function to process the downlink message
void* process_downlink(void* arg) {
    downlink_message_t* msg = (downlink_message_t*)arg;
    if (!msg) return NULL;

    lgw_log(LOG_INFO, "[INFO~] Processing MQTT downlink: %s\n", msg->payload);

    struct lgw_pkt_tx_s txpkt;
    memset(&txpkt, 0, sizeof(txpkt));

    // Parse JSON manually
    txpkt.freq_hz = extract_int_value(msg->payload, "\"freq_hz\"");
    int sf = extract_int_value(msg->payload, "\"sf\"");
    int bw = extract_int_value(msg->payload, "\"bw\"");
    int rf_chain = extract_int_value(msg->payload, "\"rf_chain\"");

    txpkt.invert_pol = extract_bool_value(msg->payload, "\"invert_pol\"");
    txpkt.rf_power = extract_int_value(msg->payload, "\"tx_power\"");

    if (txpkt.freq_hz <= 0 || sf < 7 || sf > 12 || bw <= 0 || txpkt.rf_power <= 0) {
        lgw_log(LOG_ERROR, "[ERROR~] Invalid or missing JSON fields in downlink message!\n");
        free(msg);
        return NULL;
    }

    // Map Spreading Factor (SF)
    switch (sf) {
        case 7: txpkt.datarate = DR_LORA_SF7; break;
        case 8: txpkt.datarate = DR_LORA_SF8; break;
        case 9: txpkt.datarate = DR_LORA_SF9; break;
        case 10: txpkt.datarate = DR_LORA_SF10; break;
        case 11: txpkt.datarate = DR_LORA_SF11; break;
        case 12: txpkt.datarate = DR_LORA_SF12; break;
    }

    // Map Bandwidth
    switch (bw) {
        case 125000: txpkt.bandwidth = BW_125KHZ; break;
        case 250000: txpkt.bandwidth = BW_250KHZ; break;
        case 500000: txpkt.bandwidth = BW_500KHZ; break;
        default:
            lgw_log(LOG_ERROR, "[ERROR~] Invalid Bandwidth!\n");
            free(msg);
            return NULL;
    }

    //rf_chain switch
    switch (rf_chain) {
        case 0: 
        txpkt.rf_chain = 0; 
        break;
        case 1: 
        txpkt.rf_chain = 1; 
        break;
        default:
        txpkt.rf_chain = 0;
        break;
    }


    txpkt.tx_mode = IMMEDIATE;
    //txpkt.rf_chain = 0;
    txpkt.modulation = MOD_LORA;
    txpkt.coderate = CR_LORA_4_5;
    txpkt.preamble = 8;
    txpkt.no_crc = false;
    txpkt.no_header = false;

    // Extract payload HEX string
    char hex_payload[512] = {0};
    if (extract_string_value(msg->payload, "\"payload\"", hex_payload, sizeof(hex_payload)) < 0) {
        lgw_log(LOG_ERROR, "[ERROR~] Failed to extract payload!\n");
        free(msg);
        return NULL;
    }

    // Convert HEX payload to binary
    uint8_t binary_payload[256];
    int bin_len = hex2bin(hex_payload, binary_payload, sizeof(binary_payload));
    if (bin_len < 2) {  // Ensure at least 2 bytes for device ID
        lgw_log(LOG_ERROR, "[ERROR~] Invalid HEX payload!\n");
        free(msg);
        return NULL;
    }

    // Extract first 2 bytes as device ID
    uint16_t device_id = (binary_payload[0] << 8) | binary_payload[1];

    // Remove device ID bytes from payload
    //memmove(binary_payload, binary_payload + 2, bin_len - 2);
    //bin_len -= 2;

    memcpy(txpkt.payload, binary_payload, bin_len);
    txpkt.size = bin_len;

    lgw_log(LOG_INFO, "[INFO~] Sending downlink to Device ID: 0x%04X\n", device_id);

    uint32_t current_concentrator_time;
#ifdef SX1302MOD
    pthread_mutex_lock(&GW.hal.mx_concent);
    lgw_get_instcnt(&current_concentrator_time);
    pthread_mutex_unlock(&GW.hal.mx_concent);
#else
    get_concentrator_time(&current_concentrator_time);
#endif

    txpkt.count_us = current_concentrator_time + (500 * 1000);  // Add delay

    // Add to JIT queue
    enum jit_error_e jit_result = jit_enqueue(&GW.tx.jit_queue[txpkt.rf_chain], current_concentrator_time, &txpkt, JIT_PKT_TYPE_DOWNLINK_CLASS_C);
    
    if (jit_result != JIT_ERROR_OK) {
        lgw_log(LOG_ERROR, "[ERROR~] JIT queue error %d, cannot schedule downlink!\n", jit_result);
    } else {
        lgw_log(LOG_INFO, "[INFO~] Packet added to JIT queue for transmission!\n");
    }

    free(msg);
    return NULL;
}



// Function to get the current IP address
// Function to get the IP address of a specific interface (e.g., eth1)
static void get_ip_address(char *ip_buffer, size_t buffer_size) {
    struct ifaddrs *ifaddr, *ifa;
    void *addr_ptr = NULL;
    char temp_ip[INET_ADDRSTRLEN] = "Unknown"; // Default if no valid IP found
    const char *target_interface = "eth1";    // Change this to your desired interface

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        strncpy(ip_buffer, "Unknown", buffer_size);
        return;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;

        // Ensure we only pick the correct interface
        if (strcmp(ifa->ifa_name, target_interface) != 0) {
            continue;
        }

        // Only pick IPv4 addresses
        if (ifa->ifa_addr->sa_family == AF_INET) {
            addr_ptr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, addr_ptr, temp_ip, sizeof(temp_ip));
            break;  // Stop once we find the correct IP
        }
    }

    freeifaddrs(ifaddr);

    // Copy the selected IP to the output buffer
    strncpy(ip_buffer, temp_ip, buffer_size);
}



static void mqtt_cleanup(mqttsession_s* session) {
    MQTTClientDestroy(&session->client);
    lgw_free(session);
}

/* Optionally: ping helper */
static int mqtt_sendping(mqttsession_s *session) {
    return MQTTSendPing(&session->client);
}

static long mqtt_getrtt(mqttsession_s *session) {
    return MQTTGetPingTime(&session->client) / 1000;
}

static void mqtt_dnlink_cb(struct MessageData *data, void* s) {

    mqttsession_s* session = (mqttsession_s*)s;

    if (data->message->payloadlen < 0)
        return;

    if (session->dnlink_handler)
        session->dnlink_handler(data);
}

void dnlink_handler(MessageData* data) {
    if (!data || !data->message || !data->message->payload) {
        lgw_log(LOG_ERROR, "[ERROR~] Received NULL or invalid data!\n");
        return;
    }

    pthread_t downlink_thread;
    downlink_message_t* msg = (downlink_message_t*)malloc(sizeof(downlink_message_t));
    if (!msg) {
        lgw_log(LOG_ERROR, "[ERROR~] Memory allocation failed!\n");
        return;
    }
    memset(msg, 0, sizeof(downlink_message_t));

    strncpy(msg->topicName, data->topicName, sizeof(msg->topicName) - 1);
    memcpy(msg->payload, data->message->payload, data->message->payloadlen);
    msg->payload[data->message->payloadlen] = '\0';
    msg->payloadlen = data->message->payloadlen;

    if (pthread_create(&downlink_thread, NULL, process_downlink, msg) != 0) {
        free(msg);
    } else {
        pthread_detach(downlink_thread);
    }
}



/* Connect to the MQTT broker */
static int mqtt_connect(serv_s *serv) {
    int err = -1;
    char family[64];

    /* Create a fresh MQTT session struct */
    mqtt_init(serv);
    mqttsession_s* session = (mqttsession_s*)serv->net->mqtt->session;
    if (NULL == session) return err;

    MQTTPacket_connectData connect = MQTTPacket_connectData_initializer;

    /* Perform the TCP connect */
    err = NetworkConnect(&session->network,
                         (char*)&serv->net->addr,
                         atoi((char*)&serv->net->port_up));
    if (err != SUCCESS) {
        // printf("[WARNING][%s] NetworkConnect failed.\n", serv->info.name);
        goto exit;
    }

    connect.clientID.cstring  = session->id;
    connect.keepAliveInterval = KEEP_ALIVE_INTERVAL;

    /* If we have a key, use it as username/password */
    if (NULL != session->key) {
        connect.username.cstring = session->id;
        connect.password.cstring = session->key;
    } else {
        connect.username.cstring = "admin";
        connect.password.cstring = "admin";
    }

    err = MQTTConnect(&session->client, &connect);
    if (err != SUCCESS) {
        // printf("[WARNING][%s] MQTTConnect failed (err=%d).\n", serv->info.name, err);
        goto exit;
    }

    /* Update service state */
    serv->state.live       = true;
    serv->state.stall_time = 0;
    serv->state.connecting = true;

    //Subscribe to the downlink topic
    if (session->dnlink_topic)
        err = MQTTSubscribe(&session->client, session->dnlink_topic, QOS_DOWN, &mqtt_dnlink_cb, session);
        printf("mqtt ---------------------------------------- dnlink topic: %s\n", session->dnlink_topic);
    if (err != SUCCESS) {
         printf("[WARNING][%s] -------------------------- MQTTSubscribe failed (err=%d).\n", serv->info.name, err);
        //goto exit;
    }

    /* Optionally update DB entry */
    snprintf(family, sizeof(family), "service/mqtt/%s", serv->info.name);
    lgw_db_put(family, "network", serv->state.connecting ? "online" : "offline");

exit:
    return err;
}

/* Disconnect from broker & update state */
static void mqtt_disconnect(serv_s* serv) {
    char family[64];
    mqttsession_s* session = (mqttsession_s*)serv->net->mqtt->session;
    if (!session) return;

    

    MQTTDisconnect(&session->client);
    NetworkDisconnect(&session->network);

    serv->state.connecting = false;
    snprintf(family, sizeof(family), "service/mqtt/%s", serv->info.name);
    lgw_db_put(family, "network", serv->state.connecting ? "online" : "offline");
}

/* Reconnect by fully cleaning up & creating a fresh session */
static int mqtt_reconnect(serv_s* serv) {
    serv->state.live = false;
    // printf("[INFO][%s] Reconnecting...\n", serv->info.name);

    /* Cleanup old */
    if (serv->net->mqtt->session) {
        mqtt_disconnect(serv);
        mqtt_cleanup((mqttsession_s*)serv->net->mqtt->session);
        serv->net->mqtt->session = NULL;
    }
    serv->state.connecting = false;

    /* Attempt new connect */
    return mqtt_connect(serv);
}

/* Check if the network socket is still considered connected */
static int mqtt_checkconnected(mqttsession_s *session) {
    return NetworkCheckConnected(&session->network);
}

/* Publish one uplink message */
static int mqtt_send_uplink(mqttsession_s *session, char *uplink, int len, const char *suffix) {
    int rc = FAILURE;
    MQTTMessage message;
    char full_topic[512];

    // Construct full topic with suffix if provided
    if (suffix && strlen(suffix) > 0) {
        snprintf(full_topic, sizeof(full_topic), "%s/%s", session->uplink_topic, suffix);
    } else {
        snprintf(full_topic, sizeof(full_topic), "%s", session->uplink_topic);
    }

    message.qos        = QOS_UP;
    message.retained   = 0;
    message.dup        = 0;
    message.payload    = uplink;
    message.payloadlen = len;

    if (session->uplink_topic) {
        rc = MQTTPublish(&session->client, full_topic, &message);
    }
    return rc;
}

/* Initialize the mqttsession_s struct for our "serv" */
static void mqtt_init(serv_s* serv){
    mqttsession_s* mqttsession = (mqttsession_s*)lgw_malloc(sizeof(mqttsession_s));
    memset(mqttsession, 0, sizeof(mqttsession_s));

    mqttsession->id             = serv->net->mqtt->mqtt_user;
    //mqttsession->id             = serv->info.name;
    mqttsession->key            = serv->net->mqtt->mqtt_pass;
    //mqttsession->key            = serv->info.key;
    //mqtt handler
    mqttsession->dnlink_handler = &dnlink_handler;
    mqttsession->read_buffer    = lgw_malloc(READ_BUFFER_SIZE);
    mqttsession->send_buffer    = lgw_malloc(SEND_BUFFER_SIZE);
    mqttsession->dnlink_topic   = serv->net->mqtt->dntopic;
    mqttsession->uplink_topic   = serv->net->mqtt->uptopic;

    printf("mqtt_init: id=%s\n",         mqttsession->id);
    printf("mqtt_init: key=%s\n",        mqttsession->key);
    printf("mqtt_init: dntopic=%s\n",    mqttsession->dnlink_topic);
    printf("mqtt_init: uptopic=%s\n",    mqttsession->uplink_topic);
    printf("mqtt_init: addr=%s\n",       serv->net->addr);
    printf("mqtt_init: port=%s\n",       serv->net->port_up);

    /* Initialize the network & MQTT client */
    NetworkInit(&mqttsession->network);
    MQTTClientInit(&mqttsession->client,
                   &mqttsession->network,
                   COMMAND_TIMEOUT,
                   mqttsession->send_buffer, SEND_BUFFER_SIZE,
                   mqttsession->read_buffer, READ_BUFFER_SIZE);

    serv->net->mqtt->session = (void*)mqttsession;
}


//send status to mqtt /like online with a timestamp every 30s so i know it's on and working on a differnet topic like status
void send_status(mqttsession_s* session) {
    static char last_ip[INET_ADDRSTRLEN] = "";
    char current_ip[INET_ADDRSTRLEN];

    get_ip_address(current_ip, sizeof(current_ip));

    // Only update if the IP address has changed
    if (strcmp(last_ip, current_ip) != 0) {
        strncpy(last_ip, current_ip, sizeof(last_ip));
    }

    time_t now;
    struct tm *timeinfo;
    char buffer[80];

    time(&now);
    timeinfo = localtime(&now);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

    char uplink[256];
    snprintf(uplink, sizeof(uplink), 
             "{\"status\":\"online\",\"timestamp\":\"%s\",\"ip_address\":\"%s\"}", 
             buffer, last_ip);

    mqtt_send_uplink(session, uplink, strlen(uplink), "status");
}


/*---------------------------------------------------------------------------------
 * Thread #1: Periodically checks connectivity, triggers reconnect if dead.
 *            Also verifies MQTT socket is connected, and optionally pings.
 *--------------------------------------------------------------------------------*/
static void check_internet(void* arg) {
    serv_s* serv = (serv_s*) arg;
    // printf("[INFO][%s] check_internet thread started.\n", serv->info.name);

    while (!serv->thread.stop_sig) {
        /* Sleep ~6 seconds between checks, adjust as you wish */
        wait_ms(6000);

        mqttsession_s* session = (mqttsession_s*)serv->net->mqtt->session;
        if (!session) {
            /* If no active session, attempt reconnect immediately */
            // printf("[WARNING][%s] No MQTT session; attempting reconnect.\n", serv->info.name);
            mqtt_reconnect(serv);
            continue;
        }

        /* Step 1: Check if the underlying TCP socket is connected */
        if (!mqtt_checkconnected(session)) {
            // printf("[WARNING][%s] Socket not connected. Reconnecting...\n", serv->info.name);
            mqtt_reconnect(serv);
            continue;
        }

        /* Step 2: Optionally send an MQTT PING to confirm the server is alive */
        /* If you want to see a "still connected" log, you can print before ping */
        // printf("[INFO][%s] Socket is open; sending MQTT ping to verify...\n", serv->info.name);

        if (mqtt_sendping(session) != SUCCESS) {
            /* If ping fails, do a reconnect */
            // printf("[WARNING][%s] MQTT ping failed! Reconnecting...\n", serv->info.name);
            mqtt_reconnect(serv);
            continue;
        }

        /* If we reach here, we know:
         *   1) The socket is still open
         *   2) The MQTT ping succeeded
         */
        send_status((mqttsession_s*)serv->net->mqtt->session);
        // printf("[INFO][%s] MQTT is connected and ping OK.\n", serv->info.name);
    }

    // printf("[INFO][%s] check_internet thread ended.\n", serv->info.name);
}


/*---------------------------------------------------------------------------------
 * Thread #2: Waits for a semaphore, then sends uplinks to MQTT.
 *--------------------------------------------------------------------------------*/
static void mqtt_push_up(void* arg) {
    serv_s* serv = (serv_s*) arg;
    // printf("[INFO][%s] mqtt_push_up thread starting...\n", serv->info.name);

    while (!serv->thread.stop_sig) {
        /* Wait for a signal that packets arrived or the process should stop */
        sem_wait(&serv->thread.sema);
        if (serv->thread.stop_sig) {
            /* Possibly break immediately if we want a faster shutdown */
            // break;
        }

        /* Copy packets from the global list into serv_ct_s */
        serv_ct_s* serv_ct = lgw_malloc(sizeof(serv_ct_s));
        memset(serv_ct, 0, sizeof(serv_ct_s));
        serv_ct->serv  = serv;
        int nb_pkt     = serv_ct->nb_pkt = get_rxpkt(serv_ct);

        if (nb_pkt == 0) {
            lgw_free(serv_ct);
            continue;
        }

        // printf("[DEBUG][%s] mqtt_push_up fetched %d packets.\n", serv->info.name, nb_pkt);

        /* Send each valid packet to MQTT */
        for (int i = 0; i < nb_pkt; i++) {
            struct lgw_pkt_rx_s *p = &serv_ct->rxpkt[i];
            //switch (p->status) {
            //    case STAT_CRC_OK:
            //        if (!serv->filter.fwd_valid_pkt) continue;
            //        break;
            //    case STAT_CRC_BAD:
            //        if (!serv->filter.fwd_error_pkt) continue;
            //        break;
            //    case STAT_NO_CRC:
            //        if (!serv->filter.fwd_nocrc_pkt) continue;
            //        break;
            //    default:
            //        continue;
            //}

            /* Actual uplink publish */
            int err = payload_deal((mqttsession_s*)serv->net->mqtt->session, p);
            //if (err<9) {
            //    printf("[WARNING][%s] MQTT publish error, forcing reconnect.\n",
            //           serv->info.name);
            //    mqtt_reconnect(serv);
            //}
            //if(err>9) {
            //    printf("[ERROR][%s] MQTT publish error, skipping packet.\n",serv->info.name);
            //}
            // else {
            //    printf("[INFO][%s] Sent data to MQTT server.\n", serv->info.name);
            //}
        }

        lgw_free(serv_ct);
    }

    //printf("[INFO][%s] mqtt_push_up thread ended.\n", serv->info.name);
}

/*---------------------------------------------------------------------------------
 * Helper: Publish one packet's payload to MQTT.
 *--------------------------------------------------------------------------------*/
static int payload_deal(mqttsession_s* session, struct lgw_pkt_rx_s* p) {
    if (!session || !p) {
        printf("Invalid session or packet pointer\n");
        return -1;
    }
    //printf("[INFO] Received packet with %u bytes\n", p->size);
    //print the packet
    for (int i = 0; i < p->size; i++) {
        printf("%02X", p->payload[i]);
    }

    /* Check if packet has valid CRC */
    if (p->status != STAT_CRC_OK) {
        if (p->status == STAT_CRC_BAD) {
            printf("[WARNING] Dropping packet: BAD CRC\n");
        } else if (p->status == STAT_NO_CRC) {
            printf("[WARNING] Dropping packet: NO CRC\n");
        } else if (p->status == STAT_UNDEFINED) {
            printf("[WARNING] Dropping packet: UNDEFINED STATUS\n");
        }
        return -1; /* Ignore packet */
    }

        /* Identify the radio that received the packet */
    const char* radio_used = (p->if_chain < 4) ? "Radio 1 (868 MHz)" : "Radio 0 (867.5 MHz)";

    if (p->size < 3 || p->payload[0] != '<') {
        printf("Invalid packet size or format\n");
        return -1;
    }

    int id_end = -1;
    for (int i = 1; i < p->size; i++) {
        if (p->payload[i] == '>') {
            id_end = i;
            break;
        }
    }
    if (id_end == -1) {
        printf("Closing '>' not found in payload\n");
        return -1;
    }

    char message_id[16];
    memset(message_id, 0, sizeof(message_id));
    memcpy(message_id, &p->payload[1], id_end - 1);
    message_id[id_end - 1] = '\0';
    printf("Extracted Message ID: %s\n", message_id);

    int data_size = p->size - (id_end + 1);
    if (data_size <= 0) {
        printf("Invalid data size after Message ID\n");
        return -1;
    }

    /* Convert payload to hexadecimal */
    char hex_payload[data_size * 2 + 1];
    for (int i = 0; i < data_size; i++) {
        sprintf(&hex_payload[2 * i], "%02X", p->payload[id_end + 1 + i]);
    }
    hex_payload[data_size * 2] = '\0';

    /* Map Datarate to SF (Spreading Factor) */
    char sf_str[5] = "SF?";
    switch (p->datarate) {
        case DR_LORA_SF5:  strcpy(sf_str, "SF5"); break;
        case DR_LORA_SF6:  strcpy(sf_str, "SF6"); break;
        case DR_LORA_SF7:  strcpy(sf_str, "SF7"); break;
        case DR_LORA_SF8:  strcpy(sf_str, "SF8"); break;
        case DR_LORA_SF9:  strcpy(sf_str, "SF9"); break;
        case DR_LORA_SF10: strcpy(sf_str, "SF10"); break;
        case DR_LORA_SF11: strcpy(sf_str, "SF11"); break;
        case DR_LORA_SF12: strcpy(sf_str, "SF12"); break;
        default: 
            printf("[ERROR] Unknown LoRa datarate: %u\n", p->datarate);
            break;
    }

    /* Map Bandwidth */
    char bw_str[6] = "BW?";
    switch (p->bandwidth) {
        case BW_125KHZ: strcpy(bw_str, "BW125"); break;
        case BW_250KHZ: strcpy(bw_str, "BW250"); break;
        case BW_500KHZ: strcpy(bw_str, "BW500"); break;
        default: 
            printf("[WARNING] Unknown LoRa bandwidth: %u\n", p->bandwidth);
            break;
    }

    /* Map Coding Rate */
    char cr_str[6] = "4/?";
    switch (p->coderate) {
        case CR_LORA_4_5: strcpy(cr_str, "4/5"); break;
        case CR_LORA_4_6: strcpy(cr_str, "4/6"); break;
        case CR_LORA_4_7: strcpy(cr_str, "4/7"); break;
        case CR_LORA_4_8: strcpy(cr_str, "4/8"); break;
        case 0: strcpy(cr_str, "OFF"); break;
        default:
            printf("[WARNING] Unknown LoRa coding rate: %u\n", p->coderate);
            break;
    }

    /* Build JSON message with additional metadata */
    char mqtt_message[1024];
    snprintf(mqtt_message, sizeof(mqtt_message),
             "{"
             "\"id\":\"%s\","
             "\"rssi\":%d,"
             "\"snr\":%.1f,"
             "\"frequency\":%u,"
             "\"bandwidth\":\"%s\","
             "\"spreading_factor\":\"%s\","
             "\"coding_rate\":\"%s\","
             "\"radio\":\"%s\","
             "\"payload\":\"%s\""
             "}",
             message_id,                    /* Device ID */
             (int)p->rssis,                  /* Received Signal Strength (RSSI) */
             p->snr,                         /* Signal-to-Noise Ratio (SNR) */
             p->freq_hz,                     /* Frequency in Hz */
             bw_str,                         /* Bandwidth (BW125, BW250, BW500) */
             sf_str,                         /* Spreading Factor (SF7, SF8, etc.) */
             cr_str,                         /* Coding Rate (4/5, 4/6, etc.) */
             radio_used,
             hex_payload);                    /* Payload Data */

    printf("Generated MQTT Message: %s\n", mqtt_message);

    return mqtt_send_uplink(session, mqtt_message, strlen(mqtt_message), message_id);
}



/*---------------------------------------------------------------------------------
 * Public function: Start (2) threads => check_internet() & mqtt_push_up().
 *--------------------------------------------------------------------------------*/
int mqtt_start(serv_s* serv) {
    int ret = mqtt_connect(serv);
    if (ret != SUCCESS) {
        //printf("[WARNING][%s] Initial MQTT connect failed; auto-reconnect will try.\n",serv->info.name);
    }

    /* Start connectivity-monitor thread */
    if (lgw_pthread_create_background(&serv->thread.t_internet, NULL, 
                                      (void *(*)(void*))check_internet,
                                      serv)) {
        //printf("[WARNING][%s] Cannot create check_internet thread.\n", serv->info.name);
        return -1;
    }

    /* Start push-up thread */
    if (lgw_pthread_create_background(&serv->thread.t_up, NULL,
                                      (void *(*)(void*))mqtt_push_up,
                                      serv)) {
        //printf("[WARNING][%s] Cannot create mqtt_push_up thread.\n", serv->info.name);
        return -1;
    }

    /* Increase global count */
    LGW_LIST_LOCK(&GW.rxpkts_list);
    GW.info.service_count++;
    LGW_LIST_UNLOCK(&GW.rxpkts_list);

    /* Mark DB entries, etc. */
    lgw_db_put("service/mqtt", serv->info.name, "running");
    lgw_db_put("thread",       serv->info.name, "running");

    return 0;
}

/*---------------------------------------------------------------------------------
 * Public function: Stop both threads, then cleanup MQTT.
 *--------------------------------------------------------------------------------*/
void mqtt_stop(serv_s* serv) {
    char family[64];

    /* Signal threads to stop and wake them if waiting on sem */
    serv->thread.stop_sig = true;
    sem_post(&serv->thread.sema);

    /* Wait for them to exit */
    pthread_join(serv->thread.t_internet, NULL);
    pthread_join(serv->thread.t_up, NULL);

    /* Disconnect if connected */
    if (serv->state.connecting) {
        mqtt_disconnect(serv);
    }
    if (serv->net->mqtt->session) {
        mqtt_cleanup((mqttsession_s*)serv->net->mqtt->session);
        serv->net->mqtt->session = NULL;
    }
    serv->state.connecting = false;
    serv->state.live       = false;

    /* Decrement service count */
    LGW_LIST_LOCK(&GW.rxpkts_list);
    GW.info.service_count--;
    LGW_LIST_UNLOCK(&GW.rxpkts_list);

    /* Cleanup DB entries */
    snprintf(family, sizeof(family), "service/mqtt/%s", serv->info.name);
    lgw_db_del(family, "network");
    lgw_db_del("service/mqtt", serv->info.name);
    lgw_db_del("thread",       serv->info.name);

    //printf("[INFO][%s] mqtt_stop complete.\n", serv->info.name);
}
