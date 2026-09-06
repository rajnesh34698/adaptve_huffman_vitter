# Adaptive Huffman Coding — Vitter Algorithm V (C++)

This is a C++ implementation of adaptive Huffman coding using **Vitter's Algorithm V**, based on the algorithm described by GeeksforGeeks and Vitter's published algorithm.

## Features

- Base-256 alphabet: works with arbitrary binary files.
- NYT (Not Yet Transmitted) node.
- 8-bit fixed code for a new byte.
- Implicit node numbering.
- Separate leaf/internal blocks.
- Block-leader interchange.
- Vitter Slide-and-Increment update.
- Streaming one-pass encoding/decoding.
- Binary `.huff` file format.
- Compression and decompression commands.

## Compile

```bash
g++ -std=c++17 -O2 vitter_huffman.cpp -o vitter_huffman
```

## Run

Demo:

```bash
./vitter_huffman demo
```

Compress:

```bash
./vitter_huffman compress input.txt output.huff
```

Decompress:

```bash
./vitter_huffman decompress output.huff restored.txt
```

## Important

For a 26-symbol alphabet, the GeeksforGeeks example uses `e = 4` and `r = 10`. This implementation uses a 256-symbol byte alphabet, so the new-symbol representation is simply 8 bits. The adaptive tree update itself is Vitter Algorithm V.
