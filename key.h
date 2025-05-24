#ifndef KEY_H
#define KEY_H

#include <sstream>
#include <string>
#include <vector>

#include "gmpxx.h"         // 引用 GMP++ 库，用于高精度浮点数
#include <cryptopp/sha3.h> // 引用 Crypto++ SHA3 库

// 定义哈希值长度 (SHA3-512 输出 64 字节)
#define HASHLEN 64

// Crypto++ 没有全局 byte 定义，需手动 typedef
#ifndef BYTE_TYPEDEF
#define BYTE_TYPEDEF
typedef unsigned char byte;
#endif

/**
 * @brief Key 类用于根据图像特征生成混沌序列的初始密钥
 */
class Key
{
private:
    mpf_class m_x; // 混沌系统 Logistic Map 的初始参数 x0
    mpf_class m_u; // 混沌系统 Logistic Map 的参数 u

private:
    /**
     * @brief 获取图像特征。
     * 这里使用非零AC系数的数量统计作为图像特征。
     * @param filename 图像文件路径
     * @param ss 字符串流，用于存储生成的图像特征字符串
     */
    void getImageFeature(const std::string &filename, std::stringstream &ss);

    /**
     * @brief 对图像特征字符串进行哈希。
     * 使用 SHA3-512 算法生成 512 比特 (64 字节) 的哈希值。
     * @param ss 包含图像特征的字符串流
     * @param hash 存储生成的哈希值的字节数组
     */
    void imageHash(const std::stringstream &ss, byte (&hash)[CryptoPP::SHA3_512::DIGESTSIZE]);

    /**
     * @brief 将哈希值 (字节数组) 转换为布尔值向量。
     * @param hash 待转换的哈希字节数组
     * @param hashBool 存储转换结果的布尔值向量
     */
    void byteToBool(byte (&hash)[CryptoPP::SHA3_512::DIGESTSIZE], std::vector<bool> &hashBool);

    /**
     * @brief 使用哈希值 (布尔值向量) 初始化混沌系统的 m_x 和 m_u 参数。
     * 哈希值被分成两部分，分别用于初始化 m_u 和 m_x。
     * @param hashBool 包含哈希值的布尔值向量
     */
    void initializeKey(std::vector<bool> &hashBool);

public:
    // 获取混沌系统的初始参数 x0
    mpf_class getX()
    {
        return m_x;
    }

    // 获取混沌系统的参数 u
    mpf_class getU()
    {
        return m_u;
    }

    /**
     * @brief 构造函数，根据图像文件生成密钥。
     * 这是 Key 类的唯一构造函数。
     * @param filename 图像文件路径
     */
    Key(const std::string filename);
};

#endif // KEY_H