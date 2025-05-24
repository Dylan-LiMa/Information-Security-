#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h> // For memcpy, memset
#include <math.h>   // For round

#include <vector>
#include <algorithm> // For std::sort

#include "jpeglib.h" // JPEG库头文件

#include "encryptAndDecrypt.h" // 自定义的加密解密头文件
#include "sort.h"              // 排序辅助函数头文件 (尽管实际使用了std::sort)
#include "key.h"               // 密钥生成头文件

// 外部全局变量声明 (在 main.cpp 中定义)
extern size_t channel;
extern size_t block_width;
extern size_t block_height;
extern size_t block_sum;
extern int ceiling_run;
extern int iter_times;
extern int ceiling_dc;
extern int floor_dc;
extern int zigzag[63]; // Zigzag扫描顺序

/**
 * @brief 对不包含DCC的MCU进行全局置乱 (AC系数块的置乱)
 * @param rp 排序后的随机序列，用于确定置乱顺序
 * @param ac_ptr 指向所有AC系数块的指针数组
 */
void scrambleMcuNoDcc(std::vector<randSequence> &rp, JCOEF **ac_ptr)
{
    // 临时存储所有AC系数块的指针，以便进行置乱
    JCOEF **temp_ac_ptr = (JCOEF **)malloc(sizeof(JCOEF *) * block_sum);
    if (!temp_ac_ptr)
    {
        perror("Failed to allocate memory for temp_ac_ptr");
        exit(EXIT_FAILURE);
    }

    // 复制AC系数块到临时数组中
    for (size_t i = 0; i < block_sum; ++i)
    {
        temp_ac_ptr[i] = (JCOEF *)malloc(sizeof(JCOEF) * (DCTSIZE2 - 1));
        if (!temp_ac_ptr[i])
        {
            perror("Failed to allocate memory for temp_ac_ptr[i]");
            // 释放之前已分配的内存
            for (size_t k = 0; k < i; ++k)
                free(temp_ac_ptr[k]);
            free(temp_ac_ptr);
            exit(EXIT_FAILURE);
        }
        memcpy(temp_ac_ptr[i], ac_ptr[i], sizeof(JCOEF) * (DCTSIZE2 - 1));
    }

    // 根据排序后的随机序列 rp 进行AC系数块的置乱
    // rp[i].number 包含了原始位置的索引
    for (size_t i = 0; i < block_sum; ++i)
    {
        size_t original_index = rp[i].number;
        // 将原始位置为 original_index 的块复制到当前位置 i
        memcpy(ac_ptr[i], temp_ac_ptr[original_index], sizeof(JCOEF) * (DCTSIZE2 - 1));
    }

    // 释放临时内存
    for (size_t i = 0; i < block_sum; ++i)
    {
        free(temp_ac_ptr[i]);
        temp_ac_ptr[i] = NULL;
    }
    free(temp_ac_ptr);
    temp_ac_ptr = NULL;
}

/**
 * @brief 对具有相同游程的AC系数进行全局置乱
 * @param rp 排序后的随机序列，每个游程类别对应一个内部随机序列
 * @param ac_ptr 指向所有AC系数块的指针数组
 * @param runs_ac_info_ptr 存储每个游程类别下非零AC系数的位置和值的详细信息
 * @param runs_ac_num_ptr 存储每个游程类别下非零AC系数的数量
 */
void scrambleSameRunAcc(std::vector<std::vector<randSequence>> &rp, JCOEF **ac_ptr, nonZeroAcInfo **runs_ac_info_ptr, int *runs_ac_num_ptr)
{
    // 遍历所有可能的游程长度 (0 到 ceiling_run-1)
    for (int run = 0; run < ceiling_run; ++run)
    {
        int num_ac_in_run = runs_ac_num_ptr[run]; // 当前游程类别下非零AC系数的数量

        // 如果该游程类别下没有AC系数，则跳过
        if (num_ac_in_run == 0)
            continue;

        // 临时存储当前游程类别下所有AC系数的值，以便进行置乱
        std::vector<JCOEF> temp_ac_values(num_ac_in_run);
        for (int ac_count = 0; ac_count < num_ac_in_run; ++ac_count)
        {
            temp_ac_values[ac_count] = runs_ac_info_ptr[run][ac_count].value;
        }

        // 根据排序后的随机序列 rp[run] 对 temp_ac_values 进行置乱
        for (int ac_count = 0; ac_count < num_ac_in_run; ++ac_count)
        {
            int original_index = rp[run][ac_count].number; // 原始AC系数在当前游程组中的索引

            // 获取要被置乱的AC系数的原始位置
            int block_position = runs_ac_info_ptr[run][ac_count].blockPosition;
            int zigzag_position = runs_ac_info_ptr[run][ac_count].zigzagPosition;

            // 将置乱后的值写入AC系数的原始位置
            // 值来自 temp_ac_values 的 original_index 位置
            ac_ptr[block_position][zigzag_position] = temp_ac_values[original_index];
        }
    }
}

