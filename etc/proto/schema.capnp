@0xf7298542d948dda7;

interface FluxMessageInterface {
  get @0 (m: FluxMessage) -> (x: Text);
}

struct FluxMessage {
  topic @0 :Text;
  payload @1 :Text;
  nodeid @2 :Int16 = 0;
  flags @3 :Int16 = 0;
}
