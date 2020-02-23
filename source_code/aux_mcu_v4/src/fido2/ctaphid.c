// Copyright 2019 SoloKeys Developers
//
// Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
// http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
// http://opensource.org/licenses/MIT>, at your option. This file may not be
// copied, modified, or distributed except according to those terms.
//
// Modified by MiniBLE developers
// -Removed Solo specific message support
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "solo_compat_layer.h"
#include "comms_raw_hid.h"
#include "ctaphid.h"
#include "ctap.h"

typedef enum
{
    IDLE = 0,
    HANDLING_REQUEST,
} CTAP_STATE;

typedef enum
{
    EMPTY = 0,
    BUFFERING,
    BUFFERED,
    HID_ERROR,
    HID_IGNORE,
} CTAP_BUFFER_STATE;


typedef struct
{
    uint8_t cmd;
    uint32_t cid;
    uint16_t bcnt;
    int offset;
    int bytes_written;
    uint8_t seq;
    uint8_t buf[HID_MESSAGE_SIZE];
} CTAPHID_WRITE_BUFFER;

struct CID
{
    uint32_t cid;
    uint64_t last_used;
    uint8_t busy;
    uint8_t last_cmd;
};


#define SUCESS          0
#define SEQUENCE_ERROR  1

static int state;
static struct CID CIDS[5];
#define CID_MAX (sizeof(CIDS)/sizeof(struct CID))

static uint64_t active_cid_timestamp;

static uint8_t ctap_buffer[CTAPHID_BUFFER_SIZE];
static uint32_t ctap_buffer_cid;
static int ctap_buffer_cmd;
static uint16_t ctap_buffer_bcnt;
static int ctap_buffer_offset;
static int ctap_packet_seq;

static void buffer_reset(void);

#define CTAPHID_WRITE_INIT      0x01
#define CTAPHID_WRITE_FLUSH     0x02
#define CTAPHID_WRITE_RESET     0x04

#define     ctaphid_write_buffer_init(x)    memset(x,0,sizeof(CTAPHID_WRITE_BUFFER))
static void ctaphid_write(CTAPHID_WRITE_BUFFER * wb, void * _data, int len);

void ctaphid_init(void)
{
    state = IDLE;
    buffer_reset();
    //ctap_reset_state();
}

static uint32_t get_new_cid(void)
{
    static uint32_t cid = 1;
    do
    {
        cid++;
    }while(cid == 0 || cid == 0xffffffff);
    return cid;
}

static int8_t add_cid(uint32_t cid)
{
    uint32_t i;
    for(i = 0; i < CID_MAX-1; i++)
    {
        if (!CIDS[i].busy)
        {
            CIDS[i].cid = cid;
            CIDS[i].busy = 1;
            CIDS[i].last_used = millis();
            return 0;
        }
    }
    return -1;
}

static int8_t cid_exists(uint32_t cid)
{
    uint32_t i;
    for(i = 0; i < CID_MAX-1; i++)
    {
        if (CIDS[i].cid == cid)
        {
            return 1;
        }
    }
    return 0;
}

static int8_t cid_refresh(uint32_t cid)
{
    uint32_t i;
    for(i = 0; i < CID_MAX-1; i++)
    {
        if (CIDS[i].cid == cid)
        {
            CIDS[i].last_used = millis();
            CIDS[i].busy = 1;
            return 0;
        }
    }
    return -1;
}

static int8_t cid_del(uint32_t cid)
{
    uint32_t i;
    for(i = 0; i < CID_MAX-1; i++)
    {
        if (CIDS[i].cid == cid)
        {
            CIDS[i].busy = 0;
            return 0;
        }
    }
    return -1;
}

static int is_broadcast(CTAPHID_PACKET * pkt)
{
    return (pkt->cid == CTAPHID_BROADCAST_CID);
}

static int is_init_pkt(CTAPHID_PACKET * pkt)
{
    return (pkt->pkt.init.cmd == CTAPHID_INIT);
}

static int is_cont_pkt(CTAPHID_PACKET * pkt)
{
    return !(pkt->pkt.init.cmd & TYPE_INIT);
}


