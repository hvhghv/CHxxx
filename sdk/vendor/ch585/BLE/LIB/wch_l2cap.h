/********************************** (C) COPYRIGHT ******************************
 * File Name         : wch_l2cap.h
 * Author            : WCH
 * Version           : V1.00
 * Date              : 2025/11/25
 * Description       : l2cap coc head file
 *
 * Copyright (c) 2023 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics. 
*******************************************************************************/



/******************************************************************************/
#ifndef __WCH_L2CAP_H
#define __WCH_L2CAP_H

#include "stdint.h"
#ifdef __cplusplus
extern "C"
{
#endif
#include "CH58xBLE_LIB.h"

/*********************************************************************
 * TYPEDEFS
 */

/**
 * User configurable variables format.
 */
typedef struct
{
  uint8_t maxNumPSM;        //!< Max number of Protocol/Service Multiplexers (PSM): @ref L2CAP_NUM_PSM_DEFAULT_DEFINES
  uint8_t maxNumCoChannels; //!< Max number of Connection Oriented Channels: @ref L2CAP_NUM_CO_CHANNELS_DEFAULT_DEFINES
} l2capUserCfg_t;

/**
 * Connection Request format.
 */
typedef struct
{
  uint16_t psm;         //!< LE PSM
  uint16_t srcCID;      //!< Represents CID on device sending request and receiving response
  uint16_t mtu;         //!< Specifies maximum SDU size that can be received on this channel
  uint16_t mps;         //!< Specifies maximum payload size that can be received on this channel
  uint16_t initCredits; //!< Indicates number of LE-frames that peer device can send
} l2capConnectReq_t;

/**
 * @brief   Callback function prototype to verify security when a Connection
 *          Request is received.
 *
 *          Note - It can be specified as NULL by PSM if there's no security
 *                 requirements.
 *
 * @param   connHandle - connection handle request was received on
 * @param   id - identifier matches responses with requests
 * @param   pReq - received connection request
 *
 * @return  See L2CAP Connection Response: Result values @ref L2CAP_CONN_RESULT_VALUES
 */
typedef uint16_t (*pfnVerifySecCB_t)( uint16_t connHandle, uint8_t id, l2capConnectReq_t *pReq );

/**
 * L2CAP PSM structure. Allocated one for each registered PSM.
 */
typedef struct
{
  uint16_t psm;                      //!< Registered PSM
  uint16_t mtu;                      //!< Maximum SDU size that can be received by local device
  uint16_t initPeerCredits;          //!< Number of LE-frames that peer device can send
  uint16_t peerCreditThreshold;      //!< Low threshold for peer credit count
  uint8_t maxNumChannels;            //!< Maximum number of CO Channels supported by PSM
  uint8_t taskId;                    //!< Task registered with PSM
  pfnVerifySecCB_t pfnVerifySecCB; //!< PSM security verification callback (can be NULL)
} l2capPsm_t;

/**
 * PSM information format.
 */
typedef struct
{
  uint16_t mtu;                 //!< Maximum SDU size that can be received by local device
  uint16_t mps;                 //!< Maximum payload size that can be received by local device
  uint16_t initPeerCredits;     //!< Number of LE-frames that peer device can send
  uint16_t peerCreditThreshold; //!< Low threshold for number of peer credits
  uint8_t maxNumChannels;       //!< Maximum number of CO Channels supported by PSM
  uint8_t numActiveChannels;    //!< Number of active CO Channels
} l2capPsmInfo_t;

/**
 * Connection oriented channel information format.
 */
typedef struct
{
  uint16_t psm;                 //!< PSM that channel belongs to
  uint16_t mtu;                 //!< Maximum SDU size that can be received by local device
  uint16_t mps;                 //!< Maximum payload size that can be received by local device
  uint16_t credits;             //!< Number of LE-frames that local device can send
  uint16_t peerCID;             //!< Remote channel id
  uint16_t peerMtu;             //!< Maximum SDU size that can be received by peer device
  uint16_t peerMps;             //!< Maximum payload size that can be received by peer device
  uint16_t peerCredits;         //!< Number of LE-frames that peer device can send
  uint16_t peerCreditThreshold; //!< Low threshold for peer credit count
} l2capCoCInfo_t;

/**
 * Local channel information format.
 */
typedef struct
{
  uint8_t state;         //!< Channel connection state: @ref L2CAP_CHANNEL_STATES
  l2capCoCInfo_t info; //!< Channel info
} l2capChannelInfo_t;

/**
 * L2CAP packet structure.
 */
typedef struct
{
  uint16_t CID;      //!< local channel id
  uint16_t len;      //!< length of information payload
  uint8_t *pPayload; //!< pointer to information payload. This contains the payload
                   //!< received from the upper layer protocol (outgoing packet),
                   //!< or delivered to the upper layer protocol (incoming packet).
} l2capPacket_t;


/**
 * @brief   Set the user configurable variables for the L2CAP layer.
 *
 *          Note: This function should be called BEFORE role init.
* @param   pUserCfg - pointer to user configuration
 *
 * @return  none
 */
 void L2CAP_SetUserConfig( l2capUserCfg_t *pUserCfg );

/**
 * @brief   Register a Protocol/Service Multiplexer (PSM) with L2CAP.
 *
 * @param   pPsm - pointer to PSM structure.
 *
 * @return  SUCCESS: Registration was successful.<BR>
 *          INVALIDPARAMETER: Max number of channels is greater than total supported.<BR>
 *          bleInvalidRange: PSM value is out of range.<BR>
 *          bleInvalidMtuSize: MTU size is out of range.<BR>
 *          bleNoResources: Out of resources.<BR>
 *          bleAlreadyInRequestedMode: PSM already registered.<BR>
 */
 bStatus_t L2CAP_RegisterPsm( l2capPsm_t *pPsm );


/**
 * @brief   Deregister a Protocol/Service Multiplexer with L2CAP.
 *
 * @param   taskId - task PSM belongs to.
 * @param   psm - PSM to deregister.
 *
 * @return  SUCCESS: Registration was successful.<BR>
 *          INVALIDPARAMETER: PSM or task Id is invalid.<BR>
 *          bleIncorrectMode: PSM is in use.<BR>
 */
 bStatus_t L2CAP_DeregisterPsm( uint8_t taskId, uint16_t psm );


/**
 * @brief   Get information about a given registered PSM.
 *
 * @param   psm - PSM Id.
 * @param   pInfo - structure to copy PSM info into.
 *
 * @return  SUCCESS: Operation was successful.<BR>
 *          INVALIDPARAMETER: PSM is not registered.<BR>
 */
 bStatus_t L2CAP_PsmInfo( uint16_t psm, l2capPsmInfo_t *pInfo );


/**
 * @brief   Get all active channels for a given registered PSM.
 *
 * @param   psm - PSM Id.
 * @param   numCIDs - number of CIDs can be copied.
 * @param   pCIDs - structure to copy CIDs into.
 *
 * @return  SUCCESS: Operation was successful.<BR>
 *          INVALIDPARAMETER: PSM is not registered.<BR>
 */
 bStatus_t L2CAP_PsmChannels( uint16_t psm, uint8_t numCIDs, uint16_t *pCIDs );

/**
 * @brief   Get information about a given active Connection Oriented Channel.
 *
 * @param   CID - local channel id.
 * @param   pInfo - structure to copy channel info into.
 *
 * @return  SUCCESS: Operation was successful.<BR>
 *          INVALIDPARAMETER: No such a channel.<BR>
 */
 bStatus_t L2CAP_ChannelInfo( uint16_t CID, l2capChannelInfo_t *pInfo );

/**
 * @brief   Send Flow Control Credit.
 *
 * @param   CID - local CID
 * @param   peerCredits - number of credits to give to peer device
 *
 * @return  SUCCESS: Request was sent successfully.<BR>
 *          INVALIDPARAMETER: Channel is not open.<BR>
 *          MSG_BUFFER_NOT_AVAIL: No HCI buffer is available.<BR>
 *          bleNotConnected: Connection is down.<BR>
 *          bleInvalidRange - Credits is out of range.<BR>
 *          bleMemAllocError: Memory allocation error occurred.<BR>
 */
 bStatus_t L2CAP_SendFlowCtrlCredit( uint16_t CID, uint16_t peerCredits );


/**
 * @brief   Send Connection Request.
 *
 * @param   connHandle - connection to create channel on
 * @param   psm - local PSM registered by application for resource allocation;
 * @param   peerPsm - peer's PSM to connect to
 *
 * @return  SUCCESS: Request was sent successfully.<BR>
 *          INVALIDPARAMETER: PSM is not registered.<BR>
 *          MSG_BUFFER_NOT_AVAIL: No HCI buffer is available.<BR>
 *          bleIncorrectMode: PSM not registered.<BR>
 *          bleNotConnected: Connection is down.<BR>
 *          bleNoResources: No available resource.<BR>
 *          bleMemAllocError: Memory allocation error occurred.<BR>
 */
 bStatus_t L2CAP_ConnectReq( uint16_t connHandle, uint16_t psm, uint16_t peerPsm );

/**
 * @brief   Send Connection Response.
 *
 * @param   connHandle - connection handle
 * @param   id - identifier received in connection request
 * @param   result - outcome of connection request: @ref L2CAP_CONN_RESULT_VALUES
 *
 * @return  SUCCESS: Request was sent successfully.<BR>
 *          INVALIDPARAMETER: PSM is not registered or Channel is not open.<BR>
 *          MSG_BUFFER_NOT_AVAIL: No HCI buffer is available.<BR>
 *          bleNotConnected: Connection is down.<BR>
 *          bleMemAllocError: Memory allocation error occurred.<BR>
 */
 bStatus_t L2CAP_ConnectRsp( uint16_t connHandle, uint8_t id, uint16_t result );

/**
 * @brief   Send Disconnection Request.
 *
 * @param   CID - local CID to disconnect
 *
 * @return  SUCCESS: Request was sent successfully.<BR>
 *          INVALIDPARAMETER: Channel id is invalid.<BR>
 *          MSG_BUFFER_NOT_AVAIL: No HCI buffer is available.<BR>
 *          bleNotConnected: Connection is down.<BR>
 *          bleNoResources: No available resource.<BR>
 *          bleMemAllocError: Memory allocation error occurred.<BR>
 */
 bStatus_t L2CAP_DisconnectReq( uint16_t CID );

/**
 * @brief   L2CAP implementation of the allocator functionality.
 *
 *          Note: This function should only be called by L2CAP and
 *                the upper layer protocol/application.
 *
 * @param   size - number of bytes to allocate from the heap.
 * @param   flag - memory allocation flag,
 *
 * @return  pointer to the heap allocation; NULL if error or failure.
 */
 void *L2CAP_bm_alloc( uint16_t size, uint8_t flag );

/**
 * @brief   Send data packet over an L2CAP connection oriented channel 
 *          established over a physical connection.
 *
 *          Note: Packet 'pPayload' must be allocated using L2CAP_bm_alloc().
 *
 * @param   pPkt - pointer to packet to be sent.
 *
 * @return  SUCCESS: Data was sent successfully.<BR>
 *          INVALIDPARAMETER: SDU payload is null.
 *          bleNotConnected: Connection or Channel is down.<BR>
 *          bleMemAllocError: Memory allocation error occurred.<BR>
 *          blePending: In the middle of another transmit.<BR>
 *          bleInvalidMtuSize: SDU size is larger than peer MTU.<BR>
 */
 bStatus_t L2CAP_SendSDU( l2capPacket_t *pPkt );

/*
 * END @ API
 */
/******************************************************************************/
#ifdef __cplusplus
}
#endif

#endif
