# M17_Implementations
Various implementations

## Woj's implementation (/SP5WWP)
### Overview
Written in C, it has all the components described by the protocol's specification of the stream mode:
- convolutional encoder with soft Viterbi decoder (utilizing fixed point arithmetic),
- Golay encoder with soft decoder (fixed point),
- bit interleaver and randomizer,
- cyclic redundancy check (CRC) validation,
- callsign decoder

There's no support for **any** encryption yet.

### Building
Simply `cd` to the directory of interest and
```make```

### Capabilities
Two executables are available:
- `m17-coder-sym` is used to convert a raw binary data bitstream to symbols. Symbol stream has to be
filtered with an appropriate filter before transmission, see the specification document for details.
- `m17-decoder-sym` decodes a stream of floats at `stdin`, one sample per symbol. After a valid
syncword is detected, decoding process starts. The program expects a stream of synchronized symbols
at the input. See the `/grc/symbol_recovery.grc` file for details.

### Testing
Both the encoder and the decoder can be tested simultaneously. The test setup should look as follows:<br>
`GRC flowgraph -> fifo1 -> m17-coder-sym -> fifo2 -> m17-decoder-sym -> stdout`<br>
To perform a simple test, GNURadio 3.10 is required.

Run the following commands:
```
mkfifo fifo1
mkfifo fifo2
```
This should create 2 named pipes: `fifo1` and `fifo2`. The first one is used for the "transmitted" raw
bitstream from GNURadio. The other one is used for the "receiver" part - the symbol stream.

Start gnuradio-companion, open the .grc file included in this repo (`/grc/m17_streamer.grc`) and change
the name of the named pipe to `fifo1` (at the *File Sink* block - the rightmost one). Change the location of it
if needed.

Open up 2 consoles and run:<br>
Console 1:
```
cat fifo1 | ./m17-coder-sym > fifo2
```
Console 2:
```
cat fifo2 | ./m17-decoder-sym
```

Hit the *Execte the flow graph* button in GNURadio and watch it roll.

Console 2 should show similar results, with the Frame Number advancing each frame:
![image](https://user-images.githubusercontent.com/44336093/209792966-44a7813e-13b3-45d7-92f1-02bb1bdc219f.png)
