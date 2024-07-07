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

### Cloning
Be sure to clone with `--recursive` to pull the required linked repositories, otherwise building will fail.
```
git clone --recursive https://github.com/M17-Project/M17_Implementations.git
```

### Building
First, build the shared object `libm17.so`:
```
cd M17_Implementations/libm17
make
make install
sudo ldconfig
```
Then, `cd` back up to the directory of interest (SP5WWP/m17-*) and ```make``` again.

### Capabilities
Four executables are available:
- `m17-coder-sym` is used to convert a raw binary data bitstream to symbols. Symbol stream has to be
filtered with an appropriate filter before transmission, see the specification document for details.
- `m17-decoder-sym` decodes a stream of floats at `stdin`, one sample per symbol. After a valid
syncword is detected, decoding process starts. The program expects a stream of synchronized symbols
at the input. See the `/grc/symbol_recovery.grc` file for details.
- `m17-packet-encode` is a handy tool for generating baseband (or a symbol stream, if needed) for
M17 packets. The program expects a limited stream of raw data at the stdin. The number of bytes is set
with the `-n` parameter, range 1 to 798.
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

##### Signatures and encryption
`m17-coder-sym` and `m17-decoder-sym` now offer support for ECDSA (secp256r1 256-bit prime field Weierstrass curve) stream signing and verification, AES (128, 192, 256-bit) and scrambler (8, 16, 24-bit) stream payload encryption. See relevant input parameters below and `sample_*` text files provided in `m17-coder-sym` and `m17-decoder-sym` folders.

The signature occupies 4 last data frames of the stream. It is generated after the data transmission has finished. When there's no more user data to transmit, a hash based on all the contents is calculated. That hash value is then signed with the user's private key.

Stream encoding (m17-decoder-sym) has optional input parameters, shown below.
```
-s - Private key for ECDSA signature, 32 bytes (-s [hex_string|key_file]),
-K - AES encryption key (-K [hex_string|text_file]),
-k - Scrambler encryption seed value (-k [hex_string]),
-D - Debug mode,
-h - help / print usage
```

Stream decoding (m17-decoder-sym) has optional input parameters, shown below.
```
-c - Display decoded callsigns,
-v - Display Viterbi error metrics,
-m - Display META fields,
-l - Display LSF CRC checks,
-d - Set syncword detection threshold (decimal value),
-s - Public key for ECDSA signature, 64 bytes (-s [hex_string|key_file]),
-K - AES encryption key (-K [hex_string|text_file]),
-k - Scrambler encryption seed value (-k [hex_string]),
-h - help / print usage
```

#### Packet mode
Packet encoding is available with `m17-packet-encoder`. Its input parameters are shown below.
```
-S - source callsign (uppercase alphanumeric string) max. 9 characters
-D - destination callsign (uppercase alphanumeric string) or ALL for broadcast
-C - Channel Access Number (0..15, default - 0)
-T - SMS Text Message (example: -T 'Hello World! This is a text message')
-R - Raw Hex Octets   (example: -R 010203040506070809)
-n - number of bytes, only when pre-encoded data passed over stdin (1 to 798)
-o - output file path/name
-x - binary output (M17 baseband as a packed bitstream)
-r - raw audio output - default (single channel, signed 16-bit LE, +7168 for the +1.0 symbol, 10 samples per symbol)
-s - signed 16-bit LE symbols output
-f - float symbols output compatible with m17-packet-decode
-d - raw audio output - same as -r, but no RRC filtering (debug)
-w - libsndfile wav audio output - default (single channel, signed 16-bit LE, +7168 for the +1.0 symbol, 10 samples per symbol)
```

Input data can be pre-encoded and passed over stdin. Example command:

`echo -en "\x05Testing M17 packet mode.\x00" | ./m17-packet-encode -S N0CALL -D ALL -C 0 -n 26 -f -o baseband.sym`

`-en` parameter for `echo` suppresses the trailing newline character and enables the use of `\` within the message.
`\x05` sets the packet data content and stands for text message (as per M17 Specification document, chapter 3.2 - Packet Application).

Output:

```
SMS: Testing M17 packet mode.
DST: ALL        FFFFFFFFFFFF
SRC: N0CALL     00004B13D106
CAN: 00
Data CRC:       BFEC
LSF  CRC:       432A
FN:00 (full frame)
FN:-- (ending frame)
PKT: 05 54 65 73 74 69 6E 67 20 4D 31 37 20 70 61 63 6B 65 74 20 6D 6F 64 65 2E
     00 BF EC 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