static int buffer_packet(CTAPHID_PACKET * pkt)
{
    if (pkt->pkt.init.cmd & TYPE_INIT)
    {
        ctap_buffer_bcnt = ctaphid_packet_len(pkt);
        int pkt_len = (ctap_buffer_bcnt < CTAPHID_INIT_PAYLOAD_SIZE) ? ctap_buffer_bcnt : CTAPHID_INIT_PAYLOAD_SIZE;
        ctap_buffer_cmd = pkt->pkt.init.cmd;
        ctap_buffer_cid = pkt->cid;
        ctap_buffer_offset = pkt_len;
        ctap_packet_seq = -1;
        memmove(ctap_buffer, pkt->pkt.init.payload, pkt_len);
    }
    else
    {
        int leftover = ctap_buffer_bcnt - ctap_buffer_offset;
        int diff = leftover - CTAPHID_CONT_PAYLOAD_SIZE;
        ctap_packet_seq++;
        if (ctap_packet_seq != pkt->pkt.cont.seq)
        {
            return SEQUENCE_ERROR;
        }

        if (diff <= 0)
        {
            // only move the leftover amount
            memmove(ctap_buffer + ctap_buffer_offset, pkt->pkt.cont.payload, leftover);
            ctap_buffer_offset += leftover;
        }
        else
        {
            memmove(ctap_buffer + ctap_buffer_offset, pkt->pkt.cont.payload, CTAPHID_CONT_PAYLOAD_SIZE);
            ctap_buffer_offset += CTAPHID_CONT_PAYLOAD_SIZE;
        }
    }
    return SUCESS;
}

static void buffer_reset(void)
{
    ctap_buffer_bcnt = 0;
    ctap_buffer_offset = 0;
    ctap_packet_seq = 0;
    ctap_buffer_cid = 0;
}

static int buffer_status(void)
{
    if (ctap_buffer_bcnt == 0)
    {
        return EMPTY;
    }
    else if (ctap_buffer_offset == ctap_buffer_bcnt)
    {
        return BUFFERED;
    }
    else
    {
        return BUFFERING;
    }
}

static int buffer_cmd(void)
{
    return ctap_buffer_cmd;
}

static uint32_t buffer_cid(void)
{
    return ctap_buffer_cid;
}


static int buffer_len(void)
{
    return ctap_buffer_bcnt;
}

// Buffer data and send in HID_MESSAGE_SIZE chunks
// if len == 0, FLUSH
static void ctaphid_write(CTAPHID_WRITE_BUFFER * wb, void * _data, int len)
{
    uint8_t * data = (uint8_t *)_data;
    if (_data == NULL)
    {
        if (wb->offset == 0 && wb->bytes_written == 0)
        {
            memmove(wb->buf, &wb->cid, 4);
            wb->offset += 4;

            wb->buf[4] = wb->cmd;
            wb->buf[5] = (wb->bcnt & 0xff00) >> 8;
            wb->buf[6] = (wb->bcnt & 0xff) >> 0;
            wb->offset += 3;
        }

        if (wb->offset > 0)
        {
            memset(wb->buf + wb->offset, 0, HID_MESSAGE_SIZE - wb->offset);
            ctaphid_write_block(wb->buf);
        }
        return;
    }
    int i;
    for (i = 0; i < len; i++)
    {
        if (wb->offset == 0 )
        {
            memmove(wb->buf, &wb->cid, 4);
            wb->offset += 4;

            if (wb->bytes_written == 0)
            {
                wb->buf[4] = wb->cmd;
                wb->buf[5] = (wb->bcnt & 0xff00) >> 8;
                wb->buf[6] = (wb->bcnt & 0xff) >> 0;
                wb->offset += 3;
            }
            else
            {
                wb->buf[4] = wb->seq++;
                wb->offset += 1;
            }
        }
        wb->buf[wb->offset++] = data[i];
        wb->bytes_written += 1;
        if (wb->offset == HID_MESSAGE_SIZE)
        {
            ctaphid_write_block(wb->buf);
            wb->offset = 0;
        }
    }
}


static void ctaphid_send_error(uint32_t cid, uint8_t error)
{
    CTAPHID_WRITE_BUFFER wb;
    ctaphid_write_buffer_init(&wb);

    wb.cid = cid;
    wb.cmd = CTAPHID_ERROR;
    wb.bcnt = 1;

    ctaphid_write(&wb, &error, 1);
    ctaphid_write(&wb, NULL, 0);
}

static void send_init_response(uint32_t oldcid, uint32_t newcid, uint8_t * nonce)
{
    CTAPHID_INIT_RESPONSE init_resp;
    CTAPHID_WRITE_BUFFER wb;
    ctaphid_write_buffer_init(&wb);
    wb.cid = oldcid;
    wb.cmd = CTAPHID_INIT;
    wb.bcnt = 17;

    memmove(init_resp.nonce, nonce, 8);
    init_resp.cid = newcid;
    init_resp.protocol_version = CTAPHID_PROTOCOL_VERSION;
    init_resp.version_major = 0;//?
    init_resp.version_minor = 0;//?
    init_resp.build_version = 0;//?
    init_resp.capabilities = CTAP_CAPABILITIES;

    ctaphid_write(&wb,&init_resp,sizeof(CTAPHID_INIT_RESPONSE));
    ctaphid_write(&wb,NULL,0);
}