/**
 * @brief 对相同正负符号的DCC分组进行置乱
 * @param rp 排序后的随机序列，每个DCC组对应一个内部随机序列
 * @param groups_diff_ptr 指向DCC分组的指针数组
 * @param groups_diff_num_ptr 存储每个DCC分组中DCC的数量
 * @param group_sum DCC分组的总数 (实际分组数量为 group_sum + 1)
 */
void scrambleSameSignDccGroup(std::vector<std::vector<intPair>> &rp, JCOEF **groups_diff_ptr, int *groups_diff_num_ptr, size_t group_sum)
{
    // 遍历所有DCC分组
    for (size_t group_index = 0; group_index <= group_sum; ++group_index)
    {
        int group_diff_num = groups_diff_num_ptr[group_index]; // 当前分组中DCC的数量

        // 如果分组中只有一个DCC，则无需置乱
        if (group_diff_num == 1)
        {
            continue;
        }
        else
        {
            // 临时存储当前分组中的DCC值，以便进行置乱
            JCOEF *group_dc_temp = (JCOEF *)malloc(sizeof(JCOEF) * group_diff_num);
            if (!group_dc_temp)
            {
                perror("Failed to allocate memory for group_dc_temp");
                exit(EXIT_FAILURE);
            }
            memcpy(group_dc_temp, groups_diff_ptr[group_index], sizeof(JCOEF) * group_diff_num);

            // 根据排序后的随机序列 rp[group_index] 对分组进行置乱
            // rp[group_index][diff_index].number 包含了原始位置的索引
            for (int diff_index = 0; diff_index < group_diff_num; ++diff_index)
            {
                int original_index = rp[group_index][diff_index].number; // 原始DCC在该组中的索引
                // 将原始位置为 original_index 的DCC值写入当前分组的 diff_index 位置
                groups_diff_ptr[group_index][diff_index] = group_dc_temp[original_index];
            }

            // 释放临时内存
            free(group_dc_temp);
            group_dc_temp = NULL;
        }
    }
}

/**
 * @brief DCC分组迭代交换加密
 * @param rp 排序后的随机序列，用于交换决策
 * @param diff_ptr 指向所有DCC差分系数的指针
 * @param iters_group_num_ptr 存储每次迭代中分组的数量
 */
