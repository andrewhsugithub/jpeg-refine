#include <setjmp.h>
#include <stdio.h>

// clang-format off
#include <jpeglib.h>
// clang-format on
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <queue>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static const int jpeg_zigzag_order[8][8] = {
    {0, 1, 5, 6, 14, 15, 27, 28},
    {2, 4, 7, 13, 16, 26, 29, 42},
    {3, 8, 12, 17, 25, 30, 41, 43},
    {9, 11, 18, 24, 31, 40, 44, 53},
    {10, 19, 23, 32, 39, 45, 52, 54},
    {20, 22, 33, 38, 46, 51, 55, 60},
    {21, 34, 37, 47, 50, 56, 59, 61},
    {35, 36, 48, 49, 57, 58, 62, 63}};

// This maps [0..63] ZigZag step -> Index in the linear buffer
static const int jpeg_natural_order[64] = {
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63};

// Error handling boilerplate
struct my_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};
typedef struct my_error_mgr* my_error_ptr;

void my_error_exit(j_common_ptr cinfo) {
    my_error_mgr* myerr = (my_error_mgr*)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->setjmp_buffer, 1);
}

// counts the number of bits
int jpeg_category(int v) {
    if (v == 0) return 0;
    int a = (v < 0) ? -v : v;
    int n = 0;
    while (a) {
        a >>= 1;
        n++;
    }
    return n;
}

// Helper: get number of symbols in a Huffman table
int get_huffman_symbol_count(JHUFF_TBL* tbl) {
    int count = 0;
    for (int i = 1; i <= 16; i++) count += tbl->bits[i];
    return count;
}

