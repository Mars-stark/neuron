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

#include "connection/mqtt_client.h"
#include "errcodes.h"
#include "version.h"
#include "json/neu_json_mqtt.h"
#include "json/neu_json_rw.h"

#include "parser/neu_json_node.h"

#include "monitor.h"
#include "mqtt_handle.h"

static char *generate_heartbeat_json(neu_plugin_t *plugin, UT_array *states)
{
    (void) plugin;
    char *                 version  = NEURON_VERSION;
    neu_json_states_head_t header   = { .version   = version,
                                      .timpstamp = global_timestamp };
    neu_json_states_t      json     = { 0 };
    char *                 json_str = NULL;

    json.n_state = utarray_len(states);
    json.states  = calloc(json.n_state, sizeof(neu_json_node_state_t));
    if (NULL == json.states) {
        return NULL;
    }

    utarray_foreach(states, neu_nodes_state_t *, state)
    {
        int index                  = utarray_eltidx(states, state);
        json.states[index].node    = state->node;
        json.states[index].link    = state->state.link;
        json.states[index].running = state->state.running;
    }

    neu_json_encode_with_mqtt(&json, neu_json_encode_states_resp, &header,
                              neu_json_encode_state_header_resp, &json_str);

    free(json.states);
    return json_str;
}

static char *generate_event_json(neu_plugin_t *plugin, neu_reqresp_type_e event,
                                 void *data, char **topic_p)
{
    char *json_str                                   = NULL;
    int (*encode_fn)(void *json_object, void *param) = NULL;
    union {
        neu_json_add_node_req_t     add_node;
        neu_json_del_node_req_t     del_node;
        neu_json_node_ctl_req_t     node_ctl;
        neu_json_node_setting_req_t node_setting;
    } json_req;

    switch (event) {
    case NEU_REQ_ADD_NODE_EVENT: {
        neu_req_add_node_t *add_node = data;
        json_req.add_node.name       = add_node->node;
        json_req.add_node.plugin     = add_node->plugin;
        encode_fn                    = neu_json_encode_add_node_req;
        *topic_p                     = plugin->config->node_add_topic;
        break;
    }
    case NEU_REQ_DEL_NODE_EVENT: {
        neu_req_del_node_t *del_node = data;
        json_req.del_node.name       = del_node->node;
        encode_fn                    = neu_json_encode_del_node_req;
        *topic_p                     = plugin->config->node_del_topic;
        break;
    }
    case NEU_REQ_NODE_CTL_EVENT: {
        neu_req_node_ctl_t *node_ctl = data;
        json_req.node_ctl.node       = node_ctl->node;
        json_req.node_ctl.cmd        = node_ctl->ctl;
        encode_fn                    = neu_json_encode_node_ctl_req;
        *topic_p                     = plugin->config->node_ctl_topic;
        break;
    }
    case NEU_REQ_NODE_SETTING_EVENT: {
        neu_req_node_setting_t *node_setting = data;
        json_req.node_setting.node           = node_setting->node;
        json_req.node_setting.setting        = node_setting->setting;
        encode_fn                            = neu_json_encode_node_setting_req;
        *topic_p = plugin->config->node_setting_topic;
        break;
    }
    default: {
        plog_error(plugin, "unsupported event:%s",
                   neu_reqresp_type_string(event));
        return NULL;
    }
    }

    neu_json_encode_by_fn(&json_req, encode_fn, &json_str);
    return json_str;
}

static void publish_cb(int errcode, neu_mqtt_qos_e qos, char *topic,
                       uint8_t *payload, uint32_t len, void *data)
{
    (void) qos;
    (void) topic;
    (void) len;

    neu_plugin_t *plugin = data;

    neu_adapter_update_metric_cb_t update_metric =
        plugin->common.adapter_callbacks->update_metric;

    if (0 == errcode) {
        update_metric(plugin->common.adapter, NEU_METRIC_SEND_MSGS_TOTAL, 1,
                      NULL);
    } else {
        update_metric(plugin->common.adapter, NEU_METRIC_SEND_MSG_ERRORS_TOTAL,
                      1, NULL);
    }

    free(payload);
}

static inline int publish(neu_plugin_t *plugin, neu_mqtt_qos_e qos, char *topic,
                          char *payload, size_t payload_len)
{
    neu_adapter_update_metric_cb_t update_metric =
        plugin->common.adapter_callbacks->update_metric;

    int rv = neu_mqtt_client_publish(
        plugin->mqtt_client, qos, topic, (uint8_t *) payload,
        (uint32_t) payload_len, plugin, publish_cb);
    if (0 != rv) {
        plog_error(plugin, "pub [%s, QoS%d] fail", topic, qos);
        update_metric(plugin->common.adapter, NEU_METRIC_SEND_MSG_ERRORS_TOTAL,
                      1, NULL);
        free(payload);
        rv = NEU_ERR_MQTT_PUBLISH_FAILURE;
    }

    return rv;
}

int handle_nodes_state(neu_plugin_t *plugin, neu_reqresp_nodes_state_t *states)
{
    int   rv       = 0;
    char *json_str = NULL;

    if (NULL == plugin->mqtt_client) {
        rv = NEU_ERR_MQTT_IS_NULL;
        goto end;
    }

    if (!neu_mqtt_client_is_connected(plugin->mqtt_client)) {
        // cache disable and we are disconnected
        rv = NEU_ERR_MQTT_FAILURE;
        goto end;
    }

    json_str = generate_heartbeat_json(plugin, states->states);
    if (NULL == json_str) {
        plog_error(plugin, "generate heartbeat json fail");
        rv = NEU_ERR_EINTERNAL;
        goto end;
    }

    char *         topic = plugin->config->heartbeat_topic;
    neu_mqtt_qos_e qos   = NEU_MQTT_QOS0;
    rv       = publish(plugin, qos, topic, json_str, strlen(json_str));
    json_str = NULL;

end:
    utarray_free(states->states);

    return rv;
}

int handle_events(neu_plugin_t *plugin, neu_reqresp_type_e event, void *data)
{
    int   rv       = 0;
    char *json_str = NULL;
    char *topic    = NULL;

    if (NULL == plugin->mqtt_client) {
        rv = NEU_ERR_MQTT_IS_NULL;
        goto end;
    }

    if (!neu_mqtt_client_is_connected(plugin->mqtt_client)) {
        // cache disable and we are disconnected
        rv = NEU_ERR_MQTT_FAILURE;
        goto end;
    }

    json_str = generate_event_json(plugin, event, data, &topic);
    if (NULL == json_str) {
        plog_error(plugin, "generate event:%s json fail",
                   neu_reqresp_type_string(event));
        rv = NEU_ERR_EINTERNAL;
        goto end;
    }

    neu_mqtt_qos_e qos = NEU_MQTT_QOS0;
    rv       = publish(plugin, qos, topic, json_str, strlen(json_str));
    json_str = NULL;

end:
    if (NEU_REQ_NODE_SETTING_EVENT == event) {
        free(((neu_req_node_setting_t *) data)->setting);
    }
    return rv;
}