void dccIterSwap(std::vector<std::vector<randSequence>> &rp, JCOEF *diff_ptr, int *iters_group_num_ptr)
{
    // 遍历所有迭代次数
    for (int iter_time = 1; iter_time <= iter_times; ++iter_time)
    {
        int group_num_current_iter = iters_group_num_ptr[iter_time - 1]; // 当前迭代中的分组数量

        // 遍历当前迭代中的每个分组
        for (int group_index = 0; group_index < group_num_current_iter; ++group_index)
        {
            int prev_dc = 0;       // 用于累计DC值以判断溢出
            booltype can_swap = 1; // 标记当前分组是否可以交换 (默认为可交换)

            // 从随机序列中获取当前分组的交换决策值
            int swap_decision_number = rp[iter_time - 1][group_index].number;

            // --- 检查右半部分是否引起溢出 ---
            // 右半部分的起始索引和结束索引
            size_t right_part_start_idx = 2 * iter_time * group_index + iter_time;
            size_t right_part_end_idx = 2 * (group_index + 1) * iter_time;

            for (size_t diff_idx = right_part_start_idx; diff_idx < right_part_end_idx; ++diff_idx)
            {
                if (can_swap == 1)
                { // 只有在之前没有溢出的情况下才进行溢出判断
                    prev_dc += diff_ptr[diff_idx];
                    // 检查DC值是否在有效范围内
                    if (!(prev_dc <= ceiling_dc && prev_dc >= floor_dc))
                    {
                        can_swap = 0; // 发生溢出，标记为不可交换
                    }
                }
                else
                { // 已经溢出，继续累加DC值但不进行判断
                    prev_dc += diff_ptr[diff_idx];
                }
            }

            // --- 检查左半部分是否引起溢出 ---
            // 左半部分的起始索引和结束索引
            size_t left_part_start_idx = 2 * iter_time * group_index;
            size_t left_part_end_idx = 2 * iter_time * group_index + iter_time;

            for (size_t diff_idx = left_part_start_idx; diff_idx < left_part_end_idx; ++diff_idx)
            {
                if (can_swap == 1)
                { // 只有在之前没有溢出的情况下才进行溢出判断
                    prev_dc += diff_ptr[diff_idx];
                    // 检查DC值是否在有效范围内
                    if (!(prev_dc <= ceiling_dc && prev_dc >= floor_dc))
                    {
                        can_swap = 0; // 发生溢出，标记为不可交换
                    }
                }
                else
                { // 已经溢出，继续累加DC值但不进行判断
                    prev_dc += diff_ptr[diff_idx];
                }
            }

            // --- 执行交换操作（如果可交换且决策为交换） ---
            if (can_swap == 1)
            {
                // 如果随机决策值为奇数，则进行交换
                if (swap_decision_number % 2 == 1)
                {
                    JCOEF *left_part_temp = (JCOEF *)malloc(sizeof(JCOEF) * iter_time);
                    if (!left_part_temp)
                    {
                        perror("Failed to allocate memory for left_part_temp");
                        exit(EXIT_FAILURE);
                    }

                    // 复制左半部分到临时变量
                    memcpy(left_part_temp, diff_ptr + left_part_start_idx, sizeof(JCOEF) * iter_time);
                    // 将右半部分复制到左半部分的位置
                    memcpy(diff_ptr + left_part_start_idx, diff_ptr + right_part_start_idx, sizeof(JCOEF) * iter_time);
                    // 将临时变量（原左半部分）复制到右半部分的位置
                    memcpy(diff_ptr + right_part_start_idx, left_part_temp, sizeof(JCOEF) * iter_time);

                    free(left_part_temp);
                    left_part_temp = NULL;
                }
            }
        }
    }
}

/**
 * @brief 对JPEG图像进行加密的主函数
 * @param src_name 原始图像文件名 (用于密钥生成)
 * @param diff_ptr 指向所有DC差分系数的指针
 * @param ac_ptr 指向所有AC系数块的指针数组
 */
