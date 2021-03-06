

#ifndef __IPIPMIFACE_H__
#define __IPIPMIFACE_H__


/*****************************************************************************
  1 Include Headfile
*****************************************************************************/
#include "vos.h"



/*****************************************************************************
  1.1 Cplusplus Announce
*****************************************************************************/
#ifdef __cplusplus
#if __cplusplus
    extern "C" {
#endif
#endif

/*****************************************************************************
  #pragma pack(*)    设置字节对齐方式
*****************************************************************************/
#if (VOS_OS_VER != VOS_WIN32)
#pragma pack(4)
#else
#pragma pack(push, 4)
#endif


/*****************************************************************************
  2 Macro
*****************************************************************************/


/*****************************************************************************
  3 Massage Declare
*****************************************************************************/


/*****************************************************************************
  4 Enum
*****************************************************************************/


/*****************************************************************************
  5 STRUCT
*****************************************************************************/


/*****************************************************************************
  6 UNION
*****************************************************************************/


/*****************************************************************************
  7 Extern Global Variable
*****************************************************************************/


/*****************************************************************************
  8 Fuction Extern
*****************************************************************************/
extern VOS_UINT32 IP_IsDhcpv4ClientPacket
(
    const VOS_UINT8                    *pucIpMsg,
    VOS_UINT32                          ulIpMsgLen
);

extern VOS_UINT32 IP_IsDhcpv4ServerPacket
(
    const VOS_UINT8                    *pucIpMsg,
    VOS_UINT32                          ulIpMsgLen
);

/*****************************************************************************
  9 OTHERS
*****************************************************************************/










#if (VOS_OS_VER != VOS_WIN32)
#pragma pack()
#else
#pragma pack(pop)
#endif




#ifdef __cplusplus
    #if __cplusplus
        }
    #endif
#endif

#endif /* end of IpIpmIface.h */

