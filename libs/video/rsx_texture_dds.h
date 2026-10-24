/* Bounded DDS 2D reader/writer. No GPU calls and no implicit alpha conversion.
 * DDS layout: https://learn.microsoft.com/windows/win32/direct3ddds/dx-graphics-dds-pguide */
#pragma once
#include "rsx_texture_pack.h"
#include <stdlib.h>
#include <string.h>

static uint32_t tdds_u32(const uint8_t* p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void tdds_put(uint8_t* p,uint32_t v) { for(int i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i)); }
static int tdds_block(uint32_t f) {
    if(f==71||f==72)return 8; /* BC1 */
    if(f==74||f==75||f==77||f==78||f==98||f==99)return 16; /* BC2/3/7 */
    return 0;
}
static int tdds_rgba(uint32_t f) { return f==28||f==29||f==87||f==91||f==88||f==93; }
static void tdds_layout(texpack_image* im) {
    const uint32_t block=(uint32_t)tdds_block(im->format);
    uint32_t w=im->w,h=im->h; im->bytes=0;
    for(uint32_t l=0;l<im->levels;l++) {
        im->level_off[l]=im->bytes;
        im->row_bytes[l]=block?((w+3)/4)*block:w*4;
        im->rows[l]=block?(h+3)/4:h;
        im->bytes+=(size_t)im->row_bytes[l]*im->rows[l];
        w=w>1?w/2:1;h=h>1?h/2:1;
    }
}
/* Invalid/unsupported data leaves the caller's game texture unchanged. DDS
 * arrays/cubes/volumes and premultiplied/custom alpha require conversion first. */
