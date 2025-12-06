3. JPEG does not provide default Huffman/quantization tables, and the adopted tables should be embedded into DHT/DQT segment. Some JPEG coders have their default tables (maybe the same as that given in Annex K).

1) Write a rate reduction program to replace the Huffman tables with new ones, which are adaptive to the statistics of coded symbols. Is the JPEG photo taken by your digital camera well compressed? Try your program on it!
2) Write a quality/rate adjusting program to replace the quantization tables (maybe a new one w.r.t that used by MPEG-4) and quantized DCT coefficients.

https://www.dpreview.com/sample-galleries/0450281051/canon-eos-r-samples-gallery/0993198459

MCU Grid: 75x50
New DC Huffman Table:
bits: 0 2 2 3 1 1 1 1 0 0 0 0 0 0 0 0
huffval: 3 4 2 5 1 6 7 8 0 9 10
New AC Huffman Table:
bits: 0 2 1 3 3 2 5 2 4 3 5 5 6 5 0 11
huffval: 1 2 3 0 4 17 5 18 33 6 49 7 19 34 65 81 97 113 8 20 50 129 35 145 161 21 66 82 177 193 22 51 98 114 209 9 36 67 130 225 240 52 83 146 162 241 23 37 99 115 38 53 68 84 131 178 194
New DC Huffman Table:
bits: 0 2 3 1 1 1 1 0 0 0 0 0 0 0 0 0
huffval: 1 2 0 3 4 5 6 7 8
New AC Huffman Table:
bits: 0 2 1 2 4 3 5 7 3 3 4 0 6 3 1 0
huffval: 0 1 2 3 17 4 18 33 49 5 65 81 19 34 97 113 240 6 50 129 145 161 177 193 20 66 209 35 82 225 7 21 51 241 22 37 98 114 130 146 36 162 178 194
--- Size Comparison ---
Original: 167081 bytes
Optimized: 162209 bytes
Reduction: 2.91595%

```bash
jpegtran -optimize test.jpg > jpegtran_optimized.jpg
```

Component 0:
Existing DC Huffman Table:
bits: 0 2 2 3 1 1 1 1 0 0 0 0 0 0 0 0
huffval: 3 4 2 5 1 6 7 8 0 9 10
Existing AC Huffman Table:
bits: 0 2 1 3 3 2 5 2 4 3 5 5 6 5 0 11
huffval: 1 2 3 0 4 17 5 18 33 6 49 7 19 34 65 81 97 113 8 20 50 129 35 145 161 21 66 82 177 193 22 51 98 114 209 9 36 67 130 225 240 23 83 146 162 241 37 52 99 115 53 38 68 131 178 194 84
Component 1:
Existing DC Huffman Table:
bits: 0 2 3 1 1 1 1 0 0 0 0 0 0 0 0 0
huffval: 1 2 0 3 4 5 6 7 8
Existing AC Huffman Table:
bits: 0 2 1 2 4 3 5 7 3 3 4 1 3 5 1 0
huffval: 0 1 2 3 17 4 18 33 49 5 65 81 19 34 97 113 240 6 50 129 145 161 177 193 20 66 209 35 82 225 7 21 51 241 22 114 130 146 36 37 98 162 178 194
Component 2:
Existing DC Huffman Table:
bits: 0 2 3 1 1 1 1 0 0 0 0 0 0 0 0 0
huffval: 1 2 0 3 4 5 6 7 8
Existing AC Huffman Table:
bits: 0 2 1 2 4 3 5 7 3 3 4 1 3 5 1 0
huffval: 0 1 2 3 17 4 18 33 49 5 65 81 19 34 97 113 240 6 50 129 145 161 177 193 20 66 209 35 82 225 7 21 51 241 22 114 130 146 36 37 98 162 178 194

```bash
jpegtran -quant-table qt.txt -optimize -outfile out.jpg in.jpg
```
