# Huffman Table Optimization and Re-Quantization for JPEG Images

This repository contains two C++ programs that optimize JPEG images by optimizing Huffman tables and adjusting quantization tables with MPEG-4 tables.

## Setup

1. Dependencies:

   - Install a C++ compiler that supports C++17 or later.
   - Install [libjpeg](https://github.com/winlibs/libjpeg).

   For MacOS, you can use Homebrew:

   ```bash
   brew install jpeg
   ```

   For other systems, refer to the libjpeg installation instructions.

2. Run both programs:

```bash
make run
```

To run optimization only:

```bash
make optimize
```

To run re-quantization only:

```bash
make requantize
```

> Note: Change the parameters in `Makefile` to use different input/output files or quality settings.

## Test Image

Test image used: Canon EOS R sample image from DPReview
![Test Image](https://www.dpreview.com/sample-galleries/0450281051/canon-eos-r-samples-gallery/0993198459)

## JPEG Huffman Table Optimization

### Size Reduction Results

| Comparison | Original              | Optimized using `optimize.cpp`  | Optimized using `jpegtran -optimize`              |
| ---------- | --------------------- | ------------------------------- | ------------------------------------------------- |
| File size  | 167,081 (bytes)       | 162,209 (bytes)                 | 162,171 (bytes)                                   |
| Reduction  | -                     | 2.92%                           | 2.93%                                             |
| Algorithm  | -                     | JPEG Standard Annex K.2         | Package-Merge                                     |
| Image      | ![test.jpg](test.jpg) | ![optimized.jpg](optimized.jpg) | ![jpegtran_optimized.jpg](jpegtran_optimized.jpg) |

Command used for `jpegtran` optimization:

```bash
jpegtran -optimize test.jpg > jpegtran_optimized.jpg
```

> Note: `jpegtran` is part of the libjpeg package.
> Note: I've set optimize flag to FALSE in `optimize.cpp`.

<details>
<summary>Huffman Table In-Depth Comparison Between Both Methods</summary>

Below logs are printed using `print_huffman_tables` function in `optimize.cpp`.

### DC Huffman Table for Component 0 (Y - Luminance)

**Using `optimize.cpp`**

```text
bits:   0 2 2 3 1 1 1 1 0 0 0 0 0 0 0 0
huffval: 3 4 2 5 1 6 7 8 0 9 10
```

**Using `jpegtran -optimize`**

```text
bits:   0 2 2 3 1 1 1 1 0 0 0 0 0 0 0 0
huffval: 3 4 2 5 1 6 7 8 0 9 10
```

**Difference**: None

```diff
--- optimize.cpp
+++ jpegtran -optimize
@@
 bits:   0 2 2 3 1 1 1 1 0 0 0 0 0 0 0 0
 huffval: 3 4 2 5 1 6 7 8 0 9 10
```

### AC Huffman Table for Component 0 (Y)

**Using `optimize.cpp`**

```text
bits: 0 2 1 3 3 2 5 2 4 3 5 5 6 5 0 11
huffval: 1 2 3 0 4 17 5 18 33 6 49 7 19 34 65 81 97 113 8 20 50 129 35 145 161 21 66 82 177 193 22 51 98 114 209 9 36 67 130 225 240 52 83 146 162 241 23 37 99 115 38 53 68 84 131 178 194
```

**Using `jpegtran -optimize`**

```text
bits: 0 2 1 3 3 2 5 2 4 3 5 5 6 5 0 11
huffval: 1 2 3 0 4 17 5 18 33 6 49 7 19 34 65 81 97 113 8 20 50 129 35 145 161 21 66 82 177 193 22 51 98 114 209 9 36 67 130 225 240 23 83 146 162 241 37 52 99 115 53 38 68 131 178 194 84
```

**Difference**:

```diff
--- optimize.cpp
+++ jpegtran -optimize
@@
 bits: 0 2 1 3 3 2 5 2 4 3 5 5 6 5 0 11
-huffval: 1 2 3 0 4 17 5 18 33 6 49 7 19 34 65 81 97 113 8 20 50 129 35 145 161 21 66 82 177 193 22 51 98 114 209 9 36 67 130 225 240 52 83 146 162 241 23 37 99 115 38 53 68 84 131 178 194
+huffval: 1 2 3 0 4 17 5 18 33 6 49 7 19 34 65 81 97 113 8 20 50 129 35 145 161 21 66 82 177 193 22 51 98 114 209 9 36 67 130 225 240 23 83 146 162 241 37 52 99 115 53 38 68 131 178 194 84
```

### DC Huffman Table for Component 1, 2 (Cb/Cr)

**Using `optimize.cpp`**

```text
bits: 0 2 3 1 1 1 1 0 0 0 0 0 0 0 0 0
huffval: 1 2 0 3 4 5 6 7 8
```

**Using `jpegtran -optimize`**

```text
bits: 0 2 3 1 1 1 1 0 0 0 0 0 0 0 0 0
huffval: 1 2 0 3 4 5 6 7 8
```

**Difference**: None

```diff
--- optimize.cpp
+++ jpegtran -optimize
@@
 bits: 0 2 3 1 1 1 1 0 0 0 0 0 0 0 0 0
 huffval: 1 2 0 3 4 5 6 7 8
```

### AC Huffman Table for Component 1, 2 (Cb/Cr)

**Using `optimize.cpp`**

```text
bits: 0 2 1 2 4 3 5 7 3 3 4 0 6 3 1 0
huffval: 0 1 2 3 17 4 18 33 49 5 65 81 19 34 97 113 240 6 50 129 145 161 177 193 20 66 209 35 82 225 7 21 51 241 22 37 98 114 130 146 36 162 178 194
```

**Using `jpegtran -optimize`**

```text
bits: 0 2 1 2 4 3 5 7 3 3 4 1 3 5 1 0
huffval: 0 1 2 3 17 4 18 33 49 5 65 81 19 34 97 113 240 6 50 129 145 161 177 193 20 66 209 35 82 225 7 21 51 241 22 114 130 146 36 37 98 162 178 194
```

**Difference**:

```diff
--- optimize.cpp
+++ jpegtran -optimize
@@
-bits: 0 2 1 2 4 3 5 7 3 3 4 {-0 6 3 1-} 0
+bits: 0 2 1 2 4 3 5 7 3 3 4 [+1 3 5 1+] 0
@@
-huffval: 0 1 2 3 17 4 18 33 49 5 65 81 19 34 97 113 240 6 50 129 145 161 177 193 20 66 209 35 82 225 7 21 51 241 22 37 98 114 130 146 36 162 178 194
+huffval: 0 1 2 3 17 4 18 33 49 5 65 81 19 34 97 113 240 6 50 129 145 161 177 193 20 66 209 35 82 225 7 21 51 241 22 114 130 146 36 37 98 162 178 194
```

Notice that both methods produce almost identical Huffman tables, with only minor differences in the AC table for components 1 and 2. This is likely due to different optimization algorithms used.

> Note: Use `print_huffman_tables` function in `optimize.cpp` to print existing Huffman tables or for debugging purposes.

</details>

## Re-Quantization with MPEG-4 Tables

This program adjusts the quantization tables of a JPEG image to match the MPEG-4 standard tables.

> Note: I've set optimize flag to TRUE in `requantize.cpp`, this has the same results as `jpegtran -optimize`.

### Side-by-Side Comparison

| Original Image using from `jpegtran -optimize`    | Requantized using `requantize.cpp` (Quality=10) |
| ------------------------------------------------- | ----------------------------------------------- |
| ![jpegtran_optimized.jpg](jpegtran_optimized.jpg) | ![requantized.jpg](requantized.jpg)             |

Zoomed-in comparison of images at the center of the flower:
| Original (jpegtran -optimize) | Requantized (requantize.cpp) |
| ----------------------------- | ---------------------------- |
| | |

## References

- ISO/IEC 10918-1: https://www.w3.org/Graphics/JPEG/itu-t81.pdf Annex K.2 p.144-148
- ISO/IEC 14496-2: http://wikil.lwwhome.cn:28080/wp-content/uploads/2018/06/ISO_IEC_14496-2_2004.pdf p.149

```

```
