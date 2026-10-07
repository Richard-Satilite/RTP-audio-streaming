#!/bin/sh
# Diagnostic helper. The project receiver already plays the stream directly.
# For a generic RTP player, use tools/stream_l16.sdp and point it at the sender host.
set -eu
printf '%s\n' 'Use: ./build/ac_receiver 5004'
