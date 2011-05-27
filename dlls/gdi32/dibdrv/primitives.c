/*
 * DIB driver primitives.
 *
 * Copyright 2011 Huw Davies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <assert.h>

#include "gdi_private.h"
#include "dibdrv.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dib);

static inline DWORD *get_pixel_ptr_32(const dib_info *dib, int x, int y)
{
    return (DWORD *)((BYTE*)dib->bits + y * dib->stride + x * 4);
}

static inline WORD *get_pixel_ptr_16(const dib_info *dib, int x, int y)
{
    return (WORD *)((BYTE*)dib->bits + y * dib->stride + x * 2);
}

static inline BYTE *get_pixel_ptr_8(const dib_info *dib, int x, int y)
{
    return (BYTE*)dib->bits + y * dib->stride + x;
}

static inline void do_rop_32(DWORD *ptr, DWORD and, DWORD xor)
{
    *ptr = (*ptr & and) ^ xor;
}

static inline void do_rop_16(WORD *ptr, WORD and, WORD xor)
{
    *ptr = (*ptr & and) ^ xor;
}

static inline void do_rop_8(BYTE *ptr, BYTE and, BYTE xor)
{
    *ptr = (*ptr & and) ^ xor;
}

static void solid_rects_32(const dib_info *dib, int num, const RECT *rc, DWORD and, DWORD xor)
{
    DWORD *ptr, *start;
    int x, y, i;

    for(i = 0; i < num; i++, rc++)
    {
        start = get_pixel_ptr_32(dib, rc->left, rc->top);
        for(y = rc->top; y < rc->bottom; y++, start += dib->stride / 4)
            for(x = rc->left, ptr = start; x < rc->right; x++)
                do_rop_32(ptr++, and, xor);
    }
}

static void solid_rects_16(const dib_info *dib, int num, const RECT *rc, DWORD and, DWORD xor)
{
    WORD *ptr, *start;
    int x, y, i;

    for(i = 0; i < num; i++, rc++)
    {
        start = get_pixel_ptr_16(dib, rc->left, rc->top);
        for(y = rc->top; y < rc->bottom; y++, start += dib->stride / 2)
            for(x = rc->left, ptr = start; x < rc->right; x++)
                do_rop_16(ptr++, and, xor);
    }
}

static void solid_rects_8(const dib_info *dib, int num, const RECT *rc, DWORD and, DWORD xor)
{
    BYTE *ptr, *start;
    int x, y, i;

    for(i = 0; i < num; i++, rc++)
    {
        start = get_pixel_ptr_8(dib, rc->left, rc->top);
        for(y = rc->top; y < rc->bottom; y++, start += dib->stride)
            for(x = rc->left, ptr = start; x < rc->right; x++)
                do_rop_8(ptr++, and, xor);
    }
}

static void solid_rects_null(const dib_info *dib, int num, const RECT *rc, DWORD and, DWORD xor)
{
    return;
}

static inline INT calc_offset(INT edge, INT size, INT origin)
{
    INT offset;

    if(edge - origin >= 0)
        offset = (edge - origin) % size;
    else
    {
        offset = (origin - edge) % size;
        if(offset) offset = size - offset;
    }
    return offset;
}

static inline POINT calc_brush_offset(const RECT *rc, const dib_info *brush, const POINT *origin)
{
    POINT offset;

    offset.x = calc_offset(rc->left, brush->width,  origin->x);
    offset.y = calc_offset(rc->top,  brush->height, origin->y);

    return offset;
}

static void pattern_rects_32(const dib_info *dib, int num, const RECT *rc, const POINT *origin,
                             const dib_info *brush, void *and_bits, void *xor_bits)
{
    DWORD *ptr, *start, *start_and, *and_ptr, *start_xor, *xor_ptr;
    int x, y, i;
    POINT offset;

    for(i = 0; i < num; i++, rc++)
    {
        offset = calc_brush_offset(rc, brush, origin);

        start = get_pixel_ptr_32(dib, rc->left, rc->top);
        start_and = (DWORD*)and_bits + offset.y * brush->stride / 4;
        start_xor = (DWORD*)xor_bits + offset.y * brush->stride / 4;

        for(y = rc->top; y < rc->bottom; y++, start += dib->stride / 4)
        {
            and_ptr = start_and + offset.x;
            xor_ptr = start_xor + offset.x;

            for(x = rc->left, ptr = start; x < rc->right; x++)
            {
                do_rop_32(ptr++, *and_ptr++, *xor_ptr++);
                if(and_ptr == start_and + brush->width)
                {
                    and_ptr = start_and;
                    xor_ptr = start_xor;
                }
            }

            offset.y++;
            if(offset.y == brush->height)
            {
                start_and = and_bits;
                start_xor = xor_bits;
                offset.y = 0;
            }
            else
            {
                start_and += brush->stride / 4;
                start_xor += brush->stride / 4;
            }
        }
    }
}

static void pattern_rects_16(const dib_info *dib, int num, const RECT *rc, const POINT *origin,
                             const dib_info *brush, void *and_bits, void *xor_bits)
{
    WORD *ptr, *start, *start_and, *and_ptr, *start_xor, *xor_ptr;
    int x, y, i;
    POINT offset;

    for(i = 0; i < num; i++, rc++)
    {
        offset = calc_brush_offset(rc, brush, origin);

        start = get_pixel_ptr_16(dib, rc->left, rc->top);
        start_and = (WORD*)and_bits + offset.y * brush->stride / 2;
        start_xor = (WORD*)xor_bits + offset.y * brush->stride / 2;

        for(y = rc->top; y < rc->bottom; y++, start += dib->stride / 2)
        {
            and_ptr = start_and + offset.x;
            xor_ptr = start_xor + offset.x;

            for(x = rc->left, ptr = start; x < rc->right; x++)
            {
                do_rop_16(ptr++, *and_ptr++, *xor_ptr++);
                if(and_ptr == start_and + brush->width)
                {
                    and_ptr = start_and;
                    xor_ptr = start_xor;
                }
            }

            offset.y++;
            if(offset.y == brush->height)
            {
                start_and = and_bits;
                start_xor = xor_bits;
                offset.y = 0;
            }
            else
            {
                start_and += brush->stride / 2;
                start_xor += brush->stride / 2;
            }
        }
    }
}

static void pattern_rects_8(const dib_info *dib, int num, const RECT *rc, const POINT *origin,
                            const dib_info *brush, void *and_bits, void *xor_bits)
{
    BYTE *ptr, *start, *start_and, *and_ptr, *start_xor, *xor_ptr;
    int x, y, i;
    POINT offset;

    for(i = 0; i < num; i++, rc++)
    {
        offset = calc_brush_offset(rc, brush, origin);

        start = get_pixel_ptr_8(dib, rc->left, rc->top);
        start_and = (BYTE*)and_bits + offset.y * brush->stride;
        start_xor = (BYTE*)xor_bits + offset.y * brush->stride;

        for(y = rc->top; y < rc->bottom; y++, start += dib->stride)
        {
            and_ptr = start_and + offset.x;
            xor_ptr = start_xor + offset.x;

            for(x = rc->left, ptr = start; x < rc->right; x++)
            {
                do_rop_8(ptr++, *and_ptr++, *xor_ptr++);
                if(and_ptr == start_and + brush->width)
                {
                    and_ptr = start_and;
                    xor_ptr = start_xor;
                }
            }

            offset.y++;
            if(offset.y == brush->height)
            {
                start_and = and_bits;
                start_xor = xor_bits;
                offset.y = 0;
            }
            else
            {
                start_and += brush->stride;
                start_xor += brush->stride;
            }
        }
    }
}

static void pattern_rects_null(const dib_info *dib, int num, const RECT *rc, const POINT *origin,
                               const dib_info *brush, void *and_bits, void *xor_bits)
{
    return;
}

static DWORD colorref_to_pixel_888(const dib_info *dib, COLORREF color)
{
    return ( ((color >> 16) & 0xff) | (color & 0xff00) | ((color << 16) & 0xff0000) );
}

static inline DWORD put_field(DWORD field, int shift, int len)
{
    shift = shift - (8 - len);
    if (len <= 8)
        field &= (((1 << len) - 1) << (8 - len));
    if (shift < 0)
        field >>= -shift;
    else
        field <<= shift;
    return field;
}

static DWORD colorref_to_pixel_masks(const dib_info *dib, COLORREF colour)
{
    DWORD r,g,b;

    r = GetRValue(colour);
    g = GetGValue(colour);
    b = GetBValue(colour);

    return put_field(r, dib->red_shift,   dib->red_len) |
           put_field(g, dib->green_shift, dib->green_len) |
           put_field(b, dib->blue_shift,  dib->blue_len);
}

static DWORD colorref_to_pixel_555(const dib_info *dib, COLORREF color)
{
    return ( ((color >> 19) & 0x1f) | ((color >> 6) & 0x03e0) | ((color << 7) & 0x7c00) );
}

static DWORD colorref_to_pixel_colortable(const dib_info *dib, COLORREF color)
{
    int i, best_index = 0;
    RGBQUAD rgb;
    DWORD diff, best_diff = 0xffffffff;

    rgb.rgbRed = GetRValue(color);
    rgb.rgbGreen = GetGValue(color);
    rgb.rgbBlue = GetBValue(color);

    for(i = 0; i < dib->color_table_size; i++)
    {
        RGBQUAD *cur = dib->color_table + i;
        diff = (rgb.rgbRed - cur->rgbRed) * (rgb.rgbRed - cur->rgbRed)
            +  (rgb.rgbGreen - cur->rgbGreen) * (rgb.rgbGreen - cur->rgbGreen)
            +  (rgb.rgbBlue - cur->rgbBlue) * (rgb.rgbBlue - cur->rgbBlue);

        if(diff == 0)
        {
            best_index = i;
            break;
        }

        if(diff < best_diff)
        {
            best_diff = diff;
            best_index = i;
        }
    }
    return best_index;
}

static DWORD colorref_to_pixel_null(const dib_info *dib, COLORREF color)
{
    return 0;
}

static BOOL convert_to_8888(dib_info *dst, const dib_info *src, const RECT *src_rect)
{
    DWORD *dst_start = dst->bits, *dst_pixel, src_val;
    int x, y;

    switch(src->bit_count)
    {
    case 32:
    {
        DWORD *src_start = get_pixel_ptr_32(src, src_rect->left, src_rect->top);
        if(src->funcs == &funcs_8888)
        {
            if(src->stride > 0 && dst->stride > 0 && src_rect->left == 0 && src_rect->right == src->width)
                memcpy(dst->bits, src_start, (src_rect->bottom - src_rect->top) * src->stride);
            else
            {
                for(y = src_rect->top; y < src_rect->bottom; y++)
                {
                    memcpy(dst_start, src_start, (src_rect->right - src_rect->left) * 4);
                    dst_start += dst->stride / 4;
                    src_start += src->stride / 4;
                }
            }
        }
        else
        {
            FIXME("Unsupported conversion: 32 -> 8888\n");
            return FALSE;
        }
        break;
    }

    case 16:
    {
        WORD *src_start = get_pixel_ptr_16(src, src_rect->left, src_rect->top), *src_pixel;
        if(src->funcs == &funcs_555)
        {
            for(y = src_rect->top; y < src_rect->bottom; y++)
            {
                dst_pixel = dst_start;
                src_pixel = src_start;
                for(x = src_rect->left; x < src_rect->right; x++)
                {
                    src_val = *src_pixel++;
                    *dst_pixel++ = ((src_val << 9) & 0xf80000) | ((src_val << 4) & 0x070000) |
                                   ((src_val << 6) & 0x00f800) | ((src_val << 1) & 0x000700) |
                                   ((src_val << 3) & 0x0000f8) | ((src_val >> 2) & 0x000007);
                }
                dst_start += dst->stride / 4;
                src_start += src->stride / 2;
            }
        }
        else
        {
            FIXME("Unsupported conversion: 16 -> 8888\n");
            return FALSE;
        }
        break;
    }

    case 8:
    {
        BYTE *src_start = get_pixel_ptr_8(src, src_rect->left, src_rect->top), *src_pixel;
        for(y = src_rect->top; y < src_rect->bottom; y++)
        {
            dst_pixel = dst_start;
            src_pixel = src_start;
            for(x = src_rect->left; x < src_rect->right; x++)
            {
                RGBQUAD rgb;
                src_val = *src_pixel++;
                if(src_val >= src->color_table_size) src_val = src->color_table_size - 1;
                rgb = src->color_table[src_val];
                *dst_pixel++ = rgb.rgbRed << 16 | rgb.rgbGreen << 8 | rgb.rgbBlue;
            }
            dst_start += dst->stride / 4;
            src_start += src->stride;
        }
        break;
    }

    default:
        FIXME("Unsupported conversion: %d -> 8888\n", src->bit_count);
        return FALSE;
    }

    return TRUE;
}

static BOOL convert_to_32(dib_info *dst, const dib_info *src, const RECT *src_rect)
{
    DWORD *dst_start = dst->bits, *dst_pixel, src_val;
    int x, y;

    switch(src->bit_count)
    {
    case 32:
    {
        DWORD *src_start = get_pixel_ptr_32(src, src_rect->left, src_rect->top), *src_pixel;

        if(src->funcs == &funcs_8888)
        {
            for(y = src_rect->top; y < src_rect->bottom; y++)
            {
                dst_pixel = dst_start;
                src_pixel = src_start;
                for(x = src_rect->left; x < src_rect->right; x++)
                {
                    src_val = *src_pixel++;
                    *dst_pixel++ = put_field((src_val >> 16) & 0xff, dst->red_shift,   dst->red_len)   |
                                   put_field((src_val >>  8) & 0xff, dst->green_shift, dst->green_len) |
                                   put_field( src_val        & 0xff, dst->blue_shift,  dst->blue_len);
                }
                dst_start += dst->stride / 4;
                src_start += src->stride / 4;
            }
        }
        else
        {
            FIXME("Unsupported conversion: 32 -> 32\n");
            return FALSE;
        }
        break;
    }

    case 16:
    {
        WORD *src_start = get_pixel_ptr_16(src, src_rect->left, src_rect->top), *src_pixel;
        if(src->funcs == &funcs_555)
        {
            for(y = src_rect->top; y < src_rect->bottom; y++)
            {
                dst_pixel = dst_start;
                src_pixel = src_start;
                for(x = src_rect->left; x < src_rect->right; x++)
                {
                    src_val = *src_pixel++;
                    *dst_pixel++ = put_field(((src_val >> 7) & 0xf8) | ((src_val >> 12) & 0x07), dst->red_shift,   dst->red_len) |
                                   put_field(((src_val >> 2) & 0xf8) | ((src_val >>  7) & 0x07), dst->green_shift, dst->green_len) |
                                   put_field(((src_val << 3) & 0xf8) | ((src_val >>  2) & 0x07), dst->blue_shift,  dst->blue_len);
                }
                dst_start += dst->stride / 4;
                src_start += src->stride / 2;
            }
        }
        else
        {
            FIXME("Unsupported conversion: 16 -> 8888\n");
            return FALSE;
        }
        break;
    }

    case 8:
    {
        BYTE *src_start = get_pixel_ptr_8(src, src_rect->left, src_rect->top), *src_pixel;
        for(y = src_rect->top; y < src_rect->bottom; y++)
        {
            dst_pixel = dst_start;
            src_pixel = src_start;
            for(x = src_rect->left; x < src_rect->right; x++)
            {
                RGBQUAD rgb;
                src_val = *src_pixel++;
                if(src_val >= src->color_table_size) src_val = src->color_table_size - 1;
                rgb = src->color_table[src_val];
                *dst_pixel++ = put_field(rgb.rgbRed,   dst->red_shift,   dst->red_len) |
                               put_field(rgb.rgbGreen, dst->green_shift, dst->green_len) |
                               put_field(rgb.rgbBlue,  dst->blue_shift,  dst->blue_len);
            }
            dst_start += dst->stride / 4;
            src_start += src->stride;
        }
        break;
    }

    default:
        FIXME("Unsupported conversion: %d -> 32\n", src->bit_count);
        return FALSE;
    }

    return TRUE;
}

static BOOL convert_to_555(dib_info *dst, const dib_info *src, const RECT *src_rect)
{
    WORD *dst_start = dst->bits, *dst_pixel;
    INT x, y;
    DWORD src_val;

    switch(src->bit_count)
    {
    case 32:
    {
        DWORD *src_start = get_pixel_ptr_32(src, src_rect->left, src_rect->top), *src_pixel;

        if(src->funcs == &funcs_8888)
        {
            for(y = src_rect->top; y < src_rect->bottom; y++)
            {
                dst_pixel = dst_start;
                src_pixel = src_start;
                for(x = src_rect->left; x < src_rect->right; x++)
                {
                    src_val = *src_pixel++;
                    *dst_pixel++ = ((src_val >> 9) & 0x7c00) |
                                   ((src_val >> 6) & 0x03e0) |
                                   ((src_val >> 3) & 0x001e);
                }
                dst_start += dst->stride / 2;
                src_start += src->stride / 4;
            }
        }
        else
        {
            FIXME("Unsupported conversion: 32 -> 555\n");
            return FALSE;
        }
        break;
    }

    case 16:
    {
        WORD *src_start = get_pixel_ptr_16(src, src_rect->left, src_rect->top);
        if(src->funcs == &funcs_555)
        {
            if(src->stride > 0 && dst->stride > 0 && src_rect->left == 0 && src_rect->right == src->width)
                memcpy(dst->bits, src_start, (src_rect->bottom - src_rect->top) * src->stride);
            else
            {
                for(y = src_rect->top; y < src_rect->bottom; y++)
                {
                    memcpy(dst_start, src_start, (src_rect->right - src_rect->left) * 2);
                    dst_start += dst->stride / 2;
                    src_start += src->stride / 2;
                }
            }
        }
        else
        {
            FIXME("Unsupported conversion: 16 -> 555\n");
            return FALSE;
        }
        break;
    }

    case 8:
    {
        BYTE *src_start = get_pixel_ptr_8(src, src_rect->left, src_rect->top), *src_pixel;
        for(y = src_rect->top; y < src_rect->bottom; y++)
        {
            dst_pixel = dst_start;
            src_pixel = src_start;
            for(x = src_rect->left; x < src_rect->right; x++)
            {
                RGBQUAD rgb;
                src_val = *src_pixel++;
                if(src_val >= src->color_table_size) src_val = src->color_table_size - 1;
                rgb = src->color_table[src_val];
                *dst_pixel++ = ((rgb.rgbRed   << 7) & 0x7c00) |
                               ((rgb.rgbGreen << 2) & 0x03e0) |
                               ((rgb.rgbBlue  >> 3) & 0x001f);
            }
            dst_start += dst->stride / 2;
            src_start += src->stride;
        }
        break;
    }

    default:
        FIXME("Unsupported conversion: %d -> 555\n", src->bit_count);
        return FALSE;

    }
    return TRUE;
}

static BOOL convert_to_16(dib_info *dst, const dib_info *src, const RECT *src_rect)
{
    WORD *dst_start = dst->bits, *dst_pixel;
    INT x, y;
    DWORD src_val;

    switch(src->bit_count)
    {
    case 32:
    {
        DWORD *src_start = get_pixel_ptr_32(src, src_rect->left, src_rect->top), *src_pixel;

        if(src->funcs == &funcs_8888)
        {
            for(y = src_rect->top; y < src_rect->bottom; y++)
            {
                dst_pixel = dst_start;
                src_pixel = src_start;
                for(x = src_rect->left; x < src_rect->right; x++)
                {
                    src_val = *src_pixel++;
                    *dst_pixel++ = put_field((src_val >> 16) & 0xff, dst->red_shift,   dst->red_len)   |
                                   put_field((src_val >>  8) & 0xff, dst->green_shift, dst->green_len) |
                                   put_field( src_val        & 0xff, dst->blue_shift,  dst->blue_len);
                }
                dst_start += dst->stride / 2;
                src_start += src->stride / 4;
            }
        }
        else
        {
            FIXME("Unsupported conversion: 32 -> 16\n");
            return FALSE;
        }
        break;
    }

    case 16:
    {
        WORD *src_start = get_pixel_ptr_16(src, src_rect->left, src_rect->top), *src_pixel;
        if(src->funcs == &funcs_555)
        {
            for(y = src_rect->top; y < src_rect->bottom; y++)
            {
                dst_pixel = dst_start;
                src_pixel = src_start;
                for(x = src_rect->left; x < src_rect->right; x++)
                {
                    src_val = *src_pixel++;
                    *dst_pixel++ = put_field(((src_val >> 7) & 0xf8) | ((src_val >> 12) & 0x07), dst->red_shift,   dst->red_len) |
                                   put_field(((src_val >> 2) & 0xf8) | ((src_val >>  7) & 0x07), dst->green_shift, dst->green_len) |
                                   put_field(((src_val << 3) & 0xf8) | ((src_val >>  2) & 0x07), dst->blue_shift,  dst->blue_len);
                }
                dst_start += dst->stride / 2;
                src_start += src->stride / 2;
            }
        }
        else
        {
            FIXME("Unsupported conversion: 16 -> 16\n");
            return FALSE;
        }
        break;
    }

    case 8:
    {
        BYTE *src_start = get_pixel_ptr_8(src, src_rect->left, src_rect->top), *src_pixel;
        for(y = src_rect->top; y < src_rect->bottom; y++)
        {
            dst_pixel = dst_start;
            src_pixel = src_start;
            for(x = src_rect->left; x < src_rect->right; x++)
            {
                RGBQUAD rgb;
                src_val = *src_pixel++;
                if(src_val >= src->color_table_size) src_val = src->color_table_size - 1;
                rgb = src->color_table[src_val];
                *dst_pixel++ = put_field(rgb.rgbRed,   dst->red_shift,   dst->red_len) |
                               put_field(rgb.rgbGreen, dst->green_shift, dst->green_len) |
                               put_field(rgb.rgbBlue,  dst->blue_shift,  dst->blue_len);
            }
            dst_start += dst->stride / 2;
            src_start += src->stride;
        }
        break;
    }

    default:
        FIXME("Unsupported conversion: %d -> 16\n", src->bit_count);
        return FALSE;

    }
    return TRUE;
}

static inline BOOL color_tables_match(const dib_info *d1, const dib_info *d2)
{
    assert(d1->color_table_size && d2->color_table_size);

    if(d1->color_table_size != d2->color_table_size) return FALSE;
    return !memcmp(d1->color_table, d2->color_table, d1->color_table_size * sizeof(d1->color_table[0]));
}

static BOOL convert_to_8(dib_info *dst, const dib_info *src, const RECT *src_rect)
{
    BYTE *dst_start = dst->bits, *dst_pixel;
    INT x, y;
    DWORD src_val;

    switch(src->bit_count)
    {
    case 32:
    {
        DWORD *src_start = get_pixel_ptr_32(src, src_rect->left, src_rect->top), *src_pixel;

        if(src->funcs == &funcs_8888)
        {
            for(y = src_rect->top; y < src_rect->bottom; y++)
            {
                dst_pixel = dst_start;
                src_pixel = src_start;
                for(x = src_rect->left; x < src_rect->right; x++)
                {
                    src_val = *src_pixel++;
                    *dst_pixel++ = colorref_to_pixel_colortable(dst, ((src_val >> 16) & 0x0000ff) |
                                                                     ( src_val        & 0x00ff00) |
                                                                     ((src_val << 16) & 0xff0000) );
                }
                dst_start += dst->stride;
                src_start += src->stride / 4;
            }
        }
        else
        {
            FIXME("Unsupported conversion: 32 -> 8\n");
            return FALSE;
        }
        break;
    }

    case 16:
    {
        WORD *src_start = get_pixel_ptr_16(src, src_rect->left, src_rect->top), *src_pixel;
        if(src->funcs == &funcs_555)
        {
            for(y = src_rect->top; y < src_rect->bottom; y++)
            {
                dst_pixel = dst_start;
                src_pixel = src_start;
                for(x = src_rect->left; x < src_rect->right; x++)
                {
                    src_val = *src_pixel++;
                    *dst_pixel++ = colorref_to_pixel_colortable(dst, ((src_val >>  7) & 0x0000f8) | ((src_val >> 12) & 0x000007) |
                                                                     ((src_val <<  6) & 0x00f800) | ((src_val <<  1) & 0x000700) |
                                                                     ((src_val << 19) & 0xf80000) | ((src_val << 14) & 0x070000) );
                }
                dst_start += dst->stride;
                src_start += src->stride / 2;
            }
        }
        else
        {
            FIXME("Unsupported conversion: 16 -> 8\n");
            return FALSE;
        }
        break;
    }

    case 8:
    {
        BYTE *src_start = get_pixel_ptr_8(src, src_rect->left, src_rect->top), *src_pixel;

        if(color_tables_match(dst, src))
        {
            if(src->stride > 0 && dst->stride > 0 && src_rect->left == 0 && src_rect->right == src->width)
                memcpy(dst->bits, src_start, (src_rect->bottom - src_rect->top) * src->stride);
            else
            {
                for(y = src_rect->top; y < src_rect->bottom; y++)
                {
                    memcpy(dst_start, src_start, src_rect->right - src_rect->left);
                    dst_start += dst->stride;
                    src_start += src->stride;
                }
            }
        }
        else
        {
            for(y = src_rect->top; y < src_rect->bottom; y++)
            {
                dst_pixel = dst_start;
                src_pixel = src_start;
                for(x = src_rect->left; x < src_rect->right; x++)
                {
                    RGBQUAD rgb;
                    src_val = *src_pixel++;
                    if(src_val >= src->color_table_size) src_val = src->color_table_size - 1;
                    rgb = src->color_table[src_val];
                    *dst_pixel++ = colorref_to_pixel_colortable(dst, RGB(rgb.rgbRed, rgb.rgbGreen, rgb.rgbBlue));
                }
                dst_start += dst->stride;
                src_start += src->stride;
            }
        }
        break;
    }

    default:
        FIXME("Unsupported conversion: %d -> 8\n", src->bit_count);
        return FALSE;

    }
    return TRUE;
}

static BOOL convert_to_null(dib_info *dst, const dib_info *src, const RECT *src_rect)
{
    return TRUE;
}

const primitive_funcs funcs_8888 =
{
    solid_rects_32,
    pattern_rects_32,
    colorref_to_pixel_888,
    convert_to_8888
};

const primitive_funcs funcs_32 =
{
    solid_rects_32,
    pattern_rects_32,
    colorref_to_pixel_masks,
    convert_to_32
};

const primitive_funcs funcs_555 =
{
    solid_rects_16,
    pattern_rects_16,
    colorref_to_pixel_555,
    convert_to_555
};

const primitive_funcs funcs_16 =
{
    solid_rects_16,
    pattern_rects_16,
    colorref_to_pixel_masks,
    convert_to_16
};

const primitive_funcs funcs_8 =
{
    solid_rects_8,
    pattern_rects_8,
    colorref_to_pixel_colortable,
    convert_to_8
};

const primitive_funcs funcs_null =
{
    solid_rects_null,
    pattern_rects_null,
    colorref_to_pixel_null,
    convert_to_null
};
