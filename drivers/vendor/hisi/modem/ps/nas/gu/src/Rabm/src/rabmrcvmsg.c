
#include "rabminclude.h"
#include "NasRabmMsgProc.h"
#include "NasRabmMain.h"

#include "NasUtranCtrlInterface.h"

#ifdef  __cplusplus
  #if  __cplusplus
  extern "C"{
  #endif
#endif


/*****************************************************************************
    协议栈打印打点方式下的.C文件宏定义
*****************************************************************************/
#define    THIS_FILE_ID        PS_FILE_ID_RABM_RCVMSG_C

/*****************************************************************************
   2 全局变量定义
*****************************************************************************/


/*标识流量上报定时器是否启动*/
extern RABM_FLUX_WRITE_NV_TIMER_SWITCH_ENUM_U32  g_stRabmWriteFluxToNvTimerSwitch;
VOS_VOID RABM_RcvGmmReestCnf(
                         VOS_VOID  *pMsg                                            /* 当前处理的消息                           */
                         )
{
    GMMRABM_REESTABLISH_CNF_STRU          *pTempMsg;                            /* 定义局部变量                             */
    VOS_UINT8                                 i;                                    /* 循环变量                                 */

    pTempMsg = (GMMRABM_REESTABLISH_CNF_STRU *)pMsg;

    if( RABM_FALSE == g_ucReestTimerFlg )
    {
        PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_WARNING, "RABM_RcvGmmReestCnf:WARNING:GMM->RABM g_ucReestTimerFlg Error!");
        return;
    }

    /* 停止等待REESTABLISH_CNF消息的定时器 */
    RABM_TimerStop(0);
    g_ucReestTimerFlg = RABM_FALSE;

    if( GMM_RABM_SERVICEREQ_FAILURE == pTempMsg->ulResult )
    {                                                                           /* 失败                                     */
        for( i = 0; i < RABM_PS_MAX_ENT_NUM; i++ )
        {                                                                       /* 遍历PS域RABM实体                         */
            switch( g_aRabmPsEnt[i].ucState )
            {                                                                   /* RABM实体的状态                           */
                case RABM_NSAPI_ACTIVE_NO_RAB:

                    if (VOS_TRUE == NAS_RABM_GetWPsEntRabReestFlg(i))
                    {
                        /* 清除RABM实体的重建标识 */
                        NAS_RABM_ClearWPsEntRabReestFlg(i);

                        /* 通知CDS模块RAB重建失败 */
                        NAS_RABM_SndCdsFreeBuffDataInd(i + RABM_NSAPI_OFFSET);
                    }

                    if(0 != g_aRabmPsEnt[i].UlDataSave.ucNum)
                    {
                        RABM_RelSaveData(&g_aRabmPsEnt[i].UlDataSave, RABM_UPLINK); /* 释放存储的上行数据                       */
                    }
                    g_aRabmPsEnt[i].ucStpFlg = RABM_FALSE;
                    break;
                case RABM_NULL:
                case RABM_NSAPI_ACTIVE_PENDING:
                case RABM_NSAPI_ACTIVE_WITH_RAB:
                case RABM_ACT_PENDING_WITH_RAB:
                case RABM_DATA_TRANSFER_STOP:
                    break;
                default:
                    break;
            }
        }

        /*复位2G向3G切换使用的数传恢复标志:*/
        if ( VOS_TRUE == gRabm2GTo3GDataResume )
        {
            gRabm2GTo3GDataResume = VOS_FALSE;

            /*释放在2G向3G切换开始时SNDCP缓存的数据:*/
            SN_RabmClear2G3Share();
        }
    }
    else if (GMM_RABM_SERVICEREQ_DELAYED == pTempMsg->ulResult)
    {
        NAS_RabmStartTimer(RABM_TIMER_NAME_COMMON, RABM_TIMER_RESEND_EST_REQ);
    }
    else
    {
        /* 启动RB建立保护定时器 */
        for (i = 0; i < RABM_3G_PS_MAX_ENT_NUM; i++)
        {
            if ( (RABM_NSAPI_ACTIVE_NO_RAB == NAS_RABM_GetWPsEntState(i))
              && (VOS_TRUE == NAS_RABM_GetWPsEntRabReestFlg(i)) )
            {
                NAS_RABM_StartReestRabPendingTmr(i + RABM_NSAPI_OFFSET);
            }
        }
    }
    return;
}
VOS_VOID RABM_RcvSmActInd(
    VOS_VOID                           *pMsg                                    /* 当前处理的消息                           */
)
{
    RABMSM_ACTIVATE_IND_STRU           *pTempMsg;                               /* 定义局部变量                             */
    VOS_UINT8                           ucRabUpdFlg;                            /* 定义RAB参数                              */
    VOS_UINT8                           ucEntId;                                /* 定义局部变量                             */
    VOS_UINT32                          ulEntId;
    VOS_UINT32                          ulRabmEntNum;
    VOS_UINT8                           ucState;
    QCI_TYPE_ENUM_UINT8                 enQci;

    pTempMsg = (RABMSM_ACTIVATE_IND_STRU *)pMsg;                                /* 指向当前处理的消息                       */

    ucRabUpdFlg = RABM_FALSE;                                                   /* 初始化标志                               */
    ucEntId = (VOS_UINT8)(pTempMsg->ulNsapi - RABM_NSAPI_OFFSET);                   /* 获得PS域RABM实体索引                     */
    switch(g_aRabmPsEnt[ucEntId].ucState)
    {                                                                           /* 根据实体的状态进行相应的处理             */
    case RABM_NULL:
        PS_LOG1(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvSmActInd:NORMAL:Create RABM entity(EntId):",ucEntId);

        g_aRabmPsEnt[ucEntId].QoS.ulQosLength = pTempMsg->Qos.ulLength;         /* 保存参数QoS长度                          */
        PS_MEM_CPY(g_aRabmPsEnt[ucEntId].QoS.aucQosValue,
                   pTempMsg->Qos.aucQosValue,
                   NAS_RABM_MAX_QOS_LEN);                                      /* 保存参数QoS到RABM实体                    */

        if ((RABMSM_ACT_MSG_2 == pTempMsg->ulActMsgType)
         && (VOS_TRUE == NAS_RABM_GetDataSuspendFlg()))
        {
            if ( (g_aRabmPsEnt[ucEntId].QoS.aucQosValue[3] & NAS_RABM_QOS_DELIVERY_ERRORNEOUS_SDU_MASK)
                 != (pTempMsg->Qos.aucQosValue[3] & NAS_RABM_QOS_DELIVERY_ERRORNEOUS_SDU_MASK) )
            {                                                                       /* 参数DeliveryErrSdu发生改变               */
                ucRabUpdFlg = RABM_TRUE;                                            /* 设置RAB参数发生改变标志                  */
            }
            if( g_aRabmPsEnt[ucEntId].ucPppFlg != pTempMsg->ulPppFlag )
            {                                                                       /* 没有采用PPP协议                          */
                ucRabUpdFlg = RABM_TRUE;                                            /* 设置RAB参数发生改变标志                  */
            }

            g_aRabmPsEnt[ucEntId].QoS.ulQosLength = pTempMsg->Qos.ulLength;         /* 保存参数QoS长度                          */
            PS_MEM_CPY(g_aRabmPsEnt[ucEntId].QoS.aucQosValue,
                       pTempMsg->Qos.aucQosValue,
                       NAS_RABM_MAX_QOS_LEN);                      /* 保存参数QoS到RABM实体                    */

            /* data suspend状态收到第二条pdp激活消息，设置w状态为RABM_DATA_TRANSFER_STOP */
            RABM_SetWState(ucEntId, RABM_DATA_TRANSFER_STOP);                      /* 状态迁移到RABM_DATA_TRANSFER_STOP       */
            PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvSmActInd:NORMAL:RABM_NULL ==> RABM_DATA_TRANSFER_STOP");

            if( RABM_TRUE == ucRabUpdFlg )
            {                                                                       /* RAB参数发生改变                          */
                RABM_SndRrcQosUpdReq(ucEntId);                                      /* 通知RRC                                  */
            }

            /*创建RAB_MAP映射实体*/
            NAS_RABM_CreateRabMapEntity((VOS_UINT8)(pTempMsg->ulNsapi),
                                        (VOS_UINT8)(pTempMsg->ulLinkdNsapi),
                                        (VOS_UINT8)(pTempMsg->ulNsapi));

            /* 给CDS发送消息通知CDS QOS信息 */
            enQci = NAS_RABM_GetQciFromQos(g_aRabmPsEnt[ucEntId].QoS.ulQosLength,
                                           g_aRabmPsEnt[ucEntId].QoS.aucQosValue);

            NAS_RABM_SndCdsQosFcRabCreateInd(ucEntId + RABM_NSAPI_OFFSET, enQci);
        }
        else
        {
            RABM_SetWState(ucEntId, RABM_NSAPI_ACTIVE_PENDING);                     /* 状态迁移到RABM_NSAPI_ACTIVE_PENDING      */
            PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvSmActInd:NORMAL:RABM state: RABM_NULL ==> RABM_NSAPI_ACTIVE_PENDING");
        }

        if(RABM_SM_PPP_PROT == pTempMsg->ulPppFlag)
        {
            g_aRabmPsEnt[ucEntId].ucPppFlg = RABM_SM_PPP_PROT;
        }
        else
        {
            g_aRabmPsEnt[ucEntId].ucPppFlg = RABM_SM_IP_PROT;
        }

        break;
    case RABM_NSAPI_ACTIVE_PENDING:
        if(RABMSM_ACT_MSG_1 == pTempMsg->ulActMsgType)
        {
            break;
        }

        if ( (g_aRabmPsEnt[ucEntId].QoS.aucQosValue[3] & NAS_RABM_QOS_DELIVERY_ERRORNEOUS_SDU_MASK)
             != (pTempMsg->Qos.aucQosValue[3] & NAS_RABM_QOS_DELIVERY_ERRORNEOUS_SDU_MASK) )
        {                                                                       /* 参数DeliveryErrSdu发生改变               */
            ucRabUpdFlg = RABM_TRUE;                                            /* 设置RAB参数发生改变标志                  */
        }
        if( g_aRabmPsEnt[ucEntId].ucPppFlg != pTempMsg->ulPppFlag )
        {                                                                       /* 没有采用PPP协议                          */
            ucRabUpdFlg = RABM_TRUE;                                            /* 设置RAB参数发生改变标志                  */
        }

        g_aRabmPsEnt[ucEntId].QoS.ulQosLength = pTempMsg->Qos.ulLength;         /* 保存参数QoS长度                          */
        PS_MEM_CPY(g_aRabmPsEnt[ucEntId].QoS.aucQosValue,
                   pTempMsg->Qos.aucQosValue,
                   NAS_RABM_MAX_QOS_LEN);                      /* 保存参数QoS到RABM实体                    */

        RABM_SetWState(ucEntId, RABM_NSAPI_ACTIVE_NO_RAB);                      /* 状态迁移到RABM_NSAPI_ACTIVE_NO_RAB       */

        PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvSmActInd:NORMAL:RABM_NSAPI_ACTIVE_PENDING ==> RABM_NSAPI_ACTIVE_NO_RAB");
        if( RABM_TRUE == ucRabUpdFlg )
        {                                                                       /* RAB参数发生改变                          */
            RABM_SndRrcQosUpdReq(ucEntId);                                      /* 通知RRC                                  */
        }

        /*创建RAB_MAP映射实体*/
        NAS_RABM_CreateRabMapEntity((VOS_UINT8)(pTempMsg->ulNsapi),
                                    (VOS_UINT8)(pTempMsg->ulLinkdNsapi),
                                    (VOS_UINT8)(pTempMsg->ulNsapi));

        /* 给CDS发送消息通知CDS QOS信息 */

        enQci = NAS_RABM_GetQciFromQos(g_aRabmPsEnt[ucEntId].QoS.ulQosLength,
                                       g_aRabmPsEnt[ucEntId].QoS.aucQosValue);

        NAS_RABM_SndCdsQosFcRabCreateInd(ucEntId + RABM_NSAPI_OFFSET, enQci);


        /*如果当前只存在一个PDP激活的实体，则认为当前开始有PDP激活
          此时如果FAST DORMANCY开启，则需要启动FAST DORMANCY*/
        ulRabmEntNum = 0;
        for (ulEntId = 0; ulEntId < RABM_3G_PS_MAX_ENT_NUM; ulEntId++)
        {
            ucState = NAS_RABM_GetWPsEntState(ulEntId);
            if ( (RABM_NSAPI_ACTIVE_NO_RAB == ucState)
              || (RABM_NSAPI_ACTIVE_WITH_RAB == ucState)
              || (RABM_DATA_TRANSFER_STOP == ucState) )
            {
                ulRabmEntNum++;
            }
        }

        if (1 == ulRabmEntNum)
        {
            NAS_RABM_ResumeFastDorm();
        }

        break;
    case RABM_ACT_PENDING_WITH_RAB:

        /* 保存参数QoS长度 */
        g_aRabmPsEnt[ucEntId].QoS.ulQosLength = pTempMsg->Qos.ulLength;

        PS_MEM_CPY(g_aRabmPsEnt[ucEntId].QoS.aucQosValue,
                   pTempMsg->Qos.aucQosValue,
                   NAS_RABM_MAX_QOS_LEN);


        /* 状态迁移到RABM_NSAPI_ACTIVE_WITH_RAB     */
        RABM_SetWState(ucEntId, RABM_NSAPI_ACTIVE_WITH_RAB);

        /*如果当前只存在一个PDP激活的实体，则认为当前开始有PDP激活
          此时如果FAST DORMANCY开启，则需要启动FAST DORMANCY*/
        ulRabmEntNum = 0;
        for (ulEntId = 0; ulEntId < RABM_3G_PS_MAX_ENT_NUM; ulEntId++)
        {
            ucState = NAS_RABM_GetWPsEntState(ulEntId);
            if ( (RABM_NSAPI_ACTIVE_NO_RAB == ucState)
              || (RABM_NSAPI_ACTIVE_WITH_RAB == ucState)
              || (RABM_DATA_TRANSFER_STOP == ucState) )
            {
                ulRabmEntNum++;
            }
        }

        if (1 == ulRabmEntNum)
        {
            NAS_RABM_ResumeFastDorm();
        }


        PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvSmActInd:NORMAL:RABM state: RABM_ACT_PENDING_WITH_RAB ==> RABM_NSAPI_ACTIVE_WITH_RAB ");
        if(0 != g_aRabmPsEnt[ucEntId].DlDataSave.ucNum)
        {
            RABM_SaveDataProcess(ucEntId,RABM_DOWNLINK);
        }

        /*创建RAB_MAP映射实体*/
        NAS_RABM_CreateRabMapEntity((VOS_UINT8)(pTempMsg->ulNsapi),
                                    (VOS_UINT8)(pTempMsg->ulLinkdNsapi),
                                    g_aRabmPsEnt[ucEntId].RabInfo.aucRbId[0]);

        /* 给CDS发送消息通知CDS QOS信息 */

        enQci = NAS_RABM_GetQciFromQos(g_aRabmPsEnt[ucEntId].QoS.ulQosLength,
                                       g_aRabmPsEnt[ucEntId].QoS.aucQosValue);

        NAS_RABM_SndCdsQosFcRabCreateInd(ucEntId + RABM_NSAPI_OFFSET, enQci);


        break;
    case RABM_NSAPI_ACTIVE_WITH_RAB:
    case RABM_NSAPI_ACTIVE_NO_RAB:
        break;
    case RABM_DATA_TRANSFER_STOP:
        if(RABM_FALSE == g_aRabmPsEnt[ucEntId].ucStpFlg)
        {
            g_aRabmPsEnt[ucEntId].ucStpFlg = RABM_TRUE;
            g_aRabmPsEnt[ucEntId].QoS.ulQosLength = pTempMsg->Qos.ulLength;     /* 保存参数QoS长度                          */
            PS_MEM_CPY(g_aRabmPsEnt[ucEntId].QoS.aucQosValue,
                       pTempMsg->Qos.aucQosValue,
                       NAS_RABM_MAX_QOS_LEN);                  /* 保存参数QoS到RABM实体                    */
        }
        break;
    default:
        break;
    }

    return;
}

/*******************************************************************************
  Module:      RABM_SaveDataProcess
  Function:    处理缓存的数据
  Input:       VOS_UINT8 ucEntId        实体ID
               VOS_UINT8 ucUlFlg        是否处理上行数据
  Output:      VOS_VOID
  NOTE:
  Return:      VOS_VOID
  History:
      1.  张志勇     2003.12.11   新规作成
*******************************************************************************/
VOS_VOID RABM_SaveDataProcess(
                          VOS_UINT8  ucEntId,                                       /* 实体ID                                   */
                          VOS_UINT8  ucUlFlg                                        /* 是否处理上行数据                         */
                          )
{
    PDCP_RABM_DATA_BUF_STRU  *pTempData1 = RABM_POINTER_NULL;                   /* 定义局部变量                             */
    TAFRABM_PS_DATA_REQ_STRU *pTempData3 = RABM_POINTER_NULL;                   /* 定义局部变量                             */
    RABM_DATA_LINK_STRU      *pTempData2 = RABM_POINTER_NULL;                   /* 定义局部变量                             */
    RABM_DATA_SAVE_STRU      *pTemp = RABM_POINTER_NULL;
    VOS_UINT8                    i;

    if( RABM_UPLINK == ucUlFlg )
    {
        pTemp = &(g_aRabmPsEnt[ucEntId].UlDataSave);
        for( i = 0; i < pTemp->ucNum; i++ )
        {                                                                       /* 遍历缓存的数据消息                       */
            pTempData3=(TAFRABM_PS_DATA_REQ_STRU *)pTemp->Data.pData;                                       /* 数据首地址                               */
            pTempData2=pTemp->Data.pNxt;                                        /* 下一个数据节点地址                       */
            RABM_SndPdcpDataReq(ucEntId,pTempData3);                            /* 向低层发送数据                           */
            RABM_Free(pTempData3);                                              /* 释放缓存的消息                           */
            if( pTempData2 == RABM_POINTER_NULL )
            {
                pTemp->ucNum = 0;                                               /* 缓存的数据消息个数清0                    */
                pTemp->Data.pData = RABM_POINTER_NULL;                          /* 指针清0                                  */
                pTemp->Data.pNxt = RABM_POINTER_NULL;                           /* 指针清0                                  */
            }
            else
            {
                pTemp->Data.pNxt = pTempData2->pNxt;                            /* 更新缓存的数据地址                       */
                pTemp->Data.pData = pTempData2->pData;                          /* 更新缓存的数据地址                       */
                RABM_Free(pTempData2);
            }
        }
    }
    else
    {
        pTemp = &(g_aRabmPsEnt[ucEntId].DlDataSave);
        for( i = 0; i < pTemp->ucNum; i++ )
        {                                                                       /* 遍历缓存的数据消息                       */
            pTempData1=(PDCP_RABM_DATA_BUF_STRU *)pTemp->Data.pData;            /* 数据首地址                               */
            pTempData2=pTemp->Data.pNxt;                                        /* 下一个数据节点地址                       */
            RABM_SndTafPsDataInd(ucEntId, pTempData1);                          /* 向高层发送数据                           */
            RABM_PdcpFree(pTempData1->pucData);                                 /* 释放缓存的数据                           */
            RABM_PdcpFree(pTempData1);                                          /* 释放缓存的消息                           */
            if( pTempData2 == RABM_POINTER_NULL )
            {
                pTemp->ucNum = 0;                                               /* 缓存的数据消息个数清0                    */
                pTemp->Data.pData = RABM_POINTER_NULL;                          /* 指针清0                                  */
                pTemp->Data.pNxt = RABM_POINTER_NULL;                           /* 指针清0                                  */
            }
            else
            {
                pTemp->Data.pNxt = pTempData2->pNxt;                            /* 更新缓存的数据地址                       */
                pTemp->Data.pData = pTempData2->pData;                          /* 更新缓存的数据地址                       */
                RABM_Free(pTempData2);
            }
        }
    }
}


VOS_VOID RABM_RcvSmDeactInd(
                        VOS_VOID  *pMsg                                             /* 当前处理的消息                           */
                        )
{
    RABMSM_DEACTIVATE_IND_STRU         *pTempMsg;                                    /* 定义局部变量                             */
    VOS_UINT8                           i;
    VOS_UINT32                          ulNsapi;                                      /* 定义局部变量                             */
    VOS_UINT32                          ulEntId;
    VOS_UINT32                          ulRabmEntNum;
    VOS_UINT8                           ucState;

    pTempMsg = (RABMSM_DEACTIVATE_IND_STRU *)pMsg;                              /* 指向当前处理的消息                       */
    for( i = 0; i < pTempMsg->ulDeactiveNum; i++ )
    {                                                                           /* 遍历去激活的Nsapi                        */
        ulNsapi = pTempMsg->aulNsapiList[i];                                    /* 获得NSAPI                                */
        if(ulNsapi < RABM_NSAPI_OFFSET)
        {
            continue;
        }

        if(RABM_NULL == g_aRabmPsEnt[ulNsapi-RABM_NSAPI_OFFSET].ucState)
        {                                                                       /* 实体的状态为非激活                       */
            ;
        }
        else
        {
            RABM_SetWState((VOS_UINT8)(ulNsapi-RABM_NSAPI_OFFSET), RABM_NULL);               /* 状态清空                                 */
            PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvSmDeactInd:NORMAL:RABM state = RABM_NULL ");
            PS_LOG1(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvSmDeactInd:NORMAL:Destroy RABM entity(EntId): ",(VOS_INT32)(ulNsapi-RABM_NSAPI_OFFSET));
            g_aRabmPsEnt[ulNsapi-RABM_NSAPI_OFFSET].RabInfo.ucRbNum = 0;        /* 初始化RB的个数为0                        */
            g_aRabmPsEnt[ulNsapi-RABM_NSAPI_OFFSET].ucPppFlg = RABM_SM_IP_PROT; /* 上层协议类型初始化为IP                   */

            if (VOS_TRUE == NAS_RABM_GetWPsEntRabReestFlg(ulNsapi - RABM_NSAPI_OFFSET))
            {
                /* 停止如果RB建立保护定时器 */
                NAS_RABM_StopReestRabPendingTmr((VOS_UINT8)ulNsapi);

                /* 清除RABM实体的重建标识 */
                NAS_RABM_ClearWPsEntRabReestFlg(ulNsapi - RABM_NSAPI_OFFSET);
            }

            /*清空QoS:*/
            g_aRabmPsEnt[ulNsapi-RABM_NSAPI_OFFSET].QoS.ulQosLength = 0;        /* 清空QoS                                  */
            PS_MEM_SET(g_aRabmPsEnt[ulNsapi-RABM_NSAPI_OFFSET].QoS.aucQosValue,
                       0x00,
                       sizeof(g_aRabmPsEnt[ulNsapi-RABM_NSAPI_OFFSET].QoS.aucQosValue));

            RABM_RelSaveData(&g_aRabmPsEnt[ulNsapi-RABM_NSAPI_OFFSET].DlDataSave, RABM_DOWNLINK);
            RABM_RelSaveData(&g_aRabmPsEnt[ulNsapi-RABM_NSAPI_OFFSET].UlDataSave, RABM_UPLINK);

            /* 发送消息给CDS通知CDS PDP已去激活 */
            NAS_RABM_SndCdsQosFcRabReleaseInd((VOS_UINT8)ulNsapi);

            /* 通知CDS释放缓存数据 */
            NAS_RABM_SndCdsFreeBuffDataInd((VOS_UINT8)ulNsapi);
        }

        /*删除RAB_MAP实体*/
        RABM_DelRabMap((VOS_UINT8)ulNsapi);
    }

    /*如果当前已经没有PDP激活的实体，则挂起FAST DORMANCY*/
    ulRabmEntNum = 0;
    for (ulEntId = 0; ulEntId < RABM_3G_PS_MAX_ENT_NUM; ulEntId++)
    {
        ucState = NAS_RABM_GetWPsEntState(ulEntId);
        if ( (RABM_NSAPI_ACTIVE_NO_RAB == ucState)
          || (RABM_NSAPI_ACTIVE_WITH_RAB == ucState)
          || (RABM_DATA_TRANSFER_STOP == ucState) )
        {
            ulRabmEntNum++;
        }
    }

    if (0 == ulRabmEntNum)
    {
        NAS_RABM_SuspendFastDorm();

        /* 停止RAB重建保护定时器 */
        if (VOS_TRUE == NAS_RABM_GetRabRsestTimerFlg())
        {
            RABM_TimerStop(0);
            NAS_RABM_ClearRabRsestTimerFlg();
        }
    }

    return;
}
VOS_VOID RABM_RcvSmModInd(
    VOS_VOID                           *pMsg                                               /* 当前处理的消息                           */
)
{
    RABMSM_MODIFY_IND_STRU             *pTempMsg;                               /* 定义局部变量                             */
    VOS_UINT8                           ucRabUpdFlg;                            /* 定义RAB参数                              */
    VOS_UINT8                           ucEntIndex;                             /* 定义局部变量                             */
    QCI_TYPE_ENUM_UINT8                 enQci;

    pTempMsg = (RABMSM_MODIFY_IND_STRU *)pMsg;                                  /* 指向当前处理的消息                       */

    ucRabUpdFlg = RABM_FALSE;                                                   /* 初始化标志                               */
    ucEntIndex = (VOS_UINT8)(pTempMsg->ulNsapi-RABM_NSAPI_OFFSET);                  /* 获得PS域RABM实体索引                     */
    switch(g_aRabmPsEnt[ucEntIndex].ucState)
    {                                                                           /* 根据实体的状态进行相应的处理             */
    case RABM_NULL:
        break;
    case RABM_NSAPI_ACTIVE_PENDING:
    case RABM_NSAPI_ACTIVE_NO_RAB:

        g_aRabmPsEnt[ucEntIndex].QoS.ulQosLength = pTempMsg->Qos.ulLength;      /* 保存参数QoS长度                          */
        PS_MEM_CPY(g_aRabmPsEnt[ucEntIndex].QoS.aucQosValue,
                   pTempMsg->Qos.aucQosValue,
                   NAS_RABM_MAX_QOS_LEN);                      /* 保存参数QoS到RABM实体                    */

        /* 给CDS发送消息通知CDS QOS信息 */

        enQci = NAS_RABM_GetQciFromQos(g_aRabmPsEnt[ucEntIndex].QoS.ulQosLength,
                                       g_aRabmPsEnt[ucEntIndex].QoS.aucQosValue);

        NAS_RABM_SndCdsQosFcRabCreateInd(ucEntIndex + RABM_NSAPI_OFFSET, enQci);

        break;

    case RABM_NSAPI_ACTIVE_WITH_RAB:
    case RABM_DATA_TRANSFER_STOP:
    case RABM_ACT_PENDING_WITH_RAB:

        if ( (g_aRabmPsEnt[ucEntIndex].QoS.aucQosValue[3] & NAS_RABM_QOS_DELIVERY_ERRORNEOUS_SDU_MASK)
             != (pTempMsg->Qos.aucQosValue[3] & NAS_RABM_QOS_DELIVERY_ERRORNEOUS_SDU_MASK))
        {                                                                       /* 参数DeliveryErrSdu发生改变               */
            ucRabUpdFlg = RABM_TRUE;                                            /* 设置RAB参数发生改变标志                  */
        }

        g_aRabmPsEnt[ucEntIndex].QoS.ulQosLength = pTempMsg->Qos.ulLength;      /* 保存参数QoS长度                          */
        PS_MEM_CPY(g_aRabmPsEnt[ucEntIndex].QoS.aucQosValue,
                   pTempMsg->Qos.aucQosValue,
                   NAS_RABM_MAX_QOS_LEN);                      /* 保存参数QoS到RABM实体                    */

        if( RABM_TRUE == ucRabUpdFlg )
        {                                                                       /* RAB参数发生改变                          */
            RABM_SndRrcQosUpdReq(ucEntIndex);                                   /* 通知RRC                                  */
        }

        /* 给CDS发送消息通知CDS QOS信息 */

        enQci = NAS_RABM_GetQciFromQos(g_aRabmPsEnt[ucEntIndex].QoS.ulQosLength,
                                       g_aRabmPsEnt[ucEntIndex].QoS.aucQosValue);

        NAS_RABM_SndCdsQosFcRabCreateInd(ucEntIndex + RABM_NSAPI_OFFSET, enQci);

        break;
    default:
        break;
    }

    return;
}


VOS_VOID RABM_RcvRrcStatusInd(
                          VOS_VOID  *pMsg
                          )
{
    RRRABM_STATUS_IND_STRU *pTempMsg = RABM_POINTER_NULL;
    VOS_UINT8  ucNsapi;
    VOS_UINT8  ucEntId;
    VOS_UINT8  i;

    pTempMsg = (RRRABM_STATUS_IND_STRU *)pMsg;

    if(RRC_RAB_STATUS_STOP == pTempMsg->ulStatus)
    {                                                                           /* 停止数据传输                             */
        for(i = 0; i < pTempMsg->ulRabCnt; i++)
        {                                                                       /* 执行指示停止数据传输的所有rb             */
            if(RABM_CN_PS == pTempMsg->aRab[i].enCnDomainId)
            {                                                                   /* 是ps域的rabm实体                         */
                ucNsapi = (VOS_UINT8)(pTempMsg->aRab[i].ulRabId & 0x0f);            /* 得到NSAPI                                */
                ucEntId = (VOS_UINT8)(ucNsapi - RABM_NSAPI_OFFSET);                 /* 得到实体下标                             */
                switch(g_aRabmPsEnt[ucEntId].ucState)
                {                                                               /* PS实体的状态                             */
                case RABM_NULL:
                case RABM_NSAPI_ACTIVE_PENDING:
                case RABM_DATA_TRANSFER_STOP:
                case RABM_NSAPI_ACTIVE_NO_RAB:
                    break;
                case RABM_ACT_PENDING_WITH_RAB:
                    RABM_SetWState(ucEntId, RABM_DATA_TRANSFER_STOP);
                    PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvRrcStatusInd:NORMAL:RABM state: RABM_ACT_PENDING_WITH_RAB ==> RABM_DATA_TRANSFER_STOP");
                    g_aRabmPsEnt[ucEntId].ucStpFlg = RABM_FALSE;
                    break;
                case RABM_NSAPI_ACTIVE_WITH_RAB:
                    RABM_SetWState(ucEntId, RABM_DATA_TRANSFER_STOP);
                    PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvRrcStatusInd:NORMAL:RABM state: RABM_NSAPI_ACTIVE_WITH_RAB ==> RABM_DATA_TRANSFER_STOP");
                    g_aRabmPsEnt[ucEntId].ucStpFlg = RABM_TRUE;
                    break;
                case RABM_TC_STATE_NULL:
                    break;
                default:
                    break;
                }
                if(RABM_TC_NO_START != g_RabmTcInf.ucTcStartSta)
                {
                    RABM_TcRbStatus(pTempMsg, i, TC_RAB_RELEASE);
                }
            }
        }
    }
    else
    {                                                                           /* 继续进行数据传输                         */
        for(i = 0; i < pTempMsg->ulRabCnt; i++)
        {
            if(RABM_CN_PS == pTempMsg->aRab[i].enCnDomainId)
            {                                                                   /* 是ps域的rabm实体                         */
                ucNsapi = (VOS_UINT8)(pTempMsg->aRab[i].ulRabId & 0x0f);            /* 得到NSAPI                                */
                ucEntId = (VOS_UINT8)(ucNsapi - RABM_NSAPI_OFFSET);                 /* 得到实体下标                             */
                switch(g_aRabmPsEnt[ucEntId].ucState)
                {                                                               /* 是ps域的rabm实体                         */
                case RABM_NULL:
                case RABM_NSAPI_ACTIVE_PENDING:
                case RABM_NSAPI_ACTIVE_WITH_RAB:
                case RABM_NSAPI_ACTIVE_NO_RAB:
                case RABM_ACT_PENDING_WITH_RAB:
                    break;
                case RABM_DATA_TRANSFER_STOP:
                    if(RABM_TRUE == g_aRabmPsEnt[ucEntId].ucStpFlg)
                    {
                        if(0 != g_aRabmPsEnt[ucEntId].UlDataSave.ucNum)
                        {                                                       /* 有缓存的上行数据                         */
                            RABM_SaveDataProcess(ucEntId,RABM_UPLINK);          /* 处理缓存的上行数据                       */
                        }
                        if(0 != g_aRabmPsEnt[ucEntId].DlDataSave.ucNum)
                        {                                                       /* 有缓存的下行数据                         */
                            RABM_SaveDataProcess(ucEntId,RABM_DOWNLINK);        /* 处理缓存的下行数据                       */
                        }
                        RABM_SetWState(ucEntId, RABM_NSAPI_ACTIVE_WITH_RAB);
                        PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvRrcStatusInd:NORMAL:RABM state: RABM_DATA_TRANSFER_STOP ==> RABM_NSAPI_ACTIVE_WITH_RAB");
                        g_aRabmPsEnt[ucEntId].ucStpFlg = RABM_FALSE;

                        /* 通知CDS可以发送数据 */
                        NAS_RABM_SndCdsSendBuffDataInd(ucEntId + RABM_NSAPI_OFFSET,
                                                       CDS_RABM_SEND_BUFF_DATA_ALLOWED_TYPE_SERVICE_SUCC);

                    }
                    else
                    {
                        RABM_SetWState(ucEntId, RABM_ACT_PENDING_WITH_RAB);
                        PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvRrcStatusInd:NORMAL:RABM state: RABM_DATA_TRANSFER_STOP ==> RABM_ACT_PENDING_WITH_RAB");
                    }
                    break;
                case RABM_TC_STATE_NULL:
                    break;
                default:
                    break;
                }
            }
            if(RABM_TC_NO_START != g_RabmTcInf.ucTcStartSta)
            {
                RABM_TcRbStatus(pTempMsg, i, TC_RAB_SETUP);
            }
        }
    }

}



/*******************************************************************************
  Module:   RABM_TcDataQInit
  Function: 环回测试中，PDCP->RABM数据队列的初始化
  Input:    VOS_VOID
  Output:   VOS_VOID
  NOTE:
  Return:   VOS_VOID
  History:

*******************************************************************************/
VOS_VOID RABM_TcDataQInit(VOS_VOID)
{
    RABM_Memset(&g_stPdcpRabmTcDataQ, 0, sizeof(g_stPdcpRabmTcDataQ));

    TTF_LinkInit(WUEPS_PID_RABM, &g_stPdcpRabmTcDataQ.stDataQ);

    g_stPdcpRabmTcDataQ.stCfg.ulOnceDealMaxCnt = RABM_PDCP_ONCE_DEAL_MAX_CNT;
}

/*******************************************************************************
  Module:   RABM_ClearTcDataQ
  Function: 环回测试中，清空PDCP->RABM数据队列内容
  Input:    VOS_VOID
  Output:   VOS_VOID
  NOTE:
  Return:   VOS_VOID
  History:

*******************************************************************************/
VOS_VOID RABM_ClearTcDataQ(VOS_VOID)
{
    VOS_INT32       lLockKey;
    VOS_UINT32      ulRemainCnt;
    PDCP_RABM_TC_DATA_Q_NODE_STRU *pstNode;

    for(;;)
    {
        lLockKey    = VOS_SplIMP();

        if ( 0 == TTF_Q_CNT(&g_stPdcpRabmTcDataQ.stDataQ) )
        {
            VOS_Splx(lLockKey);
            break;
        }

        VOS_Splx(lLockKey);

        pstNode  = (PDCP_RABM_TC_DATA_Q_NODE_STRU *)TTF_LinkSafeRemoveHead(WUEPS_PID_RABM, &g_stPdcpRabmTcDataQ.stDataQ, &ulRemainCnt);

        TTF_MemFree(WUEPS_PID_RABM, pstNode->pstDataMem);
        RABM_Free(pstNode);

    }
} /* RABM_ClearTcDataQ */

VOS_UINT32 RABM_EnqueueTcData(PDCP_RABM_TC_DATA_Q_NODE_STRU *pstNode)
{
    VOS_UINT32  ulNonEmptyEvent;    /* 记录队列是否发生了由空到非空的转变 */
    TTF_LINK_ST *pstDataQ;
    VOS_UINT32  ulResult;


    ulNonEmptyEvent = PS_FALSE;

    pstDataQ        = &g_stPdcpRabmTcDataQ.stDataQ;

    /*将TTF_MEM_ST结点插入队列尾部*/
    ulResult    = TTF_LinkSafeInsertTail(WUEPS_PID_RABM, pstDataQ, &(pstNode->stQNode), &ulNonEmptyEvent);
    if(PS_SUCC != ulResult)
    {
        TTF_MemFree(WUEPS_PID_RABM, pstNode->pstDataMem);
        RABM_Free(pstNode);
        RABM_LOG_WARNING("RABM_EnqueueTcData:WARNING:Insert Node fail!");
        return PS_FAIL;
    }

    if (TTF_LINK_CNT(pstDataQ) > g_stPdcpRabmTcDataQ.stStat.ulQMaxCnt)
    {
        g_stPdcpRabmTcDataQ.stStat.ulQMaxCnt = TTF_LINK_CNT(pstDataQ);
    }

    if (PS_TRUE == ulNonEmptyEvent)
    {
        /*向RABM发送数据处理指示*/
       if (PS_SUCC != NAS_UTRANCTRL_RABM_SendTcDataNotify())
       {
            /* 发送消息通知失败，需要清空整个队列 */
            RABM_ClearTcDataQ();
            return PS_FAIL;
       }
    }

    /*统计信息*/
    g_stPdcpRabmTcDataQ.stStat.ulDlTotalCnt++;

    return PS_SUCC;
} /* RABM_EnqueueTcData */


VOS_VOID RABM_RcvPsTcDataInd(
                       VOS_VOID  *pMsg                                              /* 当前处理的消息                           */
                       )
{
    PDCP_RABM_TC_DATA_STRU         *pstDataPacket;
    PDCP_RABM_TC_DATA_STRU          stDataPacket;
    VOS_UINT8                       ucEntId;
    PDCP_RABM_TC_DATA_Q_NODE_STRU  *pstDataQNode;

    VOS_INT32                       lLockKey;
    VOS_UINT32                      ulDealCnt = 0;
    VOS_UINT32                      ulRemainCnt;

    /*在新的NAS用户面架构下，非环回模式数据不再经过RABM处理，直接当作异常处理*/
    if(RABM_TC_NO_START == g_RabmTcInf.ucTcStartSta)
    {
        RABM_LOG_WARNING("RABM, RABM_RcvPsTcDataInd, Rx Data in not tc mode ");

        /*清空环回数据队列*/
        RABM_ClearTcDataQ();

        /*清空环回数据队列的统计数据*/
        RABM_Memset(&g_stPdcpRabmTcDataQ.stStat, 0, sizeof(PDCP_RABM_TC_DATA_Q_STAT_STRU));

        return;
    }

    for (;;)
    {
        /*取出队列中的结点*/
        lLockKey = VOS_SplIMP();

        /*若队列为空，则直接返回*/
        if ( 0 == TTF_LINK_CNT(&g_stPdcpRabmTcDataQ.stDataQ) )
        {
            VOS_Splx(lLockKey);
            return;
        }

        VOS_Splx(lLockKey);

        pstDataQNode = (PDCP_RABM_TC_DATA_Q_NODE_STRU *)TTF_LinkSafeRemoveHead(WUEPS_PID_RABM, &g_stPdcpRabmTcDataQ.stDataQ, &ulRemainCnt);

        if (VOS_NULL_PTR == pstDataQNode)
        {
            RABM_LOG_ERROR("ERROR: RABM_RcvPsTcDataInd, pstDataQNode is NULL!");
            return;
        }

        /*========处理该结点============*/
        /*先构造PDCP_RABM_TC_DATA_STRU的内部处理结构*/
        pstDataPacket = &stDataPacket;
        pstDataPacket->ucRbId       = pstDataQNode->ucRbId;
        pstDataPacket->ulDataBitLen = pstDataQNode->ulDataBitLen;
        pstDataPacket->pstDataMem   = pstDataQNode->pstDataMem;

        /*拷贝CRC信息(仅用于PS域环回模式二)*/
        PS_MEM_CPY(&pstDataPacket->stCrcInfo, &pstDataQNode->stCrcInfo, sizeof(pstDataQNode->stCrcInfo));


        /*RABM模块分环回和非环回两种情况分别处理PDCP的数据*/
        if ( (RABM_TC_LOOP_CLOSE == g_RabmTcInf.ucTcStartSta)
          && (VOS_FALSE == g_RabmTcInf.bFlowCtrlFlg) )
        {
            if (0 == g_RabmTcInf.ucLbCnt)
            {
                RABM_SndTcPsData(0, pstDataPacket);
            }
            else
            {
                for (ucEntId = 0; ucEntId < TC_LB_MAX_RBNUM; ucEntId++)
                {
                    if ( (pstDataPacket->ucRbId == g_RabmTcInf.aRbInf[ucEntId].ucRbId)
                      && (RABM_TRUE == g_RabmTcInf.aRbInf[ucEntId].ucEstFlg))
                    {
                        RABM_SndTcPsData(ucEntId, pstDataPacket);
                        break;
                    }
                }

                if(ucEntId == g_RabmTcInf.ucLbCnt)
                {
                    RABM_LOG1_ERROR("RABM, RABM_RcvPsTcDataInd, Can't find match Rb id <1>",
                                     pstDataPacket->ucRbId);
                }
            }
        }

        if (VOS_NULL_PTR != pstDataPacket->pstDataMem)
        {
            TTF_MemFree(WUEPS_PID_RABM, pstDataPacket->pstDataMem);
        }

        /*释放结点*/
        RABM_Free(pstDataQNode);

        /*统计*/
        ulDealCnt++;

        if ( ulDealCnt > g_stPdcpRabmTcDataQ.stStat.ulMaxCntOnce )
        {
            g_stPdcpRabmTcDataQ.stStat.ulMaxCntOnce = ulDealCnt;
        }

        /* 如果上次处理的是最后一个数据，则退出循环 */
        if (0 == ulRemainCnt)
        {
            return;
        }


        /*如果循环处理的结点个数超出了队列一次允许处理最大结点数，
          则退出循环并发送PPP_DATA_PROC_NOTIFY消息*/
        if ( ulDealCnt >= g_stPdcpRabmTcDataQ.stCfg.ulOnceDealMaxCnt )
        {
            ulDealCnt = 0;

            if (PS_SUCC != NAS_UTRANCTRL_RABM_SendTcDataNotify())
            {
                RABM_LOG_WARNING("RABM_RcvPsTcDataInd: WARNING: WTTF_PDCP_SendTcDataNotifyToRabm FAIL");
                continue;
            }
            else
            {
                return;
            }
        }
    } /* for (;;) */
}


VOS_VOID RABM_RcvRrcRabInd(VOS_VOID *pMsg)
{
    RRRABM_RAB_IND_STRU                *pTempMsg;
    VOS_UINT8                           ucRabNum = 0;
    VOS_UINT8                           i;
    VOS_UINT8                           ucEntId;
    VOS_UINT8                           ucCsRabCnt;
    VOS_UINT32                          ulRabInfoPsDomainFlg;
    NAS_RABM_FASTDORM_STATUS_ENUM_UINT32 enFastDormStatus;

    ucCsRabCnt = 0;
    pTempMsg = (RRRABM_RAB_IND_STRU *)pMsg;

    ulRabInfoPsDomainFlg = NAS_RABM_IsRabIndPsDonmain(pTempMsg);

    switch(pTempMsg->ulRabChangeType)
    {
    case NAS_RRC_RAB_SETUP_REQUEST:
        for(i = 0; i < pTempMsg->ulRabCnt; i++)
        {
            if(RABM_CN_PS == pTempMsg->aRabInfo[i].ulCnDomainId)
            {
                if ( (RABM_TC_NO_START != g_RabmTcInf.ucTcStartSta)
                  && ((pTempMsg->aRabInfo[i].ulRabId & 0x0f) < RABM_NSAPI_OFFSET) )
                {
                    ucEntId = (VOS_UINT8)(pTempMsg->aRabInfo[i].ulRabId & 0x0f);
                }
                else
                {
                    if ((pTempMsg->aRabInfo[i].ulRabId & 0x0f) < RABM_NSAPI_OFFSET)
                    {
                        continue;
                    }
                    ucEntId = (VOS_UINT8)((pTempMsg->aRabInfo[i].ulRabId & 0x0f)
                        - RABM_NSAPI_OFFSET);
                }

                if(ucEntId >= RABM_PS_MAX_ENT_NUM)
                {
                    ucRabNum = 0;
                    break;
                }
                switch(g_aRabmPsEnt[ucEntId].ucState)
                {
                case RABM_NULL:
                case RABM_NSAPI_ACTIVE_WITH_RAB:
                case RABM_DATA_TRANSFER_STOP:
                case RABM_ACT_PENDING_WITH_RAB:
                case RABM_NSAPI_ACTIVE_PENDING:
                case RABM_NSAPI_ACTIVE_NO_RAB:
                    ucRabNum++;
                    break;
                case RABM_TC_STATE_NULL:
                    ucRabNum++;
                    break;
                default:
                    break;
                }
            }
            else
            {
                ucCsRabCnt++;
                if(ucCsRabCnt > RABM_CS_MAX_ENT_NUM)
                {
                    ucRabNum = 0;
                }
                else
                {
                    ucRabNum++;
                }
            }
            if(0 == ucRabNum)
            {
                break;
            }
        }

        RABM_SndRrcRabRsp(ucRabNum , pTempMsg);
        break;
    case NAS_RRC_RAB_SETUP_SUCCESS:
        RABM_ComSaveRabPara (pTempMsg);

        if (VOS_TRUE == ulRabInfoPsDomainFlg)
        {
            /* if (当前是NAS_RABM_FASTDORM_RUNNING状态) */
            enFastDormStatus = NAS_RABM_GetCurrFastDormStatus();
            if ( (NAS_RABM_FASTDORM_STOP != enFastDormStatus)
              && (NAS_RABM_FASTDORM_PAUSE != enFastDormStatus)
              && (NAS_RABM_FASTDORM_SUSPEND != enFastDormStatus) )
            {
                /*流量计数清零*/
                NAS_RABM_ClrFastDormUlDataCnt();
                NAS_RABM_ClrFastDormDlDataCnt();

                /* 无流量计数值清0 */
                NAS_RABM_SetFastDormCurrNoFluxCntValue(0);

                /* 发送消息RRRABM_FASTDORM_STOP_REQ */
                NAS_RABM_SndWasFastDormStopReq();

                NAS_RabmStopTimer(RABM_TIMER_NAME_FD_RETRY, RABM_TIMER_FASTDORM_RETRY);
                NAS_RabmStopTimer(RABM_TIMER_NAME_COMMON,RABM_TIMER_FASTDORM_WAIT_GMM_QRY_RESULT);

                NAS_RABM_SetCurrFastDormStatus(NAS_RABM_FASTDORM_PAUSE);
                NAS_RABM_SndOmFastdormStatus();
            }
        }

        break;
    case NAS_RRC_RAB_SETUP_FAILURE:
        for(i = 0; i < pTempMsg->ulRabCnt; i++)
        {
            if(RABM_CN_PS == pTempMsg->aRabInfo[i].ulCnDomainId)
            {
                if ( (RABM_TC_NO_START != g_RabmTcInf.ucTcStartSta)
                  && ((pTempMsg->aRabInfo[i].ulRabId & 0x0f) < RABM_NSAPI_OFFSET) )
                {
                    ucEntId = (VOS_UINT8)(pTempMsg->aRabInfo[i].ulRabId & 0x0f);
                }
                else
                {
                    if ((pTempMsg->aRabInfo[i].ulRabId & 0x0f) < RABM_NSAPI_OFFSET)
                    {
                        continue;
                    }
                    ucEntId = (VOS_UINT8)((pTempMsg->aRabInfo[i].ulRabId & 0x0f)
                        - RABM_NSAPI_OFFSET);
                }

                if (VOS_TRUE == NAS_RABM_GetWPsEntRabReestFlg(ucEntId))
                {
                    /* 停止RB建立保护定时器 */
                    NAS_RABM_StopReestRabPendingTmr(ucEntId + RABM_NSAPI_OFFSET);

                    /* 清除RABM实体的重建标识 */
                    NAS_RABM_ClearWPsEntRabReestFlg(ucEntId);

                    RABM_RelSaveData(&g_aRabmPsEnt[ucEntId].DlDataSave,RABM_DOWNLINK);/* 释放存储的下行数据              */
                    g_aRabmPsEnt[ucEntId].ucStpFlg = RABM_FALSE;

                    /* 通知CDS释放缓存数据 */
                    NAS_RABM_SndCdsFreeBuffDataInd(ucEntId + RABM_NSAPI_OFFSET);
                }
            }
        }

        /*复位2G向3G切换使用的数传恢复标志:*/
        if ( VOS_TRUE == gRabm2GTo3GDataResume )
        {
            gRabm2GTo3GDataResume = VOS_FALSE;

            /*释放在2G向3G切换开始时SNDCP缓存的数据:*/
            SN_RabmClear2G3Share();
        }

        break;
    case NAS_RRC_RAB_RECONFIG:
        RABM_ComSaveRabPara(pTempMsg);
        break;
    case NAS_RRC_RAB_RELEASE:

        RABM_ComRlsRabReource(pTempMsg);
        for(i = 0; i < pTempMsg->ulRabCnt; i++)
        {
            if(RABM_CN_CS == pTempMsg->aRabInfo[i].ulCnDomainId)
            {
                API_CcSyncInd(D_RMC_RR_ACTION_RELEASE, pTempMsg->aRabInfo[i].ulRabId);
            }
        }

        break;
    default:
        break;
    }

    return;
}



VOS_VOID RABM_RcvRrcRelAllReq(VOS_VOID)
{
    VOS_UINT8                           ucEntId;
    VOS_UINT8                           ucChgMask;

    if( RABM_FALSE != g_ucReestTimerFlg )
    {
        RABM_TimerStop(0);
        g_ucReestTimerFlg = RABM_FALSE;
    }

    /*释放PS域的所有RAB*/
    for ( ucEntId = 0; ucEntId < RABM_PS_MAX_ENT_NUM; ucEntId++ )
    {
        switch(g_aRabmPsEnt[ucEntId].ucState)
        {
            case RABM_NULL:
            case RABM_NSAPI_ACTIVE_PENDING:
                break;
            case RABM_NSAPI_ACTIVE_NO_RAB:
                if (VOS_TRUE == NAS_RABM_GetWPsEntRabReestFlg(ucEntId))
                {
                    /* 停止RB建立保护定时器 */
                    NAS_RABM_StopReestRabPendingTmr(ucEntId + RABM_NSAPI_OFFSET);

                    /* 清除RABM实体的重建标识 */
                    NAS_RABM_ClearWPsEntRabReestFlg(ucEntId);

                    /* 通知CDS释放缓存数据 */
                    NAS_RABM_SndCdsFreeBuffDataInd(ucEntId + RABM_NSAPI_OFFSET);
                }
                break;
            case RABM_NSAPI_ACTIVE_WITH_RAB:
            case RABM_DATA_TRANSFER_STOP:

                if ( ((g_aRabmPsEnt[ucEntId].QoS.aucQosValue[3] & NAS_RABM_QOS_TRAFFIC_CLASS_MASK) == RABM_CONVERSATION_CLASS)
                  || ((g_aRabmPsEnt[ucEntId].QoS.aucQosValue[3] & NAS_RABM_QOS_TRAFFIC_CLASS_MASK) == RABM_STREAM_CLASS) )
                {
                    RABM_SndSmDeactReq((VOS_UINT8)(ucEntId + RABM_NSAPI_OFFSET));
                }
                g_aRabmPsEnt[ucEntId].RabInfo.ucRabId = 0xff;
                g_aRabmPsEnt[ucEntId].RabInfo.ucRbNum = 0;
                RABM_SetWState(ucEntId, RABM_NSAPI_ACTIVE_NO_RAB);
                PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvRrcRelAllReq:NORMAL:RABM state = RABM_NSAPI_ACTIVE_NO_RAB");

                /*更新RAB_MAP表中的ucAsId*/
                ucChgMask = 0;
                ucChgMask |= RAB_MAP_CHG_MASK_AS_ID;
                RABM_ChgRabMap(ucEntId + RABM_NSAPI_OFFSET,
                               ucChgMask,
                               g_aRabmPsEnt[ucEntId].RabInfo.aucRbId[0],
                               VOS_NULL_PTR,
                               VOS_NULL_PTR);
                break;
            case RABM_ACT_PENDING_WITH_RAB:
                RABM_SetWState(ucEntId, RABM_NSAPI_ACTIVE_PENDING);
                PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvRrcRelAllReq:NORMAL:RABM state: RABM_ACT_PENDING_WITH_RAB ==> RABM_NSAPI_ACTIVE_PENDING");
                g_aRabmPsEnt[ucEntId].RabInfo.ucRabId = 0xff;
                g_aRabmPsEnt[ucEntId].RabInfo.ucRbNum = 0;

                /*更新RAB_MAP表中的ucAsId*/
                ucChgMask = 0;
                ucChgMask |= RAB_MAP_CHG_MASK_AS_ID;
                RABM_ChgRabMap(ucEntId + RABM_NSAPI_OFFSET,
                               ucChgMask,
                               g_aRabmPsEnt[ucEntId].RabInfo.aucRbId[0],
                               VOS_NULL_PTR,
                               VOS_NULL_PTR);
                break;
            case RABM_TC_STATE_NULL:
                break;
            default:
                break;
        }

        g_aRabmPsEnt[ucEntId].ucStpFlg = RABM_FALSE;
        if(0 != g_aRabmPsEnt[ucEntId].DlDataSave.ucNum)
        {
            /* 释放存储的下行数据                       */
            RABM_RelSaveData(&g_aRabmPsEnt[ucEntId].DlDataSave, RABM_DOWNLINK);
        }
    }

    /*释放CS域的所有RAB*/
    for ( ucEntId = 0; ucEntId < RABM_CS_MAX_ENT_NUM; ucEntId++ )
    {
        switch(g_aRabmCsEnt[ucEntId].ucState)
        {
            case RABM_NULL:
                break;
            case RABM_SI_ACTIVE_WITH_RAB:
                RABM_SndCcSyncInd(RABMCC_RAB_INACTIVE,
                    (VOS_UINT32)g_aRabmCsEnt[ucEntId].RabInfo.ucRabId);
                g_aRabmCsEnt[ucEntId].ucState = RABM_NULL;
                PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvRrcRelAllReq:NORMAL:RABM state: RABM_SI_ACTIVE_WITH_RAB ==> RABM_NULL ");
                g_aRabmCsEnt[ucEntId].RabInfo.ucRabId = 0xff;
                g_aRabmCsEnt[ucEntId].RabInfo.ucRbNum = 0;
                break;
            default:
                break;
        }
    }

    /*释放TC的LB实体*/
    if(RABM_TC_NO_START != g_RabmTcInf.ucTcStartSta)
    {
        for ( ucEntId = 0; ucEntId < g_RabmTcInf.ucLbCnt; ucEntId++ )
        {
            if(RABM_TRUE == g_RabmTcInf.aRbInf[ucEntId].ucEstFlg)
            {
                g_RabmTcInf.aRbInf[ucEntId].ucEstFlg = RABM_FALSE;                    /* 标志该RB不可用                           */
                RABM_SndTcRabInd(ucEntId, TC_RAB_RELEASE);                            /* 通知TC该RB释放                           */
            }
        }
    }

    return;
}


/*******************************************************************************
  Module:   RABM_RcvPsDataReq
  Function: 根据PS域RABM实体的状态，进行相应的处理
  Input:    VOS_VOID    *pMsg           收到的消息
  Output:   VOS_VOID
  NOTE:
  Return:   VOS_VOID
  History:
      1. 张志勇      2003.12.11   新规作成
*******************************************************************************/
VOS_VOID RABM_RcvPsDataReq(
                       VOS_VOID  *pMsg
                       )
{
    NAS_WARNING_LOG(WUEPS_PID_RABM,
        "RABM_RcvPsDataReq: Receive data in wrong state!");

    /*直接丢弃该上行数据块:*/
    TTF_MemFree(WUEPS_PID_RABM, ((TAFRABM_PS_DATA_REQ_STRU *)pMsg)->pData);
}

/*******************************************************************************
  Module:   RABM_RcvCsDataReq
  Function: 根据CS域RABM实体的状态，进行相应的处理
  Input:    VOS_VOID    *pMsg           收到的消息
  Output:   VOS_VOID
  NOTE:
  Return:   VOS_VOID
  History:
      1. 张志勇      2003.12.11   新规作成
*******************************************************************************/
VOS_VOID RABM_RcvCsDataReq(
                       VOS_VOID  *pMsg
                       )
{
    VOS_UINT8  ucSi;
    VOS_UINT8  ucEntId;
    VOS_UINT16 usDataLen;

    usDataLen = 0;
    ucSi = (VOS_UINT8)(((TAFRABM_CS_DATA_REQ_STRU *)pMsg)->ulSi);                   /* 得到消息中的SI                           */
    ucEntId = g_aucSiMapEnt[ucSi];                                              /* 获得实体ID                               */
    if( 0xFF == ucEntId )
    {
        RABM_SndTafInActSiInd(ucSi);                                            /* 向APP回错误指示                          */
        return;
    }

    switch(g_aRabmCsEnt[ucEntId].ucState)
    {                                                                           /* 根据实体的状态进行相应的处理             */
    case RABM_NULL:
        RABM_SndTafInActSiInd(ucSi);                                            /* 向APP回错误指示                          */
        break;
    case RABM_SI_ACTIVE_WITH_RAB:
        usDataLen = (VOS_UINT16)((TAFRABM_CS_DATA_REQ_STRU *)pMsg)->ulDataLen;      /* usDataLen为字节长度                      */
        NAS_CsDomainDataReq((VOS_INT8)(g_aRabmCsEnt[ucEntId].RabInfo.aucRbId[0]),
            usDataLen,
            ((TAFRABM_CS_DATA_REQ_STRU *)pMsg)->aucData);
        break;
    default:
        break;
    }
    return;
}


VOS_VOID RABM_RcvRlcDataInd(
                        RLC_RABM_CS_DATA_IND_STRU  *pRcvData
                        )
{
    VOS_UINT8    i;
    VOS_UINT8    j;
    VOS_UINT8    ucDataValid;


    if (RABM_TC_NO_START != g_RabmTcInf.ucTcStartSta)
    {
        if (RABM_TC_LOOP_CLOSE == g_RabmTcInf.ucTcStartSta)
        {
            if (0 == g_RabmTcInf.ucLbCnt)
            {
                RABM_SndTcCsData(0, pRcvData);
            }
            else
            {
                for (i = 0; i < TC_LB_MAX_RBNUM; i++)
                {
                    if ( (pRcvData->ucRbId == g_RabmTcInf.aRbInf[i].ucRbId)
                      && (RABM_TRUE == g_RabmTcInf.aRbInf[i].ucEstFlg))
                    {
                        RABM_SndTcCsData(i, pRcvData);
                    }
                }
            }
        }


        RABM_RlcFree(pRcvData->pucData);

        return;
    }
    ucDataValid = RABM_FALSE;                                                   /* 初始化为数据无效                         */
    for(i = 0; i < RABM_CS_MAX_ENT_NUM; i++)
    {
        for(j = 0; j < g_aRabmCsEnt[i].RabInfo.ucRbNum; j++)
        {
            if(g_aRabmCsEnt[i].RabInfo.aucRbId[j] == ((VOS_UINT8)pRcvData->ucRbId))
            {
                ucDataValid = RABM_TRUE;                                        /* 设置数据有效                             */
                RABM_SndTafCsDataInd(pRcvData, i);                              /* 向APP发送数据                            */
                break;
            }
        }
        if(ucDataValid == RABM_TRUE)
        {
            break;
        }
    }

    RABM_RlcFree(pRcvData->pucData);

    return;
}
VOS_VOID RABM_RcvTcTestReq(
                       TCRABM_TEST_REQ_STRU  *pRcvMsg
                       )
{
    VOS_UINT8            *pMsg;
    TCRABM_TEST_CNF_STRU *pSndMsg;
    VOS_UINT8    i;
    VOS_UINT8    j;
    VOS_UINT8    ucRbCnt;
    pMsg = (VOS_UINT8 *)PS_ALLOC_MSG_WITH_HEADER_LEN(WUEPS_PID_RABM,
                                            sizeof(TCRABM_TEST_CNF_STRU));
    if (VOS_NULL_PTR == pMsg)
    {
        PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_ERROR, "RABM_RcvTcTestReq:ERROR:Alloc msg fail!");
        return;
    }
    pSndMsg = (TCRABM_TEST_CNF_STRU *)pMsg;

    pSndMsg->ulRabCnt = 0;                                                      /* 初始化RAB的个数                          */

    if(TC_RB_TEST_ACTIVE == pRcvMsg->ulMode)
    {
        g_RabmTcInf.ucTcStartSta = RABM_TC_LOOP_OPEN;                           /* 记录TC已经启动                           */
        for(i = 0; i < RABM_PS_MAX_ENT_NUM; i++)
        {                                                                       /* 查找PS域的RB信息                         */
            if(RABM_NULL == g_aRabmPsEnt[i].ucState)
            {                                                                   /* RB还没有建立                             */
                RABM_SetWState(i, RABM_TC_STATE_NULL);                          /* 标识可以建立用于TC测试的RB               */
                PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvTcTestReq:NORMAL:RABM state: RABM_NULL ==> RABM_TC_STATE_NULL");
            }
            else if((RABM_NSAPI_ACTIVE_WITH_RAB == g_aRabmPsEnt[i].ucState)
                || (RABM_ACT_PENDING_WITH_RAB == g_aRabmPsEnt[i].ucState))
            {                                                                   /* RB已经建立                               */
                pSndMsg->aRabInfo[pSndMsg->ulRabCnt].ulCnDomainId = RABM_CN_PS; /* 记录是PS域的RB                           */
                pSndMsg->aRabInfo[pSndMsg->ulRabCnt].ulRbId =
                    g_aRabmPsEnt[i].RabInfo.aucRbId[0];                         /* 记录RB ID                                */
                pSndMsg->aRabInfo[pSndMsg->ulRabCnt].ulRabId =
                    g_aRabmPsEnt[i].RabInfo.ucRabId;                            /* 记录RAB ID                               */
                pSndMsg->ulRabCnt++;                                            /* 增加RB的个数                             */
            }
            else
            {
            }
        }
        for(i = 0; i < RABM_CS_MAX_ENT_NUM; i++)
        {                                                                       /* 查找CS域的RB信息                         */
            if(RABM_NULL == g_aRabmCsEnt[i].ucState)
            {                                                                   /* RB还没有建立                             */
                g_aRabmCsEnt[i].ucState = RABM_TC_STATE_NULL;
                PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvTcTestReq:NORMAL:RABM state: RABM_NULL ==> RABM_TC_STATE_NULL");
            }
            if(RABM_SI_ACTIVE_WITH_RAB == g_aRabmCsEnt[i].ucState)
            {                                                                   /* RB已经建立                               */
                if(g_aRabmCsEnt[i].RabInfo.ucRbNum > 1)
                {                                                               /* 该RAB对应的RB多于一个                    */
                    continue;
                }
                pSndMsg->aRabInfo[pSndMsg->ulRabCnt].ulCnDomainId = RABM_CN_CS; /* 记录是CS域的RB                           */
                pSndMsg->aRabInfo[pSndMsg->ulRabCnt].ulRbId =
                    g_aRabmCsEnt[i].RabInfo.aucRbId[0];                         /* 记录RB ID                                */
                pSndMsg->aRabInfo[pSndMsg->ulRabCnt].ulRabId =
                    g_aRabmCsEnt[i].RabInfo.ucRabId;                            /* 记录RAB ID                               */
                pSndMsg->ulRabCnt++;
            }
        }

        ucRbCnt = (VOS_UINT8)(pSndMsg->ulRabCnt);
        for(i = 0; i < TC_LB_MAX_RBNUM; i++)
        {


            for(j = 0; j < ucRbCnt; j++)
            {
                if(pSndMsg->ulRabCnt >= (TC_LB_MAX_RBNUM))
                {
                    break;
                }

                if(g_RabmTcInf.aRbInf[i].ucRbId != pSndMsg->aRabInfo[i].ulRbId)
                {
                    pSndMsg->aRabInfo[pSndMsg->ulRabCnt].ulCnDomainId
                        = g_RabmTcInf.aRbInf[i].ucDomain;
                    pSndMsg->aRabInfo[pSndMsg->ulRabCnt].ulRbId
                        = g_RabmTcInf.aRbInf[i].ucRbId;
                    pSndMsg->aRabInfo[pSndMsg->ulRabCnt].ulRabId
                        = g_RabmTcInf.aRbInf[i].ucRabId;
                    pSndMsg->ulRabCnt++;
                }
            }
        }
    }
    else
    {
        g_RabmTcInf.ucTcStartSta = RABM_TC_NO_START;                            /* 记录TC已经停止                           */
        for(i = 0; i < RABM_PS_MAX_ENT_NUM; i++)
        {
            if(RABM_TC_STATE_NULL == g_aRabmPsEnt[i].ucState)
            {
                RABM_SetWState(i, RABM_NULL);                                   /* 恢复到TC启动前的状态                     */
                PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvTcTestReq:NORMAL:RABM state: RABM_TC_STATE_NULL ==> RABM_NULL ");
            }
        }
        for(i = 0; i < RABM_CS_MAX_ENT_NUM; i++)
        {
            if(RABM_TC_STATE_NULL == g_aRabmCsEnt[i].ucState)
            {
                g_aRabmCsEnt[i].ucState = RABM_NULL;                            /* 恢复到TC启动前的状态                     */
                PS_LOG(WUEPS_PID_RABM, VOS_NULL, PS_PRINT_NORMAL, "RABM_RcvTcTestReq:NORMAL:RABM state: RABM_TC_STATE_NULL ==> RABM_NULL");
            }
        }
        for(i = 0; i < TC_LB_MAX_RBNUM; i++)
        {
            g_RabmTcInf.aRbInf[i].ucEstFlg = RABM_FALSE;
            g_RabmTcInf.aRbInf[i].ulRlcSduSize = 0;
            g_RabmTcInf.aRbInf[i].bRlcSduSizeValid = VOS_TRUE;
        }

        g_RabmTcInf.ucLbCnt = 0;
    }

    RABM_SndTcTestCnf(pSndMsg);
}


VOS_VOID RABM_RcvTcTestLoopReq(TCRABM_TEST_LOOP_REQ_STRU *pRcvMsg)
{
    VOS_UINT8                           i;
    VOS_UINT8                           j,k;
    VOS_UINT8                           ucRbFlg;                                /* 标志是否找到相同的rb                     */
    VOS_UINT8                           aucRbId[TC_LB_MAX_RBNUM];
    VOS_UINT8                           aucDomain[TC_LB_MAX_RBNUM];
    VOS_UINT8                           ucEstCnt = 0;

    PS_MEM_SET( aucRbId, (VOS_CHAR)0xff, TC_LB_MAX_RBNUM );
    PS_MEM_SET( aucDomain, (VOS_CHAR)0xff, TC_LB_MAX_RBNUM);

    if (TC_LOOP_CLOSE == pRcvMsg->ulMode)
    {
        g_RabmTcInf.ucTcStartSta = RABM_TC_LOOP_CLOSE;                          /* 记录TC已经启动                           */
        g_RabmTcInf.ucLbCnt = (VOS_UINT8)(pRcvMsg->ulRBNum);

        g_RabmTcInf.ucTestLoopMode = pRcvMsg->ucTestLoopMode;

        if (0 != pRcvMsg->ulRBNum)
        {
            ucEstCnt = 0;
            for (j = 0; j < TC_LB_MAX_RBNUM; j++)
            {
                if (RABM_TRUE == g_RabmTcInf.aRbInf[j].ucEstFlg)
                {
                    aucRbId[ucEstCnt] = g_RabmTcInf.aRbInf[j].ucRbId;
                    aucDomain[ucEstCnt] = g_RabmTcInf.aRbInf[j].ucDomain;
                    ucEstCnt++;
                }
            }

            for (i = 0; i < TC_LB_MAX_RBNUM; i++)
            {
                ucRbFlg = RABM_FALSE;

                for (j = 0; j < pRcvMsg->ulRBNum; j++)
                {
                    if (g_RabmTcInf.aRbInf[i].ucRbId == pRcvMsg->aLBIdList[j].ulRBID)
                    {
                        g_RabmTcInf.aRbInf[i].ulRlcSduSize
                            = pRcvMsg->aLBIdList[j].ulUpRlcSduSize;             /* 记录上行SDU的大小                        */
                        if (0 == g_RabmTcInf.aRbInf[i].ulRlcSduSize)
                        {
                            g_RabmTcInf.aRbInf[i].bRlcSduSizeValid = VOS_FALSE;
                        }
                    }
                }

                for(j = 0; j < RABM_PS_MAX_ENT_NUM; j++)
                {
                    if((RABM_NSAPI_ACTIVE_WITH_RAB == g_aRabmPsEnt[j].ucState)
                        || (RABM_ACT_PENDING_WITH_RAB
                            == g_aRabmPsEnt[j].ucState))
                    {
                        for ( k = 0; k < g_aRabmPsEnt[j].RabInfo.ucRbNum; k++ )
                        {
                            if(g_RabmTcInf.aRbInf[i].ucRbId
                                        == g_aRabmPsEnt[j].RabInfo.aucRbId[k])
                            {                                                       /* 该RB是PS域的，且已经建立                 */
                                g_RabmTcInf.aRbInf[i].ucEstFlg = RABM_TRUE;         /* 标志该RB已经建立标志                     */
                                g_RabmTcInf.aRbInf[i].ucDomain = RABM_CN_PS;
                                ucRbFlg = RABM_TRUE;
                                break;
                            }
                        }
                        if ( RABM_TRUE == ucRbFlg )
                        {
                            break;
                        }
                    }
                }
                if(RABM_FALSE == ucRbFlg)
                {                                                               /* PS域没有找到相应的RB信息                 */
                    for(j = 0; j < RABM_CS_MAX_ENT_NUM; j++)
                    {
                        if(RABM_SI_ACTIVE_WITH_RAB == g_aRabmCsEnt[j].ucState)
                        {
                            for ( k = 0; k < g_aRabmCsEnt[j].RabInfo.ucRbNum; k++ )
                            {
                                if(g_aRabmCsEnt[j].RabInfo.aucRbId[k]
                                    == g_RabmTcInf.aRbInf[i].ucRbId)
                                {                                                   /* 该RB是CS域的，且已经建立                 */
                                    g_RabmTcInf.aRbInf[i].ucEstFlg = RABM_TRUE;     /* 标志该RB已经建立标志                     */
                                    g_RabmTcInf.aRbInf[i].ucDomain = RABM_CN_CS;
                                    ucRbFlg = RABM_TRUE;
                                    break;
                                }
                            }
                            if ( RABM_TRUE == ucRbFlg )
                            {
                                break;
                            }
                        }
                    }
                }
            }
            for(i = 0; i < ucEstCnt; i++)
            {
                for(j = 0; j < g_RabmTcInf.ucLbCnt; j++)
                {
                    if(aucRbId[i] == g_RabmTcInf.aRbInf[j].ucRbId)
                    {
                        g_RabmTcInf.aRbInf[j].ucEstFlg = RABM_TRUE;             /* 标志该RB已经建立标志                     */
                        g_RabmTcInf.aRbInf[j].ucDomain = aucDomain[i];
                        break;
                    }
                }
            }
        }
        else
        {
            for(i = 0; i < TC_LB_MAX_RBNUM; i++)
            {
                g_RabmTcInf.aRbInf[i].ulRlcSduSize = 0;
            }
        }
    }
    else
    {
        g_RabmTcInf.ucTcStartSta = RABM_TC_LOOP_OPEN;                           /* 记录TC已经关闭                           */
        g_RabmTcInf.bFlowCtrlFlg = VOS_FALSE;                                   /* 记录TC已经关闭                           */

        for (i = 0; i < TC_LB_MAX_RBNUM; i++)
        {
            g_RabmTcInf.aRbInf[i].ulRlcSduSize = 0;
            g_RabmTcInf.aRbInf[i].bRlcSduSizeValid = VOS_TRUE;
        }

        /*清空环回数据队列*/
        RABM_ClearTcDataQ();

        /*清空环回数据队列的统计数据*/
        RABM_Memset(&g_stPdcpRabmTcDataQ.stStat, 0, sizeof(PDCP_RABM_TC_DATA_Q_STAT_STRU));
    }

    RABM_SndTcTestLoopCnf();
}


VOS_VOID NAS_RABM_SetTcFlowCtrl(VOS_VOID)
{
    g_RabmTcInf.bFlowCtrlFlg = VOS_TRUE;
}


VOS_VOID NAS_RABM_ClearTcFlowCtrl(VOS_VOID)
{
    g_RabmTcInf.bFlowCtrlFlg = VOS_FALSE;
}


VOS_UINT32 NAS_RABM_IsRabIndPsDonmain(
    RRRABM_RAB_IND_STRU                *pstRrcRabInd
)
{
    VOS_UINT32                          ulCnt;

    for (ulCnt = 0; ulCnt < pstRrcRabInd->ulRabCnt; ulCnt++)
    {
        if (RABM_CN_PS == pstRrcRabInd->aRabInfo[ulCnt].ulCnDomainId)
        {
            return VOS_TRUE;
        }
    }

    return VOS_FALSE;
}


#ifdef  __cplusplus
  #if  __cplusplus
  }
  #endif
#endif

