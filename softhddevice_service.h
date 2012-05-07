///
///	@file softhddev_service.h @brief software HD device service header file.
///
///	Copyright (c) 2012 by durchflieger.  All Rights Reserved.
///
///	Contributor(s):
///
///	License: AGPLv3
///
///	This program is free software: you can redistribute it and/or modify
///	it under the terms of the GNU Affero General Public License as
///	published by the Free Software Foundation, either version 3 of the
///	License.
///
///	This program is distributed in the hope that it will be useful,
///	but WITHOUT ANY WARRANTY; without even the implied warranty of
///	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
///	GNU Affero General Public License for more details.
///
///	$Id: 737f8b0ecb0fee62c0587626ec61039426775c1d $
//////////////////////////////////////////////////////////////////////////////

#pragma once

#define ATMO_GRAB_SERVICE	"SoftHDDevice-AtmoGrabService-v1.0"

enum
{ GRAB_IMG_RGBA_FORMAT_B8G8R8A8 };

typedef struct
{
    int structSize;

    // request data
    int analyseSize;
    int clippedOverscan;

    // reply data
    int imgType;
    int imgSize;
    int width;
    int height;
    void *img;
} SoftHDDevice_AtmoGrabService_v1_0_t;
