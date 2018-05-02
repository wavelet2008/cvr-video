/**
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
 * author: hertz.wang hertz.wong@rock-chips.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef MPP_JPEG_ENCODER_H
#define MPP_JPEG_ENCODER_H

#include <unistd.h>
#include "vpu.h"
#include "base_encoder.h"
#include "mpp_inc.h"

class MPPJpegEncoder : public BaseEncoder {
 public:
  MPPJpegEncoder();
  virtual ~MPPJpegEncoder();
  virtual int InitConfig(MediaConfig& config) override;
  int Encode(Buffer* src, Buffer* dst);

 private:
  struct vpu_encode* venc_;
};

#endif  // MPP_JPEG_ENCODER_H