void build_huffman_table(JHUFF_TBL* htbl, std::vector<unsigned long>& freq) {
    // 1. Initialize result table (CRITICAL: Must clear garbage)
    // std::memset(htbl, 0, sizeof(JHUFF_TBL));
    // std::memset(htbl->bits, 0, sizeof(htbl->bits));
    // std::memset(htbl->huffval, 0, sizeof(htbl->huffval));

    struct SymFreq {
        int sym;
        unsigned long freq;
    };
    std::vector<SymFreq> symbols;

    for (int i = 0; i < freq.size(); i++) {
        if (freq[i] > 0) {
            symbols.push_back({i, freq[i]});
        }
    }
    // std::cout << "Symbols:";
    // for (auto& s : symbols) {
    //     std::cout << " [" << s.sym << ": " << s.freq << "] ";
    // }
    // std::cout << "\n";

    // // CRITICAL FIX: Handle solid colors (empty AC) or single colors
    // if (symbols.empty()) {
    //     symbols.push_back({0, 1});  // Dummy symbol
    // }

    // 3. Build Huffman tree using priority queue
    struct HNode {
        unsigned long freq;
        int symbol;  // -1 for internal
        HNode* left;
        HNode* right;
        HNode(unsigned long _freq, int _symbol) : freq(_freq), symbol(_symbol), left(nullptr), right(nullptr) {}
    };

    auto cmp = [](HNode* a, HNode* b) { return a->freq > b->freq; };  // min heap
    std::priority_queue<HNode*, std::vector<HNode*>, decltype(cmp)> pq(cmp);
    std::vector<HNode*> all_nodes;  // For memory cleanup

    for (auto& s : symbols) {
        HNode* node = new HNode(s.freq, s.sym);
        pq.push(node);
        all_nodes.push_back(node);
    }

    while (pq.size() >= 2) {
        HNode* left = pq.top();
        pq.pop();
        HNode* right = pq.top();
        pq.pop();
        HNode* parent = new HNode(left->freq + right->freq, -1);
        parent->left = left;
        parent->right = right;
        pq.push(parent);
        all_nodes.push_back(parent);
    }

    // 4. Calculate depths
    int codesize[257] = {0};  // Map symbol -> length

    // if (symbols.size() == 1) {
    //     codesize[symbols[0].sym] = 1;  // Force length 1 for single symbol
    // } else {
    HNode* root = pq.top();
    std::queue<std::pair<HNode*, int>> bfs;  // node, depth
    bfs.push({root, 0});

    while (!bfs.empty()) {
        auto [node, depth] = bfs.front();
        bfs.pop();
        if (node->symbol >= 0) {
            codesize[node->symbol] = depth;
        }
        if (node->left) bfs.push({node->left, depth + 1});
        if (node->right) bfs.push({node->right, depth + 1});
    }
    // }

    // std::cout << "Code lengths:\n";
    // for (auto& s : symbols) {
    //     std::cout << " Symbol " << s.sym << ": length " << codesize[s.sym] << "\n";
    // }

    // Cleanup
    for (HNode* n : all_nodes) delete n;

    // 5. Count codes at each length
    // bits[k] = number of codes with length k
    std::vector<int> bits(33, 0);
    for (auto& s : symbols) {
        int len = codesize[s.sym];
        if (len > 32) len = 32;  // Safety clamp
        if (len > 0) bits[len]++;
    }
    // std::cout << "Initial bits count:\n";
    // for (int i = 0; i < bits.size(); i++) {
    //     if (bits[i] > 0) {
    //         std::cout << " Length " << i << ": " << bits[i] << " codes\n";
    //     }
    // }
    // std::cout << "\n";

    for (int i = 32; i > 16; i--) {
        while (bits[i] > 0) {
            int j = i - 1;  // move one of the codes up
            j--;
            while (j >= 0 && bits[j] == 0) j--;  // find the next longest length category to split

            bits[i] -= 2;      // take two codes from length i
            bits[i - 1]++;     // move one of the codes at length i up a level
            bits[j + 1] += 2;  // split parent node at j to move the parent node down and move the other code at length i up
            bits[j]--;         // remove original node at j
        }
    }
    // for (int i = 16; i > 0; i--) {
    //     if (bits[i] > 0) {
    //         bits[i]--;  // since jpeg standard requires reserved code 11111111
    //         break;
    //     }
    // }

    for (int i = 15; i > 0; i--) {
        if (bits[i] > 0) {
            bits[i]--;      // Remove one code from length i
            bits[i + 1]++;  // Move it to length i+1
            break;          // We successfully broke the complete tree
        }
    }
    // std::cout << "Adjusted bits count:\n";
    // for (int i = 0; i < bits.size(); i++) {
    //     if (bits[i] > 0) {
    //         std::cout << " Length " << i << ": " << bits[i] << " codes\n";
    //     }
    // }

    for (int i = 1; i <= 16; i++) {
        htbl->bits[i] = bits[i];
    }

    // std::sort(symbols.begin(), symbols.end(), [](const SymFreq& a, const SymFreq& b) {
    //     if (a.freq != b.freq)
    //         return a.freq > b.freq;  // Higher freq = Shorter code
    //     return a.sym < b.sym;        // Stability tie-breaker
    // });

    // int huffval_idx = 0;
    // for (int len = 1; len <= 16; len++) {
    //     int count = bits[len];
    //     for (int k = 0; k < count; k++) {
    //         if (huffval_idx < symbols.size()) {
    //             htbl->huffval[huffval_idx] = symbols[huffval_idx].sym;
    //             huffval_idx++;
    //         }
    //     }
    // }

    std::vector<std::pair<int, int>> sorted_syms;  // (codesize, symbol)
    for (auto& s : symbols) {
        int len = codesize[s.sym];
        if (len > 16) len = 16;  // Clamp after adjustment
        sorted_syms.push_back({len, s.sym});
    }

    // Sort by code length first, then by symbol value (JPEG canonical order)
    std::sort(sorted_syms.begin(), sorted_syms.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
        if (a.first != b.first)
            return a.first < b.first;  // Shorter code length first
        return a.second < b.second;    // Then by symbol value
    });

    // Reassign code lengths based on bits[] slots
    // Walk through bits[1..16] and assign symbols in sorted order
    // int idx = 0;
    // for (int len = 1; len <= 16; len++) {
    //     for (int k = 0; k < bits[len] && idx < sorted_syms.size(); k++) {
    //         htbl->huffval[idx] = sorted_syms[idx].second;
    //         idx++;
    //     }
    // }
    // Reassign code lengths based on bits[] slots
    int idx = 0;
    for (int len = 1; len <= 16; len++) {
        for (int k = 0; k < bits[len]; k++) {
            if (idx >= sorted_syms.size()) break;

            htbl->huffval[idx] = sorted_syms[idx].second;
            idx++;
        }
    }

    // CRITICAL CHECK: Did we drop any used symbols?
    if (idx < sorted_syms.size()) {
        std::cerr << "Warning: Huffman table overflow! Dropping "
                  << (sorted_syms.size() - idx) << " symbols." << std::endl;
        // If we dropped a symbol with freq > 0, the image IS corrupted.
        // In a production app, you would fall back to standard tables here.
    }
}

