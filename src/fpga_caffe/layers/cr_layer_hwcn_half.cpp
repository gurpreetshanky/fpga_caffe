#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include "../../../include/fpga_caffe/layer.hpp"
#include "../../../include/fpga_caffe/half.hpp"
#include "../../../include/fpga_caffe/vector_types.hpp"

#define OCFACT 1 

/* Kernel used for computing direct convolution forward and backward. 
 * input:         flattened input array containing image data
 * weights:       convolution filters
 * bias:          flattened bias array
 * output:        output of the convolution
 */ 

chalf16 max9(chalf16 poolInBuf[9][16 * 16], int n, short16 *outMask) {
#pragma HLS INLINE
  chalf16 reduce_s1[4];
#pragma HLS ARRAY_PARTITION variable=reduce_s1 complete
  chalf16 reduce_s2[2];
#pragma HLS ARRAY_PARTITION variable=reduce_s2 complete
  chalf16 reduce_s3;
  chalf16 reduce_s4;

  short16 mask_s0[9];
#pragma HLS ARRAY_PARTITION variable=mask_s0 complete
  short16 mask_s1[4];
#pragma HLS ARRAY_PARTITION variable=mask_s1 complete
  short16 mask_s2[2];
#pragma HLS ARRAY_PARTITION variable=mask_s2 complete
  short16 mask_s3;

  short16 mask_s4;

  for (int i = 0; i < 9; ++i)
    mask_s0[i] = i;

  for (int i = 0; i < 4; ++i)
    reduce_s1[i] = max(poolInBuf[i * 2][n], poolInBuf[i * 2 + 1][n],
        mask_s0[i * 2], mask_s0[i * 2 + 1], &mask_s1[i]);

  for (int i = 0; i < 2; ++i)
    reduce_s2[i] = max(reduce_s1[i * 2], reduce_s1[i * 2 + 1],
        mask_s1[i * 2], mask_s1[i * 2 + 1], &mask_s2[i]);

  reduce_s3 = max(reduce_s2[0], reduce_s2[1], mask_s2[0], mask_s2[1],
      &mask_s3);

  reduce_s4 = max(reduce_s3, poolInBuf[8][n], mask_s3, mask_s0[8],
      &mask_s4);

  *outMask = mask_s4;

  return reduce_s4;
}

int mode_select_idx(int idx_fw, int idx_bw, bool mode) {
#pragma HLS INLINE
  if (mode)
    return idx_bw;
  else
    return idx_fw;
}

short mode_select_size(short size_fw, short size_bw, bool mode) {
#pragma HLS INLINE
  if (mode)
    return size_bw;
  else
    return size_fw;
}

chalf relu_bw(chalf input, bool enable) {
#pragma HLS INLINE off
  chalf res = (enable) ? input : chalf(0);
  return res;
}

void relu_fw(chalf16 outBuf[OCFACT][256], short outBufRelu[OCFACT][256],
    int num_iter) {
  RELU_FW: for (int i = 0; i < num_iter; ++i) {
  #pragma HLS pipeline
    for (int k = 0; k < OCFACT; ++k) {
      chalf16 val = max(outBuf[k][i]);
      outBuf[k][i] = val;
      short reluOut = 0;
      reluOut |= (val.s0 != chalf(0)) ? 1 << 0 : 0;
      reluOut |= (val.s1 != chalf(0)) ? 1 << 1 : 0;
      reluOut |= (val.s2 != chalf(0)) ? 1 << 2 : 0;
      reluOut |= (val.s3 != chalf(0)) ? 1 << 3 : 0;
      reluOut |= (val.s4 != chalf(0)) ? 1 << 4 : 0;
      reluOut |= (val.s5 != chalf(0)) ? 1 << 5 : 0;
      reluOut |= (val.s6 != chalf(0)) ? 1 << 6 : 0;
      reluOut |= (val.s7 != chalf(0)) ? 1 << 7 : 0;
      reluOut |= (val.s8 != chalf(0)) ? 1 << 8 : 0;
      reluOut |= (val.s9 != chalf(0)) ? 1 << 9 : 0;
      reluOut |= (val.sa != chalf(0)) ? 1 << 10 : 0;
      reluOut |= (val.sb != chalf(0)) ? 1 << 11 : 0;
      reluOut |= (val.sc != chalf(0)) ? 1 << 12 : 0;
      reluOut |= (val.sd != chalf(0)) ? 1 << 13 : 0;
      reluOut |= (val.se != chalf(0)) ? 1 << 14 : 0;
      reluOut |= (val.sf != chalf(0)) ? 1 << 15 : 0;
      outBufRelu[k][i] = reluOut;
    }
  }
}

