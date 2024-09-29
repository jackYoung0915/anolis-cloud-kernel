#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/ktime.h>

struct hook_time_pair {
	char hook[32];
	uint64_t timestamp;
};

struct packet_trace {
	struct hook_time_pair hooks_time[4];
};

struct packet_id {
	uint32_t portpair;
	uint64_t addrpair;
	uint16_t protocol;
}

struct packet {
	struct packet_id p_id;
	struct packet_trace p_trace;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, struct packet_id);
    __type(value, struct packet_trace);
} packet_map SEC(".maps");

static __always_inline void get_hook_message(struct packet *p, const char *hook_name,
					      uint32_t portpair, uint64_t addrpair,
					      uint16_t protocol, int hook_index) {

	p->p_trace.hooks_time[hook_index].timestamp = bpf_ktime_get_ns();

	bpf_probe_read_str(p->p_trace.hooks_time[hook_index].hook, 
					sizeof(p->p_trace.hooks_time[hook_index].hook), 
					hook_name);

	p->p_id.portpair = portpair;
	p->p_id.addrpair = addrpair;
	p->p_id.protocol = protocol;
}

TRACEPOINT_PROBE(smc_msg_event, smc_send_start) {
	struct packet p;
	uint32_t portpair;
	uint64_t addrpair;
	uint16_t protocol;

	uint32_t *portpair_ptr = &args->smc->sk->sk_portpair;
	uint64_t *addrpair_ptr = &args->smc->sk->sk_addrpair;
	uint16_t *protocol_ptr = &args->smc->sk->protocol;

	bpf_probe_read(&portpair, sizeof(portpair), portpair_ptr);
	bpf_probe_read(&addrpair, sizeof(addrpair), addrpair_ptr);
	bpf_probe_read(&protocol, sizeof(protocol), protocol_ptr);

	get_hook_message(&p, "smc_send_start",
					portpair, addrpair, protocol, 0);

    struct packet_trace *existing_trace;
    existing_trace = bpf_map_lookup_elem(&packet_map, &p.p_id);

    if (!existing_trace) {
        bpf_map_update_elem(&packet_map, &p.p_id, &p.p_trace, BPF_ANY);
    } else {
        struct packet_trace updated_trace = *existing_trace;
    	updated_trace.hooks_time[0] = p.p_trace.hooks_time[0];
    	bpf_map_update_elem(&packet_map, &p.p_id, &updated_trace, BPF_ANY);
    }
    return 0;
}

TRACEPOINT_PROBE(smc_msg_event, smc_tx_sendmsg) {
	struct packet p;
	uint32_t portpair;
	uint64_t addrpair;
	uint16_t protocol;

	uint32_t *portpair_ptr = &args->smc->sk->sk_portpair;
	uint64_t *addrpair_ptr = &args->smc->sk->sk_addrpair;
	uint16_t *protocol_ptr = &args->smc->sk->protocol;

	bpf_probe_read(&portpair, sizeof(portpair), portpair_ptr);
	bpf_probe_read(&addrpair, sizeof(addrpair), addrpair_ptr);
	bpf_probe_read(&protocol, sizeof(protocol), protocol_ptr);

	get_hook_message(&p, "smc_tx_sendmsg",
					portpair, addrpair, protocol, 1);

    struct packet_trace *existing_trace;
    existing_trace = bpf_map_lookup_elem(&packet_map, &p.p_id);

    if (!existing_trace) {
        bpf_map_update_elem(&packet_map, &p.p_id, &p.p_trace, BPF_ANY);
    } else {
        struct packet_trace updated_trace = *existing_trace;
    	updated_trace.hooks_time[1] = p.p_trace.hooks_time[1];
    	bpf_map_update_elem(&packet_map, &p.p_id, &updated_trace, BPF_ANY);
    }
    return 0;
}

TRACEPOINT_PROBE(some_event, smc_RDMA_sendmsg)
{
	struct packet p;
	uint32_t portpair;
	uint64_t addrpair;
	uint16_t protocol;

	uint32_t *portpair_ptr = &args->smc->sk->sk_portpair;
	uint64_t *addrpair_ptr = &args->smc->sk->sk_addrpair;
	uint16_t *protocol_ptr = &args->smc->sk->protocol;

	bpf_probe_read(&portpair, sizeof(portpair), portpair_ptr);
	bpf_probe_read(&addrpair, sizeof(addrpair), addrpair_ptr);
	bpf_probe_read(&protocol, sizeof(protocol), protocol_ptr);

	get_hook_message(&p, "smc_RDMA_sendmsg", 
					portpair, addrpair, protocol, 2);

    struct packet_trace *existing_trace;
    existing_trace = bpf_map_lookup_elem(&packet_map, &p.p_id);

    if (!existing_trace) {
        bpf_map_update_elem(&packet_map, &p.p_id, &p.p_trace, BPF_ANY);
    } else {
    	struct packet_trace updated_trace = *existing_trace;
    	updated_trace.hooks_time[2] = p.p_trace.hooks_time[2];
    	bpf_map_update_elem(&packet_map, &p.p_id, &updated_trace, BPF_ANY);
    }
    return 0;
}

TRACEPOINT_PROBE(smc_msg_event, smc_send_complete)
{
	struct packet p;
	uint32_t portpair;
	uint64_t addrpair;
	uint16_t protocol;

	uint32_t *portpair_ptr = &args->smc->sk->sk_portpair;
	uint64_t *addrpair_ptr = &args->smc->sk->sk_addrpair;
	uint16_t *protocol_ptr = &args->smc->sk->protocol;

	bpf_probe_read(&portpair, sizeof(portpair), portpair_ptr);
	bpf_probe_read(&addrpair, sizeof(addrpair), addrpair_ptr);
	bpf_probe_read(&protocol, sizeof(protocol), protocol_ptr);

	get_hook_message(&p, "smc_send_complete", 
					portpair, addrpair, protocol, 3);

    struct packet_trace *existing_trace;
    existing_trace = bpf_map_lookup_elem(&packet_map, &p.p_id);

    if (!existing_trace) {
        bpf_map_update_elem(&packet_map, &p.p_id, &p.p_trace, BPF_ANY);
    } else {
        struct packet_trace updated_trace = *existing_trace;
    	updated_trace.hooks_time[3] = p.p_trace.hooks_time[3];
    	bpf_map_update_elem(&packet_map, &p.p_id, &updated_trace, BPF_ANY);
    }
    return 0;
}