// Optional: print table for debugging
void print_huff_table(JHUFF_TBL* tbl) {
    printf("bits: ");
    for (int i = 1; i <= 16; i++) printf("%d ", tbl->bits[i]);
    printf("\nhuffval: ");
    int n = get_huffman_symbol_count(tbl);
    for (int i = 0; i < n; i++) printf("%d ", tbl->huffval[i]);
    printf("\n");
}

void write_with_optimized_tables(const std::string& input_filename, const std::string& output_filename) {
    struct jpeg_decompress_struct srcinfo;
    struct jpeg_compress_struct dstinfo;
    struct my_error_mgr jsrcerr, jdsterr;

    FILE* input_file = fopen(input_filename.c_str(), "rb");
    FILE* output_file = fopen(output_filename.c_str(), "wb");

    if (!input_file || !output_file) {
        std::cerr << "Error opening files." << std::endl;
        return;
    }

    // 1. Setup Decompression (Read Source)
    srcinfo.err = jpeg_std_error(&jsrcerr.pub);
    jsrcerr.pub.error_exit = my_error_exit;
    if (setjmp(jsrcerr.setjmp_buffer)) return;
    jpeg_create_decompress(&srcinfo);
    jpeg_stdio_src(&srcinfo, input_file);
    jpeg_read_header(&srcinfo, TRUE);
    jvirt_barray_ptr* coef_arrays = jpeg_read_coefficients(&srcinfo);

    // 2. Setup Compression (Write Destination)
    dstinfo.err = jpeg_std_error(&jdsterr.pub);
    jdsterr.pub.error_exit = my_error_exit;

    jpeg_create_compress(&dstinfo);
    jpeg_stdio_dest(&dstinfo, output_file);

    // Copy critical params
    // jpeg_set_defaults(&dstinfo);
    jpeg_copy_critical_parameters(&srcinfo, &dstinfo);
    // if (dstinfo.num_components > 1) {
    //     dstinfo.num_scans = dstinfo.num_components;

    //     // Use a non-const pointer to populate the data
    //     jpeg_scan_info* scan_ptr = (jpeg_scan_info*)(*dstinfo.mem->alloc_small)(
    //         (j_common_ptr)&dstinfo, JPOOL_IMAGE, dstinfo.num_scans * sizeof(jpeg_scan_info));

    //     for (int i = 0; i < dstinfo.num_components; i++) {
    //         scan_ptr[i].comps_in_scan = 1;
    //         scan_ptr[i].component_index[0] = i;
    //         scan_ptr[i].Ss = 0;
    //         scan_ptr[i].Se = 63;
    //         scan_ptr[i].Ah = 0;
    //         scan_ptr[i].Al = 0;
    //     }

    //     // Assign to the struct (const pointer accepts non-const address)
    //     dstinfo.scan_info = scan_ptr;
    // }

    // CRITICAL FIX 1: Initialize defaults to setup internal structures
    // jpeg_calc_jpeg_dimensions(&dstinfo);

    // CRITICAL FIX 2: Disable libjpeg's internal optimization (we are doing it manually)
    // dstinfo.optimize_coding = FALSE;

    // 1. Reset Stats and DC Predictors
    std::vector<std::vector<unsigned long>>
        dc_stats(NUM_HUFF_TBLS, std::vector<unsigned long>(256, 0));
    std::vector<std::vector<unsigned long>> ac_stats(NUM_HUFF_TBLS, std::vector<unsigned long>(256, 0));
    std::vector<bool> dc_table_used(NUM_HUFF_TBLS, false);
    std::vector<bool> ac_table_used(NUM_HUFF_TBLS, false);

    // DC prediction tracks the last DC value PER COMPONENT
    std::vector<int> last_dc_val(dstinfo.num_components, 0);

    // 2. Determine MCU dimensions
    // The number of MCUs across and down the image
    // int max_h_samp = 0;
    // int max_v_samp = 0;
    // for (int c = 0; c < dstinfo.num_components; c++) {
    //     max_h_samp = std::max(max_h_samp, dstinfo.comp_info[c].h_samp_factor);
    //     max_v_samp = std::max(max_v_samp, dstinfo.comp_info[c].v_samp_factor);
    // }

    // int mcu_width = (dstinfo.image_width + (max_h_samp * 8) - 1) / (max_h_samp * 8);
    // int mcu_height = (dstinfo.image_height + (max_v_samp * 8) - 1) / (max_v_samp * 8);

    // std::cout << "MCU dimensions: " << mcu_width << " x " << mcu_height << "\n";

    int mcu_width = srcinfo.MCUs_per_row;
    int mcu_height = srcinfo.MCU_rows_in_scan;

    // // Safety check (optional but good for debugging)
    // if (mcu_width == 0 || mcu_height == 0) {
    //     // Fallback: Calculate manually if srcinfo is somehow empty (rare)
    //     // For standard 4:2:0, MCU is 16x16 pixels.
    //     int max_h_samp = srcinfo.max_h_samp_factor;  // usually 2
    //     int max_v_samp = srcinfo.max_v_samp_factor;  // usually 2
    //     int mcu_pixel_width = max_h_samp * 8;
    //     int mcu_pixel_height = max_v_samp * 8;

    //     mcu_width = (srcinfo.image_width + mcu_pixel_width - 1) / mcu_pixel_width;
    //     mcu_height = (srcinfo.image_height + mcu_pixel_height - 1) / mcu_pixel_height;
    // }

    std::cout << "MCU Grid: " << mcu_width << "x" << mcu_height << "\n";

    // 3. MCU Iteration Loop (The order the JPEG writer uses)
    for (int mcu_y = 0; mcu_y < mcu_height; mcu_y++) {
        for (int mcu_x = 0; mcu_x < mcu_width; mcu_x++) {
            // Inside one MCU, iterate through all components (Y, then Cb, then Cr)
            for (int c = 0; c < dstinfo.num_components; c++) {
                jpeg_component_info* compptr = &dstinfo.comp_info[c];

                // Mark tables as used
                dc_table_used[compptr->dc_tbl_no] = true;
                ac_table_used[compptr->ac_tbl_no] = true;

                // 4. Block Iteration within Component within MCU
                // A component might have multiple blocks in one MCU (e.g., Y has 4 blocks in 4:2:0)
                for (int y_pos = 0; y_pos < compptr->v_samp_factor; y_pos++) {
                    for (int x_pos = 0; x_pos < compptr->h_samp_factor; x_pos++) {
                        // Calculate the absolute block coordinate in the full image
                        int block_row = (mcu_y * compptr->v_samp_factor) + y_pos;
                        int block_col = (mcu_x * compptr->h_samp_factor) + x_pos;

                        // Access the data (Fetch single block row)
                        // Note: access_virt_barray is fast enough for this usage pattern
                        JBLOCKARRAY buffer = (srcinfo.mem->access_virt_barray)(
                            (j_common_ptr)&srcinfo, coef_arrays[c], block_row, 1, FALSE);

                        JCOEFPTR coef_block = buffer[0][block_col];

                        // --- DC Stats (With Correct MCU-Order Prediction) ---
                        int dc = coef_block[0];
                        int diff = dc - last_dc_val[c];  // Diff against last block of THIS component
                        last_dc_val[c] = dc;             // Update predictor

                        dc_stats[compptr->dc_tbl_no][jpeg_category(diff)]++;

                        // --- AC Stats (Standard ZigZag) ---
                        int run = 0;
                        int last_k = 63;

                        // Optimization: Find last non-zero
                        while (last_k > 0 && coef_block[jpeg_natural_order[last_k]] == 0) last_k--;

                        for (int k = 1; k <= last_k; k++) {
                            int idx = jpeg_natural_order[k];
                            int ac = coef_block[idx];

                            if (ac == 0) {
                                run++;
                            } else {
                                while (run >= 16) {
                                    ac_stats[compptr->ac_tbl_no][0xF0]++;  // ZRL
                                    run -= 16;
                                }
                                int size = jpeg_category(ac);
                                int symbol = (run << 4) | size;
                                ac_stats[compptr->ac_tbl_no][symbol]++;
                                run = 0;
                            }
                        }
                        if (last_k < 63) {
                            ac_stats[compptr->ac_tbl_no][0x00]++;  // EOB
                        }
                    }
                }
            }
        }
    }

    // CRITICAL FIX 3: Build Tables AFTER scanning the whole image
    for (int i = 0; i < NUM_HUFF_TBLS; i++) {
        if (dc_table_used[i]) {
            if (dstinfo.dc_huff_tbl_ptrs[i] == nullptr)
                dstinfo.dc_huff_tbl_ptrs[i] = jpeg_alloc_huff_table((j_common_ptr)&dstinfo);

            build_huffman_table(dstinfo.dc_huff_tbl_ptrs[i], dc_stats[i]);
            std::cout << "New DC Huffman Table:\n";
            print_huff_table(dstinfo.dc_huff_tbl_ptrs[i]);
        }

        if (ac_table_used[i]) {
            if (dstinfo.ac_huff_tbl_ptrs[i] == nullptr)
                dstinfo.ac_huff_tbl_ptrs[i] = jpeg_alloc_huff_table((j_common_ptr)&dstinfo);

            build_huffman_table(dstinfo.ac_huff_tbl_ptrs[i], ac_stats[i]);
            std::cout << "New AC Huffman Table:\n";
            print_huff_table(dstinfo.ac_huff_tbl_ptrs[i]);
        }
    }

    jpeg_write_coefficients(&dstinfo, coef_arrays);

    jpeg_finish_compress(&dstinfo);
    jpeg_destroy_compress(&dstinfo);
    jpeg_finish_decompress(&srcinfo);
    jpeg_destroy_decompress(&srcinfo);

    fclose(input_file);
    fclose(output_file);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: ./dummy_test <input.jpg>" << std::endl;
        return 1;
    }

    std::string input = argv[1];
    std::string output = "output_sabotaged.jpg";

    try {
        size_t size_orig = fs::file_size(input);

        // Run the transcode
        write_with_optimized_tables(input, output);

        size_t size_new = fs::file_size(output);

        std::cout << "--- Size Comparison ---" << std::endl;
        std::cout << "Original:  " << size_orig << " bytes" << std::endl;
        std::cout << "Optimized: " << size_new << " bytes" << std::endl;
        std::cout << "Reduction:  " << (double)(size_orig - size_new) / size_orig * 100.0 << "%" << std::endl;

    } catch (std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }

    return 0;
}