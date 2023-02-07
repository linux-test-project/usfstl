/*
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdlib.h>
#include <usfstl/vhost.h>
#include <usfstl/sched.h>
#include <usfstl/list.h>
#include <usfstl/opt.h>
#include <linux/vhost.h>
#include <linux/virtio_ring.h>
#include "main.h"

#define ETHOFFS 10

static USFSTL_LIST(client_list);
static unsigned int clients;
static unsigned int pktdelay;

struct usfstl_net_client {
	struct usfstl_list_entry list;
	char name[30];
	int idx;
	uint8_t addr[6];
	bool addrvalid;
	struct usfstl_vhost_user_dev *dev;
};

struct usfstl_net_packet {
	struct usfstl_job job;
	void *transmitter;
	unsigned int len;
	char name[30];
	uint8_t buf[];
};

static void packet_job_callback(struct usfstl_job *job)
{
	struct usfstl_net_client *client;
	struct usfstl_net_packet *pkt;

	pkt = container_of(job, struct usfstl_net_packet, job);
	usfstl_for_each_list_item(client, &client_list, list) {
		if (client == pkt->transmitter)
			continue;

		if ((pkt->buf[ETHOFFS + 0] & 1) ||
		    (client->addrvalid && memcmp(client->addr, pkt->buf + ETHOFFS, 6) == 0))
			usfstl_vhost_user_dev_notify(client->dev, 0, pkt->buf, pkt->len);
	}

	free(pkt);
}

static void vu_net_client_handle(struct usfstl_vhost_user_dev *dev,
				 struct usfstl_vhost_user_buf *buf,
				 unsigned int vring)
{
	struct usfstl_net_client *client = dev->data;
	struct usfstl_net_packet *pkt;
	size_t sz;

	USFSTL_ASSERT(buf->n_out_sg);

	sz = iov_len(buf->out_sg, buf->n_out_sg);
	pkt = malloc(sizeof(*pkt) + sz);
	memset(pkt, 0, sizeof(*pkt));

	pkt->len = sz;
	pkt->transmitter = client;
	iov_read(pkt->buf, sz, buf->out_sg, buf->n_out_sg);

	if (pktdelay) {
		pkt->job.start = usfstl_sched_current_time(&scheduler) + pktdelay;
		pkt->job.callback = packet_job_callback;
		sprintf(pkt->name, "packet from %d", client->idx);
		pkt->job.name = pkt->name;
		usfstl_sched_add_job(&scheduler, &pkt->job);
	} else {
		packet_job_callback(&pkt->job);
	}

	if (!client->addrvalid && pkt->len >= ETHOFFS + 12) {
		memcpy(client->addr, pkt->buf + ETHOFFS + 6, 6);
		client->addrvalid = true;
		printf("learned addr %.2x:%.2x:%.2x:%.2x:%.2x:%.2x for %s\r\n",
		       client->addr[0], client->addr[1], client->addr[2],
		       client->addr[3], client->addr[4], client->addr[5],
		       client->name);
	}
}

static void vu_net_client_connected(struct usfstl_vhost_user_dev *dev)
{
	struct usfstl_net_client *client;

	client = calloc(1, sizeof(struct usfstl_net_client));
	if (!client)
		return;

	clients++;

	dev->data = client;
	client->dev = dev;
	sprintf(client->name, "net %d", clients);
	client->idx = clients;
	usfstl_list_append(&client_list, &client->list);
	printf("net client %d connected\r\n", clients);
}

static void vu_net_client_disconnected(struct usfstl_vhost_user_dev *dev)
{
	struct usfstl_net_client *client = dev->data;

	clients--;

	usfstl_list_item_remove(&client->list);
	free(client);
}

static const struct usfstl_vhost_user_ops net_ops = {
	.connected = vu_net_client_connected,
	.disconnected = vu_net_client_disconnected,
	.handle = vu_net_client_handle,
};

static struct usfstl_vhost_user_server net_server = {
	.ops = &net_ops,
	.max_queues = 2,
	.input_queues = 1 << 1,
	.scheduler = &scheduler,
	.protocol_features = 1ULL << VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS,
};

USFSTL_OPT_STR("net", 'n', "socket", net_server.socket,
	       "socket for vhost-user networking");

static float delayf = 0.1f;

USFSTL_OPT_FLOAT("net-delay", 0, "delay [ms]", delayf,
	         "delay (in milliseconds, can be float) for packets, default 0.1");

void net_init(void)
{
	if (net_server.socket)
		usfstl_vhost_user_server_start(&net_server);

	// convert to nanoseconds
	pktdelay = 1000 * 1000 * delayf;
}

void net_exit(void)
{
	if (net_server.socket)
		usfstl_vhost_user_server_stop(&net_server);
}
