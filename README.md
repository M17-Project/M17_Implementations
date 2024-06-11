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

### Cloning
Be sure to clone with `--recursive` to pull the linked libm17 repository, otherwise, building will fail.
```
git clone https://github.com/M17-Project/M17_Implementations.git --recursive
```

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
-f - float symbols output compatible with m17-packet-decode
-d - raw audio output - same as -r, but no RRC filtering (debug)
-w - libsndfile wav audio output - default (single channel, signed 16-bit LE, +7168 for the +1.0 symbol, 10 samples per symbol)
```

Input data is passed over stdin. Example command:

`echo -en "\x05Testing M17 packet mode." | ./m17-packet-encode -S N0CALL -D ALL -C 0 -n 25 -f -o baseband.sym`

`-en` parameter for `echo` suppresses the trailing newline character and enables the use of `\` within the message.
`\x05` sets the packet data content and stands for text message (as per M17 Specification document, chapter 3.2 - Packet Application).

Output:

```
DST: ALL	FFFFFFFFFFFF
SRC: N0CALL	00004B13D106
Data CRC:	BFEC
LSF  CRC:	432A
FN:00 (full frame)
0554657374696E67204D3137207061636B6574206D6F64652E00
FN:-- (ending frame)
00BFEC0000000000000000000000000000000000000000000084
FULL: 0554657374696E67204D3137207061636B6574206D6F64652E00BFEC
 SMS: Testing M17 packet mode.
```

Decode packet created with above sample:

`cat baseband.sym | ./m17-packet-decode`

Output: 

```
DST: FFFFFFFFFFFF SRC: 00004B13D106 TYPE: 0002 META: 0000000000000000000000000000 LSF_CRC_OK 
Testing M17 packet mode.
```

Encode directly as wav format (skip sox):

`echo -en "\x05Testing M17 packet mode.\x00" | ./m17-packet-encode -S N0CALL -D AB1CDE -C 7 -n 26 -w -o baseband.wav`

Output:

```
DST: AB1CDE	00001F245D51
SRC: N0CALL	00004B13D106
Data CRC:	BFEC
LSF  CRC:	F754
FN:00 (full frame)
0554657374696E67204D3137207061636B6574206D6F64652E00
FN:-- (ending frame)
00BFEC0000000000000000000000000000000000000000000084
FULL: 0554657374696E67204D3137207061636B6574206D6F64652E00BFEC
 SMS: Testing M17 packet mode.
```

Decode with M17-FME:

`m17-fme -r -w baseband.wav -v 1`

Output:

```
M17 Project - Florida Man Edition                          
Build Version: 2024-1-g4f2c15c 
Session Number: A4F5 
M17 Project RF Audio Frame Demodulator. 
SNDFile (.wav, .rrc) Input File: baseband.wav 
Payload Verbosity: 1; 


M17 LSF Frame Sync (08:57:09): 
 DST: AB1CDE    SRC: N0CALL    CAN: 7; Data Packet
 LSF: 00 00 1F 24 5D 51 00 00 4B 13 D1 06 03 82 00
      00 00 00 00 00 00 00 00 00 00 00 00 00 F7 54
      (CRC CHK) E: F754; C: F754;
M17 PKT Frame Sync (08:57:09):  CNT: 00; PBC: 00; EOT: 0;
 pkt: 0554657374696E67204D3137207061636B6574206D6F64652E00
M17 PKT Frame Sync (08:57:09):  CNT: 01; LST: 01; EOT: 1;
 pkt: 00BFEC0000000000000000000000000000000000000000000084 Protocol: SMS;
 SMS: Testing M17 packet mode.
 PKT: 05 54 65 73 74 69 6E 67 20 4D 31 37 20 70 61 63 6B 65 74 20 6D 6F 64 65 2E
      00 BF EC 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
      (CRC CHK) E: BFEC; C: BFEC;
M17  No Frame Sync (08:57:09): 

```