void ctaphid_check_timeouts(void)
{
    uint8_t i;
    for(i = 0; i < CID_MAX; i++)
    {
        if (CIDS[i].busy && ((millis() - CIDS[i].last_used) >= 750))
        {
            printf1(TAG_HID, "TIMEOUT CID: %08x", CIDS[i].cid);
            ctaphid_send_error(CIDS[i].cid, CTAP1_ERR_TIMEOUT);
            CIDS[i].busy = 0;
            if (CIDS[i].cid == buffer_cid())
            {
                buffer_reset();
            }
            // memset(CIDS + i, 0, sizeof(struct CID));
        }
    }

}

void ctaphid_update_status(int8_t status)
{
    CTAPHID_WRITE_BUFFER wb;
    //printf1(TAG_HID, "Send device update %d!",status);
    ctaphid_write_buffer_init(&wb);

    wb.cid = buffer_cid();
    wb.cmd = CTAPHID_KEEPALIVE;
    wb.bcnt = 1;

    ctaphid_write(&wb, &status, 1);
    ctaphid_write(&wb, NULL, 0);
}

static int ctaphid_buffer_packet(uint32_t * pkt_raw, uint8_t * cmd, uint32_t * cid, int * len)
{
    CTAPHID_PACKET * pkt = (CTAPHID_PACKET *)(pkt_raw);

    if (!is_cont_pkt(pkt)) {printf2(TAG_ERR, "  length: %d", ctaphid_packet_len(pkt));}

    int ret;
    uint32_t oldcid;
    uint32_t newcid;


    *cid = pkt->cid;

    if (is_init_pkt(pkt))
    {
        if (ctaphid_packet_len(pkt) != 8)
        {
            printf2(TAG_ERR, "Error,invalid length field for init packet");
            *cmd = CTAP1_ERR_INVALID_LENGTH;
            return HID_ERROR;
        }
        if (pkt->cid == 0)
        {
            printf2(TAG_ERR,"Error, invalid cid 0");
            *cmd = CTAP1_ERR_INVALID_CHANNEL;
            return HID_ERROR;
        }

        ctaphid_init();
        if (is_broadcast(pkt))
        {
            // Check if any existing cids are busy first ?
            printf1(TAG_HID,"adding a new cid");
            oldcid = CTAPHID_BROADCAST_CID;
            newcid = get_new_cid();
            ret = add_cid(newcid);
            // handle init here
        }
        else
        {
            printf1(TAG_HID, "synchronizing to cid");
            oldcid = pkt->cid;
            newcid = pkt->cid;
            if (cid_exists(newcid))
                ret = cid_refresh(newcid);
            else
                ret = add_cid(newcid);
        }
        if (ret == -1)
        {
            printf2(TAG_ERR, "Error, not enough memory for new CID.  return BUSY.");
            *cmd = CTAP1_ERR_CHANNEL_BUSY;
            return HID_ERROR;
        }
        send_init_response(oldcid, newcid, pkt->pkt.init.payload);
        cid_del(newcid);

        return HID_IGNORE;
    }
    else
    {
        if (pkt->cid == CTAPHID_BROADCAST_CID)
        {
            *cmd = CTAP1_ERR_INVALID_CHANNEL;
            return HID_ERROR;
        }

        if (! cid_exists(pkt->cid) && ! is_cont_pkt(pkt))
        {
            if (buffer_status() == EMPTY)
            {
                add_cid(pkt->cid);
            }
        }

        if (cid_exists(pkt->cid))
        {
            if (buffer_status() == BUFFERING)
            {
                if (pkt->cid == buffer_cid() && ! is_cont_pkt(pkt))
                {
                    printf2(TAG_ERR,"INVALID_SEQ");
                    printf2(TAG_ERR,"Have %d/%d bytes", ctap_buffer_offset, ctap_buffer_bcnt);
                    *cmd = CTAP1_ERR_INVALID_SEQ;
                    return HID_ERROR;
                }
                else if (pkt->cid != buffer_cid())
                {
                    if (! is_cont_pkt(pkt))
                    {
                        printf2(TAG_ERR,"BUSY with %08x", buffer_cid());
                        *cmd = CTAP1_ERR_CHANNEL_BUSY;
                        return HID_ERROR;
                    }
                    else
                    {
                        printf2(TAG_ERR,"ignoring random cont packet from %04x",pkt->cid);
                        return HID_IGNORE;
                    }
                }
            }
            if (! is_cont_pkt(pkt))
            {

                if (ctaphid_packet_len(pkt) > CTAPHID_BUFFER_SIZE)
                {
                    *cmd = CTAP1_ERR_INVALID_LENGTH;
                    return HID_ERROR;
                }
            }
            else
            {
                if (buffer_status() == EMPTY || pkt->cid != buffer_cid())
                {
                    printf2(TAG_ERR,"ignoring random cont packet from %04x",pkt->cid);
                    return HID_IGNORE;
                }
            }

            if (buffer_packet(pkt) == SEQUENCE_ERROR)
            {
                printf2(TAG_ERR,"Buffering sequence error");
                *cmd = CTAP1_ERR_INVALID_SEQ;
                return HID_ERROR;
            }
            ret = cid_refresh(pkt->cid);
            if (ret != 0)
            {
                printf2(TAG_ERR,"Error, refresh cid failed");
                exit(1);
            }
        }
        else if (is_cont_pkt(pkt))
        {
            printf2(TAG_ERR,"ignoring unwarranted cont packet");

            // Ignore
            return HID_IGNORE;
        }
        else
        {
            printf2(TAG_ERR,"BUSY");
            *cmd = CTAP1_ERR_CHANNEL_BUSY;
            return HID_ERROR;
        }
    }

    *len = buffer_len();
    *cmd = buffer_cmd();
    return buffer_status();
}

