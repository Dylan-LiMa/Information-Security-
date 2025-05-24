#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <fstream>
#include <iostream>
#include <assert.h>

// 替换为更标准和跨平台的获取文件大小方式
#include <sys/stat.h> // For stat()

/**
 * @brief 获取文件大小 (字节)
 * @param file_name 文件路径
 * @return 文件大小，如果失败返回 -1
 */
int fileSize(const std::string &file_name)
{
    struct stat stat_buf;
    int rc = stat(file_name.c_str(), &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
}

/**
 * @brief 比较两个图像文件是否完全相等 (逐字节比较)
 * @param file_name1 第一个文件路径
 * @param file_name2 第二个文件路径
 * @return 如果文件内容完全相同返回 true，否则返回 false
 */
bool isImageEqual(const std::string &file_name1, const std::string &file_name2)
{
    int file_length1 = fileSize(file_name1);
    int file_length2 = fileSize(file_name2);

    /* 如果两个文件长度不相等，它们肯定不相等 */
    if (file_length1 != file_length2 || file_length1 == -1)
    {
        return false;
    }

    // 使用 fstream 以二进制模式打开文件进行逐字节读取
    std::ifstream file_read_handler1(file_name1.c_str(), std::ifstream::binary);
    std::ifstream file_read_handler2(file_name2.c_str(), std::ifstream::binary);

    if (!file_read_handler1.is_open() || !file_read_handler2.is_open())
    {
        std::cerr << "Error: Unable to open one or both files for comparison." << std::endl;
        return false;
    }

    char ch1, ch2;
    int count_bytes = 0;
    // 逐字节读取并比较文件内容
    while (file_read_handler1.get(ch1) && file_read_handler2.get(ch2))
    {
        if (ch1 != ch2)
        {
            file_read_handler1.close();
            file_read_handler2.close();
            return false; // 发现不匹配的字节
        }
        ++count_bytes;
    }

    // 确保读取的字节数与文件长度一致 (尽管文件长度已检查过)
    assert(count_bytes == file_length1);

    file_read_handler1.close();
    file_read_handler2.close();

    return true; // 所有字节都匹配
}