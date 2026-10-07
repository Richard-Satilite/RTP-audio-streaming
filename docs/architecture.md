# Architecture

The streaming path is deliberately split into real-time and non-real-time stages.

```text
OS system-audio output
        |
        v
 miniaudio loopback/monitor
        |
        | PCM S16 stereo, 48 kHz
        v
 audio callback
        |
        v
 SPSC ring buffer
        |
        v
 sender thread
        |
        +-- L16 encoder
        +-- RTP packetizer
        +-- UDP sendto()
        |
        v
      network
        |
        v
 UDP receiver thread
        |
        +-- RTP validation
        +-- SSRC/PT validation
        +-- sequence/loss detection
        +-- jitter/reordering buffer
        |
        v
 playback PCM ring buffer
        |
        v
 miniaudio playback callback
        |
        v
 local audio output
```

## Why the capture callback does not send UDP

The audio callback must return quickly and deterministically. It only copies complete PCM frames into the SPSC ring buffer. Network I/O, RTP encoding and statistics happen on the sender thread.

The ring is bounded to approximately 500 ms. If it fills, new audio frames are dropped and the sender reports the number of dropped frames at shutdown rather than allowing latency to grow without bound.

## Sender platform behavior

- Windows: WASAPI loopback captures the selected/default playback endpoint.
- Linux/Unix: miniaudio's native loopback API is not available for the common ALSA/PulseAudio backends. The sender therefore looks for a PulseAudio/PipeWire compatibility-layer monitor source and opens it as a capture device. `--device` can select a specific monitor.

## Receiver

The receiver keeps up to 128 RTP packets in the jitter buffer. It waits for a configurable prebuffer, reorders packets by sequence number, and declares a packet lost when the reorder window has advanced far enough. Lost packets are concealed as silence so playback timing remains continuous.

RFC 3550-style interarrival jitter is calculated from RTP timestamps and packet arrival time and reported in milliseconds when the receiver stops.
