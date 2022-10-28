import capnp
import json

# Load the schema (this method is more reliable)
capnp.remove_import_hook()
schema_capnp = capnp.load('schema.capnp')


# Create a new message
# message = schema_capnp.FluxMessage.new_message()
 

def main():

    client = capnp.TwoPartyClient('127.0.0.1:12345')
    print(f"🍓 Created client {client}")

    # Bootstrap the server capability and cast it to the FluxMessageInterface
    msg = client.bootstrap().cast_as(schema_capnp.FluxMessageInterface)
    request = msg.get_request()
    
    # Prepare the flux message!
    request.m.topic = "kvs.ping"
    request.m.payload = json.dumps({"fee": "fi"})
    promise = request.send()

    # asynchronous
    # promise.then(lambda ret: print(ret)).wait()

    # synchronous
    result = promise.wait()
    print(json.loads(result.x))
    
if __name__ == "__main__":
    main()