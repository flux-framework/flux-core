#!/usr/bin/env python3

import flatbuffers
import os 
import json
import sys

here = os.path.abspath(os.path.dirname(__file__))

sys.path.insert(0, here)
from Flux.Framework import Message

# Example of how to use FlatBuffers to create and read binary buffers.

def main():
    builder = flatbuffers.Builder(0)

    # Populate the Flux message
    topic = builder.CreateString('kvs.ping')
    payload = builder.CreateString(json.dumps({"fee": "fi"}))
    Message.MessageStart(builder)
    Message.MessageAddTopic(builder, topic)
    Message.MessageAddPayload(builder, payload)

    # These are technically the defaults
    Message.MessageAddNodeid(builder, 0)
    Message.MessageAddFlags(builder, 0)
    msg = Message.MessageEnd(builder)
    builder.Finish(msg)
    

    # This is a FlatBuffer that we could store on disk or send over a network.
    # Instead, we are going to access this buffer right away (as if we just
    # received it). This is a byte array
    buf = builder.Output()

    # Note: We use `0` for the offset here, since we got the data using the
    # `builder.Output()` method. This simulates the data you would store/receive
    # in your FlatBuffer. If you wanted to read from the `builder.Bytes` directly,
    # you would need to pass in the offset of `builder.Head()`, as the builder
    # actually constructs the buffer backwards.
    recreated = Message.Message.GetRootAsMessage(buf, 0)
    print("Payload: %s" % recreated.Payload())
    print("Topic: %s" % recreated.Topic())
    print("Flags: %s" % recreated.Flags())
    print("Nodeid: %s" % recreated.Nodeid())

if __name__ == "__main__":
    main()