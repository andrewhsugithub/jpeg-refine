#include <setjmp.h>
#include <stdio.h>

// clang-format off
#include <jpeglib.h>
// clang-format on

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

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

// Helper to count how many symbols are actually defined in a table
int get_huffman_symbol_count(JHUFF_TBL* tbl) {
    int count = 0;
    for (int i = 1; i <= 16; i++) {
        count += tbl->bits[i];
    }
    return count;
}

// This function takes a valid table and makes it "Dummy/Bad"
// It reverses the assignment of symbols, giving common symbols the longest codes.
void optimize_huffman_table(JHUFF_TBL* tbl) {
    if (tbl == nullptr) return;

    int symbol_count = get_huffman_symbol_count(tbl);

    // We only reverse the valid portion of the huffval array
    // This keeps the set of symbols valid, but destroys the compression efficiency.
    std::reverse(tbl->huffval, tbl->huffval + symbol_count);
}

void write_with_dummy_tables(const std::string& input_filename, const std::string& output_filename) {
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
    if (setjmp(jsrcerr.setjmp_buffer)) { /* Error handler */
        return;
    }

    jpeg_create_decompress(&srcinfo);
    jpeg_stdio_src(&srcinfo, input_file);
    jpeg_read_header(&srcinfo, TRUE);
    jvirt_barray_ptr* coef_arrays = jpeg_read_coefficients(&srcinfo);

    // 2. Setup Compression (Write Destination)
    dstinfo.err = jpeg_std_error(&jdsterr.pub);
    jdsterr.pub.error_exit = my_error_exit;

    jpeg_create_compress(&dstinfo);
    jpeg_stdio_dest(&dstinfo, output_file);

    // Copy critical image params (Width, Height, Color Space)
    jpeg_copy_critical_parameters(&srcinfo, &dstinfo);

    // 3. Generate Standard Tables first
    // We let libjpeg set up the standard defaults, then we will modify them in place.
    // jpeg_set_defaults(&dstinfo);

    // IMPORTANT: Turn off optimization.
    // If TRUE, libjpeg calculates new tables and overwrites our dummy ones.
    dstinfo.optimize_coding = FALSE;

    // 4. Inject "Dummy" (Sabotaged) Values
    // We iterate over the DC and AC tables libjpeg just created and mess them up.
    for (int i = 0; i < NUM_HUFF_TBLS; i++) {
        if (dstinfo.dc_huff_tbl_ptrs[i] != nullptr) {
            optimize_huffman_table(dstinfo.dc_huff_tbl_ptrs[i]);
        }
        if (dstinfo.ac_huff_tbl_ptrs[i] != nullptr) {
            optimize_huffman_table(dstinfo.ac_huff_tbl_ptrs[i]);
        }
    }

    // 5. Write Data
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
        write_with_dummy_tables(input, output);

        size_t size_new = fs::file_size(output);

        std::cout << "--- Size Comparison ---" << std::endl;
        std::cout << "Original:  " << size_orig << " bytes" << std::endl;
        std::cout << "Dummy Tbl: " << size_new << " bytes" << std::endl;
        std::cout << "Increase:  " << (double)(size_new - size_orig) / size_orig * 100.0 << "%" << std::endl;

    } catch (std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }

    return 0;
}