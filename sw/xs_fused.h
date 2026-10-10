/* sw/gen_xs_fused.py が作る。手で直さない。w と x は別の配列（x が古い状態、w が新しい状態） */
#define XS_FUSED(w, x) \
    do { \
        (w)[0] = (x)[0] ^ (x)[7]; \
        (w)[1] = (x)[1] ^ (x)[8]; \
        (w)[2] = (x)[2] ^ (x)[9]; \
        (w)[3] = (x)[3] ^ (x)[10]; \
        (w)[4] = (x)[4] ^ (x)[11]; \
        (w)[5] = (x)[5] ^ (x)[12]; \
        (w)[6] = (x)[0] ^ (x)[6] ^ (x)[13]; \
        (w)[7] = (x)[1] ^ (x)[7] ^ (x)[14]; \
        (w)[8] = (x)[2] ^ (x)[8] ^ (x)[15]; \
        (w)[9] = (x)[3] ^ (x)[9] ^ (x)[16]; \
        (w)[10] = (x)[4] ^ (x)[10] ^ (x)[17]; \
        (w)[11] = (x)[5] ^ (x)[11] ^ (x)[18]; \
        (w)[12] = (x)[6] ^ (x)[12] ^ (x)[19]; \
        (w)[13] = (x)[0] ^ (x)[7] ^ (x)[13] ^ (x)[20]; \
        (w)[14] = (x)[1] ^ (x)[8] ^ (x)[14] ^ (x)[21]; \
        (w)[15] = (x)[2] ^ (x)[9] ^ (x)[15] ^ (x)[22]; \
        (w)[16] = (x)[3] ^ (x)[10] ^ (x)[16] ^ (x)[23]; \
        (w)[17] = (x)[0] ^ (x)[4] ^ (x)[7] ^ (x)[11] ^ (x)[17] ^ (x)[24]; \
        (w)[18] = (x)[1] ^ (x)[5] ^ (x)[8] ^ (x)[12] ^ (x)[18] ^ (x)[25]; \
        (w)[19] = (x)[2] ^ (x)[6] ^ (x)[9] ^ (x)[13] ^ (x)[19] ^ (x)[26]; \
        (w)[20] = (x)[3] ^ (x)[7] ^ (x)[10] ^ (x)[14] ^ (x)[20] ^ (x)[27]; \
        (w)[21] = (x)[4] ^ (x)[8] ^ (x)[11] ^ (x)[15] ^ (x)[21] ^ (x)[28]; \
        (w)[22] = (x)[5] ^ (x)[9] ^ (x)[12] ^ (x)[16] ^ (x)[22] ^ (x)[29]; \
        (w)[23] = (x)[0] ^ (x)[6] ^ (x)[10] ^ (x)[13] ^ (x)[17] ^ (x)[23] ^ (x)[30]; \
        (w)[24] = (x)[1] ^ (x)[7] ^ (x)[11] ^ (x)[14] ^ (x)[18] ^ (x)[24] ^ (x)[31]; \
        (w)[25] = (x)[2] ^ (x)[8] ^ (x)[12] ^ (x)[15] ^ (x)[19] ^ (x)[25] ^ (x)[32]; \
        (w)[26] = (x)[3] ^ (x)[9] ^ (x)[13] ^ (x)[16] ^ (x)[20] ^ (x)[26] ^ (x)[33]; \
        (w)[27] = (x)[4] ^ (x)[10] ^ (x)[14] ^ (x)[17] ^ (x)[21] ^ (x)[27] ^ (x)[34]; \
        (w)[28] = (x)[5] ^ (x)[11] ^ (x)[15] ^ (x)[18] ^ (x)[22] ^ (x)[28] ^ (x)[35]; \
        (w)[29] = (x)[6] ^ (x)[12] ^ (x)[16] ^ (x)[19] ^ (x)[23] ^ (x)[29] ^ (x)[36]; \
        (w)[30] = (x)[0] ^ (x)[7] ^ (x)[13] ^ (x)[17] ^ (x)[20] ^ (x)[24] ^ (x)[30] ^ (x)[37]; \
        (w)[31] = (x)[1] ^ (x)[8] ^ (x)[14] ^ (x)[18] ^ (x)[21] ^ (x)[25] ^ (x)[31] ^ (x)[38]; \
        (w)[32] = (x)[2] ^ (x)[9] ^ (x)[15] ^ (x)[19] ^ (x)[22] ^ (x)[26] ^ (x)[32] ^ (x)[39]; \
        (w)[33] = (x)[3] ^ (x)[10] ^ (x)[16] ^ (x)[20] ^ (x)[23] ^ (x)[27] ^ (x)[33] ^ (x)[40]; \
        (w)[34] = (x)[4] ^ (x)[11] ^ (x)[17] ^ (x)[21] ^ (x)[24] ^ (x)[28] ^ (x)[34] ^ (x)[41]; \
        (w)[35] = (x)[5] ^ (x)[12] ^ (x)[18] ^ (x)[22] ^ (x)[25] ^ (x)[29] ^ (x)[35] ^ (x)[42]; \
        (w)[36] = (x)[6] ^ (x)[13] ^ (x)[19] ^ (x)[23] ^ (x)[26] ^ (x)[30] ^ (x)[36] ^ (x)[43]; \
        (w)[37] = (x)[7] ^ (x)[14] ^ (x)[20] ^ (x)[24] ^ (x)[27] ^ (x)[31] ^ (x)[37] ^ (x)[44]; \
        (w)[38] = (x)[8] ^ (x)[15] ^ (x)[21] ^ (x)[25] ^ (x)[28] ^ (x)[32] ^ (x)[38] ^ (x)[45]; \
        (w)[39] = (x)[9] ^ (x)[16] ^ (x)[22] ^ (x)[26] ^ (x)[29] ^ (x)[33] ^ (x)[39] ^ (x)[46]; \
        (w)[40] = (x)[10] ^ (x)[17] ^ (x)[23] ^ (x)[27] ^ (x)[30] ^ (x)[34] ^ (x)[40] ^ (x)[47]; \
        (w)[41] = (x)[11] ^ (x)[18] ^ (x)[24] ^ (x)[28] ^ (x)[31] ^ (x)[35] ^ (x)[41] ^ (x)[48]; \
        (w)[42] = (x)[12] ^ (x)[19] ^ (x)[25] ^ (x)[29] ^ (x)[32] ^ (x)[36] ^ (x)[42] ^ (x)[49]; \
        (w)[43] = (x)[13] ^ (x)[20] ^ (x)[26] ^ (x)[30] ^ (x)[33] ^ (x)[37] ^ (x)[43] ^ (x)[50]; \
        (w)[44] = (x)[14] ^ (x)[21] ^ (x)[27] ^ (x)[31] ^ (x)[34] ^ (x)[38] ^ (x)[44] ^ (x)[51]; \
        (w)[45] = (x)[15] ^ (x)[22] ^ (x)[28] ^ (x)[32] ^ (x)[35] ^ (x)[39] ^ (x)[45] ^ (x)[52]; \
        (w)[46] = (x)[16] ^ (x)[23] ^ (x)[29] ^ (x)[33] ^ (x)[36] ^ (x)[40] ^ (x)[46] ^ (x)[53]; \
        (w)[47] = (x)[17] ^ (x)[24] ^ (x)[30] ^ (x)[34] ^ (x)[37] ^ (x)[41] ^ (x)[47] ^ (x)[54]; \
        (w)[48] = (x)[18] ^ (x)[25] ^ (x)[31] ^ (x)[35] ^ (x)[38] ^ (x)[42] ^ (x)[48] ^ (x)[55]; \
        (w)[49] = (x)[19] ^ (x)[26] ^ (x)[32] ^ (x)[36] ^ (x)[39] ^ (x)[43] ^ (x)[49] ^ (x)[56]; \
        (w)[50] = (x)[20] ^ (x)[27] ^ (x)[33] ^ (x)[37] ^ (x)[40] ^ (x)[44] ^ (x)[50] ^ (x)[57]; \
        (w)[51] = (x)[21] ^ (x)[28] ^ (x)[34] ^ (x)[38] ^ (x)[41] ^ (x)[45] ^ (x)[51] ^ (x)[58]; \
        (w)[52] = (x)[22] ^ (x)[29] ^ (x)[35] ^ (x)[39] ^ (x)[42] ^ (x)[46] ^ (x)[52] ^ (x)[59]; \
        (w)[53] = (x)[23] ^ (x)[30] ^ (x)[36] ^ (x)[40] ^ (x)[43] ^ (x)[47] ^ (x)[53] ^ (x)[60]; \
        (w)[54] = (x)[24] ^ (x)[31] ^ (x)[37] ^ (x)[41] ^ (x)[44] ^ (x)[48] ^ (x)[54] ^ (x)[61]; \
        (w)[55] = (x)[25] ^ (x)[32] ^ (x)[38] ^ (x)[42] ^ (x)[45] ^ (x)[49] ^ (x)[55] ^ (x)[62]; \
        (w)[56] = (x)[26] ^ (x)[33] ^ (x)[39] ^ (x)[43] ^ (x)[46] ^ (x)[50] ^ (x)[56] ^ (x)[63]; \
        (w)[57] = (x)[27] ^ (x)[34] ^ (x)[40] ^ (x)[44] ^ (x)[47] ^ (x)[57]; \
        (w)[58] = (x)[28] ^ (x)[35] ^ (x)[41] ^ (x)[45] ^ (x)[48] ^ (x)[58]; \
        (w)[59] = (x)[29] ^ (x)[36] ^ (x)[42] ^ (x)[46] ^ (x)[49] ^ (x)[59]; \
        (w)[60] = (x)[30] ^ (x)[37] ^ (x)[43] ^ (x)[47] ^ (x)[50] ^ (x)[60]; \
        (w)[61] = (x)[31] ^ (x)[38] ^ (x)[44] ^ (x)[48] ^ (x)[51] ^ (x)[61]; \
        (w)[62] = (x)[32] ^ (x)[39] ^ (x)[45] ^ (x)[49] ^ (x)[52] ^ (x)[62]; \
        (w)[63] = (x)[33] ^ (x)[40] ^ (x)[46] ^ (x)[50] ^ (x)[53] ^ (x)[63]; \
    } while (0)