void encrypt(const char *src_name, JCOEF *diff_ptr, JCOEF **ac_ptr)
{
    // 使用图像文件名初始化 Key 类，生成混沌序列的初始参数 x 和 u
    Key key(src_name);
    mpf_class x = key.getX();
    mpf_class u = key.getU();

    // 用于生成随机序列的临时 randSequence 结构体
    randSequence r;

    /*************************************************** scrambleSameSignDccGroup ***********************************************************/
    // 1. 分割DCC序列为相同符号的分组
    size_t group_sum = 0;           // 实际分组数量为 group_sum + 1
    int group_diff_num_current = 0; // 当前分组中的DCC数量
    booltype current_sign;          // 当前DCC分组的符号 (1为正，0为负)

    // 分配内存来存储每个分组中DCC的数量
    int *groups_diff_num_ptr = (int *)malloc(sizeof(int) * block_sum);
    if (!groups_diff_num_ptr)
    {
        perror("Failed to allocate memory for groups_diff_num_ptr");
        exit(EXIT_FAILURE);
    }
    memset(groups_diff_num_ptr, 0, sizeof(int) * block_sum);

    // 遍历所有DCC，进行分组
    for (size_t block_idx = 0; block_idx < block_sum; ++block_idx)
    {
        if (block_idx == 0)
        { // 第一个DCC用于初始化符号
            current_sign = (diff_ptr[block_idx] >= 0) ? 1 : 0;
            group_diff_num_current = 1;
        }
        else
        {
            // 如果当前DCC与前一个DCC符号相同
            if ((diff_ptr[block_idx] >= 0 && current_sign == 1) || (diff_ptr[block_idx] < 0 && current_sign == 0))
            {
                ++group_diff_num_current;
            }
            else
            {                                                            // 符号不同，开始新的分组
                groups_diff_num_ptr[group_sum] = group_diff_num_current; // 存储前一个分组的数量
                group_diff_num_current = 1;                              // 新分组的DCC数量从1开始
                current_sign = !current_sign;                            // 切换符号
                ++group_sum;                                             // 分组总数加1
            }
        }
    }
    groups_diff_num_ptr[group_sum] = group_diff_num_current; // 存储最后一个分组的数量

    // 2. 复制DCC分组到动态数组中
    JCOEF **groups_diff_ptr = (JCOEF **)malloc(sizeof(JCOEF *) * (group_sum + 1));
    if (!groups_diff_ptr)
    {
        perror("Failed to allocate memory for groups_diff_ptr");
        free(groups_diff_num_ptr);
        exit(EXIT_FAILURE);
    }
    int diff_index_offset = 0;
    for (size_t group_idx = 0; group_idx <= group_sum; ++group_idx)
    {
        int num_in_group = groups_diff_num_ptr[group_idx];
        groups_diff_ptr[group_idx] = (JCOEF *)malloc(sizeof(JCOEF) * num_in_group);
        if (!groups_diff_ptr[group_idx])
        {
            perror("Failed to allocate memory for groups_diff_ptr[group_idx]");
            // 释放之前已分配的内存
            for (size_t k = 0; k < group_idx; ++k)
                free(groups_diff_ptr[k]);
            free(groups_diff_ptr);
            free(groups_diff_num_ptr);
            exit(EXIT_FAILURE);
        }
        memcpy(groups_diff_ptr[group_idx], diff_ptr + diff_index_offset, sizeof(JCOEF) * num_in_group);
        diff_index_offset += num_in_group;
    }

    // 3. 生成用于DCC相同符号置乱的随机序列
    std::vector<randSequence> temp_rp1;
    for (size_t i = 0; i < block_sum; ++i)
    {
        x = u * x * (1 - x); // Logistic Map 混沌序列生成
        r.number = i;
        r.value = x;
        temp_rp1.push_back(r);
    }
    // 对生成的随机序列按值进行排序
    std::sort(temp_rp1.begin(), temp_rp1.end(), [](const randSequence &lhs, const randSequence &rhs)
              { return lhs.value < rhs.value; });

    // 4. 将随机序列分配到每个DCC分组中
    std::vector<std::vector<intPair>> rp1(group_sum + 1);
    int rand_index_counter = 0;
    for (size_t group_idx = 0; group_idx <= group_sum; ++group_idx)
    {
        int num_in_group = groups_diff_num_ptr[group_idx];
        assert(num_in_group >= 1); // 确保每个分组至少有一个DCC

        for (int diff_idx = 0; diff_idx < num_in_group; ++diff_idx)
        {
            intPair ip;
            ip.number = diff_idx;                           // 原始索引
            ip.value = temp_rp1[rand_index_counter].number; // 排序后的随机序列中的原始索引
            rp1[group_idx].push_back(ip);
            ++rand_index_counter;
        }
        // 对每个分组内的 intPair 序列按值进行排序
        std::sort(rp1[group_idx].begin(), rp1[group_idx].end(), [](const intPair &lhs, const intPair &rhs)
                  { return lhs.value < rhs.value; });
    }

    // 5. 执行DCC相同符号置乱
    scrambleSameSignDccGroup(rp1, groups_diff_ptr, groups_diff_num_ptr, group_sum);

    // 6. 将置乱后的DCC分组写回到原始的diff_ptr中
    diff_index_offset = 0;
    for (size_t group_idx = 0; group_idx <= group_sum; ++group_idx)
    {
        int num_in_group = groups_diff_num_ptr[group_idx];
        if (num_in_group == 1)
        { // 如果分组中只有一个DCC，则直接跳过
            ++diff_index_offset;
        }
        else
        {
            memcpy(diff_ptr + diff_index_offset, groups_diff_ptr[group_idx], sizeof(JCOEF) * num_in_group);
            diff_index_offset += num_in_group;
        }
    }

    // 7. 释放为DCC分组分配的内存
    for (size_t group_idx = 0; group_idx <= group_sum; ++group_idx)
    {
        free(groups_diff_ptr[group_idx]);
        groups_diff_ptr[group_idx] = NULL;
    }
    free(groups_diff_ptr);
    groups_diff_ptr = NULL;
    free(groups_diff_num_ptr);
    groups_diff_num_ptr = NULL;

    /********************************************************** DccIterSwap *****************************************************************/
    // 1. 分配内存来存储每次迭代中DCC分组的数量
    int *iters_group_num_ptr = (int *)malloc(sizeof(int) * iter_times);
    if (!iters_group_num_ptr)
    {
        perror("Failed to allocate memory for iters_group_num_ptr");
        exit(EXIT_FAILURE);
    }

    // 2. 为每次迭代生成随机序列
    std::vector<std::vector<randSequence>> rp2(iter_times);
    for (int iter_time_val = 1; iter_time_val <= iter_times; ++iter_time_val)
    {
        int group_num_iter = block_sum / (iter_time_val * 2); // 计算当前迭代的分组数量
        iters_group_num_ptr[iter_time_val - 1] = group_num_iter;

        for (int group_idx = 0; group_idx < group_num_iter; ++group_idx)
        {
            x = u * x * (1 - x); // 继续生成混沌序列
            r.number = group_idx;
            r.value = x;
            rp2[iter_time_val - 1].push_back(r);
        }
        // 对每个迭代的随机序列按值进行排序
        std::sort(rp2[iter_time_val - 1].begin(), rp2[iter_time_val - 1].end(), [](const randSequence &lhs, const randSequence &rhs)
                  { return lhs.value < rhs.value; });
    }

    // 3. 执行DCC分组迭代交换
    dccIterSwap(rp2, diff_ptr, iters_group_num_ptr);

    // 4. 释放内存
    free(iters_group_num_ptr);
    iters_group_num_ptr = NULL;

    /****************************************************** scrambleSameRunAcc **************************************************************/
    // 1. 统计每个游程长度下非零AC系数的数量
    int *runs_ac_num_ptr = (int *)malloc(sizeof(int) * ceiling_run);
    if (!runs_ac_num_ptr)
    {
        perror("Failed to allocate memory for runs_ac_num_ptr");
        exit(EXIT_FAILURE);
    }
    memset(runs_ac_num_ptr, 0, sizeof(int) * ceiling_run);

    for (size_t block_idx = 0; block_idx < block_sum; ++block_idx)
    {
        int zero_run_count = 0;
        for (int zigzag_idx = 0; zigzag_idx < DCTSIZE2 - 1; ++zigzag_idx)
        {
            if (ac_ptr[block_idx][zigzag_idx] != 0)
            { // 如果是非零AC系数
                if (zero_run_count < ceiling_run)
                {
                    ++runs_ac_num_ptr[zero_run_count]; // 统计该游程长度下的AC系数数量
                }
                zero_run_count = 0; // 重置游程计数
            }
            else
            {
                ++zero_run_count; // 零AC系数，增加游程计数
            }
        }
    }

    // 2. 记录非零AC系数的位置信息 (blockPosition, zigzagPosition, value)
    nonZeroAcInfo **runs_ac_info_ptr = (nonZeroAcInfo **)malloc(sizeof(nonZeroAcInfo *) * ceiling_run);
    if (!runs_ac_info_ptr)
    {
        perror("Failed to allocate memory for runs_ac_info_ptr");
        free(runs_ac_num_ptr);
        exit(EXIT_FAILURE);
    }
    for (int run_val = 0; run_val < ceiling_run; ++run_val)
    {
        runs_ac_info_ptr[run_val] = (nonZeroAcInfo *)malloc(sizeof(nonZeroAcInfo) * runs_ac_num_ptr[run_val]);
        if (!runs_ac_info_ptr[run_val] && runs_ac_num_ptr[run_val] > 0)
        { // 如果需要分配但失败
            perror("Failed to allocate memory for runs_ac_info_ptr[run_val]");
            for (int k = 0; k < run_val; ++k)
                free(runs_ac_info_ptr[k]);
            free(runs_ac_info_ptr);
            free(runs_ac_num_ptr);
            exit(EXIT_FAILURE);
        }
    }

    // 用于跟踪每个游程类别已找到的AC系数数量
    int *counter_ptr = (int *)malloc(sizeof(int) * ceiling_run);
    if (!counter_ptr)
    {
        perror("Failed to allocate memory for counter_ptr");
        for (int k = 0; k < ceiling_run; ++k)
            free(runs_ac_info_ptr[k]);
        free(runs_ac_info_ptr);
        free(runs_ac_num_ptr);
        exit(EXIT_FAILURE);
    }
    memset(counter_ptr, 0, sizeof(int) * ceiling_run);

    for (size_t block_idx = 0; block_idx < block_sum; ++block_idx)
    {
        int zero_run_count = 0;
        for (int zigzag_idx = 0; zigzag_idx < DCTSIZE2 - 1; ++zigzag_idx)
        {
            if (ac_ptr[block_idx][zigzag_idx] != 0)
            {
                if (zero_run_count < ceiling_run)
                {
                    int offset = counter_ptr[zero_run_count];
                    runs_ac_info_ptr[zero_run_count][offset].blockPosition = block_idx;
                    runs_ac_info_ptr[zero_run_count][offset].zigzagPosition = zigzag_idx;
                    runs_ac_info_ptr[zero_run_count][offset].value = ac_ptr[block_idx][zigzag_idx];
                    ++counter_ptr[zero_run_count];
                }
                zero_run_count = 0;
            }
            else
            {
                ++zero_run_count;
            }
        }
    }

    // 3. 生成用于ACC相同游程置乱的随机序列
    std::vector<std::vector<randSequence>> rp3(ceiling_run);
    for (int run_val = 0; run_val < ceiling_run; ++run_val)
    {
        for (int ac_idx = 0; ac_idx < runs_ac_num_ptr[run_val]; ++ac_idx)
        {
            x = u * x * (1 - x); // 继续生成混沌序列
            r.number = ac_idx;
            r.value = x;
            rp3[run_val].push_back(r);
        }
        // 对每个游程类别的随机序列按值进行排序
        std::sort(rp3[run_val].begin(), rp3[run_val].end(), [](const randSequence &lhs, const randSequence &rhs)
                  { return lhs.value < rhs.value; });
    }

    // 4. 执行ACC相同游程置乱
    scrambleSameRunAcc(rp3, ac_ptr, runs_ac_info_ptr, runs_ac_num_ptr);

    // 5. 释放内存
    for (int run_val = 0; run_val < ceiling_run; ++run_val)
    {
        free(runs_ac_info_ptr[run_val]);
        runs_ac_info_ptr[run_val] = NULL;
    }
    free(runs_ac_info_ptr);
    runs_ac_info_ptr = NULL;
    free(runs_ac_num_ptr);
    runs_ac_num_ptr = NULL;
    free(counter_ptr);
    counter_ptr = NULL;

    /***************************************************** scrambleMcuNoDcc ***************************************************************/
    // 1. 生成用于MCU全局置乱的随机序列
    std::vector<randSequence> rp4;
    for (size_t block_idx = 0; block_idx < block_sum; ++block_idx)
    {
        x = u * x * (1 - x); // 继续生成混沌序列
        r.number = block_idx;
        r.value = x;
        rp4.push_back(r);
    }
    // 对随机序列按值进行排序
    std::sort(rp4.begin(), rp4.end(), [](const randSequence &lhs, const randSequence &rhs)
              { return lhs.value < rhs.value; });

    // 2. 执行MCU全局置乱
    scrambleMcuNoDcc(rp4, ac_ptr);
}

