#!/usr/bin/env python3
#
#  Simple test for issue #3470: multiple threads calling reactor_run
#   causes libev assertion failure.
#
from queue import Queue
import sys
import threading

import flux


def cb(f, watcher, msg, i):
    print(f"{i}: {msg.payload_str}")
    watcher.stop()


def get_events(i, queue):
    f = flux.Flux()
    f.event_subscribe("test-event")
    queue.put(True)
    f.msg_watcher_create(cb, topic_glob="test-event", args=i).start()
    f.reactor_run()


def main():
    nthreads = 2
    threads = []
    queue = Queue()
    for i in range(0, nthreads):
        thread = threading.Thread(
            target=get_events,
            args=(
                i,
                queue,
            ),
        )
        thread.start()
        threads.append(thread)

    print(f"starting {nthreads} threads", file=sys.stderr)

    # Ensure threads have subscribed to 'test-event'
    for thread in threads:
        queue.get()
        print(f"got response from {thread}", file=sys.stderr)

    print(f"{nthreads} threads started", file=sys.stderr)

    flux.Flux().event_send("test-event", "hello")

    print(f"published test-event", file=sys.stderr)

    for thread in threads:
        thread.join()

    print("Done", file=sys.stderr)


if __name__ == "__main__":
    main()
