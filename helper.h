#ifndef HELPER_H
#define HELPER_H

#include <string>
#include <cstdio> // for FILE*, fopen, fclose

// 获取文件大小
int fileSize(const std::string &file_name);
// 比较两个图像文件是否完全相等
bool isImageEqual(const std::string &file_name1, const std::string &file_name2);

#endif // HELPER_H