/**
 * @brief 将修改后的JPEG系数保存到文件
 * @param cinfo 指向JPEG解压缩信息结构体的指针 (用于复制参数)
 * @param coeff 指向虚拟块数组的指针 (包含修改后的系数)
 * @param img_name 输出图像的文件名
 */
void saveJpeg(struct jpeg_decompress_struct *cinfo, jvirt_barray_ptr *coeff, const char *img_name)
{
    struct jpeg_compress_struct cinfo_enc;
    struct jpeg_error_mgr jerr_enc;
    FILE *outfile = fopen(img_name, "wb");
    if (!outfile)
    {
        perror("Failed to open output JPEG file for writing");
        exit(EXIT_FAILURE);
    }

    cinfo_enc.err = jpeg_std_error(&jerr_enc);
    jpeg_create_compress(&cinfo_enc);
    jpeg_stdio_dest(&cinfo_enc, outfile);

    // 复制原始JPEG文件的关键参数到压缩结构体，确保格式兼容性
    j_compress_ptr cinfo_enc_ptr = &cinfo_enc;
    jpeg_copy_critical_parameters((j_decompress_ptr)cinfo, cinfo_enc_ptr);

    // 写入加密后的系数
    jpeg_write_coefficients(cinfo_enc_ptr, coeff);

    jpeg_finish_compress(&cinfo_enc);
    jpeg_destroy_compress(&cinfo_enc);
    fclose(outfile);
}

