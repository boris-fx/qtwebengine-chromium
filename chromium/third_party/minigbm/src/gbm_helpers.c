/*
 * Copyright (c) 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stddef.h>
#include <stdio.h>

#include "drv.h"
#include "gbm.h"

drv_format_t gbm_convert_format(uint32_t format)
{

	switch (format) {
	case GBM_FORMAT_C8:
		return DRV_FORMAT_C8;
	case GBM_FORMAT_R8:
		return DRV_FORMAT_R8;
	case GBM_FORMAT_RG88:
		return DRV_FORMAT_RG88;
	case GBM_FORMAT_GR88:
		return DRV_FORMAT_GR88;
	case GBM_FORMAT_RGB332:
		return DRV_FORMAT_RGB332;
	case GBM_FORMAT_BGR233:
		return DRV_FORMAT_BGR233;
	case GBM_FORMAT_XRGB4444:
		return DRV_FORMAT_XRGB4444;
	case GBM_FORMAT_XBGR4444:
		return DRV_FORMAT_XBGR4444;
	case GBM_FORMAT_RGBX4444:
		return DRV_FORMAT_RGBX4444;
	case GBM_FORMAT_BGRX4444:
		return DRV_FORMAT_BGRX4444;
	case GBM_FORMAT_ARGB4444:
		return DRV_FORMAT_ARGB4444;
	case GBM_FORMAT_ABGR4444:
		return DRV_FORMAT_ABGR4444;
	case GBM_FORMAT_RGBA4444:
		return DRV_FORMAT_RGBA4444;
	case GBM_FORMAT_BGRA4444:
		return DRV_FORMAT_BGRA4444;
	case GBM_FORMAT_XRGB1555:
		return DRV_FORMAT_XRGB1555;
	case GBM_FORMAT_XBGR1555:
		return DRV_FORMAT_XBGR1555;
	case GBM_FORMAT_RGBX5551:
		return DRV_FORMAT_RGBX5551;
	case GBM_FORMAT_BGRX5551:
		return DRV_FORMAT_BGRX5551;
	case GBM_FORMAT_ARGB1555:
		return DRV_FORMAT_ARGB1555;
	case GBM_FORMAT_ABGR1555:
		return DRV_FORMAT_ABGR1555;
	case GBM_FORMAT_RGBA5551:
		return DRV_FORMAT_RGBA5551;
	case GBM_FORMAT_BGRA5551:
		return DRV_FORMAT_BGRA5551;
	case GBM_FORMAT_RGB565:
		return DRV_FORMAT_RGB565;
	case GBM_FORMAT_BGR565:
		return DRV_FORMAT_BGR565;
	case GBM_FORMAT_RGB888:
		return DRV_FORMAT_RGB888;
	case GBM_FORMAT_BGR888:
		return DRV_FORMAT_BGR888;
	case GBM_FORMAT_XRGB8888:
		return DRV_FORMAT_XRGB8888;
	case GBM_FORMAT_XBGR8888:
		return DRV_FORMAT_XBGR8888;
	case GBM_FORMAT_RGBX8888:
		return DRV_FORMAT_RGBX8888;
	case GBM_FORMAT_BGRX8888:
		return DRV_FORMAT_BGRX8888;
	case GBM_FORMAT_ARGB8888:
		return DRV_FORMAT_ARGB8888;
	case GBM_FORMAT_ABGR8888:
		return DRV_FORMAT_ABGR8888;
	case GBM_FORMAT_RGBA8888:
		return DRV_FORMAT_RGBA8888;
	case GBM_FORMAT_BGRA8888:
		return DRV_FORMAT_BGRA8888;
	case GBM_FORMAT_XRGB2101010:
		return DRV_FORMAT_XRGB2101010;
	case GBM_FORMAT_XBGR2101010:
		return DRV_FORMAT_XBGR2101010;
	case GBM_FORMAT_RGBX1010102:
		return DRV_FORMAT_RGBX1010102;
	case GBM_FORMAT_BGRX1010102:
		return DRV_FORMAT_BGRX1010102;
	case GBM_FORMAT_ARGB2101010:
		return DRV_FORMAT_ARGB2101010;
	case GBM_FORMAT_ABGR2101010:
		return DRV_FORMAT_ABGR2101010;
	case GBM_FORMAT_RGBA1010102:
		return DRV_FORMAT_RGBA1010102;
	case GBM_FORMAT_BGRA1010102:
		return DRV_FORMAT_BGRA1010102;
	case GBM_FORMAT_YUYV:
		return DRV_FORMAT_YUYV;
	case GBM_FORMAT_YVYU:
		return DRV_FORMAT_YVYU;
	case GBM_FORMAT_UYVY:
		return DRV_FORMAT_UYVY;
	case GBM_FORMAT_VYUY:
		return DRV_FORMAT_VYUY;
	case GBM_FORMAT_AYUV:
		return DRV_FORMAT_AYUV;
	case GBM_FORMAT_NV12:
		return DRV_FORMAT_NV12;
	case GBM_FORMAT_NV21:
		return DRV_FORMAT_NV21;
	case GBM_FORMAT_NV16:
		return DRV_FORMAT_NV16;
	case GBM_FORMAT_NV61:
		return DRV_FORMAT_NV61;
	case GBM_FORMAT_YUV410:
		return DRV_FORMAT_YUV410;
	case GBM_FORMAT_YVU410:
		return DRV_FORMAT_YVU410;
	case GBM_FORMAT_YUV411:
		return DRV_FORMAT_YUV411;
	case GBM_FORMAT_YVU411:
		return DRV_FORMAT_YVU411;
	case GBM_FORMAT_YUV420:
		return DRV_FORMAT_YUV420;
	case GBM_FORMAT_YVU420:
		return DRV_FORMAT_YVU420;
	case GBM_FORMAT_YUV422:
		return DRV_FORMAT_YUV422;
	case GBM_FORMAT_YVU422:
		return DRV_FORMAT_YVU422;
	case GBM_FORMAT_YUV444:
		return DRV_FORMAT_YVU444;
	case GBM_FORMAT_YVU444:
		return DRV_FORMAT_YVU444;
	}

	fprintf(stderr, "minigbm: UNKNOWN FORMAT %d\n", format);
	return DRV_FORMAT_NONE;
}

uint64_t gbm_convert_flags(uint32_t flags)
{
	uint64_t usage = DRV_BO_USE_NONE;

	if (flags & GBM_BO_USE_SCANOUT)
		usage |= DRV_BO_USE_SCANOUT;
	if (flags & GBM_BO_USE_CURSOR)
		usage |= DRV_BO_USE_CURSOR;
	if (flags & GBM_BO_USE_CURSOR_64X64)
		usage |= DRV_BO_USE_CURSOR_64X64;
	if (flags & GBM_BO_USE_RENDERING)
		usage |= DRV_BO_USE_RENDERING;
	if (flags & GBM_BO_USE_LINEAR)
		usage |= DRV_BO_USE_LINEAR;

	return usage;
}
