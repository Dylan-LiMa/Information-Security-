#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <dirent.h> // 用于目录操作
#include <string.h> // 用于字符串操作 (strcpy, strcat, strstr)
#include <time.h>   // For clock() or time() (实际未使用，但通常用于性能计时)

#include <iostream> // For std::cout, std::cerr

#include "jpeglib.h" // JPEG库头文件

#include "encryptAndDecrypt.h" // 加密解密方案头文件
#include "sort.h"              // 排序辅助函数头文件
#include "helper.h"            // 辅助函数头文件

/* 图像通道数 (例如，1代表灰度，3代表RGB) */
size_t channel;

/* DCT块的宽度 (行数) */
size_t block_width;

/* DCT块的高度 (列数) */
size_t block_height;

/* 图像中DCT块的总数 */
size_t block_sum;

/* Zigzag扫描顺序数组，用于AC系数的线性化和重构 */
int zigzag[63] = {1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7,
                  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53,
                  60, 61, 54, 47, 55, 62, 63};

/* 将要置乱的游程的最大数量 (0-62) */
int ceiling_run = 63;

/* DCC迭代交换加密的最大迭代次数 */
int iter_times = 15;

/* 量化DC系数的有效范围上限 */
int ceiling_dc;
/* 量化DC系数的有效范围下限 */
int floor_dc;

int main(int argc, char *argv[])
{
    // 检查命令行参数数量
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <image_directory_path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // 复制命令行参数中的路径，确保可修改
    char *path_arg = argv[1];
    int path_length = strlen(path_arg);
    char *image_directory_path = (char *)malloc(sizeof(char) * (path_length + 1));
    if (!image_directory_path)
    {
        perror("Failed to allocate memory for image_directory_path");
        exit(EXIT_FAILURE);
    }
    strcpy(image_directory_path, path_arg);

    // 存储图像文件名的指针数组，最多处理10000张图像
    char **image_ptr = (char **)malloc(sizeof(char *) * 10000);
    if (!image_ptr)
    {
        perror("Failed to allocate memory for image_ptr");
        free(image_directory_path);
        exit(EXIT_FAILURE);
    }
    int image_num = 0; // 实际找到的图像数量

    DIR *directory_ptr;
    struct dirent *entry;

    // 打开图像目录
    directory_ptr = opendir(image_directory_path);
    if (directory_ptr == NULL)
    {
        fprintf(stderr, "Error: Could not open directory '%s'\n", image_directory_path);
        free(image_directory_path);
        free(image_ptr);
        exit(EXIT_FAILURE);
    }
    else
    {
        // 遍历目录中的文件
        while ((entry = readdir(directory_ptr)) != NULL)
        {
            // 查找以 ".jpg" 结尾的文件
            if (strstr(entry->d_name, ".jpg") != NULL)
            {
                // 构建完整的文件路径
                size_t full_path_len = strlen(image_directory_path) + strlen(entry->d_name) + 2; // +2 for '/' or '\' and null terminator
                image_ptr[image_num] = (char *)malloc(sizeof(char) * full_path_len);
                if (!image_ptr[image_num])
                {
                    perror("Failed to allocate memory for image_ptr[image_num]");
                    // 释放之前已分配的内存
                    for (int k = 0; k < image_num; ++k)
                        free(image_ptr[k]);
                    free(image_ptr);
                    free(image_directory_path);
                    closedir(directory_ptr);
                    exit(EXIT_FAILURE);
                }
                strcpy(image_ptr[image_num], image_directory_path);

                // 根据操作系统添加路径分隔符
#ifdef _WIN32
                strcat(image_ptr[image_num], "\\");
#else
                strcat(image_ptr[image_num], "/");
#endif
                strcat(image_ptr[image_num], entry->d_name);
                ++image_num;

                // 限制图像数量，避免内存耗尽
                if (image_num >= 10000)
                {
                    std::cerr << "Warning: Reached maximum image limit (10000). Some images might not be processed." << std::endl;
                    break;
                }
            }
        }
        closedir(directory_ptr); // 关闭目录句柄
    }

    // 对每个图像进行加密和解密
    for (int j = 0; j < image_num; ++j)
    {
        char *img_name = image_ptr[j];

        // 构建加密后的文件名 (例如: image-enc.jpg)
        size_t base_name_len = strlen(img_name) - 4;                          // 减去 ".jpg" 的长度
        char *enc_name = (char *)malloc(sizeof(char) * (base_name_len + 10)); // 足够容纳 "-enc.jpg\0"
        if (!enc_name)
        {
            perror("Failed to allocate memory for enc_name");
            continue; // 跳过当前图像
        }
        strncpy(enc_name, img_name, base_name_len);
        enc_name[base_name_len] = '\0'; // 终止字符串
        strcat(enc_name, "-enc.jpg");

        // 执行加密
        std::cout << "Encrypting: " << img_name << " -> " << enc_name << std::endl;
        proposedEncryptionScheme(img_name, enc_name, 0); // 0表示加密

        // 构建解密后的文件名 (例如: image-dec.jpg)
        char *dec_name = (char *)malloc(sizeof(char) * (base_name_len + 10)); // 足够容纳 "-dec.jpg\0"
        if (!dec_name)
        {
            perror("Failed to allocate memory for dec_name");
            free(enc_name); // 释放已分配的enc_name
            continue;       // 跳过当前图像
        }
        strncpy(dec_name, img_name, base_name_len);
        dec_name[base_name_len] = '\0';
        strcat(dec_name, "-dec.jpg");

        // 执行解密
        std::cout << "Decrypting: " << enc_name << " -> " << dec_name << std::endl;
        proposedEncryptionScheme(enc_name, dec_name, 1); // 1表示解密

        // 检查原始图像和解密后的图像是否相等
        if (!isImageEqual(img_name, dec_name))
        {
            std::cout << "Verification FAILED for: " << img_name << std::endl;
        }
        else
        {
            std::cout << "Verification PASSED for: " << img_name << std::endl;
        }

        // 释放为当前图像文件名分配的内存
        free(enc_name);
        enc_name = NULL;
        free(dec_name);
        dec_name = NULL;
    }

    // 释放所有图像文件路径的内存
    for (int j = 0; j < image_num; ++j)
    {
        free(image_ptr[j]);
        image_ptr[j] = NULL;
    }
    free(image_ptr);
    image_ptr = NULL;

    free(image_directory_path);
    image_directory_path = NULL;

    std::cout << "Processed " << image_num << " images in directory: " << path_arg << std::endl;
    std::cout << "Program finished successfully." << std::endl;
    return 0;
}