/* 窓を流す形: 読み 115・XOR 155・書き 64、同時に生きる値 26 個 */
#if defined(__aarch64__)
#define XS_A64(W_, X_) \
    __asm__ volatile( \
        "ldr q0, [%[x], #0]\n\t" \
        "ldr q1, [%[x], #16]\n\t" \
        "ldr q2, [%[x], #32]\n\t" \
        "ldr q3, [%[x], #48]\n\t" \
        "ldr q4, [%[x], #64]\n\t" \
        "ldr q5, [%[x], #80]\n\t" \
        "ldr q6, [%[x], #96]\n\t" \
        "ldr q7, [%[x], #112]\n\t" \
        "eor v0.16b, v0.16b, v7.16b\n\t" \
        "str q0, [%[w], #0]\n\t" \
        "ldr q8, [%[x], #128]\n\t" \
        "eor v1.16b, v1.16b, v8.16b\n\t" \
        "str q1, [%[w], #16]\n\t" \
        "ldr q9, [%[x], #144]\n\t" \
        "eor v2.16b, v2.16b, v9.16b\n\t" \
        "str q2, [%[w], #32]\n\t" \
        "ldr q10, [%[x], #160]\n\t" \
        "eor v3.16b, v3.16b, v10.16b\n\t" \
        "str q3, [%[w], #48]\n\t" \
        "ldr q11, [%[x], #176]\n\t" \
        "eor v4.16b, v4.16b, v11.16b\n\t" \
        "str q4, [%[w], #64]\n\t" \
        "ldr q12, [%[x], #192]\n\t" \
        "eor v5.16b, v5.16b, v12.16b\n\t" \
        "str q5, [%[w], #80]\n\t" \
        "ldr q13, [%[x], #208]\n\t" \
        "ldr q14, [%[x], #0]\n\t" \
        "eor v14.16b, v13.16b, v14.16b\n\t" \
        "eor v6.16b, v6.16b, v14.16b\n\t" \
        "str q6, [%[w], #96]\n\t" \
        "ldr q13, [%[x], #224]\n\t" \
        "ldr q15, [%[x], #16]\n\t" \
        "eor v15.16b, v13.16b, v15.16b\n\t" \
        "eor v7.16b, v7.16b, v15.16b\n\t" \
        "str q7, [%[w], #112]\n\t" \
        "ldr q13, [%[x], #240]\n\t" \
        "ldr q16, [%[x], #32]\n\t" \
        "eor v16.16b, v13.16b, v16.16b\n\t" \
        "eor v8.16b, v8.16b, v16.16b\n\t" \
        "str q8, [%[w], #128]\n\t" \
        "ldr q13, [%[x], #256]\n\t" \
        "ldr q17, [%[x], #48]\n\t" \
        "eor v17.16b, v13.16b, v17.16b\n\t" \
        "eor v9.16b, v9.16b, v17.16b\n\t" \
        "str q9, [%[w], #144]\n\t" \
        "ldr q13, [%[x], #272]\n\t" \
        "ldr q18, [%[x], #64]\n\t" \
        "eor v18.16b, v13.16b, v18.16b\n\t" \
        "eor v10.16b, v10.16b, v18.16b\n\t" \
        "str q10, [%[w], #160]\n\t" \
        "ldr q13, [%[x], #288]\n\t" \
        "ldr q19, [%[x], #80]\n\t" \
        "eor v19.16b, v13.16b, v19.16b\n\t" \
        "eor v11.16b, v11.16b, v19.16b\n\t" \
        "str q11, [%[w], #176]\n\t" \
        "ldr q13, [%[x], #304]\n\t" \
        "ldr q20, [%[x], #96]\n\t" \
        "eor v20.16b, v13.16b, v20.16b\n\t" \
        "eor v12.16b, v12.16b, v20.16b\n\t" \
        "str q12, [%[w], #192]\n\t" \
        "ldr q13, [%[x], #320]\n\t" \
        "ldr q21, [%[x], #112]\n\t" \
        "eor v21.16b, v13.16b, v21.16b\n\t" \
        "eor v14.16b, v14.16b, v21.16b\n\t" \
        "str q14, [%[w], #208]\n\t" \
        "ldr q13, [%[x], #336]\n\t" \
        "ldr q22, [%[x], #128]\n\t" \
        "eor v22.16b, v13.16b, v22.16b\n\t" \
        "eor v15.16b, v15.16b, v22.16b\n\t" \
        "str q15, [%[w], #224]\n\t" \
        "ldr q13, [%[x], #352]\n\t" \
        "ldr q23, [%[x], #144]\n\t" \
        "eor v23.16b, v13.16b, v23.16b\n\t" \
        "eor v16.16b, v16.16b, v23.16b\n\t" \
        "str q16, [%[w], #240]\n\t" \
        "ldr q13, [%[x], #368]\n\t" \
        "ldr q24, [%[x], #160]\n\t" \
        "eor v24.16b, v13.16b, v24.16b\n\t" \
        "eor v17.16b, v17.16b, v24.16b\n\t" \
        "str q17, [%[w], #256]\n\t" \
        "ldr q13, [%[x], #384]\n\t" \
        "ldr q25, [%[x], #176]\n\t" \
        "eor v25.16b, v13.16b, v25.16b\n\t" \
        "eor v18.16b, v18.16b, v25.16b\n\t" \
        "eor v0.16b, v18.16b, v0.16b\n\t" \
        "str q0, [%[w], #272]\n\t" \
        "ldr q0, [%[x], #400]\n\t" \
        "ldr q13, [%[x], #192]\n\t" \
        "eor v13.16b, v0.16b, v13.16b\n\t" \
        "eor v19.16b, v19.16b, v13.16b\n\t" \
        "eor v1.16b, v19.16b, v1.16b\n\t" \
        "str q1, [%[w], #288]\n\t" \
        "ldr q1, [%[x], #416]\n\t" \
        "ldr q0, [%[x], #208]\n\t" \
        "eor v0.16b, v1.16b, v0.16b\n\t" \
        "eor v20.16b, v20.16b, v0.16b\n\t" \
        "eor v2.16b, v20.16b, v2.16b\n\t" \
        "str q2, [%[w], #304]\n\t" \
        "ldr q2, [%[x], #432]\n\t" \
        "ldr q1, [%[x], #224]\n\t" \
        "eor v1.16b, v2.16b, v1.16b\n\t" \
        "eor v21.16b, v21.16b, v1.16b\n\t" \
        "eor v3.16b, v21.16b, v3.16b\n\t" \
        "str q3, [%[w], #320]\n\t" \
        "ldr q3, [%[x], #448]\n\t" \
        "ldr q2, [%[x], #240]\n\t" \
        "eor v2.16b, v3.16b, v2.16b\n\t" \
        "eor v22.16b, v22.16b, v2.16b\n\t" \
        "eor v4.16b, v22.16b, v4.16b\n\t" \
        "str q4, [%[w], #336]\n\t" \
        "ldr q4, [%[x], #464]\n\t" \
        "ldr q3, [%[x], #256]\n\t" \
        "eor v3.16b, v4.16b, v3.16b\n\t" \
        "eor v23.16b, v23.16b, v3.16b\n\t" \
        "eor v5.16b, v23.16b, v5.16b\n\t" \
        "str q5, [%[w], #352]\n\t" \
        "ldr q5, [%[x], #480]\n\t" \
        "ldr q4, [%[x], #272]\n\t" \
        "eor v4.16b, v5.16b, v4.16b\n\t" \
        "eor v24.16b, v24.16b, v4.16b\n\t" \
        "eor v6.16b, v24.16b, v6.16b\n\t" \
        "str q6, [%[w], #368]\n\t" \
        "ldr q6, [%[x], #496]\n\t" \
        "ldr q5, [%[x], #288]\n\t" \
        "eor v5.16b, v6.16b, v5.16b\n\t" \
        "eor v25.16b, v25.16b, v5.16b\n\t" \
        "eor v7.16b, v25.16b, v7.16b\n\t" \
        "str q7, [%[w], #384]\n\t" \
        "ldr q7, [%[x], #512]\n\t" \
        "ldr q6, [%[x], #304]\n\t" \
        "eor v6.16b, v7.16b, v6.16b\n\t" \
        "eor v13.16b, v13.16b, v6.16b\n\t" \
        "eor v8.16b, v13.16b, v8.16b\n\t" \
        "str q8, [%[w], #400]\n\t" \
        "ldr q8, [%[x], #528]\n\t" \
        "ldr q7, [%[x], #320]\n\t" \
        "eor v7.16b, v8.16b, v7.16b\n\t" \
        "eor v0.16b, v0.16b, v7.16b\n\t" \
        "eor v9.16b, v0.16b, v9.16b\n\t" \
        "str q9, [%[w], #416]\n\t" \
        "ldr q9, [%[x], #544]\n\t" \
        "ldr q8, [%[x], #336]\n\t" \
        "eor v8.16b, v9.16b, v8.16b\n\t" \
        "eor v1.16b, v1.16b, v8.16b\n\t" \
        "eor v10.16b, v1.16b, v10.16b\n\t" \
        "str q10, [%[w], #432]\n\t" \
        "ldr q10, [%[x], #560]\n\t" \
        "ldr q9, [%[x], #352]\n\t" \
        "eor v9.16b, v10.16b, v9.16b\n\t" \
        "eor v2.16b, v2.16b, v9.16b\n\t" \
        "eor v11.16b, v2.16b, v11.16b\n\t" \
        "str q11, [%[w], #448]\n\t" \
        "ldr q11, [%[x], #576]\n\t" \
        "ldr q10, [%[x], #368]\n\t" \
        "eor v10.16b, v11.16b, v10.16b\n\t" \
        "eor v3.16b, v3.16b, v10.16b\n\t" \
        "eor v12.16b, v3.16b, v12.16b\n\t" \
        "str q12, [%[w], #464]\n\t" \
        "ldr q12, [%[x], #592]\n\t" \
        "ldr q11, [%[x], #384]\n\t" \
        "eor v11.16b, v12.16b, v11.16b\n\t" \
        "eor v4.16b, v4.16b, v11.16b\n\t" \
        "eor v14.16b, v4.16b, v14.16b\n\t" \
        "str q14, [%[w], #480]\n\t" \
        "ldr q14, [%[x], #608]\n\t" \
        "ldr q12, [%[x], #400]\n\t" \
        "eor v12.16b, v14.16b, v12.16b\n\t" \
        "eor v5.16b, v5.16b, v12.16b\n\t" \
        "eor v15.16b, v5.16b, v15.16b\n\t" \
        "str q15, [%[w], #496]\n\t" \
        "ldr q15, [%[x], #624]\n\t" \
        "ldr q14, [%[x], #416]\n\t" \
        "eor v14.16b, v15.16b, v14.16b\n\t" \
        "eor v6.16b, v6.16b, v14.16b\n\t" \
        "eor v16.16b, v6.16b, v16.16b\n\t" \
        "str q16, [%[w], #512]\n\t" \
        "ldr q16, [%[x], #640]\n\t" \
        "ldr q15, [%[x], #432]\n\t" \
        "eor v15.16b, v16.16b, v15.16b\n\t" \
        "eor v7.16b, v7.16b, v15.16b\n\t" \
        "eor v17.16b, v7.16b, v17.16b\n\t" \
        "str q17, [%[w], #528]\n\t" \
        "ldr q17, [%[x], #656]\n\t" \
        "ldr q16, [%[x], #448]\n\t" \
        "eor v16.16b, v17.16b, v16.16b\n\t" \
        "eor v8.16b, v8.16b, v16.16b\n\t" \
        "eor v18.16b, v8.16b, v18.16b\n\t" \
        "str q18, [%[w], #544]\n\t" \
        "ldr q18, [%[x], #672]\n\t" \
        "ldr q17, [%[x], #464]\n\t" \
        "eor v17.16b, v18.16b, v17.16b\n\t" \
        "eor v9.16b, v9.16b, v17.16b\n\t" \
        "eor v19.16b, v9.16b, v19.16b\n\t" \
        "str q19, [%[w], #560]\n\t" \
        "ldr q19, [%[x], #688]\n\t" \
        "ldr q18, [%[x], #480]\n\t" \
        "eor v18.16b, v19.16b, v18.16b\n\t" \
        "eor v10.16b, v10.16b, v18.16b\n\t" \
        "eor v20.16b, v10.16b, v20.16b\n\t" \
        "str q20, [%[w], #576]\n\t" \
        "ldr q20, [%[x], #704]\n\t" \
        "ldr q19, [%[x], #496]\n\t" \
        "eor v19.16b, v20.16b, v19.16b\n\t" \
        "eor v11.16b, v11.16b, v19.16b\n\t" \
        "eor v21.16b, v11.16b, v21.16b\n\t" \
        "str q21, [%[w], #592]\n\t" \
        "ldr q21, [%[x], #720]\n\t" \
        "ldr q20, [%[x], #512]\n\t" \
        "eor v20.16b, v21.16b, v20.16b\n\t" \
        "eor v12.16b, v12.16b, v20.16b\n\t" \
        "eor v22.16b, v12.16b, v22.16b\n\t" \
        "str q22, [%[w], #608]\n\t" \
        "ldr q22, [%[x], #736]\n\t" \
        "ldr q21, [%[x], #528]\n\t" \
        "eor v21.16b, v22.16b, v21.16b\n\t" \
        "eor v14.16b, v14.16b, v21.16b\n\t" \
        "eor v23.16b, v14.16b, v23.16b\n\t" \
        "str q23, [%[w], #624]\n\t" \
        "ldr q23, [%[x], #752]\n\t" \
        "ldr q22, [%[x], #544]\n\t" \
        "eor v22.16b, v23.16b, v22.16b\n\t" \
        "eor v15.16b, v15.16b, v22.16b\n\t" \
        "eor v24.16b, v15.16b, v24.16b\n\t" \
        "str q24, [%[w], #640]\n\t" \
        "ldr q24, [%[x], #768]\n\t" \
        "ldr q23, [%[x], #560]\n\t" \
        "eor v23.16b, v24.16b, v23.16b\n\t" \
        "eor v16.16b, v16.16b, v23.16b\n\t" \
        "eor v25.16b, v16.16b, v25.16b\n\t" \
        "str q25, [%[w], #656]\n\t" \
        "ldr q25, [%[x], #784]\n\t" \
        "ldr q24, [%[x], #576]\n\t" \
        "eor v24.16b, v25.16b, v24.16b\n\t" \
        "eor v17.16b, v17.16b, v24.16b\n\t" \
        "eor v13.16b, v17.16b, v13.16b\n\t" \
        "str q13, [%[w], #672]\n\t" \
        "ldr q13, [%[x], #800]\n\t" \
        "ldr q25, [%[x], #592]\n\t" \
        "eor v25.16b, v13.16b, v25.16b\n\t" \
        "eor v18.16b, v18.16b, v25.16b\n\t" \
        "eor v0.16b, v18.16b, v0.16b\n\t" \
        "str q0, [%[w], #688]\n\t" \
        "ldr q0, [%[x], #816]\n\t" \
        "ldr q13, [%[x], #608]\n\t" \
        "eor v13.16b, v0.16b, v13.16b\n\t" \
        "eor v19.16b, v19.16b, v13.16b\n\t" \
        "eor v1.16b, v19.16b, v1.16b\n\t" \
        "str q1, [%[w], #704]\n\t" \
        "ldr q1, [%[x], #832]\n\t" \
        "ldr q0, [%[x], #624]\n\t" \
        "eor v0.16b, v1.16b, v0.16b\n\t" \
        "eor v20.16b, v20.16b, v0.16b\n\t" \
        "eor v2.16b, v20.16b, v2.16b\n\t" \
        "str q2, [%[w], #720]\n\t" \
        "ldr q2, [%[x], #848]\n\t" \
        "ldr q1, [%[x], #640]\n\t" \
        "eor v1.16b, v2.16b, v1.16b\n\t" \
        "eor v21.16b, v21.16b, v1.16b\n\t" \
        "eor v3.16b, v21.16b, v3.16b\n\t" \
        "str q3, [%[w], #736]\n\t" \
        "ldr q3, [%[x], #864]\n\t" \
        "ldr q2, [%[x], #656]\n\t" \
        "eor v2.16b, v3.16b, v2.16b\n\t" \
        "eor v22.16b, v22.16b, v2.16b\n\t" \
        "eor v4.16b, v22.16b, v4.16b\n\t" \
        "str q4, [%[w], #752]\n\t" \
        "ldr q4, [%[x], #880]\n\t" \
        "ldr q22, [%[x], #672]\n\t" \
        "eor v22.16b, v4.16b, v22.16b\n\t" \
        "eor v23.16b, v23.16b, v22.16b\n\t" \
        "eor v5.16b, v23.16b, v5.16b\n\t" \
        "str q5, [%[w], #768]\n\t" \
        "ldr q5, [%[x], #896]\n\t" \
        "ldr q23, [%[x], #688]\n\t" \
        "eor v23.16b, v5.16b, v23.16b\n\t" \
        "eor v24.16b, v24.16b, v23.16b\n\t" \
        "eor v6.16b, v24.16b, v6.16b\n\t" \
        "str q6, [%[w], #784]\n\t" \
        "ldr q6, [%[x], #912]\n\t" \
        "ldr q24, [%[x], #704]\n\t" \
        "eor v24.16b, v6.16b, v24.16b\n\t" \
        "eor v25.16b, v25.16b, v24.16b\n\t" \
        "eor v7.16b, v25.16b, v7.16b\n\t" \
        "str q7, [%[w], #800]\n\t" \
        "ldr q7, [%[x], #928]\n\t" \
        "ldr q25, [%[x], #720]\n\t" \
        "eor v25.16b, v7.16b, v25.16b\n\t" \
        "eor v13.16b, v13.16b, v25.16b\n\t" \
        "eor v8.16b, v13.16b, v8.16b\n\t" \
        "str q8, [%[w], #816]\n\t" \
        "ldr q8, [%[x], #944]\n\t" \
        "ldr q13, [%[x], #736]\n\t" \
        "eor v13.16b, v8.16b, v13.16b\n\t" \
        "eor v0.16b, v0.16b, v13.16b\n\t" \
        "eor v9.16b, v0.16b, v9.16b\n\t" \
        "str q9, [%[w], #832]\n\t" \
        "ldr q9, [%[x], #960]\n\t" \
        "ldr q0, [%[x], #752]\n\t" \
        "eor v0.16b, v9.16b, v0.16b\n\t" \
        "eor v1.16b, v1.16b, v0.16b\n\t" \
        "eor v10.16b, v1.16b, v10.16b\n\t" \
        "str q10, [%[w], #848]\n\t" \
        "ldr q10, [%[x], #976]\n\t" \
        "ldr q1, [%[x], #768]\n\t" \
        "eor v1.16b, v10.16b, v1.16b\n\t" \
        "eor v2.16b, v2.16b, v1.16b\n\t" \
        "eor v11.16b, v2.16b, v11.16b\n\t" \
        "str q11, [%[w], #864]\n\t" \
        "ldr q11, [%[x], #992]\n\t" \
        "ldr q2, [%[x], #784]\n\t" \
        "eor v2.16b, v11.16b, v2.16b\n\t" \
        "eor v22.16b, v22.16b, v2.16b\n\t" \
        "eor v12.16b, v22.16b, v12.16b\n\t" \
        "str q12, [%[w], #880]\n\t" \
        "ldr q12, [%[x], #1008]\n\t" \
        "ldr q22, [%[x], #800]\n\t" \
        "eor v22.16b, v12.16b, v22.16b\n\t" \
        "eor v23.16b, v23.16b, v22.16b\n\t" \
        "eor v14.16b, v23.16b, v14.16b\n\t" \
        "str q14, [%[w], #896]\n\t" \
        "eor v15.16b, v24.16b, v15.16b\n\t" \
        "str q15, [%[w], #912]\n\t" \
        "eor v16.16b, v25.16b, v16.16b\n\t" \
        "str q16, [%[w], #928]\n\t" \
        "eor v17.16b, v13.16b, v17.16b\n\t" \
        "str q17, [%[w], #944]\n\t" \
        "eor v18.16b, v0.16b, v18.16b\n\t" \
        "str q18, [%[w], #960]\n\t" \
        "eor v19.16b, v1.16b, v19.16b\n\t" \
        "str q19, [%[w], #976]\n\t" \
        "eor v20.16b, v2.16b, v20.16b\n\t" \
        "str q20, [%[w], #992]\n\t" \
        "eor v21.16b, v22.16b, v21.16b\n\t" \
        "str q21, [%[w], #1008]\n\t" \
        : : [w] "r"(W_), [x] "r"(X_) \
        : "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31")
