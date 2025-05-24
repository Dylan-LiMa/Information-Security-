#include "sort.h"

#include <stdio.h>

// 这个文件在原始代码中是注释掉的，保持原样。
// void swap(randSequence* rp, int low, int high)
// {
//     randSequence temp = *(rp + low);
//     *(rp + low)  = *(rp + high);
//     *(rp + high) = temp;
// }

// int partition(randSequence* rp, int low, int high)
// {
//     double privotKey = (rp + low)->value;
//     while(low < high)
//     {
//         while((low < high) && ((rp+high)->value >= privotKey))
//             -- high;

//         swap(rp, low, high);
//         while((low < high) && ((rp+low)->value <= privotKey))
//             ++ low;

//         swap(rp, low, high);
//     }

//     return low;
// }

// void quickSort(randSequence* rp, int low, int high)
// {
//     if(low < high)
//     {
//         int privotLoc = partition(rp, low, high);
//         quickSort(rp, low, privotLoc-1);
//         quickSort(rp, privotLoc+1, high);
//     }
// }

// void printSeq(randSequence* rp, int length)
// {
//     int i;
//     for(i = 0; i != length; ++ i)
//         printf("%4d %f\n", (rp+i)->number, (rp+i)->value);
// }