# Vsock Proxy

Vsock Proxy to proxy TCP connection to vsock and vice versa.

This is intended for UID2 traffic forwarding between host and AWS Nitro Enclaves.

## How to build

```
mkdir uid2-aws-enclave-vsockproxy/build
cd uid2-aws-enclave-vsockproxy/build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
make
make test
```

## How to use

Create a config file using syntax similar to YAML (not real YAML).

```
# config.notyaml
---
http-service:
  service: direct
  listen: tcp://0.0.0.0:80
  connect: vsock://42:8080

sockx-proxy:
  service: direct
  listen: vsock://3:3305
  connect: tcp://127.0.0.1:3305

tcp-to-tcp:
  service: direct
  listen: tcp://127.0.0.1:4000
  connect: tcp://10.10.10.10:4001
```

This configuration file instructs the proxy to:
 - listen on all IPv4 addresses on TCP port 80 and forward connections to vsock address 42:8080;
 - listen on vsock address 3:3305 and forward connections to localhost (IPv4) TCP port 3305;
 - listen on localhost (IPv4) TCP port 4000 and forward connections to 10.10.10.10 TCP port 4001.

Start vsock-bridge:

```
./vsock-bridge --config config.notyaml
```

Run `./vsock-bridge -h` to get details for other supported command line options.

## Logging

In daemon mode the proxy logs to system (with ident `vsockpx`). In frontend mode logs go to stdout.

The log level can be configured through command line option `--log-level`.
