# DNS Server from Scratch

I am building a DNS server in C from scratch to deepen my understanding of networking, memory, and computer science fundamentals so I can become better at building systems.

[![progress-banner](https://backend.codecrafters.io/progress/dns-server/7ba68edb-607d-4eec-8192-614cb5ba8504)](https://app.codecrafters.io/users/Saitejar0203)

All eight stages of the [CodeCrafters DNS challenge](https://app.codecrafters.io/courses/dns-server/overview) are complete. Each stage has its own implementation commit: UDP listener, header encoding, question encoding, answer encoding, header parsing, question parsing, compressed names, and forwarding.

## Run

Requires a C23 compiler and CMake 3.13 or later. No external C libraries are needed.

```sh
./your_program.sh
# Forward queries to an existing DNS resolver instead of returning the demo address:
./your_program.sh --resolver 1.1.1.1:53
```

The server listens on UDP port 2053. Without `--resolver`, it returns the demonstration IPv4 address `8.8.8.8`. In forwarding mode it splits multiple questions into separate upstream queries and merges the returned A records, preserving record values and TTLs.

```sh
dig @127.0.0.1 -p 2053 example.com A +noedns
```

On a Mac where Git or the compiler selects an unconfigured Xcode installation, prefix commands with `DEVELOPER_DIR=/Library/Developer/CommandLineTools`.

## Verify

The integration tests use Python's standard library as a UDP client and controlled resolver; the server itself is C.

```sh
cmake -S . -B build
cmake --build build
python3 tests/test_packets.py compression
python3 tests/test_forwarding.py
codecrafters submit
```

Tests cover variable names, compressed suffixes, malformed pointers, independent upstream queries, compressed upstream responses, and merged records. The C code was also checked with address and undefined-behavior sanitizers.

This is a learning implementation for IPv4 A records: a single-threaded UDP loop, up to 16 questions and 4096-byte packets, with a two-second upstream receive timeout. It does not implement TCP fallback, caching, DNSSEC, or a full recursive resolver. The earlier Python starter is preserved locally and in Git history; C is the maintained implementation.
