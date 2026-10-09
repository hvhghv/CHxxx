/********************************** (C) COPYRIGHT *******************************
 * File Name          : epstruct.h
 * Description        : LWIP 结构体打包结束（GCC）
 *******************************************************************************/

#ifndef LWIP_ARCH_EPSTRUCT_H
#define LWIP_ARCH_EPSTRUCT_H

#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__((__packed__))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x

#endif /* LWIP_ARCH_EPSTRUCT_H */
