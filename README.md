# M17_Implementations
Various implementations

## Woj's implementation (/SP5WWP)
### Overview
Written in C, it has all the components described by the protocol's specification of the stream and packet modes:
- convolutional encoder with soft Viterbi decoder (utilizing fixed point arithmetic),
- Golay encoder with soft decoder (fixed point),
- bit interleaver and randomizer,
- cyclic redundancy check (CRC) calculation (both LSF and arbitrary input),
- callsign encoder and decoder

There's no support for **any** encryption yet.

### Building
First, build the shared object `libm17.so`:
```
cd M17_Implementations/libm17
make
make install
sudo ldconfig
```
Then, `cd` back up to the directory of interest and ```make``` again.

### Capabilities
Four executables are available:
- `m17-coder-sym` is used to convert a raw binary data bitstream to symbols. Symbol stream has to be
filtered with an appropriate filter before transmission, see the specification document for details.
- `m17-decoder-sym` decodes a stream of floats at `stdin`, one sample per symbol. After a valid
syncword is detected, decoding process starts. The program expects a stream of synchronized symbols
at the input. See the `/grc/symbol_recovery.grc` file for details.
- `m17-packet-encode` is a handy tool for generating baseband (or a symbol stream, if needed) for
M17 packets. The program expects a limited stream of raw data at the stdin. The number of bytes is set
with the `-n` parameter, range 1 to 800.
- `m17-packet-decode` decodes incoming packets.

### Testing
#### Stream mode
Both the encoder and the decoder can be tested simultaneously. The test setup should look as follows:<br>
`GRC flowgraph -> /tmp/fifo1 -> m17-coder-sym -> /tmp/fifo2 -> m17-decoder-sym -> stdout`<br>
To perform a simple test, GNURadio 3.10 is required.

Run the following commands:
```
mkfifo /tmp/fifo1
mkfifo /tmp/fifo2
```
This should create 2 named pipes: `fifo1` and `fifo2`. The first one is used for the "transmitted" raw
bitstream from GNURadio. The other one is used for the "receiver" part - the symbol stream.

Start gnuradio-companion, open the .grc file included in this repo (`/grc/m17_streamer.grc`).

Open up 2 terminals and run:<br>
Terminal 1:
```
cat /tmp/fifo1 | ./m17-coder-sym > /tmp/fifo2
```
Terminal 2:
```
cat /tmp/fifo2 | ./m17-decoder-sym
```

Hit the *Execute the flow graph* button in GNURadio and watch it roll.

Terminal 2 should show similar results, with the Frame Number advancing each frame:
![image](https://user-images.githubusercontent.com/44336093/209792966-44a7813e-13b3-45d7-92f1-02bb1bdc219f.png)

#### Packet mode
Packet encoding is available with `m17-packet-encoder`. Its input parameters are shown below.
```
-S - source callsign (uppercase alphanumeric string) max. 9 characters
-D - destination callsign (uppercase alphanumeric string) or ALL for broadcast
-C - Channel Access Number (0..15, default - 0)
-n - number of bytes (1 to 798)
-o - output file path/name
-x - binary output (M17 baseband as a packed bitstream)
-r - raw audio output - default (single channel, signed 16-bit LE, +7168 for the +1.0 symbol, 10 samples per symbol)
-s - signed 16-bit LE symbols output
```

Input data is passed over stdin. Example command:

`echo -en "\x05Testing M17 packet mode." | ./m17-packet-encode -S N0CALL -D ALL -C 0 -n 25 -o baseband.rrc`

`-en` parameter for `echo` suppresses the trailing newline character and enables the use of `\` within the message.
`\x05` sets the packet data content and stands for text message (as per M17 Specification document, chapter 3.2 - Packet Application).
If a WAVE file format is required for the baseband, sox can be used:

`sox -t raw -r 48000 -b 16 -c 1 -L -e signed-integer baseband.rrc baseband.wav`

This line converts .rrc to .wav. SDRangel successfully decoding a packet:
![SDRangel screen dump](https://github.com/M17-Project/M17_Implementations/assets/44336093/d2cd195c-6126-4b48-b516-36d20dced9ce)

The two characters at the end of the message are probably CRC bytes erroneously decoded by SDRangel as a part of the text message.
