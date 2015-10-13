import json
import multiprocessing
import subprocess
import os
import time

#fallback if no CPU count is specified
def get_CPU_count ():
    try:
        return multiprocessing.cpu_count()
    except (ImportError, NotImplementedError):
        return 0

def kill_server(proc):
	print "Shutting down server..."
	proc.terminate()
	time.sleep(0.5)

	if proc.poll() is None:
		print "Server still running. Waiting 2 sec and forcing shutdown..."
		time.sleep(2)
		proc.kill()

	if proc.poll() is None:
		raise Exception("Something went wrong shutting down the server...")
	else:
		print "Server shut down..."
            
#load settings from json file
def load_settings(path):
    with open(path) as data_file:
        data = json.load(data_file)

    if (type(data) is dict) and ("hosts" in data) and (type(data["hosts"]) is list):
        hosts = data["hosts"]
    else:
        print "No hosts specified"
        return

    server = data["hyrise_default"]
    dispatcher_port = data["port"] if "port" in data else 8888
    node_id = 0
    processes = []
    try:
        for host in hosts:
            settings = [server, "--port="+str(host["port"]), "--corecount="+str(host["core_count"]), "--coreoffset="+str(host["core_offset"]), "--nodeId="+str(node_id), "--dispatcherport="+str(dispatcher_port)]
            if "numa_nodes" in host:
                settings.append("--nodes="+host["numa_nodes"])
                settings.append("--memorynodes="+host["numa_nodes"])
            print "Starting server: " + server, host["port"]
            proc = subprocess.Popen(settings, stdout=open(os.devnull, 'w'), stderr=open('logfile.log', 'a+'), preexec_fn=os.setpgrp)
            processes.append(proc)
            node_id += 1
            time.sleep(1)

        dispatcher = data["dispatcher"]
        print "Starting dispatcher on port %d" % dispatcher_port 
        proc = subprocess.Popen([dispatcher, str(dispatcher_port), path], preexec_fn=os.setpgrp)
        processes.append(proc)
    except:
        for proc in processes:
            kill_server(proc)
        raise
    return processes

CONFIG_PATH = './settings.json'

if __name__ == '__main__':
    print 'Start Hyirise-R Cluster with following settings: %s  ...' % CONFIG_PATH
    processes = load_settings(CONFIG_PATH)
    while True:
	print 'Enter "exit" or "q" for Cluster shutdown'
        cmd = raw_input(">> ")
        if (cmd == "exit") or (cmd == "q"):
            for proc in processes:
                kill_server(proc)
            break

