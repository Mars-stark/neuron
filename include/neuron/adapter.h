/**
 * NEURON IIoT System for Industry 4.0
 * Copyright (C) 2020-2022 EMQ Technologies Co., Ltd All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 **/

#ifndef _NEU_ADAPTER_H_
#define _NEU_ADAPTER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "type.h"

typedef enum {
    NEU_NA_TYPE_DRIVER = 1,
    NEU_NA_TYPE_APP    = 2,
} neu_adapter_type_e,
    neu_node_type_e;

#include "adapter_msg.h"

typedef struct neu_adapter        neu_adapter_t;
typedef struct neu_adapter_driver neu_adapter_driver_t;
typedef struct neu_adapter_app    neu_adapter_app_t;

typedef enum {
    NEU_NODE_LINK_STATUS_DISCONNECTED = 0,
    NEU_NODE_LINK_STATUS_CONNECTING   = 1,
    NEU_NODE_LINK_STATUS_CONNECTED    = 2,
} neu_node_link_status_e;

typedef enum {
    NEU_NODE_RUNNING_STATUS_IDLE    = 0,
    NEU_NODE_RUNNING_STATUS_INIT    = 1,
    NEU_NODE_RUNNING_STATUS_READY   = 2,
    NEU_NODE_RUNNING_STATUS_RUNNING = 3,
    NEU_NODE_RUNNING_STATUS_STOPPED = 4,
} neu_node_running_status_e;

typedef struct {
    neu_node_running_status_e running;
    neu_node_link_status_e    link;
} neu_node_status_t;

typedef struct adapter_callbacks {
    int (*command)(neu_adapter_t *adapter, neu_reqresp_head_t head, void *data);
    int (*response)(neu_adapter_t *adapter, neu_reqresp_head_t *head,
                    void *data);

    union {
        struct {
            void (*update)(neu_adapter_t *adapter, const char *group,
                           const char *tag, neu_dvalue_t value);
            void (*write_response)(neu_adapter_t *adapter, void *req,
                                   neu_error error);
        } driver;
    };
} adapter_callbacks_t;

#ifdef __cplusplus
}
#endif

#endif