extern "C" {

void cr_layer_hwcn_half(chalf16 *input, chalf16 *weights, chalf *bias,
    chalf16 *output, short *tagVals, int *params, int group_idx) { 
// Ports 
#pragma HLS data_pack variable=weights
#pragma HLS data_pack variable=output
#pragma HLS data_pack variable=input
#pragma HLS INTERFACE m_axi port=input offset=slave bundle=gmem1
#pragma HLS INTERFACE m_axi port=output offset=slave bundle=gmem2
#pragma HLS INTERFACE m_axi port=weights offset=slave bundle=gmem3
#pragma HLS INTERFACE m_axi port=bias offset=slave bundle=gmem4
#pragma HLS INTERFACE m_axi port=tagVals offset=slave bundle=gmem5
#pragma HLS INTERFACE m_axi port=params offset=slave bundle=gmem6
#pragma HLS INTERFACE s_axilite port=input bundle=control
#pragma HLS INTERFACE s_axilite port=output bundle=control
#pragma HLS INTERFACE s_axilite port=weights bundle=control
#pragma HLS INTERFACE s_axilite port=bias bundle=control
#pragma HLS INTERFACE s_axilite port=tagVals bundle=control
#pragma HLS INTERFACE s_axilite port=params bundle=control
#pragma HLS INTERFACE s_axilite port=group_idx bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

  // Input tile buffer
  chalf16 inBuf[4][2 * 256 * 16];
#pragma HLS ARRAY_PARTITION variable=inBuf complete dim=1

  short inBufRelu[4][2 * 256 * 16];
#pragma HLS ARRAY_PARTITION variable=inBufRelu complete dim=1

  short outBufRelu[OCFACT][256];
#pragma HLS ARRAY_PARTITION variable=outBufRelu complete dim=1

  // Output buffer used for writing
  chalf16 outBuf[OCFACT][256];
#pragma HLS ARRAY_PARTITION variable=outBuf complete dim=1

  // Weight buffer
  chalf16 wBuf[OCFACT][256];
#pragma HLS ARRAY_PARTITION variable=wBuf complete dim=1

  // Bias buffer
  chalf biasBuf[OCFACT][(6144 / OCFACT)];
#pragma HLS ARRAY_PARTITION variable=biasBuf complete dim=1

  chalf16 poolInBuf[9][16 * 16];
#pragma HLS ARRAY_PARTITION variable=poolInBuf complete dim=1

  chalf16 poolOutBuf[16 * 16];

  chalf16 poolOutBufBW[9][16 * 16];
#pragma HLS ARRAY_PARTITION variable=poolOutBufBW complete dim=1

  chalf16 poolInBufBW[16 * 16];

  short outMask[16 * 256];
#pragma HLS ARRAY_PARTITION variable=outMask cyclic factor=16 dim=1

  short inMask[16 * 256];
#pragma HLS ARRAY_PARTITION variable=inMask cyclic factor=16 dim=1

  chalf multRes[OCFACT][4][16];
#pragma HLS ARRAY_PARTITION variable=multRes complete dim=1
#pragma HLS ARRAY_PARTITION variable=multRes complete dim=2
#pragma HLS ARRAY_PARTITION variable=multRes complete dim=3

  chalf weightFW[16];
#pragma HLS ARRAY_PARTITION variable=weightFW complete

  chalf weightVal[4][16];
#pragma HLS ARRAY_PARTITION variable=weightVal complete dim=1
#pragma HLS ARRAY_PARTITION variable=weightVal complete dim=2

  chalf inVal[4][16];
#pragma HLS ARRAY_PARTITION variable=inVal complete dim=1
#pragma HLS ARRAY_PARTITION variable=inVal complete dim=2

  chalf addTreeS1[OCFACT][32];
#pragma HLS ARRAY_PARTITION variable=addTreeS1 complete dim=1
#pragma HLS ARRAY_PARTITION variable=addTreeS1 complete dim=2

  chalf addTreeS2[OCFACT][16];
#pragma HLS ARRAY_PARTITION variable=addTreeS2 complete dim=1
#pragma HLS ARRAY_PARTITION variable=addTreeS2 complete dim=2

  chalf addTreeS3[OCFACT][4][2];
#pragma HLS ARRAY_PARTITION variable=addTreeS3 complete dim=1
#pragma HLS ARRAY_PARTITION variable=addTreeS3 complete dim=2
#pragma HLS ARRAY_PARTITION variable=addTreeS3 complete dim=3

  chalf finalOut[OCFACT][16];
#pragma HLS ARRAY_PARTITION variable=finalOut complete dim=1
#pragma HLS ARRAY_PARTITION variable=finalOut complete dim=2

  chalf addTreeS4[OCFACT][4];
#pragma HLS ARRAY_PARTITION variable=addTreeS4 complete dim=1
#pragma HLS ARRAY_PARTITION variable=addTreeS4 complete dim=2

  chalf addres_f[OCFACT][16];
#pragma HLS ARRAY_PARTITION variable=addres_f complete dim=1
#pragma HLS ARRAY_PARTITION variable=addres_f complete dim=2

  chalf wUpdate[OCFACT][16];
#pragma HLS ARRAY_PARTITION variable=wUpdate complete dim=1
#pragma HLS ARRAY_PARTITION variable=wUpdate complete dim=2

  bool reluEn[OCFACT][16];
#pragma HLS ARRAY_PARTITION variable=reluEn complete dim=1
#pragma HLS ARRAY_PARTITION variable=reluEn complete dim=2

  short inChannels = params[0];
  short outChannels = params[1];
  short burstChannels = params[2];
  short rpo = params[3];
  short ocrdfact = params[4];
  ap_uint<9> burstoc = params[5];
  ap_uint<10> ydim = params[6];
  ap_uint<10> xdim = params[7];
  ap_uint<5> ksize = params[9];
  short numgroups = params[10];
  ap_uint<10> numImages = params[11];
  short fc = params[12];
  short relu = params[13];
  short backward = params[14];
  ap_uint<4> stride = params[15];
  ap_uint<4> pad = params[16];
  bool mode = (backward == 1);
  short pool = params[17];
  ap_uint<3> pksize = params[18];

  assert((pksize == 2) || (pksize == 3));
  assert(ksize <= 11);
  assert(ksize >= 1);
  assert(burstChannels <= 2048);
  assert(burstChannels >= 4);
  assert(numImages >= 192);
  assert(numImages <= 256);

  ap_uint<10> xdim_out = ((xdim - ksize + 2 * pad) / stride) + 1;
  ap_uint<10> ydim_out = xdim_out;

  ap_uint<8> imgFact = numImages >> 4;
  short burstFact = burstChannels >> 2;
  short icFact = (inChannels % 16 == 0) ? (inChannels >> 4) :
    (inChannels >> 4) + 1;
  short wcFact = (burstChannels % 16 == 0) ? (burstChannels >> 4) :
    (burstChannels >> 4) + 1;
  short out_div = ocrdfact / OCFACT;
  short ofm_iters = (ocrdfact % OCFACT == 0) ? out_div : out_div + 1;
  
  if (pool == 0) {
    if (backward == 0) {
      for (int o = 0; o < ofm_iters; ++o) {
        for (int k = 0; k < OCFACT; ++k) {
          int biasOffset = (o * OCFACT + k) * burstoc + outChannels
            * group_idx;
          int biasSize = burstoc;
          if ((o * OCFACT + k) * burstoc + burstoc > outChannels) {
            short newBurst = outChannels - (o * OCFACT + k) * burstoc;
            biasSize = newBurst;
          }
          bool writeEnable = ((o * OCFACT + k) * burstoc < outChannels);
          if (writeEnable) {
            memcpy(biasBuf[k] + o * burstoc, bias + biasOffset,
              sizeof(chalf) * biasSize);
          }
        }
      }
    }
    for (int n = 0; n < rpo; ++n) {
      for (int o = 0; o < ofm_iters; ++o) {
        for (int y = 0; y < ydim_out; ++y) {
          for (int x = 0; x < xdim_out; ++x) {
            ap_uint<8> yk_off = 0;
            ap_uint<8> xk_off = 0;
            ap_uint<8> yksize = 0;
            ap_uint<8> xksize = 0;
            bool xkset = false;
            bool ykset = false;
            for (int p = 0; p < ksize; ++p) {
              for (int q = 0; q < ksize; ++q) {
                short in_y = y * stride - pad + p;
                short in_x = x * stride - pad + q;
                int inIdx = (((in_y * xdim + in_x) * numgroups + group_idx) *
                    inChannels + n * burstChannels) * imgFact;
                int inBufIdx = (p * ksize + q) * burstFact * imgFact;
                short inSize = burstFact * imgFact;

                if (in_y >= 0 && in_y < ydim) {
                  if (q == 0)
                    yksize++;
                  if (!ykset) {
                    yk_off = p;
                    ykset = true;
                  }
                }
                if (in_x >= 0 && in_x < xdim) {
                  if (p == 0)
                    xksize++;
                  if (!xkset) {
                    xk_off = q;
                    xkset = true;
                  }
                }

                if (in_y >= 0 && in_y < ydim && in_x >= 0 && in_x < xdim) {
                  if ((x != 0) && (stride == 1) && (q != ksize - 1)) {
                    short q_off = burstFact * imgFact;
                    SHIFT_LOOP: for (int i = 0; i < inSize; ++i) {
#pragma HLS pipeline
#pragma HLS dependence variable=inBuf inter false
#pragma HLS dependence variable=inBufRelu inter false
                      for (int j = 0; j < 4; ++j) {
                        inBuf[j][i + inBufIdx] = inBuf[j][i + inBufIdx
                          + q_off];
                        if ((backward != 0) && relu)
                          inBufRelu[j][i + inBufIdx] =
                            inBufRelu[j][i + inBufIdx + q_off];
                      }
                    }
                  } else {
                    for (int j = 0; j < 4; ++j) {
                      int f_inIdx = inIdx + j * burstFact * imgFact;
                      memcpy(inBuf[j] + inBufIdx, input + f_inIdx,
                          sizeof(chalf16) * inSize);
                      if ((backward != 0) && relu)
                        memcpy(inBufRelu[j] + inBufIdx, tagVals + f_inIdx,
                            sizeof(short) * inSize);
                    }
                  }
                }
              }
            }

            if ((n == 0) && (backward == 0)) {
              for (int b = 0; b < burstoc; ++b) {
                for (int i = 0; i < imgFact; ++i) {
#pragma HLS pipeline
                  for (int k = 0; k < OCFACT; ++k) {
                    outBuf[k][b * imgFact + i] =
                      biasBuf[k][o * burstoc + b];
                  }
                }
              }
            } else if ((n == 0) && (backward == 2)) {
              for (int i = 0; i < burstoc * imgFact; ++i) {
#pragma HLS pipeline
                for (int k = 0; k < OCFACT; ++k) {
                  outBuf[k][i] = chalf(0);
                }
              }
            } else if ((backward == 1) && (x == 0) && (y == 0)) {
              for (int i = 0; i < burstoc * ksize * ksize * wcFact; ++i) {
#pragma HLS pipeline
                for (int k = 0; k < OCFACT; ++k) {
                  outBuf[k][i] = chalf(0);
                }
              }
            } else {
              for (int k = 0; k < OCFACT; ++k) {
                int outIdx, outIdxFW, outIdxBW;
                short outSize, outSizeFW, outSizeBW;
                outIdxBW = ((o * OCFACT + k) * burstoc + outChannels *
                    group_idx) * ksize * ksize * icFact + n * ksize * ksize *
                    wcFact;
                outIdxFW = (((y * xdim_out + x) * numgroups + group_idx) *
                  outChannels + (o * OCFACT + k) * burstoc) * imgFact; 
                outSizeBW = burstoc * ksize * ksize * wcFact;
                outSizeFW = burstoc * imgFact;
                if ((o * OCFACT + k) * burstoc + burstoc > outChannels) {
                  short newBurst = outChannels - (o * OCFACT + k) * burstoc;
                  outSizeBW = newBurst * ksize * ksize * wcFact;
                  outSizeFW =  newBurst * imgFact;
                }

                outIdx = mode_select_idx(outIdxFW, outIdxBW, mode);
                outSize = mode_select_size(outSizeFW, outSizeBW, mode);
                bool readEnable = ((o * OCFACT + k) * burstoc < outChannels)
                  && (!mode);

                if (readEnable)
                  memcpy(outBuf[k], output + outIdx, sizeof(chalf16) *
                      outSize);
              }
            }  
            for (int b = 0; b < burstoc; ++b) {
              for (int k = 0; k < OCFACT; ++k) {
                int wIdxFW, wIdxBW, wIdx;
                short wSizeFW, wSizeBW, wSize;
                wIdxBW = (((y * xdim_out + x) * numgroups + group_idx) *
                    outChannels + (o * OCFACT + k) * burstoc + b) * imgFact;
                wSizeBW = imgFact;
                wIdxFW = ((o * OCFACT + k) * burstoc + b + outChannels *
                  group_idx) * ksize * ksize * icFact + n * ksize * ksize *
                  wcFact;
                wSizeFW = ksize * ksize * wcFact;

                wIdx = mode_select_idx(wIdxFW, wIdxBW, mode);
                wSize = mode_select_size(wSizeFW, wSizeBW, mode);

                bool readEnable = ((o * OCFACT + k) * burstoc + b <
                    outChannels);
                if (readEnable)
                  memcpy(wBuf[k], weights + wIdx, sizeof(chalf16) * wSize);
              }

              ap_uint<8> w_off = 0;
              ap_uint<5> img_off = 0;
              ap_uint<8> iter = 0;
              ap_uint<8> xdim_off = 0;
              ap_uint<8> ydim_off = 0;
              ap_uint<2> counter_bw = 0;
              ap_uint<2> counter_fw = 0;
              short mac_iterations = yksize * xksize * imgFact * burstFact;
              MAC_LOOP: for (int i = 0; i < mac_iterations; ++i, ++iter,
                ++counter_bw) {
#pragma HLS pipeline
#pragma HLS DEPENDENCE variable outBuf inter false
#pragma HLS DEPENDENCE variable outBufRelu inter false
#pragma HLS DEPENDENCE variable finalOut inter false
#pragma HLS DEPENDENCE variable wUpdate inter false
                if (!mode) {
                  if (iter == imgFact) {
                    if (w_off == burstFact - 1) {
                      counter_fw = 0;
                      w_off = 0;
                      if (xdim_off == xksize - 1) {
                        xdim_off = 0;
                        ydim_off++;
                      } else {
                        xdim_off++;
                      }
                    } else {
                      counter_fw++;
                      w_off++;
                    }
                    iter = 0;
                  }
                  img_off = iter;
                } else {
                  if (iter == burstFact) {
                    if (xdim_off == xksize - 1) {
                      xdim_off = 0;
                      if (ydim_off == yksize - 1) {
                        ydim_off = 0;
                        img_off++;
                      } else {
                        ydim_off++;
                      }
                    } else {
                      xdim_off++;
                    }
                    iter = 0;
                  }
                  w_off = iter;
                }

                short filt_off = (yk_off + ydim_off) * ksize + xk_off + xdim_off;
                short wIdxFW = filt_off * wcFact + (w_off >> 2);
                short wIdxBW = img_off;
                short wIdx = (mode) ? wIdxBW : wIdxFW;
                short foutIdx = counter_bw * 4;
                short inIdx = (filt_off * burstFact + w_off) * imgFact
                  + img_off;
                short outIdxFW = b * imgFact + img_off;
                short outIdxBW = b * ksize * ksize * wcFact +
                  filt_off * wcFact + (w_off >> 2);
                short outIdx = (mode) ? outIdxBW : outIdxFW;
                bool accEnable = (mode) ? (counter_bw == 3) : true;
                bool reluOn = (relu && ((backward == 1) ||
                    (backward == 2) || (backward == 3)));

                for (int k = 0; k < OCFACT; ++k) {
                  weightFW[0] = wBuf[k][wIdx].s0;
                  weightFW[1] = wBuf[k][wIdx].s1;
                  weightFW[2] = wBuf[k][wIdx].s2;
                  weightFW[3] = wBuf[k][wIdx].s3;   
                  weightFW[4] = wBuf[k][wIdx].s4;
                  weightFW[5] = wBuf[k][wIdx].s5;
                  weightFW[6] = wBuf[k][wIdx].s6;
                  weightFW[7] = wBuf[k][wIdx].s7;
                  weightFW[8] = wBuf[k][wIdx].s8;
                  weightFW[9] = wBuf[k][wIdx].s9;
                  weightFW[10] = wBuf[k][wIdx].sa;
                  weightFW[11] = wBuf[k][wIdx].sb;
                  weightFW[12] = wBuf[k][wIdx].sc;
                  weightFW[13] = wBuf[k][wIdx].sd;
                  weightFW[14] = wBuf[k][wIdx].se;
                  weightFW[15] = wBuf[k][wIdx].sf;
                  for (int m = 0; m < 4; ++m) {
                    for (int j = 0; j < 16; ++j) {
                      if (mode)
                        weightVal[m][j] = weightFW[j];
                      else
                        weightVal[m][j] = weightFW[counter_fw * 4 + m];
                    }

                    short reluVal = inBufRelu[m][inIdx];
                    bool fwMode = (backward == 0);

                    for (int j = 0; j < 16; ++j)
                      reluEn[k][j] = (reluOn && ((reluVal >> j) & 0x1)) ||
                        fwMode || (relu == 0);

                    inVal[m][0] = relu_bw(inBuf[m][inIdx].s0, reluEn[k][0]);
                    inVal[m][1] = relu_bw(inBuf[m][inIdx].s1, reluEn[k][1]);
                    inVal[m][2] = relu_bw(inBuf[m][inIdx].s2, reluEn[k][2]);
                    inVal[m][3] = relu_bw(inBuf[m][inIdx].s3, reluEn[k][3]);
                    inVal[m][4] = relu_bw(inBuf[m][inIdx].s4, reluEn[k][4]);
                    inVal[m][5] = relu_bw(inBuf[m][inIdx].s5, reluEn[k][5]);
                    inVal[m][6] = relu_bw(inBuf[m][inIdx].s6, reluEn[k][6]);
                    inVal[m][7] = relu_bw(inBuf[m][inIdx].s7, reluEn[k][7]);
                    inVal[m][8] = relu_bw(inBuf[m][inIdx].s8, reluEn[k][8]);
                    inVal[m][9] = relu_bw(inBuf[m][inIdx].s9, reluEn[k][9]);
                    inVal[m][10] = relu_bw(inBuf[m][inIdx].sa, reluEn[k][10]);
                    inVal[m][11] = relu_bw(inBuf[m][inIdx].sb, reluEn[k][11]);
                    inVal[m][12] = relu_bw(inBuf[m][inIdx].sc, reluEn[k][12]);
                    inVal[m][13] = relu_bw(inBuf[m][inIdx].sd, reluEn[k][13]);
                    inVal[m][14] = relu_bw(inBuf[m][inIdx].se, reluEn[k][14]);
                    inVal[m][15] = relu_bw(inBuf[m][inIdx].sf, reluEn[k][15]);

                    for (int j = 0; j < 16; ++j) 
                      multRes[k][m][j] = inVal[m][j] * weightVal[m][j];
                  }

                  for (int off = 0; off < 2; ++off) {
                    for (int m = 0; m < 2; ++m) {
                      for (int j = 0; j < 8; ++j) {
                        chalf temp1, temp2;
                        if (mode) {
                          temp1 = multRes[k][off * 2 + m][j * 2];
                          temp2 = multRes[k][off * 2 + m][j * 2 + 1];
                        } else {
                          temp1 = multRes[k][off * 2 + 0][m * 8 + j];
                          temp2 = multRes[k][off * 2 + 1][m * 8 + j];
                        }
                        addTreeS1[k][(off * 2 + m) * 8 + j] = temp1 + temp2;
                      }
                    }
                  }
                  for (int off = 0; off < 2; ++off) {
                    for (int m = 0; m < 2; ++m) {
                      for (int j = 0; j < 4; ++j) {
                        chalf temp1, temp2;
                        if (mode) {
                          temp1 = addTreeS1[k][(off * 2 + m) * 8 + j * 2];
                          temp2 = addTreeS1[k][(off * 2 + m) * 8 + j * 2 + 1];
                        } else {
                          temp1 = addTreeS1[k][(off * 2 + m) * 4 + j];
                          temp2 = addTreeS1[k][(off * 2 + m) * 4 + j + 16];
                        }
                        addTreeS2[k][(off * 2 + m) * 4 + j] = temp1 + temp2;
                      }
                    }
                  }

                  for (int m = 0; m < 4; ++m) {
                    for (int j = 0; j < 2; ++j)
                      addTreeS3[k][m][j] = addTreeS2[k][m * 4 + j * 2] +
                        addTreeS2[k][m * 4 + j * 2 + 1];
                    addTreeS4[k][m] = addTreeS3[k][m][0] + addTreeS3[k][m][1];
                  }

                  for (int m = 0; m < 4; ++m)
                    wUpdate[k][foutIdx + m] = addTreeS4[k][m];

                  for (int j = 0; j < 16; ++j) {
                    if (mode)
                      finalOut[k][j] = wUpdate[k][j];
                    else
                      finalOut[k][j] = addTreeS2[k][j];
                  }
                  if (accEnable) {
                    outBuf[k][outIdx] += finalOut[k];
                  }               
                }
              }
            }
            if (relu && (backward == 0) && (n == rpo - 1)) {
              relu_fw(outBuf, outBufRelu, burstoc * imgFact);
            }

            for (int k = 0; k < OCFACT; ++k) {
              int outIdx, outIdxFW, outIdxBW;
              short outSize, outSizeFW, outSizeBW;
              outIdxBW = ((o * OCFACT + k) * burstoc + outChannels *
                  group_idx) * ksize * ksize * icFact + n * ksize * ksize *
                  wcFact;
              outIdxFW = (((y * xdim_out + x) * numgroups + group_idx) *
                outChannels + (o * OCFACT + k) * burstoc) * imgFact;
              outSizeBW = burstoc * ksize * ksize * wcFact;
              outSizeFW = burstoc * imgFact;
              if ((o * OCFACT + k) * burstoc + burstoc > outChannels) { 
                short newBurst = outChannels - (o * OCFACT + k) * burstoc;
                outSizeBW = newBurst * ksize * ksize * wcFact;
                outSizeFW =  newBurst * imgFact;
              }

              outIdx = mode_select_idx(outIdxFW, outIdxBW, mode);
              outSize = mode_select_size(outSizeFW, outSizeBW, mode);

              bool writeEnable = ((o * OCFACT + k) * burstoc < outChannels)
                && ((!mode) || ((x == xdim_out - 1) && (y == ydim_out - 1)));

              if (relu && (writeEnable) && (backward == 0) && (n == rpo - 1)) {
                memcpy(tagVals + outIdx, outBufRelu[k], sizeof(short) *
                    outSize);
              }

              if (writeEnable)
                memcpy(output + outIdx, outBuf[k], sizeof(chalf16) * outSize);
            }
          }
        }
      }
    }
  } else {
    short pooled_height = ydim - pksize;
    if ((pooled_height & 0x1) == 1)
      pooled_height = (pooled_height >> 1) + 2;
    else
      pooled_height = (pooled_height >> 1) + 1;

    short pooled_width = pooled_height;

    if (backward == 0) {
      for (int ph = 0; ph < pooled_height; ++ph) {
        for (int pw = 0; pw < pooled_width; ++pw) {
          int hstart = ph * 2;
          int wstart = pw * 2;
          for (int c = 0; c < rpo; ++c) {
            for (int h = 0; h < 3; ++h) {
              for (int w = 0; w < 3; ++w) {
                int inIdx = (((hstart + h) * xdim + (wstart + w)) *
                    inChannels + c * burstChannels) * imgFact;
                if ((hstart + h < ydim) && (wstart + w < xdim) &&
                    (h < pksize) && (w < pksize))
                  memcpy(poolInBuf[h * 3 + w], input + inIdx,
                      sizeof(chalf16) * imgFact * burstChannels);
                else
                  for (int n = 0; n < imgFact * burstChannels; ++n)
#pragma HLS pipeline
                    poolInBuf[h * 3 + w][n] = chalf(CHALF_MIN_VAL);
              }
            }
            POOL_LOOP: for (int n = 0; n < (imgFact * burstChannels) >> 1;
              ++n) {
#pragma HLS pipeline
              for (int j = 0; j < 2; ++j) {
                short16 mask;
                poolOutBuf[n * 2 + j] = max9(poolInBuf, n * 2 + j, &mask);
                outMask[(n * 2 + j) * 16 + 0] = mask.s0;
                outMask[(n * 2 + j) * 16 + 1] = mask.s1;
                outMask[(n * 2 + j) * 16 + 2] = mask.s2;
                outMask[(n * 2 + j) * 16 + 3] = mask.s3;
                outMask[(n * 2 + j) * 16 + 4] = mask.s4;
                outMask[(n * 2 + j) * 16 + 5] = mask.s5;
                outMask[(n * 2 + j) * 16 + 6] = mask.s6;
                outMask[(n * 2 + j) * 16 + 7] = mask.s7;
                outMask[(n * 2 + j) * 16 + 8] = mask.s8;
                outMask[(n * 2 + j) * 16 + 9] = mask.s9;
                outMask[(n * 2 + j) * 16 + 10] = mask.sa;
                outMask[(n * 2 + j) * 16 + 11] = mask.sb;
                outMask[(n * 2 + j) * 16 + 12] = mask.sc;
                outMask[(n * 2 + j) * 16 + 13] = mask.sd;
                outMask[(n * 2 + j) * 16 + 14] = mask.se;
                outMask[(n * 2 + j) * 16 + 15] = mask.sf;
              }
            }
            int outIdx = ((ph * pooled_width + pw) * inChannels +
                c * burstChannels) * imgFact;
            memcpy(output + outIdx, poolOutBuf, sizeof(chalf16) *
                imgFact * burstChannels);
            memcpy(tagVals + outIdx * 16, outMask,
                sizeof(short) * numImages * burstChannels);
          }
        }
      }
    } else {
      for (int ph = 0; ph < pooled_height; ++ph) {
        for (int pw = 0; pw < pooled_width; ++pw) {
          for (int c = 0; c < rpo; ++c) {
            int hstart = ph * 2;
            int wstart = pw * 2;
            int inIdx = ((ph * pooled_width + pw) * inChannels + c *
                burstChannels) * imgFact;
            memcpy(poolInBufBW, input + inIdx, sizeof(chalf16) * imgFact
                * burstChannels);
            memcpy(inMask, tagVals + inIdx * 16,
                sizeof(short) * numImages * burstChannels);
            for (int h = 0; h < 3; ++h) {
              for (int w = 0; w < 3; ++w) {
                int outIdx = (((hstart + h) * xdim + (wstart + w))
                    * inChannels + c * burstChannels) * imgFact;
                if ((hstart + h < ydim) && (wstart + w < xdim) &&
                    (h < pksize) && (w < pksize) && ((h == 0) || (w == 0)) &&
                    (pksize == 3)) {
                  memcpy(poolOutBufBW[h * 3 + w], output + outIdx,
                      sizeof(chalf16) * imgFact * burstChannels);
                } else {
                  for (int n = 0; n < imgFact * burstChannels; ++n) {
#pragma HLS pipeline
                    poolOutBufBW[h * 3 + w][n] = chalf(0);
                  }
                }
              }
            }
            for (int n = 0; n < imgFact * burstChannels; ++n) {
#pragma HLS pipeline
#pragma HLS DEPENDENCE variable poolInBuf inter false
              poolOutBufBW[inMask[n * 16 + 0]][n].s0 += poolInBufBW[n].s0;
              poolOutBufBW[inMask[n * 16 + 1]][n].s1 += poolInBufBW[n].s1;
              poolOutBufBW[inMask[n * 16 + 2]][n].s2 += poolInBufBW[n].s2;
              poolOutBufBW[inMask[n * 16 + 3]][n].s3 += poolInBufBW[n].s3;
              poolOutBufBW[inMask[n * 16 + 4]][n].s4 += poolInBufBW[n].s4;
              poolOutBufBW[inMask[n * 16 + 5]][n].s5 += poolInBufBW[n].s5;
              poolOutBufBW[inMask[n * 16 + 6]][n].s6 += poolInBufBW[n].s6;
              poolOutBufBW[inMask[n * 16 + 7]][n].s7 += poolInBufBW[n].s7;
              poolOutBufBW[inMask[n * 16 + 8]][n].s8 += poolInBufBW[n].s8;
              poolOutBufBW[inMask[n * 16 + 9]][n].s9 += poolInBufBW[n].s9;
              poolOutBufBW[inMask[n * 16 + 10]][n].sa += poolInBufBW[n].sa;
              poolOutBufBW[inMask[n * 16 + 11]][n].sb += poolInBufBW[n].sb;
              poolOutBufBW[inMask[n * 16 + 12]][n].sc += poolInBufBW[n].sc;
              poolOutBufBW[inMask[n * 16 + 13]][n].sd += poolInBufBW[n].sd;
              poolOutBufBW[inMask[n * 16 + 14]][n].se += poolInBufBW[n].se;
              poolOutBufBW[inMask[n * 16 + 15]][n].sf += poolInBufBW[n].sf;
            }
            for (int h = 0; h < 3; ++h) {
              for (int w = 0; w < 3; ++w) {
                int outIdx = (((hstart + h) * xdim + (wstart + w))
                    * inChannels + c * burstChannels) * imgFact;
                if ((hstart + h < ydim) && (wstart + w < xdim) &&
                    (h < pksize) && (w < pksize))
                  memcpy(output + outIdx, poolOutBufBW[h * 3 + w],
                      sizeof(chalf16) * imgFact * burstChannels);

              }
            } 
          }
        }
      }
    }
  }
}

}
