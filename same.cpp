#include <setjmp.h>
#include <stdio.h>
// clang-format off
#include <jpeglib.h>
// clang-format on
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

struct my_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

void my_error_exit(j_common_ptr cinfo) {
    my_error_mgr* myerr = (my_error_mgr*)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->setjmp_buffer, 1);
}

// Helper to DEEP COPY a Huffman table
void copy_huff_table(j_compress_ptr dstinfo, j_decompress_ptr srcinfo, int tbl_index, int is_dc) {
    JHUFF_TBL** src_tbls = is_dc ? srcinfo->dc_huff_tbl_ptrs : srcinfo->ac_huff_tbl_ptrs;
    JHUFF_TBL** dst_tbls = is_dc ? dstinfo->dc_huff_tbl_ptrs : dstinfo->ac_huff_tbl_ptrs;

    if (src_tbls[tbl_index] == NULL) return;

    // Allocate new table in destination
    if (dst_tbls[tbl_index] == NULL) {
        dst_tbls[tbl_index] = jpeg_alloc_huff_table((j_common_ptr)dstinfo);
    }

    // Copy the contents (bits and values)
    std::memcpy(dst_tbls[tbl_index]->bits, src_tbls[tbl_index]->bits, sizeof(src_tbls[tbl_index]->bits));
    std::memcpy(dst_tbls[tbl_index]->huffval, src_tbls[tbl_index]->huffval, sizeof(src_tbls[tbl_index]->huffval));
}

void write_with_same_tables(const std::string& input_filename, const std::string& output_filename) {
    struct jpeg_decompress_struct srcinfo;
    struct jpeg_compress_struct dstinfo;
    struct my_error_mgr jsrcerr, jdsterr;

    FILE* input_file = fopen(input_filename.c_str(), "rb");
    FILE* output_file = fopen(output_filename.c_str(), "wb");

    if (!input_file || !output_file) {
        std::cerr << "Error opening files." << std::endl;
        return;
    }

    // --- SETUP DECOMPRESS ---
    srcinfo.err = jpeg_std_error(&jsrcerr.pub);
    jsrcerr.pub.error_exit = my_error_exit;
    if (setjmp(jsrcerr.setjmp_buffer)) return;

    jpeg_create_decompress(&srcinfo);
    jpeg_stdio_src(&srcinfo, input_file);

    // 1. IMPORTANT: Tell libjpeg to save markers (EXIF, ICC, etc.)
    // We must do this BEFORE reading the header.
    // 0xFFFF saves all markers.
    jpeg_save_markers(&srcinfo, JPEG_COM, 0xFFFF);
    for (int m = 0; m < 16; m++) {
        jpeg_save_markers(&srcinfo, JPEG_APP0 + m, 0xFFFF);
    }

    jpeg_read_header(&srcinfo, TRUE);
    jvirt_barray_ptr* coef_arrays = jpeg_read_coefficients(&srcinfo);

    // --- SETUP COMPRESS ---
    dstinfo.err = jpeg_std_error(&jdsterr.pub);
    jdsterr.pub.error_exit = my_error_exit;

    jpeg_create_compress(&dstinfo);
    jpeg_stdio_dest(&dstinfo, output_file);

    // Copy critical image params
    jpeg_copy_critical_parameters(&srcinfo, &dstinfo);

    // 2. Deep copy Huffman Tables
    // Note: We don't loop over num_components to assign tables.
    // We assume standard slot IDs (0 and 1) or copy what the source claimed to use.
    for (int c = 0; c < srcinfo.num_components - 1; c++) {
        int dc_tbl_idx = srcinfo.comp_info[c].dc_tbl_no;
        int ac_tbl_idx = srcinfo.comp_info[c].ac_tbl_no;

        // Ensure destination component uses the same ID
        dstinfo.comp_info[c].dc_tbl_no = dc_tbl_idx;
        dstinfo.comp_info[c].ac_tbl_no = ac_tbl_idx;

        copy_huff_table(&dstinfo, &srcinfo, dc_tbl_idx, 1);  // DC
        copy_huff_table(&dstinfo, &srcinfo, ac_tbl_idx, 0);  // AC
    }

    // Ensure optimize coding is OFF
    dstinfo.optimize_coding = FALSE;

    // 3. Write coefficients
    jpeg_write_coefficients(&dstinfo, coef_arrays);

    // 4. Copy Markers (EXIF, etc) to the output
    // We iterate the linked list of markers from source and write them to dest
    jpeg_saved_marker_ptr marker = srcinfo.marker_list;
    while (marker != NULL) {
        jpeg_write_marker(&dstinfo, marker->marker, marker->data, marker->data_length);
        marker = marker->next;
    }

    jpeg_finish_compress(&dstinfo);
    jpeg_destroy_compress(&dstinfo);
    jpeg_finish_decompress(&srcinfo);
    jpeg_destroy_decompress(&srcinfo);

    fclose(input_file);
    fclose(output_file);
}

int main(int argc, char* argv[]) {
    if (argc < 2) return 1;
    // write_with_same_tables(argv[1], "output_same.jpg");
    try {
        size_t size_orig = fs::file_size(argv[1]);

        // Run the transcode
        write_with_same_tables(argv[1], "output_same.jpg");

        size_t size_new = fs::file_size("output_same.jpg");

        std::cout << "--- Size Comparison ---" << std::endl;
        std::cout << "Original:  " << size_orig << " bytes" << std::endl;
        std::cout << "Optimized: " << size_new << " bytes" << std::endl;
        std::cout << "Reduction:  " << (double)(size_orig - size_new) / size_orig * 100.0 << "%" << std::endl;

    } catch (std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
    return 0;
}