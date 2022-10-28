# Captain Proto

> No... Captain Flux! He's a hero! 👩‍🚒️

I am following the [installation](https://capnproto.org/install.html) instructions here.
The tool should be installed to the devcontainers environment. Then we define our protocol buffer
file as [schema.capnp](schema.capnp) and use within Python. This is a fairly simple setup
(to test things out and learn about this library!). First, you'll need flux bindings on your
path and to start flux:

```consoel
export PYTHONPATH=/usr/local/lib/python3.8/site-packages
flux start --test-size=4
```

Note that the path will be default added in the devcontainers environment soon!
To start the server portion:

```console
$ python3 run-server.py
Starting Flux Message Interface! pkill python3 in another terminal to 🔴
```

Yes - nothing elegant here - we are using a pkill to python3 to kill the server! 💀 
That will start the server running on `127.0.0.1:12345`. Then we can test the client with [run-client.py](run-client.py):

```console
$ python3 run-client.py 
🍓 Created client <capnp.lib.capnp.TwoPartyClient object at 0x7f071c5e3220>
{'fee': 'fi', 'route': 'de5d06ad!e058728f!0ae58f31!ee699da9', 'userid': 0, 'rolemask': 1}
```

What we basically do via the client is prepare the request, send it to the server, and then
print what is returned. The server handles created a Flux RPC and running "get" for it.
It's super simple, but kind of neat! I think we can make cooler stuff off of this 
basic idea.