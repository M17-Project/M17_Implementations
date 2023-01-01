#!/bin/bash -x

# This script ASSUMEs:
# - you're running this from the 'testing' dir under the 'SP5WWP' dir
# - you have made the ../m17-coder/m17-coder-sym coder program
# - you have made the ../m17-decoder/m17-decoder-sym coder program
# - you have loaded the ./grc/m17_streamer.grc program into gnuradio-companion
# - you have changed the File Sink's path to /tmp/fifo1
# - you have used Run->Generate to create the ../grc/m17_streamer.py file

# Check for the bits needed
if [ ! -x ../m17-coder/m17-coder-sym ]; then
    echo "coder executable not available"
    exit -1
fi
if [ ! -x ../m17-decoder/m17-decoder-sym ]; then
    echo "decoder executable not available"
    exit -2
fi
if [ ! -f ../grc/m17_streamer.py ]; then
    echo "m17_streamer.py program not available"
    echo "use gnradio-companion to load m17_streamer.grc and generate it"
    exit -3
fi

# Make sure pipes (named FIFOs) exist, and if not, create them
if [ ! -p /tmp/fifo1 ]; then
    mkfifo /tmp/fifo1
fi
if [ ! -p /tmp/fifo2 ]; then
    mkfifo /tmp/fifo2
fi

# Run the encoder
../m17-coder/m17-coder-sym < /tmp/fifo1 > /tmp/fifo2 &
EPID=$!

# Run the decoder 
../m17-decoder/m17-decoder-sym < /tmp/fifo2 &
DPID=$!

# Run the flowgraph
python3 ../grc/m17_streamer.py

# Try to kill any remaining background processes after the flowgraph exits
kill $DPID $EPID