Decode packet created with above sample:

`cat baseband.sym | ./m17-packet-decode`

Output: 

```
DST: FFFFFFFFFFFF SRC: 00004B13D106 TYPE: 0002 META: 0000000000000000000000000000 LSF_CRC_OK 
SMS: Testing M17 packet mode.
```

Encode User Text String as wav format:

`./m17-packet-encode -S N0CALL -D AB1CDE -T 'This is an SMS Text Message Generated by m17-packet-encode' -C 7 -w -o baseband.wav`

Output:

```
SMS: This is an SMS Text Message Generated by m17-packet-encode
DST: AB1CDE     00001F245D51
SRC: N0CALL     00004B13D106
CAN: 07
Data CRC:       FD81
LSF  CRC:       F754
FN:00 (full frame)
FN:01 (full frame)
FN:-- (ending frame)
PKT: 05 54 68 69 73 20 69 73 20 61 6E 20 53 4D 53 20 54 65 78 74 20 4D 65 73 73
     61 67 65 20 47 65 6E 65 72 61 74 65 64 20 62 79 20 6D 31 37 2D 70 61 63 6B
     65 74 2D 65 6E 63 6F 64 65 00 FD 81 00 00 00 00 00 00 00 00 00 00 00 00 00
```

Decode with M17-FME:

`m17-fme -r -w baseband.wav -v 1`

Output:

```
M17 Project - Florida Man Edition                          
Build Version: 2024-10-g060dd21 
Session Number: A751 
M17 Project RF Audio Frame Demodulator. 
SNDFile (.wav, .rrc) Input File: baseband.wav 
Payload Verbosity: 1; 


M17 LSF Frame Sync (16:12:34): 
 DST: AB1CDE    SRC: N0CALL    CAN: 7; Data Packet FT: 0382; ET: 0; ES: 0;
 LSF: 00 00 1F 24 5D 51 00 00 4B 13 D1 06 03 82 00
      00 00 00 00 00 00 00 00 00 00 00 00 00 F7 54
      (CRC CHK) E: F754; C: F754;
M17 PKT Frame Sync (16:12:34):  CNT: 00; PBC: 00; EOT: 0;
 pkt: 055468697320697320616E20534D532054657874204D65737300
M17 PKT Frame Sync (16:12:34):  CNT: 01; PBC: 01; EOT: 0;
 pkt: 6167652047656E657261746564206279206D31372D7061636B04
M17 PKT Frame Sync (16:12:34):  CNT: 02; LST: 12; EOT: 1;
 pkt: 65742D656E636F646500FD8100000000000000000000000000B0 Protocol: SMS;
 SMS: This is an SMS Text Message Generated by m17-packet-encode
 PKT: 05 54 68 69 73 20 69 73 20 61 6E 20 53 4D 53 20 54 65 78 74 20 4D 65 73 73
      61 67 65 20 47 65 6E 65 72 61 74 65 64 20 62 79 20 6D 31 37 2D 70 61 63 6B
      65 74 2D 65 6E 63 6F 64 65 00 FD 81 00 00 00 00 00 00 00 00 00 00 00 00 00
      (CRC CHK) E: FD81; C: FD81;
```

Encode Raw Hex Octet String as float symbol format:

`./m17-packet-encode -S N0CALL -D AB1CDE -R  010203040506070809 -C 7 -f -o float.sym`

Output:

```
Raw Len: 9; Raw Octets: 01 02 03 04 05 06 07 08 09
DST: AB1CDE     00001F245D51
SRC: N0CALL     00004B13D106
CAN: 07
Data CRC:       D7CE
LSF  CRC:       F754
FN:-- (ending frame)
PKT: 01 02 03 04 05 06 07 08 09 D7 CE 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

Decode (rolling) packets created with above sample:

`tail -f float.sym | ./m17-packet-decode`

Output: 

```
DST: 00001F245D51 SRC: 00004B13D106 TYPE: 0382 META: 0000000000000000000000000000 LSF_CRC_OK 
PKT: 01 02 03 04 05 06 07 08 09 D7 CE
```
