#include <uwsgi.h>

extern struct uwsgi_server uwsgi;

struct ganglia_config {
	char *host;
	uint64_t host_len;
	char *groups;
	size_t groups_len;
	int fd;
	union uwsgi_sockaddr addr;
        socklen_t addr_len;
	struct uwsgi_buffer *ub_metadata;
	struct uwsgi_buffer *ub_data;
	struct uwsgi_stats_pusher_instance *uspi;
};

/*
	ganglia uses xdr encoding for its packets

	there are two packets: metadata and data, we need to generate both
*/

static int xdr_uint(struct uwsgi_buffer *ub, uint32_t n) {
	return uwsgi_buffer_u32be(ub, n);
}

static int xdr_str(struct uwsgi_buffer *ub, char *buf, uint32_t len) {
	char *pad = "\0\0\0\0";
	if (xdr_uint(ub, len)) return -1;
	if (len == 0) return 0;
	if (uwsgi_buffer_append(ub, buf, len)) return -1;
	uint32_t pad_len = ((len+3) / 4) * 4;	
	if (pad_len - len > 0) {
		if (uwsgi_buffer_append(ub, pad, pad_len - len)) return -1;
	}
	return 0;
}

static int ganglia_metric(struct ganglia_config *gc, struct uwsgi_metric *um) {
	// build the packet (it will be sent via udp)

	// metadata
	// int(128) + hostname + metric_name + spoof(bool) + type(double) + metric_name, units(empty), SLOPE, tmax(freq), dmax(0) + int(1) + "GROUP" + groups
	struct uwsgi_buffer *ub = gc->ub_metadata;
	ub->pos = 0;
	if (xdr_uint(ub, 128)) return -1;
	if (xdr_str(ub, gc->host, gc->host_len)) return -1;
	if (xdr_str(ub, um->name, um->name_len)) return -1;
	// assume the host is spoofed
	if (xdr_uint(ub, 1)) return -1;
	if (xdr_str(ub, "double", 6)) return -1;
	if (xdr_str(ub, um->name, um->name_len)) return -1;
	if (xdr_str(ub, "", 0)) return -1;
	if (um->type == UWSGI_METRIC_COUNTER) {
		if (xdr_uint(ub, 1)) return -1;
	}
	else {
		if (xdr_uint(ub, 3)) return -1;
	}
	if (xdr_uint(ub, gc->uspi->freq)) return -1;
	if (xdr_uint(ub, 0)) return -1;
	if (gc->groups_len > 0) {
		if (xdr_uint(ub, 1)) return -1;
		if (xdr_str(ub, "GROUP", 5)) return -1;
		if (xdr_str(ub, gc->groups, gc->groups_len)) return -1;
	}
	else {
		if (xdr_uint(ub, 0)) return -1;
	}

	// data
	ub = gc->ub_data;
	ub->pos = 0;
	if (xdr_uint(ub, 128+5)) return -1;
	if (xdr_str(ub, gc->host, gc->host_len)) return -1;
	if (xdr_str(ub, um->name, um->name_len)) return -1;
	if (xdr_uint(ub, 1)) return -1;
	if (xdr_str(ub, "%%s", 0)) return -1;
	char *num64 = uwsgi_64bit2str(*um->value);
	int ret = xdr_str(ub, num64, strlen(num64));
	free(num64);
	return ret;
}

// main function for sending stats
static void stats_pusher_ganglia(struct uwsgi_stats_pusher_instance *uspi, time_t now, char *json, size_t json_len) {
	// on setup error, we simply exit to avoid leaks
	if (!uspi->configured) {
		struct ganglia_config *gc = uwsgi_calloc(sizeof(struct ganglia_config));
		gc->host = uwsgi.hostname;
		gc->host_len = uwsgi.hostname_len;
		char *node = NULL;
		if (strchr(uspi->arg, '=')) {	
			if (uwsgi_kvlist_parse(uspi->arg, strlen(uspi->arg), ',', '=',
				"addr", &node,
				"node", &node,
				"host", &gc->host,
				"group", &gc->groups,
				"groups", &gc->groups,
				NULL)) {
				uwsgi_log("[uwsgi-ganglia] invalid keyval syntax\n");
				exit(1);
			}
			if (gc->host) gc->host_len = strlen(gc->host);
			if (gc->groups) gc->groups_len = strlen(gc->groups);
		}
		else {
			node = uspi->arg;
		}
		if (!node) {
			uwsgi_log("[uwsgi-ganglia] you need to specify an address\n");
			exit(1);
		}
		char *colon = strchr(node, ':');
		if (!colon) {
			uwsgi_log("[uwsgi-ganglia] invalid address\n");
			exit(1);
		}
		gc->addr_len = socket_to_in_addr(node, colon, 0, &gc->addr.sa_in);
		gc->fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (gc->fd < 0) {
			uwsgi_error("stats_pusher_ganglia()/socket()");
			exit(1);
		}
		uwsgi_socket_nb(gc->fd);
		gc->ub_metadata = uwsgi_buffer_new(uwsgi.page_size);
		gc->ub_data = uwsgi_buffer_new(uwsgi.page_size);
		gc->uspi = uspi;
		uspi->data = gc;
		uspi->configured = 1;
		uwsgi_log_verbose("[uwsgi-ganglia] configured node %s\n", node);
	}


	struct ganglia_config *gc = (struct ganglia_config *) uspi->data;
	struct uwsgi_metric *um = uwsgi.metrics;
	while(um) {
		uwsgi_rlock(uwsgi.metrics_lock);
		int ret = ganglia_metric(gc, um);
		uwsgi_rwunlock(uwsgi.metrics_lock);
		if (ret) {
			uwsgi_log_verbose("[uwsgi-ganglia] unable to generate packet for %.*s\n", um->name_len, um->name);
		}
		else {
			// send the packet
			if (sendto(gc->fd, gc->ub_metadata->buf, gc->ub_metadata->pos, 0, (struct sockaddr *) &gc->addr.sa_in, gc->addr_len) < 0) {
                		uwsgi_error("stats_pusher_ganglia()/sendto()");
        		}
			if (sendto(gc->fd, gc->ub_data->buf, gc->ub_data->pos, 0, (struct sockaddr *) &gc->addr.sa_in, gc->addr_len) < 0) {
                		uwsgi_error("stats_pusher_ganglia()/sendto()");
        		}
		}
		um = um->next;
	}

}

static void ganglia_register(void) {
        struct uwsgi_stats_pusher *usp = uwsgi_register_stats_pusher("ganglia", stats_pusher_ganglia);
        // we use a custom format not the JSON one
        usp->raw = 1;
}

struct uwsgi_plugin ganglia_plugin = {
        .name = "ganglia",
        .on_load = ganglia_register,
};

