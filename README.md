# RTP Audio Streaming

Experimental unicast system-audio streaming over RTP/UDP.

## Features

- Windows WASAPI system-output loopback capture.
- Linux/PipeWire/PulseAudio monitor-source capture when exposed by the audio backend.
- S16 PCM at 48 kHz stereo.
- L16 RTP payloads.
- Dedicated sender thread with bounded SPSC ring buffer between audio capture and network I/O.
- Dedicated receiver thread with RTP validation.
- Jitter/reordering buffer with configurable prebuffer.
- Packet loss, duplicate, late and reorder counters.
- RFC 3550-style interarrival jitter estimate.
- Playback ring buffer and local audio playback through miniaudio.
- CTest coverage for RTP, L16, ring buffer and jitter logic.

## Build

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Sender

Windows:

```text
build\\ac_sender.exe 192.168.1.20:5004
```

Linux/PipeWire/PulseAudio:

```sh
./build/ac_sender 192.168.1.20:5004
```

If the automatic monitor selection does not find the desired output source, enumerate devices:

```sh
./build/ac_sender 127.0.0.1:5004 --list-devices
```

Then select a monitor/source by substring:

```sh
./build/ac_sender 192.168.1.20:5004 --device 'Monitor of'
```

The sender never performs UDP I/O from the audio callback. The callback writes PCM to a bounded ring buffer; the sender thread packetizes and transmits it.

## Receiver

On the destination device:

```sh
./build/ac_receiver 5004
```

Optional prebuffer:

```sh
./build/ac_receiver 5004 --prebuf-ms 80
```

Optional SSRC lock:

```sh
./build/ac_receiver 5004 --ssrc 123456
```

The receiver prints final statistics when Enter is pressed, for example:

```text
RTP stats: received=20000 accepted=19997 lost=3 late=0 duplicate=0 reordered=4 malformed=0 jitter=0.420 ms buffered=0
```

## Network test

The RTP stream can also be inspected with Wireshark. Capture UDP port 5004 and decode the UDP payload as RTP when the dynamic payload type is not detected automatically. The SDP description in `tools/stream_l16.sdp` identifies payload type 96 as `L16/48000/2`.

## Platform limitation

miniaudio exposes native loopback only through WASAPI. On Linux, system-output capture depends on the PulseAudio/PipeWire monitor source being available. ALSA-only configurations without a monitor/virtual capture source require an OS-specific audio-routing setup before this application can capture the system output.
