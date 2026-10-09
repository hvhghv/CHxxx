/********************************** (C) COPYRIGHT *******************************
 * File Name          : bpstruct.h
 * Description        : LWIP 结构体打包开始（GCC）
 *******************************************************************************/

#ifndef LWIP_ARCH_BPSTRUCT_H
#define LWIP_ARCH_BPSTRUCT_H

#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__((__packed__))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x

#endif /* LWIP_ARCH_BPSTRUCT_H */
