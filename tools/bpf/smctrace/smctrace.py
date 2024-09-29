import json
import time
from bcc import BPF

bpf_program_file = "smc_trace.c"
b = BPF(src_file=bpf_program_file)

def generate_packet_data(b):
    packet_data = []
    try:
        packet_map = b.get_table("packet_map")
        for key, value in packet_map.items():
            packet_info = {
                "packet_id": {
                    "portpair": key.portpair,
                    "addrpair": key.addrpair,
                    "protocol": key.protocol
                },
                "packet_trace": {
                    "hooks_time": [
                        {"hook": value.hooks_time[0].hook.decode('utf-8', 'replace').rstrip('\x00'), "timestamp": value.hooks_time[0].timestamp},
                        {"hook": value.hooks_time[1].hook.decode('utf-8', 'replace').rstrip('\x00'), "timestamp": value.hooks_time[1].timestamp},
                        {"hook": value.hooks_time[2].hook.decode('utf-8', 'replace').rstrip('\x00'), "timestamp": value.hooks_time[2].timestamp},
                        {"hook": value.hooks_time[3].hook.decode('utf-8', 'replace').rstrip('\x00'), "timestamp": value.hooks_time[3].timestamp}
                    ]
                }
            }
            packet_data.append(packet_info)
    except Exception as e:
        print(f"Error generating packet data: {e}")
    return packet_data

def save_to_json(filename, data):
    try:
        with open(filename, 'a') as f:  
            json.dump(data, f, indent=4)
            f.write('\n')  
    except Exception as e:
        print(f"Error saving data to file: {e}")

if __name__ == "__main__":
    json_file = f'latency_data_{time.strftime("%Y-%m-%d_%H-%M-%S")}.json'
    
    cache = []
    
    try:
        print("smctrace starts running...")
        while True:
            data = generate_packet_data(b)
            cache.extend(data)         
            if len(cache) >= 100:
                save_to_json(json_file, cache)
                cache.clear()
            time.sleep(10)
    
    except KeyboardInterrupt:
        print("terminated by user, loading final data...")
        if cache:
            save_to_json(json_file, cache)
        print(f"all data is saved to: {json_file}")
