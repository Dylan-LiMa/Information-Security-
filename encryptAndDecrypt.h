#ifndef ENCRYPTANDDECRYPT_H
#define ENCRYPTANDDECRYPT_H

#include <vector>
#include <stdio.h> // For FILE*

#include "jpeglib.h" // 引用 jpeglib 库
#include "gmpxx.h"   // 引用 GMP++ 库

// 定义布尔类型
typedef int booltype;

// 随机序列结构体：用于存储排序的随机数及其原始索引
typedef struct
{
    int number;      // 原始索引
    mpf_class value; // 混沌序列生成的随机数值
} randSequence;

// 整数对结构体：用于某些置乱中的索引映射
typedef struct
{
    int number; // 索引
    int value;  // 值
} intPair;

/* 非零AC系数的信息：
 * blockPosition: DCT块的位置
 * zigzagPosition: 在zigzag扫描序列中的位置
 * value: 非零量化AC系数的幅值
 */
typedef struct
{
    int blockPosition;
    int zigzagPosition;
    int value;
} nonZeroAcInfo;

// 加密函数声明
void scrambleMcuNoDcc(std::vector<randSequence> &rp, JCOEF **ac_ptr);
void scrambleSameRunAcc(std::vector<std::vector<randSequence>> &rp, JCOEF **ac_ptr, nonZeroAcInfo **runs_ac_info_ptr, int *runs_ac_num_ptr);
void dccIterSwap(std::vector<std::vector<randSequence>> &rp, JCOEF *diff_ptr, int *iters_group_num_ptr);
void scrambleSameSignDccGroup(std::vector<std::vector<intPair>> &rp, JCOEF **groups_diff_ptr, int *groups_diff_num_ptr, size_t group_sum);
void encrypt(const char *src_name, JCOEF *diff_ptr, JCOEF **ac_ptr);

// 解密函数声明
void reScrambleMcuNoDcc(std::vector<randSequence> &rp, JCOEF **ac_ptr);
void reScrambleSameRunAcc(std::vector<std::vector<randSequence>> &rp, JCOEF **ac_ptr, nonZeroAcInfo **runs_ac_info_ptr, int *runs_ac_num_ptr);
void reDccIterSwap(std::vector<std::vector<randSequence>> &rp, JCOEF *diff_ptr, int *iters_group_num_ptr);
void reScrambleSameSignDccGroup(std::vector<std::vector<intPair>> &rp, JCOEF **groups_diff_ptr, int *groups_diff_num_ptr, size_t group_sum);
void decrypt(const char *enc_name, JCOEF *diff_ptr, JCOEF **ac_ptr);

// JPEG文件保存函数
void saveJpeg(struct jpeg_decompress_struct *cinfo, jvirt_barray_ptr *coeff, const char *img_name);

// 整体加密/解密方案的入口函数
void proposedEncryptionScheme(const char *src_name, const char *dst_name, int is_decryption);

#endif // ENCRYPTANDDECRYPT_H