#include <assert.h>
#include <string.h> // For memcpy, memset
#include <vector>
#include <algorithm> // For std::sort

#include "encryptAndDecrypt.h" // 自定义的加密解密头文件
#include "sort.h"              // 排序辅助函数头文件 (尽管实际使用了std::sort)
#include "key.h"               // 密钥生成头文件

// 外部全局变量声明 (在 main.cpp 中定义)
extern size_t block_width;
extern size_t block_height;
extern size_t block_sum;
extern int ceiling_run;
extern int iter_times;
extern int ceiling_dc;
extern int floor_dc;

/**
 * @brief 对不包含DCC的MCU进行全局逆置乱 (AC系数块的逆置乱)
 * @param rp 排序后的随机序列，用于确定逆置乱顺序
 * @param ac_ptr 指向所有AC系数块的指针数组
 */
void reScrambleMcuNoDcc(std::vector<randSequence> &rp, JCOEF **ac_ptr)
{
    // 临时存储所有AC系数块的指针
    JCOEF **temp_ac_ptr = (JCOEF **)malloc(sizeof(JCOEF *) * block_sum);
    if (!temp_ac_ptr)
    {
        perror("Failed to allocate memory for temp_ac_ptr");
        exit(EXIT_FAILURE);
    }

    // 复制AC系数块到临时数组
    for (size_t i = 0; i < block_sum; ++i)
    {
        temp_ac_ptr[i] = (JCOEF *)malloc(sizeof(JCOEF) * (DCTSIZE2 - 1));
        if (!temp_ac_ptr[i])
        {
            perror("Failed to allocate memory for temp_ac_ptr[i]");
            for (size_t k = 0; k < i; ++k)
                free(temp_ac_ptr[k]);
            free(temp_ac_ptr);
            exit(EXIT_FAILURE);
        }
        memcpy(temp_ac_ptr[i], ac_ptr[i], sizeof(JCOEF) * (DCTSIZE2 - 1));
    }

    // 根据排序后的随机序列 rp 进行AC系数块的逆置乱
    // rp[i].number 包含了原始位置的索引
    // 这里将 temp_ac_ptr[i] (加密后的i位置) 复制到 ac_ptr[rp[i].number] (原始位置)
    for (size_t i = 0; i < block_sum; ++i)
    {
        size_t original_index = rp[i].number;
        memcpy(ac_ptr[original_index], temp_ac_ptr[i], sizeof(JCOEF) * (DCTSIZE2 - 1));
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
 * @brief 对具有相同游程的AC系数进行全局逆置乱
 * @param rp 排序后的随机序列，每个游程类别对应一个内部随机序列
 * @param ac_ptr 指向所有AC系数块的指针数组
 * @param runs_ac_info_ptr 存储每个游程类别下非零AC系数的位置和值的详细信息
 * @param runs_ac_num_ptr 存储每个游程类别下非零AC系数的数量
 */
void reScrambleSameRunAcc(std::vector<std::vector<randSequence>> &rp, JCOEF **ac_ptr, nonZeroAcInfo **runs_ac_info_ptr, int *runs_ac_num_ptr)
{
    // 遍历所有可能的游程长度
    for (int run = 0; run < ceiling_run; ++run)
    {
        int num_ac_in_run = runs_ac_num_ptr[run]; // 当前游程类别下非零AC系数的数量

        // 如果该游程类别下没有AC系数，则跳过
        if (num_ac_in_run == 0)
            continue;

        // 临时存储当前游程类别下所有AC系数的值，以便进行逆置乱
        std::vector<JCOEF> temp_ac_values(num_ac_in_run);
        // 从当前 ac_ptr 中读取这些值，但它们已经是被置乱后的
        for (int ac_count = 0; ac_count < num_ac_in_run; ++ac_count)
        {
            // runs_ac_info_ptr[run][ac_count] 存储的是原始位置信息
            // 但 ac_ptr[block_position][zigzag_position] 处的值已经是被置乱后的
            int block_position = runs_ac_info_ptr[run][ac_count].blockPosition;
            int zigzag_position = runs_ac_info_ptr[run][ac_count].zigzagPosition;
            temp_ac_values[ac_count] = ac_ptr[block_position][zigzag_position];
        }

        // 根据排序后的随机序列 rp[run] 对 temp_ac_values 进行逆置乱
        for (int ac_count = 0; ac_count < num_ac_in_run; ++ac_count)
        {
            int original_index_in_sorted_rp = rp[run][ac_count].number; // 排序后的rp中，当前ac_count位置对应的值的原始索引

            // 获取要被还原的AC系数的原始位置
            int block_position = runs_ac_info_ptr[run][original_index_in_sorted_rp].blockPosition;
            int zigzag_position = runs_ac_info_ptr[run][original_index_in_sorted_rp].zigzagPosition;

            // 将 temp_ac_values[ac_count] (当前有序值) 写入其在 runs_ac_info_ptr 中对应的原始位置
            // 这是一个逆置乱操作：将按原始顺序的 value 恢复到其原始位置
            ac_ptr[block_position][zigzag_position] = temp_ac_values[ac_count];
        }
    }
}

/**
 * @brief 对相同正负符号的DCC分组进行逆置乱
 * @param rp 排序后的随机序列，每个DCC组对应一个内部随机序列
 * @param groups_diff_ptr 指向DCC分组的指针数组
 * @param groups_diff_num_ptr 存储每个DCC分组中DCC的数量
 * @param group_sum DCC分组的总数 (实际分组数量为 group_sum + 1)
 */
void reScrambleSameSignDccGroup(std::vector<std::vector<intPair>> &rp, JCOEF **groups_diff_ptr, int *groups_diff_num_ptr, size_t group_sum)
{
    // 遍历所有DCC分组
    for (size_t group_index = 0; group_index <= group_sum; ++group_index)
    {
        int group_diff_num = groups_diff_num_ptr[group_index]; // 当前分组中DCC的数量

        // 如果分组中只有一个DCC，则无需逆置乱
        if (group_diff_num == 1)
        {
            continue;
        }
        else
        {
            // 临时存储当前分组中的DCC值，以便进行逆置乱
            JCOEF *group_dc_temp = (JCOEF *)malloc(sizeof(JCOEF) * group_diff_num);
            if (!group_dc_temp)
            {
                perror("Failed to allocate memory for group_dc_temp");
                exit(EXIT_FAILURE);
            }
            // 复制当前（已被置乱的）分组DCC值到临时数组
            memcpy(group_dc_temp, groups_diff_ptr[group_index], sizeof(JCOEF) * group_diff_num);

            // 根据排序后的随机序列 rp[group_index] 对分组进行逆置乱
            // rp[group_index][diff_index].number 包含了原始位置的索引
            for (int diff_index = 0; diff_index < group_diff_num; ++diff_index)
            {
                int original_index = rp[group_index][diff_index].number; // 原始DCC在该组中的索引
                // 将 temp_dc_temp[diff_index] (当前排序值) 写入 groups_diff_ptr[group_index][original_index] (原始位置)
                groups_diff_ptr[group_index][original_index] = group_dc_temp[diff_index];
            }

            // 释放临时内存
            free(group_dc_temp);
            group_dc_temp = NULL;
        }
    }
}

/**
 * @brief DCC分组迭代交换解密
 * 解密顺序与加密顺序相反，即从最大的分组大小开始迭代到最小
 * @param rp 排序后的随机序列，用于交换决策
 * @param diff_ptr 指向所有DCC差分系数的指针
 * @param iters_group_num_ptr 存储每次迭代中分组的数量 (与加密时相同)
 */
void reDccIterSwap(std::vector<std::vector<randSequence>> &rp, JCOEF *diff_ptr, int *iters_group_num_ptr)
{
    // 遍历所有迭代次数，从大到小（与加密时相反）
    for (int iter_time = iter_times; iter_time >= 1; --iter_time)
    {
        int group_num_current_iter = iters_group_num_ptr[iter_time - 1]; // 当前迭代中的分组数量

        // 遍历当前迭代中的每个分组
        for (int group_index = 0; group_index < group_num_current_iter; ++group_index)
        {
            int prev_dc = 0;       // 用于累计DC值以判断溢出 (与加密时逻辑保持一致)
            booltype can_swap = 1; // 标记当前分组是否可以交换

            // 从随机序列中获取当前分组的交换决策值 (与加密时相同)
            int swap_decision_number = rp[iter_time - 1][group_index].number;

            // --- 检查右半部分是否引起溢出 ---
            size_t right_part_start_idx = 2 * iter_time * group_index + iter_time;
            size_t right_part_end_idx = 2 * (group_index + 1) * iter_time;

            for (size_t diff_idx = right_part_start_idx; diff_idx < right_part_end_idx; ++diff_idx)
            {
                if (can_swap == 1)
                {
                    prev_dc += diff_ptr[diff_idx];
                    if (!(prev_dc <= ceiling_dc && prev_dc >= floor_dc))
                    {
                        can_swap = 0;
                    }
                }
                else
                {
                    prev_dc += diff_ptr[diff_idx];
                }
            }

            // --- 检查左半部分是否引起溢出 ---
            size_t left_part_start_idx = 2 * iter_time * group_index;
            size_t left_part_end_idx = 2 * iter_time * group_index + iter_time;

            for (size_t diff_idx = left_part_start_idx; diff_idx < left_part_end_idx; ++diff_idx)
            {
                if (can_swap == 1)
                {
                    prev_dc += diff_ptr[diff_idx];
                    if (!(prev_dc <= ceiling_dc && prev_dc >= floor_dc))
                    {
                        can_swap = 0;
                    }
                }
                else
                {
                    prev_dc += diff_ptr[diff_idx];
                }
            }

            // --- 执行逆交换操作（如果可交换且决策为交换） ---
            if (can_swap == 1)
            {
                // 如果随机决策值为奇数，则进行逆交换
                if (swap_decision_number % 2 == 1)
                { // 交换条件与加密时相同
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
 * @brief 对JPEG图像进行解密的主函数
 * 解密顺序与加密顺序相反
 * @param enc_name 加密图像文件名 (用于密钥生成)
 * @param diff_ptr 指向所有DC差分系数的指针
 * @param ac_ptr 指向所有AC系数块的指针数组
 */
void decrypt(const char *enc_name, JCOEF *diff_ptr, JCOEF **ac_ptr)
{
    // 使用加密图像文件名初始化 Key 类，生成混沌序列的初始参数 x 和 u
    // 必须与加密时使用相同的密钥和相同的生成顺序
    Key key(enc_name);
    mpf_class x = key.getX();
    mpf_class u = key.getU();

    // 用于生成随机序列的临时 randSequence 结构体
    randSequence r;

    // --- 1. 为所有加密步骤生成随机序列 ---
    // 为了确保解密时随机序列与加密时完全一致，需要按加密时的顺序重新生成所有随机序列。
    // 然后再逆序使用它们进行解密。

    // 为 scrambleSameSignDccGroup 步骤生成随机序列 (temp_rp1)
    std::vector<randSequence> temp_rp1_for_dcc_sign_shuffling;
    for (size_t i = 0; i < block_sum; ++i)
    {
        x = u * x * (1 - x); // Logistic Map 混沌序列生成
        r.number = i;
        r.value = x;
        temp_rp1_for_dcc_sign_shuffling.push_back(r);
    }
    std::sort(temp_rp1_for_dcc_sign_shuffling.begin(), temp_rp1_for_dcc_sign_shuffling.end(), [](const randSequence &lhs, const randSequence &rhs)
              { return lhs.value < rhs.value; });

    // 为 DccIterSwap 步骤生成随机序列 (rp2)
    int *iters_group_num_ptr_for_dcc_iter = (int *)malloc(sizeof(int) * iter_times);
    if (!iters_group_num_ptr_for_dcc_iter)
    {
        perror("Failed to allocate memory for iters_group_num_ptr_for_dcc_iter");
        exit(EXIT_FAILURE);
    }
    std::vector<std::vector<randSequence>> rp2_for_dcc_iter(iter_times);
    for (int iter_time_val = 1; iter_time_val <= iter_times; ++iter_time_val)
    {
        int group_num_iter = block_sum / (iter_time_val * 2);
        iters_group_num_ptr_for_dcc_iter[iter_time_val - 1] = group_num_iter;
        for (int group_idx = 0; group_idx < group_num_iter; ++group_idx)
        {
            x = u * x * (1 - x);
            r.number = group_idx;
            r.value = x;
            rp2_for_dcc_iter[iter_time_val - 1].push_back(r);
        }
        std::sort(rp2_for_dcc_iter[iter_time_val - 1].begin(), rp2_for_dcc_iter[iter_time_val - 1].end(), [](const randSequence &lhs, const randSequence &rhs)
                  { return lhs.value < rhs.value; });
    }

    // 为 scrambleSameRunAcc 步骤生成随机序列 (rp3)
    // 需要先重新计算 runs_ac_num_ptr
    int *runs_ac_num_ptr_for_acc_shuffling = (int *)malloc(sizeof(int) * ceiling_run);
    if (!runs_ac_num_ptr_for_acc_shuffling)
    {
        perror("Failed to allocate memory for runs_ac_num_ptr_for_acc_shuffling");
        exit(EXIT_FAILURE);
    }
    memset(runs_ac_num_ptr_for_acc_shuffling, 0, sizeof(int) * ceiling_run);
    for (size_t block_idx = 0; block_idx < block_sum; ++block_idx)
    {
        int zero_run_count = 0;
        for (int zigzag_idx = 0; zigzag_idx < DCTSIZE2 - 1; ++zigzag_idx)
        {
            if (ac_ptr[block_idx][zigzag_idx] != 0)
            {
                if (zero_run_count < ceiling_run)
                {
                    ++runs_ac_num_ptr_for_acc_shuffling[zero_run_count];
                }
                zero_run_count = 0;
            }
            else
            {
                ++zero_run_count;
            }
        }
    }

    std::vector<std::vector<randSequence>> rp3_for_acc_shuffling(ceiling_run);
    for (int run_val = 0; run_val < ceiling_run; ++run_val)
    {
        for (int ac_idx = 0; ac_idx < runs_ac_num_ptr_for_acc_shuffling[run_val]; ++ac_idx)
        {
            x = u * x * (1 - x);
            r.number = ac_idx;
            r.value = x;
            rp3_for_acc_shuffling[run_val].push_back(r);
        }
        std::sort(rp3_for_acc_shuffling[run_val].begin(), rp3_for_acc_shuffling[run_val].end(), [](const randSequence &lhs, const randSequence &rhs)
                  { return lhs.value < rhs.value; });
    }

    // 为 scrambleMcuNoDcc 步骤生成随机序列 (rp4)
    std::vector<randSequence> rp4_for_mcu_shuffling;
    for (size_t block_idx = 0; block_idx < block_sum; ++block_idx)
    {
        x = u * x * (1 - x);
        r.number = block_idx;
        r.value = x;
        rp4_for_mcu_shuffling.push_back(r);
    }
    std::sort(rp4_for_mcu_shuffling.begin(), rp4_for_mcu_shuffling.end(), [](const randSequence &lhs, const randSequence &rhs)
              { return lhs.value < rhs.value; });

    /***************************************************** reScrambleMcuNoDcc *************************************************************/
    // 解密顺序：最后加密的先解密
    reScrambleMcuNoDcc(rp4_for_mcu_shuffling, ac_ptr);

    /***************************************************** reScrambleSameRunAcc *************************************************************/
    // 在重新计算 AC info 之前，先解密 ACC 相同游程置乱
    nonZeroAcInfo **runs_ac_info_ptr_for_acc_shuffling = (nonZeroAcInfo **)malloc(sizeof(nonZeroAcInfo *) * ceiling_run);
    if (!runs_ac_info_ptr_for_acc_shuffling)
    {
        perror("Failed to allocate memory for runs_ac_info_ptr_for_acc_shuffling");
        exit(EXIT_FAILURE);
    }
    for (int run_val = 0; run_val < ceiling_run; ++run_val)
    {
        runs_ac_info_ptr_for_acc_shuffling[run_val] = (nonZeroAcInfo *)malloc(sizeof(nonZeroAcInfo) * runs_ac_num_ptr_for_acc_shuffling[run_val]);
        if (!runs_ac_info_ptr_for_acc_shuffling[run_val] && runs_ac_num_ptr_for_acc_shuffling[run_val] > 0)
        {
            perror("Failed to allocate memory for runs_ac_info_ptr_for_acc_shuffling[run_val]");
            for (int k = 0; k < run_val; ++k)
                free(runs_ac_info_ptr_for_acc_shuffling[k]);
            free(runs_ac_info_ptr_for_acc_shuffling);
            exit(EXIT_FAILURE);
        }
    }

    int *counter_ptr_for_acc_shuffling = (int *)malloc(sizeof(int) * ceiling_run);
    if (!counter_ptr_for_acc_shuffling)
    {
        perror("Failed to allocate memory for counter_ptr_for_acc_shuffling");
        for (int k = 0; k < ceiling_run; ++k)
            free(runs_ac_info_ptr_for_acc_shuffling[k]);
        free(runs_ac_info_ptr_for_acc_shuffling);
        exit(EXIT_FAILURE);
    }
    memset(counter_ptr_for_acc_shuffling, 0, sizeof(int) * ceiling_run);

    for (size_t block_idx = 0; block_idx < block_sum; ++block_idx)
    {
        int zero_run_count = 0;
        for (int zigzag_idx = 0; zigzag_idx < DCTSIZE2 - 1; ++zigzag_idx)
        {
            if (ac_ptr[block_idx][zigzag_idx] != 0)
            {
                if (zero_run_count < ceiling_run)
                {
                    int offset = counter_ptr_for_acc_shuffling[zero_run_count];
                    runs_ac_info_ptr_for_acc_shuffling[zero_run_count][offset].blockPosition = block_idx;
                    runs_ac_info_ptr_for_acc_shuffling[zero_run_count][offset].zigzagPosition = zigzag_idx;
                    runs_ac_info_ptr_for_acc_shuffling[zero_run_count][offset].value = ac_ptr[block_idx][zigzag_idx];
                    ++counter_ptr_for_acc_shuffling[zero_run_count];
                }
                zero_run_count = 0;
            }
            else
            {
                ++zero_run_count;
            }
        }
    }

    reScrambleSameRunAcc(rp3_for_acc_shuffling, ac_ptr, runs_ac_info_ptr_for_acc_shuffling, runs_ac_num_ptr_for_acc_shuffling);

    // 释放内存
    for (int run_val = 0; run_val < ceiling_run; ++run_val)
    {
        free(runs_ac_info_ptr_for_acc_shuffling[run_val]);
        runs_ac_info_ptr_for_acc_shuffling[run_val] = NULL;
    }
    free(runs_ac_info_ptr_for_acc_shuffling);
    runs_ac_info_ptr_for_acc_shuffling = NULL;
    free(runs_ac_num_ptr_for_acc_shuffling);
    runs_ac_num_ptr_for_acc_shuffling = NULL;
    free(counter_ptr_for_acc_shuffling);
    counter_ptr_for_acc_shuffling = NULL;

    /****************************************************** reDccIterSwap ****************************************************************/
    reDccIterSwap(rp2_for_dcc_iter, diff_ptr, iters_group_num_ptr_for_dcc_iter);
    free(iters_group_num_ptr_for_dcc_iter);
    iters_group_num_ptr_for_dcc_iter = NULL;

    /**************************************************** reScrambleSameSignDccGroup **********************************************************/
    // 1. 分割DCC序列为相同符号的分组 (根据当前状态下的DCC符号)
    size_t group_sum_dec = 0;
    int group_diff_num_current_dec = 0;
    booltype current_sign_dec;

    int *groups_diff_num_ptr_dec = (int *)malloc(sizeof(int) * block_sum);
    if (!groups_diff_num_ptr_dec)
    {
        perror("Failed to allocate memory for groups_diff_num_ptr_dec");
        exit(EXIT_FAILURE);
    }
    memset(groups_diff_num_ptr_dec, 0, sizeof(int) * block_sum);

    for (size_t block_idx = 0; block_idx < block_sum; ++block_idx)
    {
        if (block_idx == 0)
        {
            current_sign_dec = (diff_ptr[block_idx] >= 0) ? 1 : 0;
            group_diff_num_current_dec = 1;
        }
        else
        {
            if ((diff_ptr[block_idx] >= 0 && current_sign_dec == 1) || (diff_ptr[block_idx] < 0 && current_sign_dec == 0))
            {
                ++group_diff_num_current_dec;
            }
            else
            {
                groups_diff_num_ptr_dec[group_sum_dec] = group_diff_num_current_dec;
                group_diff_num_current_dec = 1;
                ++group_sum_dec;
                current_sign_dec = !current_sign_dec;
            }
        }
    }
    groups_diff_num_ptr_dec[group_sum_dec] = group_diff_num_current_dec;

    // 2. 复制DCC分组到动态数组中
    JCOEF **groups_diff_ptr_dec = (JCOEF **)malloc(sizeof(JCOEF *) * (group_sum_dec + 1));
    if (!groups_diff_ptr_dec)
    {
        perror("Failed to allocate memory for groups_diff_ptr_dec");
        free(groups_diff_num_ptr_dec);
        exit(EXIT_FAILURE);
    }
    int diff_index_offset_dec = 0;
    for (size_t group_idx = 0; group_idx <= group_sum_dec; ++group_idx)
    {
        int num_in_group = groups_diff_num_ptr_dec[group_idx];
        groups_diff_ptr_dec[group_idx] = (JCOEF *)malloc(sizeof(JCOEF) * num_in_group);
        if (!groups_diff_ptr_dec[group_idx])
        {
            perror("Failed to allocate memory for groups_diff_ptr_dec[group_idx]");
            for (size_t k = 0; k < group_idx; ++k)
                free(groups_diff_ptr_dec[k]);
            free(groups_diff_ptr_dec);
            free(groups_diff_num_ptr_dec);
            exit(EXIT_FAILURE);
        }
        memcpy(groups_diff_ptr_dec[group_idx], diff_ptr + diff_index_offset_dec, sizeof(JCOEF) * num_in_group);
        diff_index_offset_dec += num_in_group;
    }

    // 3. 将之前生成的随机序列分配到DCC分组中 (与加密时相同)
    std::vector<std::vector<intPair>> rp1_for_dcc_sign_shuffling_dec(group_sum_dec + 1);
    int rand_index_counter_dec = 0;
    for (size_t group_idx = 0; group_idx <= group_sum_dec; ++group_idx)
    {
        int num_in_group = groups_diff_num_ptr_dec[group_idx];
        assert(num_in_group >= 1);

        for (int diff_idx = 0; diff_idx < num_in_group; ++diff_idx)
        {
            intPair ip;
            ip.number = diff_idx;
            ip.value = temp_rp1_for_dcc_sign_shuffling[rand_index_counter_dec].number;
            rp1_for_dcc_sign_shuffling_dec[group_idx].push_back(ip);
            ++rand_index_counter_dec;
        }
        std::sort(rp1_for_dcc_sign_shuffling_dec[group_idx].begin(), rp1_for_dcc_sign_shuffling_dec[group_idx].end(), [](const intPair &lhs, const intPair &rhs)
                  { return lhs.value < rhs.value; });
    }

    // 4. 执行DCC相同符号逆置乱
    reScrambleSameSignDccGroup(rp1_for_dcc_sign_shuffling_dec, groups_diff_ptr_dec, groups_diff_num_ptr_dec, group_sum_dec);

    // 5. 将逆置乱后的DCC分组写回到原始的diff_ptr中
    diff_index_offset_dec = 0;
    for (size_t group_idx = 0; group_idx <= group_sum_dec; ++group_idx)
    {
        int num_in_group = groups_diff_num_ptr_dec[group_idx];
        if (num_in_group == 1)
        {
            ++diff_index_offset_dec;
        }
        else
        {
            memcpy(diff_ptr + diff_index_offset_dec, groups_diff_ptr_dec[group_idx], sizeof(JCOEF) * num_in_group);
            diff_index_offset_dec += num_in_group;
        }
    }

    // 6. 释放内存
    for (size_t group_idx = 0; group_idx <= group_sum_dec; ++group_idx)
    {
        free(groups_diff_ptr_dec[group_idx]);
        groups_diff_ptr_dec[group_idx] = NULL;
    }
    free(groups_diff_ptr_dec);
    groups_diff_ptr_dec = NULL;
    free(groups_diff_num_ptr_dec);
    groups_diff_num_ptr_dec = NULL;
}