extern void _check_ret(CborError ret, int line, const char * filename);
#define check_hardcore(r)   _check_ret(r,__LINE__, __FILE__);\
                            if ((r) != CborNoError) exit(1);

/**
 * Removed Solo specific messages
 */
uint8_t ctaphid_handle_packet(uint32_t * pkt_raw)
{
    uint8_t cmd;
    uint32_t cid;
    int len;
#ifndef DISABLE_CTAPHID_CBOR
    int status;
#endif

    static uint8_t is_busy = 0;
    static CTAPHID_WRITE_BUFFER wb;
    CTAP_RESPONSE ctap_resp;

    int bufstatus = ctaphid_buffer_packet(pkt_raw, &cmd, &cid, &len);

    if (bufstatus == HID_IGNORE)
    {
        return 0;
    }

    if (bufstatus == HID_ERROR)
    {
        cid_del(cid);
        if (cmd == CTAP1_ERR_INVALID_SEQ)
        {
            buffer_reset();
        }
        ctaphid_send_error(cid, cmd);
        return 0;
    }

    if (bufstatus == BUFFERING)
    {
        active_cid_timestamp = millis();
        return 0;
    }


    switch(cmd)
    {

        case CTAPHID_INIT:
            printf2(TAG_ERR,"CTAPHID_INIT, error this should already be handled");
            exit(1);
            break;
#ifndef DISABLE_CTAPHID_PING
        case CTAPHID_PING:
            printf1(TAG_HID,"CTAPHID_PING");

            ctaphid_write_buffer_init(&wb);
            wb.cid = cid;
            wb.cmd = CTAPHID_PING;
            wb.bcnt = len;
            timestamp();
            ctaphid_write(&wb, ctap_buffer, len);
            ctaphid_write(&wb, NULL,0);
            printf1(TAG_TIME,"PING writeback: %d ms",timestamp());

            break;
#endif
#ifndef DISABLE_CTAPHID_WINK
        case CTAPHID_WINK:
            printf1(TAG_HID,"CTAPHID_WINK");

            ctaphid_write_buffer_init(&wb);

            device_wink();

            wb.cid = cid;
            wb.cmd = CTAPHID_WINK;

            ctaphid_write(&wb,NULL,0);

            break;
#endif
#ifndef DISABLE_CTAPHID_CBOR
        case CTAPHID_CBOR:
            printf1(TAG_HID,"CTAPHID_CBOR");

            if (len == 0)
            {
                printf2(TAG_ERR,"Error,invalid 0 length field for cbor packet");
                ctaphid_send_error(cid, CTAP1_ERR_INVALID_LENGTH);
                return 0;
            }
            if (is_busy)
            {
                printf1(TAG_HID,"Channel busy for CBOR");
                ctaphid_send_error(cid, CTAP1_ERR_CHANNEL_BUSY);
                return 0;
            }
            is_busy = 1;
            ctap_response_init(&ctap_resp);
            status = ctap_request(ctap_buffer, len, &ctap_resp);

            ctaphid_write_buffer_init(&wb);
            wb.cid = cid;
            wb.cmd = CTAPHID_CBOR;
            wb.bcnt = (ctap_resp.length+1);


            timestamp();
            ctaphid_write(&wb, &status, 1);
            ctaphid_write(&wb, ctap_resp.data, ctap_resp.length);
            ctaphid_write(&wb, NULL, 0);
            printf1(TAG_TIME,"CBOR writeback: %d ms",timestamp());
            is_busy = 0;
            break;
#endif
        case CTAPHID_CANCEL:
            printf1(TAG_HID,"CTAPHID_CANCEL");
            is_busy = 0;
            break;
        default:
            printf2(TAG_ERR,"error, unimplemented HID cmd: %02x\r", buffer_cmd());
            ctaphid_send_error(cid, CTAP1_ERR_INVALID_COMMAND);
            break;
    }
    cid_del(cid);
    buffer_reset();

    printf1(TAG_HID,"");
    if (!is_busy) return cmd;
    else return 0;

}
