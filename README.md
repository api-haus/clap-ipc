# clap-ipc

The **GPL-3.0 host** of the CLAP-router IPC seam. Runs as a standalone process:
dlopens CLAP plugins, builds an RCU instrument/effect graph, renders through a
device or offline-capture backend, and serves a client over a control channel +
shared-memory ring. It is plugin-generic — every plugin is loaded by path +
factory index; no plugin name is baked in.

This is the only GPL-licensed package in the project, because it links the CLAP
SDK and loads GPL plugins in its own address space. Clients speak to it across a
process boundary and link none of its code (see the seam below), so they stay
MIT. This repository is the corresponding source for any distributed `clap-ipc`
binary (GPL-3.0 §3/§6).

**License:** GPL-3.0. Submodules: the CLAP SDK ([free-audio/clap](https://github.com/free-audio/clap))
and [music-router](https://github.com/api-haus/music-router) (vendored so this builds standalone).

| repo | license | role |
|---|---|---|
| [music-router](https://github.com/api-haus/music-router) | MIT | wire codec (submodule of this repo) |
| [clap-ipc-client](https://github.com/api-haus/clap-ipc-client) | MIT | client library + C ABI |
| [clap-ipc](https://github.com/api-haus/clap-ipc) | GPL-3.0 | this — the host |
| [is.zori.unity-clap-router](https://github.com/api-haus/is.zori.unity-clap-router) | MIT | Unity/C# bindings |

## Build

```bash
git clone --recursive https://github.com/api-haus/clap-ipc
cmake -S clap-ipc -B build && cmake --build build   # -> clap-ipc (PortAudio device backend if available)
```