/**
 * @brief JPEG加密/解密方案的整体入口函数
 * 该函数负责读取JPEG，提取系数，调用加密/解密，并写回JPEG
 * @param src_name 源图像文件路径
 * @param dst_name 目标图像文件路径
 * @param is_decryption 标志，0表示加密，1表示解密
 */
void proposedEncryptionScheme(const char *src_name, const char *dst_name, int is_decryption)
{
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    jvirt_barray_ptr *coeff; // 虚拟块数组指针，用于存储DCT系数

    FILE *infile = fopen(src_name, "rb");
    if (!infile)
    {
        perror("Failed to open source JPEG file for reading");
        exit(EXIT_FAILURE);
    }

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, infile);
    (void)jpeg_read_header(&cinfo, TRUE); // 读取JPEG文件头

    // 读取JPEG系数
    coeff = jpeg_read_coefficients(&cinfo);
    channel = cinfo.num_components; // 获取图像通道数

    // 遍历每个图像分量 (Y, Cb, Cr)
    for (size_t co = 0; co < channel; ++co)
    {
        jpeg_component_info *comp_info = &cinfo.comp_info[co];

        // 获取量化表，用于计算DC系数的有效范围
        JQUANT_TBL *tbl = comp_info->quant_table;
        int dc_step = tbl->quantval[0];               // DC系数的量化步长
        ceiling_dc = round((double)(1016) / dc_step); // DC系数上限
        floor_dc = round((double)(-1024) / dc_step);  // DC系数下限

        block_width = comp_info->width_in_blocks;   // 当前分量的块宽度
        block_height = comp_info->height_in_blocks; // 当前分量的块高度

        // 针对某些特殊图像库数据进行调整 (例如，确保宽度和高度为偶数)
        if (block_width % 2 != 0)
            block_width--;
        if (block_height % 2 != 0)
            block_height--;

        block_sum = block_height * block_width; // 当前分量的总块数

        // 访问虚拟块数组，获取当前分量的DCT系数块
        JBLOCKARRAY block_array = (cinfo.mem->access_virt_barray)((j_common_ptr)&cinfo, coeff[co], 0,
                                                                  comp_info->v_samp_factor, FALSE);

        // 分配内存用于存储DC差分系数和AC系数
        JCOEF *diff_ptr = (JCOEF *)malloc(sizeof(JCOEF) * block_sum);
        JCOEF **ac_ptr = (JCOEF **)malloc(sizeof(JCOEF *) * block_sum);
        if (!diff_ptr || !ac_ptr)
        {
            perror("Failed to allocate memory for diff_ptr or ac_ptr");
            exit(EXIT_FAILURE);
        }

        // 分离DC和AC系数，并存储AC为zigzag顺序
        JCOEF prev_dc = 0; // 用于DC差分编码的上一块DC值
        size_t block_index = 0;

        // 根据分量类型（Y或Cb/Cr）处理块的遍历方式
        // 亮度分量 (Y) 可能会有不同的采样因子 (如4:2:0，Y分量是2x2个块对应一个Cb/Cr块)
        // 原始代码中对co==0（通常是亮度分量）和co!=0（色度分量）有不同的处理循环，
        // 这里是为了处理不同采样率的块顺序。
        if (co == 0 && channel > 1)
        { // 亮度分量，且是多通道图像
            // 按照2x2的MCU结构遍历Y分量块
            for (JDIMENSION h_mcu = 0; h_mcu < block_height; h_mcu += 2)
            {
                for (JDIMENSION w_mcu = 0; w_mcu < block_width; w_mcu += 2)
                {
                    // 处理 2x2 个 Y 分量块
                    JDIMENSION h_blocks[] = {h_mcu, h_mcu, h_mcu + 1, h_mcu + 1};
                    JDIMENSION w_blocks[] = {w_mcu, w_mcu + 1, w_mcu, w_mcu + 1};

                    for (int i_block = 0; i_block < 4; ++i_block)
                    {
                        JCOEFPTR block_ptr = block_array[h_blocks[i_block]][w_blocks[i_block]];

                        // 提取DC差分系数
                        diff_ptr[block_index] = block_ptr[0] - prev_dc;
                        prev_dc = block_ptr[0]; // 更新prev_dc

                        // 提取AC系数并按zigzag顺序存储
                        ac_ptr[block_index] = (JCOEF *)malloc(sizeof(JCOEF) * (DCTSIZE2 - 1));
                        if (!ac_ptr[block_index])
                        {
                            perror("Failed to allocate memory for ac_ptr[block_index]");
                            // 释放之前已分配的内存
                            for (size_t k = 0; k < block_index; ++k)
                                free(ac_ptr[k]);
                            free(ac_ptr);
                            free(diff_ptr);
                            exit(EXIT_FAILURE);
                        }
                        for (int i_zigzag = 0; i_zigzag < DCTSIZE2 - 1; ++i_zigzag)
                        {
                            ac_ptr[block_index][i_zigzag] = block_ptr[zigzag[i_zigzag]];
                        }
                        ++block_index;
                    }
                }
            }
        }
        else
        { // 单通道图像或色度分量 (Cb/Cr)
            for (JDIMENSION h = 0; h < block_height; ++h)
            {
                for (JDIMENSION w = 0; w < block_width; ++w)
                {
                    JCOEFPTR block_ptr = block_array[h][w];

                    // 提取DC差分系数
                    diff_ptr[block_index] = block_ptr[0] - prev_dc;
                    prev_dc = block_ptr[0];

                    // 提取AC系数并按zigzag顺序存储
                    ac_ptr[block_index] = (JCOEF *)malloc(sizeof(JCOEF) * (DCTSIZE2 - 1));
                    if (!ac_ptr[block_index])
                    {
                        perror("Failed to allocate memory for ac_ptr[block_index]");
                        for (size_t k = 0; k < block_index; ++k)
                            free(ac_ptr[k]);
                        free(ac_ptr);
                        free(diff_ptr);
                        exit(EXIT_FAILURE);
                    }
                    for (int i_zigzag = 0; i_zigzag < DCTSIZE2 - 1; ++i_zigzag)
                    {
                        ac_ptr[block_index][i_zigzag] = block_ptr[zigzag[i_zigzag]];
                    }
                    ++block_index;
                }
            }
        }

        // 调用加密或解密函数
        if (!is_decryption)
        {
            encrypt(src_name, diff_ptr, ac_ptr);
        }
        else
        {
            decrypt(src_name, diff_ptr, ac_ptr);
        }

        // 将加密/解密后的系数写回 block_array
        block_index = 0;
        prev_dc = 0; // 重置prev_dc，用于反向差分编码

        if (co == 0 && channel > 1)
        { // 亮度分量，且是多通道图像
            for (JDIMENSION h_mcu = 0; h_mcu < block_height; h_mcu += 2)
            {
                for (JDIMENSION w_mcu = 0; w_mcu < block_width; w_mcu += 2)
                {
                    JDIMENSION h_blocks[] = {h_mcu, h_mcu, h_mcu + 1, h_mcu + 1};
                    JDIMENSION w_blocks[] = {w_mcu, w_mcu + 1, w_mcu, w_mcu + 1};

                    for (int i_block = 0; i_block < 4; ++i_block)
                    {
                        JCOEFPTR block_ptr = block_array[h_blocks[i_block]][w_blocks[i_block]];

                        // 写回DC系数 (反向差分编码)
                        block_ptr[0] = diff_ptr[block_index] + prev_dc;
                        prev_dc = block_ptr[0];

                        // 写回AC系数
                        for (int i_zigzag = 0; i_zigzag < DCTSIZE2 - 1; ++i_zigzag)
                        {
                            block_ptr[zigzag[i_zigzag]] = ac_ptr[block_index][i_zigzag];
                        }
                        ++block_index;
                    }
                }
            }
        }
        else
        { // 单通道图像或色度分量 (Cb/Cr)
            for (JDIMENSION h = 0; h < block_height; ++h)
            {
                for (JDIMENSION w = 0; w < block_width; ++w)
                {
                    JCOEFPTR block_ptr = block_array[h][w];

                    // 写回DC系数
                    block_ptr[0] = diff_ptr[block_index] + prev_dc;
                    prev_dc = block_ptr[0];

                    // 写回AC系数
                    for (int i_zigzag = 0; i_zigzag < DCTSIZE2 - 1; ++i_zigzag)
                    {
                        block_ptr[zigzag[i_zigzag]] = ac_ptr[block_index][i_zigzag];
                    }
                    ++block_index;
                }
            }
        }

        // 释放为当前分量分配的内存
        free(diff_ptr);
        diff_ptr = NULL;
        for (size_t i = 0; i < block_sum; ++i)
        {
            free(ac_ptr[i]);
            ac_ptr[i] = NULL;
        }
        free(ac_ptr);
        ac_ptr = NULL;
    }

    // 保存JPEG文件，并清理JPEG解压缩结构体
    saveJpeg(&cinfo, coeff, dst_name);
    jpeg_destroy_decompress(&cinfo);
    fclose(infile);
}