#elif defined(__arm__) && defined(__ARM_NEON)
/* A9 向けに並べ替えた形（結果の遅れ 読み 4・XOR 3、先読み 12 命令）: 同時に生きる値 28 個 */
#define XS_A32(W_, X_) \
    __asm__ volatile( \
        "vldr d0, [%[x], #0]\n\t" \
        "vldr d1, [%[x], #16]\n\t" \
        "vldr d2, [%[x], #32]\n\t" \
        "vldr d3, [%[x], #48]\n\t" \
        "vldr d4, [%[x], #64]\n\t" \
        "vldr d5, [%[x], #80]\n\t" \
        "vldr d6, [%[x], #96]\n\t" \
        "vldr d7, [%[x], #112]\n\t" \
        "vldr d8, [%[x], #128]\n\t" \
        "vldr d9, [%[x], #144]\n\t" \
        "vldr d10, [%[x], #160]\n\t" \
        "veor d0, d0, d7\n\t" \
        "veor d1, d1, d8\n\t" \
        "veor d2, d2, d9\n\t" \
        "vstr d0, [%[w], #0]\n\t" \
        "vstr d1, [%[w], #16]\n\t" \
        "vstr d2, [%[w], #32]\n\t" \
        "veor d3, d3, d10\n\t" \
        "vldr d11, [%[x], #176]\n\t" \
        "vldr d12, [%[x], #192]\n\t" \
        "vstr d3, [%[w], #48]\n\t" \
        "vldr d13, [%[x], #208]\n\t" \
        "veor d4, d4, d11\n\t" \
        "veor d5, d5, d12\n\t" \
        "vldr d14, [%[x], #0]\n\t" \
        "vstr d4, [%[w], #64]\n\t" \
        "vstr d5, [%[w], #80]\n\t" \
        "vldr d15, [%[x], #224]\n\t" \
        "veor d14, d13, d14\n\t" \
        "vldr d13, [%[x], #16]\n\t" \
        "vldr d16, [%[x], #240]\n\t" \
        "veor d6, d6, d14\n\t" \
        "vldr d17, [%[x], #32]\n\t" \
        "veor d13, d15, d13\n\t" \
        "vstr d6, [%[w], #96]\n\t" \
        "vldr d15, [%[x], #256]\n\t" \
        "veor d7, d7, d13\n\t" \
        "veor d17, d16, d17\n\t" \
        "vldr d16, [%[x], #48]\n\t" \
        "vstr d7, [%[w], #112]\n\t" \
        "veor d8, d8, d17\n\t" \
        "vldr d18, [%[x], #272]\n\t" \
        "veor d16, d15, d16\n\t" \
        "vstr d8, [%[w], #128]\n\t" \
        "vldr d15, [%[x], #64]\n\t" \
        "veor d9, d9, d16\n\t" \
        "vldr d19, [%[x], #288]\n\t" \
        "vldr d20, [%[x], #80]\n\t" \
        "vstr d9, [%[w], #144]\n\t" \
        "veor d15, d18, d15\n\t" \
        "vldr d18, [%[x], #304]\n\t" \
        "veor d20, d19, d20\n\t" \
        "veor d10, d10, d15\n\t" \
        "vldr d19, [%[x], #96]\n\t" \
        "veor d11, d11, d20\n\t" \
        "vstr d10, [%[w], #160]\n\t" \
        "vldr d21, [%[x], #320]\n\t" \
        "vstr d11, [%[w], #176]\n\t" \
        "veor d19, d18, d19\n\t" \
        "vldr d18, [%[x], #112]\n\t" \
        "vldr d22, [%[x], #336]\n\t" \
        "veor d12, d12, d19\n\t" \
        "vldr d23, [%[x], #128]\n\t" \
        "veor d18, d21, d18\n\t" \
        "vstr d12, [%[w], #192]\n\t" \
        "vldr d21, [%[x], #352]\n\t" \
        "veor d14, d14, d18\n\t" \
        "veor d23, d22, d23\n\t" \
        "vldr d22, [%[x], #144]\n\t" \
        "vstr d14, [%[w], #208]\n\t" \
        "veor d13, d13, d23\n\t" \
        "vldr d24, [%[x], #368]\n\t" \
        "veor d22, d21, d22\n\t" \
        "vstr d13, [%[w], #224]\n\t" \
        "vldr d21, [%[x], #160]\n\t" \
        "veor d17, d17, d22\n\t" \
        "vldr d25, [%[x], #384]\n\t" \
        "vldr d26, [%[x], #176]\n\t" \
        "vstr d17, [%[w], #240]\n\t" \
        "veor d21, d24, d21\n\t" \
        "vldr d24, [%[x], #400]\n\t" \
        "veor d26, d25, d26\n\t" \
        "veor d16, d16, d21\n\t" \
        "vldr d25, [%[x], #192]\n\t" \
        "veor d15, d15, d26\n\t" \
        "vstr d16, [%[w], #256]\n\t" \
        "vldr d27, [%[x], #416]\n\t" \
        "veor d0, d15, d0\n\t" \
        "veor d25, d24, d25\n\t" \
        "vldr d24, [%[x], #208]\n\t" \
        "vstr d0, [%[w], #272]\n\t" \
        "veor d20, d20, d25\n\t" \
        "vldr d0, [%[x], #432]\n\t" \
        "veor d24, d27, d24\n\t" \
        "veor d1, d20, d1\n\t" \
        "vldr d27, [%[x], #224]\n\t" \
        "veor d19, d19, d24\n\t" \
        "vstr d1, [%[w], #288]\n\t" \
        "vldr d1, [%[x], #448]\n\t" \
        "veor d2, d19, d2\n\t" \
        "veor d27, d0, d27\n\t" \
        "vldr d0, [%[x], #240]\n\t" \
        "vstr d2, [%[w], #304]\n\t" \
        "veor d18, d18, d27\n\t" \
        "vldr d2, [%[x], #464]\n\t" \
        "veor d0, d1, d0\n\t" \
        "veor d3, d18, d3\n\t" \
        "vldr d1, [%[x], #256]\n\t" \
        "veor d23, d23, d0\n\t" \
        "vstr d3, [%[w], #320]\n\t" \
        "vldr d3, [%[x], #480]\n\t" \
        "veor d4, d23, d4\n\t" \
        "veor d1, d2, d1\n\t" \
        "vldr d2, [%[x], #272]\n\t" \
        "vstr d4, [%[w], #336]\n\t" \
        "veor d22, d22, d1\n\t" \
        "vldr d4, [%[x], #496]\n\t" \
        "veor d2, d3, d2\n\t" \
        "veor d5, d22, d5\n\t" \
        "vldr d3, [%[x], #288]\n\t" \
        "veor d21, d21, d2\n\t" \
        "vstr d5, [%[w], #352]\n\t" \
        "vldr d5, [%[x], #512]\n\t" \
        "veor d6, d21, d6\n\t" \
        "veor d3, d4, d3\n\t" \
        "vldr d4, [%[x], #304]\n\t" \
        "vstr d6, [%[w], #368]\n\t" \
        "veor d26, d26, d3\n\t" \
        "vldr d6, [%[x], #528]\n\t" \
        "veor d4, d5, d4\n\t" \
        "veor d7, d26, d7\n\t" \
        "vldr d5, [%[x], #320]\n\t" \
        "veor d25, d25, d4\n\t" \
        "vstr d7, [%[w], #384]\n\t" \
        "vldr d7, [%[x], #544]\n\t" \
        "veor d8, d25, d8\n\t" \
        "veor d5, d6, d5\n\t" \
        "vldr d6, [%[x], #336]\n\t" \
        "vstr d8, [%[w], #400]\n\t" \
        "veor d24, d24, d5\n\t" \
        "vldr d8, [%[x], #560]\n\t" \
        "veor d6, d7, d6\n\t" \
        "veor d9, d24, d9\n\t" \
        "vldr d7, [%[x], #352]\n\t" \
        "veor d27, d27, d6\n\t" \
        "vstr d9, [%[w], #416]\n\t" \
        "vldr d9, [%[x], #576]\n\t" \
        "veor d10, d27, d10\n\t" \
        "veor d7, d8, d7\n\t" \
        "vldr d8, [%[x], #368]\n\t" \
        "vstr d10, [%[w], #432]\n\t" \
        "veor d0, d0, d7\n\t" \
        "vldr d10, [%[x], #592]\n\t" \
        "veor d8, d9, d8\n\t" \
        "veor d11, d0, d11\n\t" \
        "vldr d9, [%[x], #384]\n\t" \
        "veor d1, d1, d8\n\t" \
        "vstr d11, [%[w], #448]\n\t" \
        "vldr d11, [%[x], #608]\n\t" \
        "veor d12, d1, d12\n\t" \
        "veor d9, d10, d9\n\t" \
        "vldr d10, [%[x], #400]\n\t" \
        "vstr d12, [%[w], #464]\n\t" \
        "veor d2, d2, d9\n\t" \
        "vldr d12, [%[x], #624]\n\t" \
        "veor d10, d11, d10\n\t" \
        "veor d14, d2, d14\n\t" \
        "vldr d11, [%[x], #416]\n\t" \
        "veor d3, d3, d10\n\t" \
        "vstr d14, [%[w], #480]\n\t" \
        "vldr d14, [%[x], #640]\n\t" \
        "veor d13, d3, d13\n\t" \
        "veor d11, d12, d11\n\t" \
        "vldr d12, [%[x], #432]\n\t" \
        "vstr d13, [%[w], #496]\n\t" \
        "veor d4, d4, d11\n\t" \
        "vldr d13, [%[x], #656]\n\t" \
        "veor d12, d14, d12\n\t" \
        "veor d17, d4, d17\n\t" \
        "vldr d14, [%[x], #448]\n\t" \
        "veor d5, d5, d12\n\t" \
        "vstr d17, [%[w], #512]\n\t" \
        "vldr d17, [%[x], #672]\n\t" \
        "veor d16, d5, d16\n\t" \
        "veor d14, d13, d14\n\t" \
        "vldr d13, [%[x], #464]\n\t" \
        "vstr d16, [%[w], #528]\n\t" \
        "veor d6, d6, d14\n\t" \
        "vldr d16, [%[x], #688]\n\t" \
        "veor d13, d17, d13\n\t" \
        "veor d15, d6, d15\n\t" \
        "vldr d17, [%[x], #480]\n\t" \
        "veor d7, d7, d13\n\t" \
        "vstr d15, [%[w], #544]\n\t" \
        "vldr d15, [%[x], #704]\n\t" \
        "veor d20, d7, d20\n\t" \
        "veor d17, d16, d17\n\t" \
        "vldr d16, [%[x], #496]\n\t" \
        "vstr d20, [%[w], #560]\n\t" \
        "veor d8, d8, d17\n\t" \
        "vldr d20, [%[x], #720]\n\t" \
        "veor d16, d15, d16\n\t" \
        "veor d19, d8, d19\n\t" \
        "vldr d15, [%[x], #512]\n\t" \
        "veor d9, d9, d16\n\t" \
        "vstr d19, [%[w], #576]\n\t" \
        "vldr d19, [%[x], #736]\n\t" \
        "veor d18, d9, d18\n\t" \
        "veor d15, d20, d15\n\t" \
        "vldr d20, [%[x], #528]\n\t" \
        "vstr d18, [%[w], #592]\n\t" \
        "veor d10, d10, d15\n\t" \
        "vldr d18, [%[x], #752]\n\t" \
        "veor d20, d19, d20\n\t" \
        "veor d23, d10, d23\n\t" \
        "vldr d19, [%[x], #544]\n\t" \
        "veor d11, d11, d20\n\t" \
        "vstr d23, [%[w], #608]\n\t" \
        "vldr d23, [%[x], #768]\n\t" \
        "veor d22, d11, d22\n\t" \
        "veor d19, d18, d19\n\t" \
        "vldr d18, [%[x], #560]\n\t" \
        "vstr d22, [%[w], #624]\n\t" \
        "veor d12, d12, d19\n\t" \
        "vldr d22, [%[x], #784]\n\t" \
        "veor d18, d23, d18\n\t" \
        "veor d21, d12, d21\n\t" \
        "vldr d23, [%[x], #576]\n\t" \
        "veor d14, d14, d18\n\t" \
        "vstr d21, [%[w], #640]\n\t" \
        "vldr d21, [%[x], #800]\n\t" \
        "veor d26, d14, d26\n\t" \
        "veor d23, d22, d23\n\t" \
        "vldr d22, [%[x], #592]\n\t" \
        "vstr d26, [%[w], #656]\n\t" \
        "veor d13, d13, d23\n\t" \
        "vldr d26, [%[x], #816]\n\t" \
        "veor d22, d21, d22\n\t" \
        "veor d25, d13, d25\n\t" \
        "vldr d21, [%[x], #608]\n\t" \
        "veor d17, d17, d22\n\t" \
        "vstr d25, [%[w], #672]\n\t" \
        "vldr d25, [%[x], #832]\n\t" \
        "veor d24, d17, d24\n\t" \
        "veor d21, d26, d21\n\t" \
        "vldr d26, [%[x], #624]\n\t" \
        "vstr d24, [%[w], #688]\n\t" \
        "veor d16, d16, d21\n\t" \
        "vldr d24, [%[x], #848]\n\t" \
        "veor d26, d25, d26\n\t" \
        "veor d27, d16, d27\n\t" \
        "vldr d25, [%[x], #640]\n\t" \
        "veor d15, d15, d26\n\t" \
        "vstr d27, [%[w], #704]\n\t" \
        "vldr d27, [%[x], #864]\n\t" \
        "veor d0, d15, d0\n\t" \
        "veor d25, d24, d25\n\t" \
        "vldr d24, [%[x], #656]\n\t" \
        "vstr d0, [%[w], #720]\n\t" \
        "veor d20, d20, d25\n\t" \
        "vldr d0, [%[x], #880]\n\t" \
        "veor d24, d27, d24\n\t" \
        "veor d1, d20, d1\n\t" \
        "vldr d27, [%[x], #672]\n\t" \
        "veor d19, d19, d24\n\t" \
        "vstr d1, [%[w], #736]\n\t" \
        "vldr d1, [%[x], #896]\n\t" \
        "veor d2, d19, d2\n\t" \
        "veor d27, d0, d27\n\t" \
        "vldr d0, [%[x], #688]\n\t" \
        "vstr d2, [%[w], #752]\n\t" \
        "veor d18, d18, d27\n\t" \
        "vldr d2, [%[x], #912]\n\t" \
        "veor d0, d1, d0\n\t" \
        "veor d3, d18, d3\n\t" \
        "vldr d18, [%[x], #704]\n\t" \
        "veor d23, d23, d0\n\t" \
        "vstr d3, [%[w], #768]\n\t" \
        "vldr d3, [%[x], #928]\n\t" \
        "veor d4, d23, d4\n\t" \
        "veor d18, d2, d18\n\t" \
        "vldr d2, [%[x], #720]\n\t" \
        "vstr d4, [%[w], #784]\n\t" \
        "veor d22, d22, d18\n\t" \
        "vldr d4, [%[x], #944]\n\t" \
        "veor d2, d3, d2\n\t" \
        "veor d5, d22, d5\n\t" \
        "vldr d22, [%[x], #736]\n\t" \
        "veor d21, d21, d2\n\t" \
        "vstr d5, [%[w], #800]\n\t" \
        "vldr d5, [%[x], #960]\n\t" \
        "veor d6, d21, d6\n\t" \
        "veor d22, d4, d22\n\t" \
        "vldr d4, [%[x], #752]\n\t" \
        "vstr d6, [%[w], #816]\n\t" \
        "veor d26, d26, d22\n\t" \
        "vldr d6, [%[x], #976]\n\t" \
        "veor d4, d5, d4\n\t" \
        "veor d7, d26, d7\n\t" \
        "vldr d26, [%[x], #768]\n\t" \
        "veor d25, d25, d4\n\t" \
        "vstr d7, [%[w], #832]\n\t" \
        "vldr d7, [%[x], #992]\n\t" \
        "veor d8, d25, d8\n\t" \
        "veor d26, d6, d26\n\t" \
        "vldr d6, [%[x], #784]\n\t" \
        "vstr d8, [%[w], #848]\n\t" \
        "veor d24, d24, d26\n\t" \
        "vldr d8, [%[x], #1008]\n\t" \
        "veor d6, d7, d6\n\t" \
        "veor d9, d24, d9\n\t" \
        "vldr d24, [%[x], #800]\n\t" \
        "veor d27, d27, d6\n\t" \
        "vstr d9, [%[w], #864]\n\t" \
        "veor d12, d18, d12\n\t" \
        "veor d10, d27, d10\n\t" \
        "veor d24, d8, d24\n\t" \
        "vstr d12, [%[w], #912]\n\t" \
        "vstr d10, [%[w], #880]\n\t" \
        "veor d0, d0, d24\n\t" \
        "veor d14, d2, d14\n\t" \
        "veor d13, d22, d13\n\t" \
        "veor d11, d0, d11\n\t" \
        "vstr d14, [%[w], #928]\n\t" \
        "vstr d13, [%[w], #944]\n\t" \
        "vstr d11, [%[w], #896]\n\t" \
        "veor d17, d4, d17\n\t" \
        "veor d16, d26, d16\n\t" \
        "veor d15, d6, d15\n\t" \
        "vstr d17, [%[w], #960]\n\t" \
        "vstr d16, [%[w], #976]\n\t" \
        "vstr d15, [%[w], #992]\n\t" \
        "veor d20, d24, d20\n\t" \
        "vstr d20, [%[w], #1008]\n\t" \
        : : [w] "r"(W_), [x] "r"(X_) \
        : "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31")
#endif
