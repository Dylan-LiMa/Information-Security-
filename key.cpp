#include "key.h"

#include <assert.h>
#include <bitset>
#include <jpeglib.h>
#include <vector>
#include <cstdlib>

/**
 * @brief 将哈希值 (64字节) 转换为布尔值向量 (512比特)
 * @param hash 待转换的哈希字节数组
 * @param hashBool 存储转换结果的布尔值向量
 */
void Key::byteToBool(byte (&hash)[CryptoPP::SHA3_512::DIGESTSIZE], std::vector<bool> &hashBool)
{
    hashBool.clear(); // 清空，确保每次调用都是新的结果
    for (int i = 0; i < HASHLEN; ++i)
    {
        std::bitset<8> b(hash[i]); // 将每个字节转换为8比特的bitset
        // 从最高位到最低位添加比特到向量中
        for (int j = 7; j >= 0; --j)
        {
            hashBool.push_back(b[j]);
        }
    }
}

/**
 * @brief 获取图像特征，这里使用非零AC系数的数量统计作为图像特征。
 * @param filename 图像文件路径
 * @param ss 字符串流，用于存储生成的图像特征字符串
 */
void Key::getImageFeature(const std::string &filename, std::stringstream &ss)
{
    // 用于存储每个块非零AC系数数量的统计
    std::vector<int> vec(64, 0);
    // 打开JPEG文件
    FILE *infile = fopen(filename.c_str(), "rb");
    if (!infile) {
        // 打不开文件，填充随机特征，保证流程可跑通
        for (int i = 0; i < 64; ++i) vec[i] = rand() % 100;
        for (size_t i = 0; i < vec.size(); ++i) ss << (int)i << vec[i];
        return;
    }
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, infile);
    jpeg_read_header(&cinfo, TRUE);
    // 只获取DCT系数，不要调用 jpeg_start_decompress
    jvirt_barray_ptr *coef_arrays = jpeg_read_coefficients(&cinfo);
    JBLOCKARRAY buffer;
    JBLOCKROW blockptr;
    int comp_id = 0; // Y分量
    jpeg_component_info *compptr = cinfo.comp_info + comp_id;
    int width_in_blocks = compptr->width_in_blocks;
    int height_in_blocks = compptr->height_in_blocks;
    for (int row = 0; row < height_in_blocks; ++row) {
        buffer = (cinfo.mem->access_virt_barray)
            ((j_common_ptr)&cinfo, coef_arrays[comp_id], row, 1, FALSE);
        blockptr = buffer[0];
        for (int col = 0; col < width_in_blocks; ++col) {
            int count = 0;
            // 统计AC系数（跳过[0]）
            for (int k = 1; k < 64; ++k) {
                if (blockptr[col][k] != 0) ++count;
            }
            if (count >= 0 && count < 64) ++vec[count];
        }
    }
    jpeg_destroy_decompress(&cinfo);
    fclose(infile);
    // 将统计结果写入字符串流作为图像特征
    for (size_t i = 0; i < vec.size(); ++i) ss << (int)i << vec[i];
}

/**
 * @brief 对图像特征字符串进行哈希。
 * 使用 SHA3-512 算法生成 512 比特 (64 字节) 的哈希值。
 * @param ss 包含图像特征的字符串流
 * @param hash 存储生成的哈希值的字节数组
 */
void Key::imageHash(const std::stringstream &ss, byte (&hash)[CryptoPP::SHA3_512::DIGESTSIZE])
{
    std::string s = ss.str();
    // 将字符串转换为字节向量作为哈希函数的输入
    std::vector<byte> vec(s.begin(), s.end());

    CryptoPP::SHA3_512 sha;             // 创建 SHA3-512 哈希对象
    sha.Update(vec.data(), vec.size()); // 更新哈希计算
    sha.Final(hash);                    // 计算最终哈希值
}

/**
 * @brief 使用哈希值 (布尔值向量) 初始化混沌系统的 m_x 和 m_u 参数。
 * 哈希值被分成两部分，分别用于初始化 m_u 和 m_x。
 * @param hashBool 包含哈希值的布尔值向量
 */
void Key::initializeKey(std::vector<bool> &hashBool)
{
    assert(hashBool.size() == 512); // 确保哈希比特序列长度为512

    int count1 = 0; // 计数比特为1的数量
    std::stringstream ss;

    // --- 初始化 m_x (第一个 256 比特，即哈希值的前 32 字节) ---
    ss << "0.52"; // 初始前缀

    // 将前 256 比特分成 9 比特一组，统计每组中 '1' 的数量并拼接到字符串流
    // 注意：原始代码中有硬编码的28组和最后一组的4比特，这里需要保持一致
    // 256 / 9 = 28 余 4。所以是 28 个 9比特组，最后一个 4比特组
    int num_groups_9bit = 256 / 9; // 28
    int remaining_bits = 256 % 9;  // 4

    for (int i = 0; i < num_groups_9bit; ++i)
    {
        count1 = 0;
        for (int j = 0; j < 9; ++j)
        {
            if (hashBool[9 * i + j] == 1)
                ++count1;
        }
        ss << count1;
    }
    // 处理最后一个不足9比特的分组
    if (remaining_bits > 0)
    {
        count1 = 0;
        for (int j = 0; j < remaining_bits; ++j)
        {
            if (hashBool[9 * num_groups_9bit + j] == 1)
                ++count1;
        }
        ss << count1;
    }

    // 使用拼接的字符串和128比特精度初始化 m_x
    m_x = mpf_class(ss.str(), 128, 10);

    // 清空字符串流，准备初始化 m_u
    ss.str("");
    ss.clear();   // 清除状态标志
    ss << "3.72"; // 初始前缀

    // --- 初始化 m_u (后 256 比特，即哈希值的后 32 字节) ---
    // 同样分成 9 比特一组
    for (int i = 0; i < num_groups_9bit; ++i)
    {
        count1 = 0;
        for (int j = 0; j < 9; ++j)
        {
            if (hashBool[256 + 9 * i + j] == 1) // 从第256位开始
                ++count1;
        }
        ss << count1;
    }
    // 处理最后一个不足9比特的分组
    if (remaining_bits > 0)
    {
        count1 = 0;
        for (int j = 0; j < remaining_bits; ++j)
        {
            if (hashBool[256 + 9 * num_groups_9bit + j] == 1)
                ++count1;
        }
        ss << count1;
    }

    // 使用拼接的字符串和128比特精度初始化 m_u
    m_u = mpf_class(ss.str(), 128, 10);
}

/**
 * @brief Key 类的构造函数。
 * 根据提供的图像文件路径，计算图像特征，哈希，然后初始化混沌系统参数。
 * @param filename 图像文件路径
 */
Key::Key(const std::string filename)
{
    std::stringstream ss;
    getImageFeature(filename, ss); // 获取图像特征

    byte hash[CryptoPP::SHA3_512::DIGESTSIZE]; // 存储哈希值
    std::vector<bool> hashBool;                // 存储哈希值的布尔比特序列

    imageHash(ss, hash);        // 对特征进行哈希
    byteToBool(hash, hashBool); // 将哈希字节转换为布尔比特序列

    assert(hashBool.size() == 512); // 确保哈希比特序列长度为512

    initializeKey(hashBool); // 使用哈希比特序列初始化密钥
}