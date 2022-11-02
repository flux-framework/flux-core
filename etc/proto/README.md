# Flatbuffers

We have a [schema.fbs](schema.fbs) that defines a basic schema for a Flux message.
After installing [Flatbuffer](https://google.github.io/flatbuffers/flatbuffers_guide_tutorial.html) 
(which comes in the devcontainer for Flux core) we can 
compile the schema into our language of choice, C++ for Flux:\

```console
$ flatc --cpp schema.fbs
```

This will generate [schema_generated.h]. We can try for Python too!

```console
$ flatc --python schema.fbs
```

And this generates a Python module for the entire namespace [Flux](Flux).

```
# tree Flux/
Flux/
├── Framework
│   ├── Message.py
│   └── __init__.py
└── __init__.py
```

This next demo script doesn't use Flux's already existing Python API (but it could).
Since flatbuffers doesn't come with an easy server/client implementation I'm not sure
the direction we'd want to take here. Here is how to serialize (and unserialize)
the buffer, just as an example:

```console
# python3 run-example.py 
Payload: b'{"fee": "fi"}'
Topic: b'kvs.ping'
Flags: 0
Nodeid: 0
```