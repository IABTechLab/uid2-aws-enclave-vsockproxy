# Vsock Proxy

Vsock Proxy to proxy TCP connection to vsock and vise versa.

This is intended for UID2 traffic forwarding between host and AWS Nitro Enclaves.

## How to build

```
mkdir uid2-aws-enclave-vsockproxy/build
cd uid2-aws-enclave-vsockproxy/build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
make
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
```

Start vsockpx

```
./vsockpx --config config.notyaml
```

Traffic hitting host:80 port will be forwarded to vsock address 42:8080.
