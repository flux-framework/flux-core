#!/usr/bin/env python3

import capnp
import flux 
import json
from flux.rpc import RPC
 
# Load the schema (this method is more reliable)
capnp.remove_import_hook()
schema_capnp = capnp.load('schema.capnp')

# You'll need flux bindings on your path and start flux
# export PYTHONPATH=/usr/local/lib/python3.8/site-packages
# flux start --test-size=4

class TestServer(schema_capnp.FluxMessageInterface.Server):
    def __init__(self):
        self.handle = flux.Flux()

    def get(self, m, **kwargs):
        """
        m should be a FluxMessage with topic, nodeid, etc.
        """
        print(f"Topic: {m.topic}")
        print(f"Payload: {m.payload}")
        print(f"NodeId: {m.nodeid}")
        print(f"Flags: {m.flags}")
        rpc = RPC(self.handle, m.topic, m.payload, m.nodeid, m.flags)
        # This loads but is expecting a string (text)
        return json.dumps(rpc.get())

def main():
    # Let's write / read from a server
    write = '127.0.0.1:12345'

    # This can also be a socket pair I think?
    # read, write = socket.socketpair()
    server = capnp.TwoPartyServer(write, bootstrap=TestServer())
    print('Starting Flux Message Interface! pkill python3 in another terminal to 🔴')
    server.run_forever()

if __name__ == "__main__":
    main()
