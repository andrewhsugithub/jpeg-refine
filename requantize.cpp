#include <setjmp.h>
#include <stdio.h>

// clang-format off
#include <jpeglib.h>
// clang-format on
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

static const unsigned int mpeg4_default_intra_matrix[64] = {
    8, 17, 18, 19, 21, 23, 25, 27,
    17, 18, 19, 21, 23, 25, 27, 28,
    20, 21, 22, 23, 24, 26, 28, 30,
    21, 22, 23, 24, 26, 28, 30, 32,
    22, 23, 24, 26, 28, 30, 32, 35,
    23, 24, 26, 28, 30, 32, 35, 38,
    25, 26, 28, 30, 32, 35, 38, 41,
    27, 28, 30, 32, 35, 38, 41, 45};

struct my_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

void my_error_exit(j_common_ptr cinfo) {
    my_error_mgr* myerr = (my_error_mgr*)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->setjmp_buffer, 1);
}

JCOEF requantize_coeff(JCOEF val, int old_q, int new_q) {
    if (val == 0) return 0;

    long long dct_val = (long long)val * old_q;

    if (dct_val >= 0)
        return (JCOEF)((dct_val + new_q / 2) / new_q);
    else
        return (JCOEF)((dct_val - new_q / 2) / new_q);
}

void process_image_quality(const std::string& input_filename, const std::string& output_filename, int quality) {
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

    dstinfo.optimize_coding = TRUE;

    unsigned int new_luma_table[64];
    unsigned int new_chroma_table[64];

    JQUANT_TBL* old_luma_q = srcinfo.quant_tbl_ptrs[0];
    JQUANT_TBL* old_chroma_q = srcinfo.quant_tbl_ptrs[1];

    int scale_factor;
    if (quality < 50) {
        scale_factor = 5000 / quality;
    } else {
        scale_factor = 200 - (quality * 2);
    }

    for (int i = 0; i < 64; i++) {
        long val_luma = ((long)mpeg4_default_intra_matrix[i] * scale_factor + 50) / 100;
        if (val_luma < 1) val_luma = 1;
        if (val_luma > 255) val_luma = 255;
        new_luma_table[i] = (unsigned int)val_luma;

        long val_chroma = ((long)mpeg4_default_intra_matrix[i] * scale_factor + 50) / 100;
        if (val_chroma < 1) val_chroma = 1;
        if (val_chroma > 255) val_chroma = 255;
        new_chroma_table[i] = (unsigned int)val_chroma;
    }

    jpeg_add_quant_table(&dstinfo, 0, new_luma_table, 100, FALSE);
    jpeg_add_quant_table(&dstinfo, 1, new_chroma_table, 100, FALSE);

    JBLOCKARRAY buffer;

    for (int c = 0; c < dstinfo.num_components; c++) {
        jpeg_component_info* dst_compptr = &dstinfo.comp_info[c];
        jpeg_component_info* src_compptr = &srcinfo.comp_info[c];

        dst_compptr->quant_tbl_no = (c == 0) ? 0 : 1;

        int src_tbl_index = src_compptr->quant_tbl_no;
        int dst_tbl_index = dst_compptr->quant_tbl_no;

        JQUANT_TBL* old_q_tbl = srcinfo.quant_tbl_ptrs[src_tbl_index];
        JQUANT_TBL* new_q_tbl = dstinfo.quant_tbl_ptrs[dst_tbl_index];

        if (old_q_tbl == nullptr || new_q_tbl == nullptr) continue;

        int height_in_blocks = src_compptr->height_in_blocks;
        int width_in_blocks = src_compptr->width_in_blocks;

        for (int row_blk = 0; row_blk < height_in_blocks; row_blk++) {
            buffer = (srcinfo.mem->access_virt_barray)(
                (j_common_ptr)&srcinfo, coef_arrays[c], row_blk, 1, TRUE);

            for (int col_blk = 0; col_blk < width_in_blocks; col_blk++) {
                JCOEFPTR block = buffer[0][col_blk];

                for (int k = 0; k < 64; k++) {
                    int old_q = old_q_tbl->quantval[k];
                    int new_q = new_q_tbl->quantval[k];
                    block[k] = requantize_coeff(block[k], old_q, new_q);
                }
            }
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
        printf("Usage: %s <input.jpg> <output.jpg> [quality]\n", argv[0]);
        printf("  quality: 1-100 (default 10)\n");
        return 1;
    }

    std::string input = argv[1];
    std::string output = argv[2];
    int quality = 10;

    if (argc >= 4) {
        quality = std::stoi(argv[3]);
    }

    std::cout << "Processing with MPEG-4 Quantization Matrix (Quality: " << quality << ")..." << std::endl;
    process_image_quality(input, output, quality);

    return 0;
}