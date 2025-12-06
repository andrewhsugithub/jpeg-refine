#include <setjmp.h>
#include <stdio.h>

// clang-format off
#include <jpeglib.h>
// clang-format on
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <queue>
#include <string>
#include <vector>

namespace fs = std::filesystem;

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

int get_huffman_symbol_count(JHUFF_TBL* tbl) {
    int count = 0;
    for (int i = 1; i <= 16; i++) count += tbl->bits[i];
    return count;
}

void build_huffman_table(JHUFF_TBL* htbl, std::vector<unsigned long>& freq) {
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

    struct HNode {
        unsigned long freq;
        int symbol;  // -1 for internal
        HNode* left;
        HNode* right;
        HNode(unsigned long _freq, int _symbol) : freq(_freq), symbol(_symbol), left(nullptr), right(nullptr) {}
    };

    auto cmp = [](HNode* a, HNode* b) { return a->freq > b->freq; };  // min heap
    std::priority_queue<HNode*, std::vector<HNode*>, decltype(cmp)> pq(cmp);

    for (auto& s : symbols) {
        HNode* node = new HNode(s.freq, s.sym);
        pq.push(node);
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
    }

    int codesize[257] = {0};  // Map symbol -> length

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

    std::vector<int> bits(33, 0);
    for (auto& s : symbols) {
        int len = codesize[s.sym];
        if (len > 32) len = 32;  // safety clamp
        if (len > 0) bits[len]++;
    }

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

    for (int i = 15; i > 0; i--) {
        if (bits[i] > 0) {
            bits[i]--;      // remove one code from length i
            bits[i + 1]++;  // move to length i+1
            break;
        }
    }

    for (int i = 1; i <= 16; i++) {
        htbl->bits[i] = bits[i];
    }

    std::vector<std::pair<int, int>> sorted_syms;  // codesize, symbol
    for (auto& s : symbols) {
        int len = codesize[s.sym];
        sorted_syms.push_back({len, s.sym});
    }

    std::sort(sorted_syms.begin(), sorted_syms.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
        if (a.first != b.first)
            return a.first < b.first;  // shorter code length first
        return a.second < b.second;    // symbol value
    });

    int idx = 0;
    for (int len = 1; len <= 16; len++) {
        for (int k = 0; k < bits[len]; k++) {
            if (idx >= sorted_syms.size()) break;
            htbl->huffval[idx] = sorted_syms[idx].second;
            idx++;
        }
    }
}

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
        if (input_file) fclose(input_file);
        if (output_file) fclose(output_file);
        return;
    }

    srcinfo.err = jpeg_std_error(&jsrcerr.pub);
    jsrcerr.pub.error_exit = my_error_exit;
    if (setjmp(jsrcerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&srcinfo);
        if (input_file) fclose(input_file);
        if (output_file) fclose(output_file);
        return;
    }
    jpeg_create_decompress(&srcinfo);
    jpeg_stdio_src(&srcinfo, input_file);
    jpeg_read_header(&srcinfo, TRUE);

    jvirt_barray_ptr* coef_arrays = jpeg_read_coefficients(&srcinfo);

    dstinfo.err = jpeg_std_error(&jdsterr.pub);
    jdsterr.pub.error_exit = my_error_exit;

    if (setjmp(jdsterr.setjmp_buffer)) {
        jpeg_destroy_compress(&dstinfo);
        jpeg_destroy_decompress(&srcinfo);
        if (input_file) fclose(input_file);
        if (output_file) fclose(output_file);
        return;
    }

    jpeg_create_compress(&dstinfo);
    jpeg_stdio_dest(&dstinfo, output_file);

    jpeg_copy_critical_parameters(&srcinfo, &dstinfo);

    dstinfo.optimize_coding = FALSE;

    std::vector<std::vector<unsigned long>>
        dc_stats(NUM_HUFF_TBLS, std::vector<unsigned long>(256, 0));
    std::vector<std::vector<unsigned long>> ac_stats(NUM_HUFF_TBLS, std::vector<unsigned long>(256, 0));
    std::vector<bool> dc_table_used(NUM_HUFF_TBLS, false);
    std::vector<bool> ac_table_used(NUM_HUFF_TBLS, false);

    std::vector<int> last_dc_val(dstinfo.num_components, 0);

    int mcu_width = srcinfo.MCUs_per_row;
    int mcu_height = srcinfo.MCU_rows_in_scan;

    for (int mcu_y = 0; mcu_y < mcu_height; mcu_y++) {
        for (int mcu_x = 0; mcu_x < mcu_width; mcu_x++) {
            for (int c = 0; c < dstinfo.num_components; c++) {
                jpeg_component_info* compptr = &dstinfo.comp_info[c];

                dc_table_used[compptr->dc_tbl_no] = true;
                ac_table_used[compptr->ac_tbl_no] = true;

                for (int y_pos = 0; y_pos < compptr->v_samp_factor; y_pos++) {
                    for (int x_pos = 0; x_pos < compptr->h_samp_factor; x_pos++) {
                        int block_row = (mcu_y * compptr->v_samp_factor) + y_pos;
                        int block_col = (mcu_x * compptr->h_samp_factor) + x_pos;

                        JBLOCKARRAY buffer = (srcinfo.mem->access_virt_barray)(
                            (j_common_ptr)&srcinfo, coef_arrays[c], block_row, 1, FALSE);

                        JCOEFPTR coef_block = buffer[0][block_col];

                        int dc = coef_block[0];
                        int diff = dc - last_dc_val[c];
                        last_dc_val[c] = dc;

                        dc_stats[compptr->dc_tbl_no][jpeg_category(diff)]++;

                        int run = 0;
                        int last_k = 63;
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

    for (int i = 0; i < NUM_HUFF_TBLS; i++) {
        if (dc_table_used[i]) {
            if (dstinfo.dc_huff_tbl_ptrs[i] == nullptr)
                dstinfo.dc_huff_tbl_ptrs[i] = jpeg_alloc_huff_table((j_common_ptr)&dstinfo);

            build_huffman_table(dstinfo.dc_huff_tbl_ptrs[i], dc_stats[i]);
            // std::cout << "New DC Huffman Table:\n";
            // print_huff_table(dstinfo.dc_huff_tbl_ptrs[i]);
        }

        if (ac_table_used[i]) {
            if (dstinfo.ac_huff_tbl_ptrs[i] == nullptr)
                dstinfo.ac_huff_tbl_ptrs[i] = jpeg_alloc_huff_table((j_common_ptr)&dstinfo);

            build_huffman_table(dstinfo.ac_huff_tbl_ptrs[i], ac_stats[i]);
            // std::cout << "New AC Huffman Table:\n";
            // print_huff_table(dstinfo.ac_huff_tbl_ptrs[i]);
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
    if (argc < 3) {
        printf("Usage: %s <input.jpg> <optimized.jpg>\n", argv[0]);
        return 1;
    }

    std::string input = argv[1];
    std::string output = argv[2];

    try {
        size_t size_orig = fs::file_size(input);

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