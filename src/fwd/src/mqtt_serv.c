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
#include "loragw_hal.h"

#include "service.h"
#include "mqtt_service.h"


#include <stdio.h>
#include "gwcfg.h" // Ensure this is included to access the loaded structure



DECLARE_GW;  /* Provided by Dragino's codebase */

static int  payload_deal(mqttsession_s* session, struct lgw_pkt_rx_s* p);
static void mqtt_init(serv_s* serv);
static void mqtt_push_up(void* arg);       /* Thread: sends uplinks */
static void check_internet(void* arg);     /* Thread: monitors connectivity */

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
    time_t now;
    struct tm *timeinfo;
    char buffer[80];

    time(&now);
    timeinfo = localtime(&now);
    strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", timeinfo);

    char uplink[256];
    snprintf(uplink, sizeof(uplink), "{\"status\":\"online\",\"timestamp\":\"%s\"}", buffer);
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