static texpack_image* texpack_dds_parse(const uint8_t* file,size_t n) {
    if(!file||n<128||memcmp(file,"DDS ",4)||tdds_u32(file+4)!=124||tdds_u32(file+76)!=32)return NULL;
    uint32_t w=tdds_u32(file+16),h=tdds_u32(file+12),levels=tdds_u32(file+28);
    if(!w||!h||w>16384||h>16384||tdds_u32(file+24)>1||(tdds_u32(file+112)&0x20fe00u))return NULL;
    if(!levels)levels=1;
    uint32_t max_levels=1;for(uint32_t d=w>h?w:h;d>1;d>>=1)max_levels++;
    if(levels>max_levels||levels>16)return NULL;
    uint32_t flags=tdds_u32(file+80),fourcc=tdds_u32(file+84),format=0,opaque=0;
    size_t start=128;int legacy_rgb=0;uint32_t bits=0,r=0,g=0,b=0,a=0;
    if(flags&4) {
        if(fourcc==0x31545844)format=71; /* DXT1 */
        else if(fourcc==0x33545844)format=74; /* DXT3 */
        else if(fourcc==0x35545844)format=77; /* DXT5 */
        else if(fourcc==0x30315844) {
            if(n<148||tdds_u32(file+132)!=3||tdds_u32(file+140)!=1||(tdds_u32(file+136)&4))return NULL;
            uint32_t alpha=tdds_u32(file+144);
            if(alpha>3||alpha==2)return NULL; /* premultiplied cannot be sampled as straight alpha */
            opaque=alpha==3; format=tdds_u32(file+128);start=148;
        } else return NULL;
    } else if(flags&0x40) {
        bits=tdds_u32(file+88);r=tdds_u32(file+92);g=tdds_u32(file+96);b=tdds_u32(file+100);a=tdds_u32(file+104);
        if((bits!=24&&bits!=32)||g!=0xff00||!((r==0xff&&b==0xff0000)||(r==0xff0000&&b==0xff)))return NULL;
        if(a!=0&&a!=0xff000000)return NULL;
        if(bits==24&&a)return NULL;
        legacy_rgb=1;opaque=!(flags&1)||!a;format=28;
    } else return NULL;
    if(!tdds_block(format)&&!tdds_rgba(format))return NULL;
    /* D3D12 requires block alignment for the top-level BC resource. */
    if((format==98||format==99)&&((w&3)||(h&3)))return NULL;
    if(format==88||format==93)opaque=1;
    texpack_image* im=(texpack_image*)calloc(1,sizeof *im);if(!im)return NULL;
    im->w=w;im->h=h;im->levels=levels;im->format=format;im->opaque=opaque;tdds_layout(im);
    if(im->bytes>(512ull<<20)){free(im);return NULL;}
    if(!legacy_rgb&&!tdds_block(format)&&(tdds_u32(file+8)&8)&&tdds_u32(file+20)!=w*4){free(im);return NULL;}
    size_t source_bytes=0;uint32_t lw=w,lh=h;
    for(uint32_t l=0;l<levels;l++) {
        size_t pitch=legacy_rgb?(size_t)lw*(bits/8):im->row_bytes[l];
        if(legacy_rgb&&l==0&&(tdds_u32(file+8)&8)) {
            uint32_t declared=tdds_u32(file+20);
            if(declared<pitch){free(im);return NULL;}pitch=declared;
        }
        size_t rows=legacy_rgb?lh:im->rows[l];
        if(pitch>n||rows>(n-start-source_bytes)/pitch){free(im);return NULL;}
        source_bytes+=pitch*rows;
        lw=lw>1?lw/2:1;lh=lh>1?lh/2:1;
    }
    im->data=(uint8_t*)malloc(im->bytes);if(!im->data){free(im);return NULL;}
    if(!legacy_rgb)memcpy(im->data,file+start,im->bytes);
    else {
        size_t offset=start;lw=w;lh=h;
        for(uint32_t l=0;l<levels;l++) {
            size_t pitch=(size_t)lw*(bits/8);
            if(l==0&&(tdds_u32(file+8)&8))pitch=tdds_u32(file+20);
            for(uint32_t y=0;y<lh;y++)for(uint32_t x=0;x<lw;x++) {
                const uint8_t* s=file+offset+y*pitch+x*(bits/8);
                uint8_t* d=im->data+im->level_off[l]+((size_t)y*lw+x)*4;
                d[0]=s[r==0xff?0:2];d[1]=s[1];d[2]=s[b==0xff?0:2];d[3]=opaque?255:s[3];
            }
            offset+=pitch*lh;lw=lw>1?lw/2:1;lh=lh>1?lh/2:1;
        }
    }
    if(tdds_block(format)&&((w&3)||(h&3))) {
        texpack_image expanded=*im;expanded.format=(format==72||format==75||format==78)?29:28;tdds_layout(&expanded);
        expanded.data=(uint8_t*)malloc(expanded.bytes);if(!expanded.data){free(im->data);free(im);return NULL;}
        uint32_t sw=w,sh=h;
        int bc=(format==71||format==72)?1:(format==74||format==75)?2:3;
        for(uint32_t l=0;l<levels;l++) {
            texpack_decode_bc(bc,im->data+im->level_off[l],im->row_bytes[l],sw,sh,expanded.data+expanded.level_off[l]);
            sw=sw>1?sw/2:1;sh=sh>1?sh/2:1;
        }
        free(im->data);*im=expanded;
    }
    return im;
}
/* Writes the standard legacy DDS header for RGBA8 or original BC1/2/3 blocks.
 * Payload is tightly packed, in top-to-bottom mip order. */
static int texpack_dds_header(uint8_t header[128],uint32_t w,uint32_t h,uint32_t levels,uint32_t format) {
    if(!w||!h||!levels||levels>16||!(format==28||format==71||format==74||format==77))return 0;
    memset(header,0,128);memcpy(header,"DDS ",4);tdds_put(header+4,124);
    int block=tdds_block(format);
    tdds_put(header+8,0x1007u|(block?0x80000u:8u)|(levels>1?0x20000u:0));
    tdds_put(header+12,h);tdds_put(header+16,w);
    tdds_put(header+20,block?((w+3)/4)*((h+3)/4)*block:w*4);
    tdds_put(header+28,levels);tdds_put(header+76,32);
    if(block){tdds_put(header+80,4);tdds_put(header+84,format==71?0x31545844:format==74?0x33545844:0x35545844);}
    else {tdds_put(header+80,0x41);tdds_put(header+88,32);tdds_put(header+92,0xff);tdds_put(header+96,0xff00);tdds_put(header+100,0xff0000);tdds_put(header+104,0xff000000);}
    tdds_put(header+108,0x1000|(levels>1?0x400008:0));return 1;
}
