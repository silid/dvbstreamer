/*
Copyright (C) 2009  Adam Charrett

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA

deliverymethod.h

Delivery Method management functions.

*/
#ifndef _DELIVERYMETHOD_H
#define _DELIVERYMETHOD_H
#include "ts.h"
/**
 * @defgroup DeliveryMethod Delivery Method Management
 * @{
 */
/**
 * Structure to represent an instance of a delivery method.
 * Implementors should consider the following structure as the 'base class' and
 * should extend it with the state they require for the output method.
 * For example:
 * @code
 * struct UDPOutputDeliveryMethodInstance_t
 * {
    char *mrl;
 *  void (*SendPacket)(DeliveryMethodInstance_t *this, TSPacket_t packet);
 *  void (*DestroyInstance)(DeliveryMethodInstance_t *this);
 *  int tos;
 *  int packetsPerDatagram;
 *  Socket socketFD;
 *  int packetCount;
 *  char *buffer;
 * }
 * @endcode
 */
typedef struct DeliveryMethodInstance_t
{
    /**
     * The media resource locator used to create the instance.
     */
    char *mrl;


    /**
     * Pointer to the table of functions used to manipulate this instance.
     */
    struct DeliveryMethodInstanceOps_t const *ops;
    
    /**
     * Field used to hold private information for the type of instance.
     */
    void *private;
}
DeliveryMethodInstance_t;

/**
 * Structure used to define the functions used to manipulate a DeliveryMethodInstance.
 */
typedef struct DeliveryMethodInstanceOps_t
{
    /**
     * Output a packet.
     * @param this The instance of the DeliveryMethodInstance_t to send the packet using.
     * @param packet The packet to send.
     */
    void(*OutputPacket)(struct DeliveryMethodInstance_t *this, TSPacket_t *packet);
    /**
     * Output an opaque block of data.
     * @param this The instance of the DeliveryMethodInstance_t to send the packet using.
     * @param block Pointer to the data to send.
     * @param block_len The length of the data in bytes to send.
     */
    void(*OutputBlock)(struct DeliveryMethodInstance_t *this, void *block, unsigned long blockLen);
    /**
     * Destroy an instace of DeliveryMethodInstance_t.
     * @param this The instance of the DeliveryMethodInstance_t to free.
     */
    void(*DestroyInstance)(struct DeliveryMethodInstance_t *this);
    /**
     * Reserves space for the specified number of packets at the start of the 
     * stream.
     * Until the header is set with SetHeader(), the header packets will be 
     * stuffing packets.
     * This function must be called before any packets have been sent to this 
     * instance.
     * 
     * @param this The instance of the DeliveryMethodInstance_t to apply this to. 
     * @param packets The number of packets to reserve space for.
     */
     void (*ReserveHeaderSpace)(struct DeliveryMethodInstance_t *this, int packets);

    /**
     * Set the PAT and PMT header packets. This function can be called any time 
     * after a call to ReserveHeaderSpace().
     * @param this The instance of the DeliveryMethodInstance_t to apply this to.
     * @param packets The packets to write as the header.
     * @param count The number of packets to write.
     */
     void (*SetHeader)(struct DeliveryMethodInstance_t *this, 
                        TSPacket_t *packets, int count);
}DeliveryMethodInstanceOps_t;




/**
 * Structure used to describe a Delivery Method Handler.
 * The handler should implement the CanHandle() function to test if it can handle
 * a Media Resource Locator passed to it. If it can it should then expect the
 * CreateInstance method to be called for the same mrl.
 * MRLs will be in the form \<delivery method\>://\<url\>[,\<options\>]
 * For example udp could be (ppd == Packets Per Datagram)
 * udp://localhost:1234,tos=25,ppd=7
 */
typedef struct DeliveryMethodHandler_t
{
	bool (*CanHandle)(char *mrl); /**< Function callback to test if the handler can handle the specified mrl. */
	DeliveryMethodInstance_t* (*CreateInstance)(char *mrl); /**< Function to create an instance for the specified mrl. */
}DeliveryMethodHandler_t;

/**
 * @internal
 * Initialise the Delivery method manager.
 * @return 0 on success.
 */
int DeliveryMethodManagerInit(void);

/**
 * @internal
 * De-Initialise the Delivery method manager.
 */
void DeliveryMethodManagerDeInit(void);

/**
 * Register a Delivery Method handler with the manager.
 * @param handler The handler to register.
 */
void DeliveryMethodManagerRegister(DeliveryMethodHandler_t *handler);

/**
 * Unregiser a Delivery Method handler with the manager.
 * @param handler The handler to remove from the available handlers.
 */
void DeliveryMethodManagerUnRegister(DeliveryMethodHandler_t *handler);

/**
 * Create a new DeliveryMethodInstance_t that can handle the supplied MRL.
 * @param mrl The MRL the delivery method should handle.
 * @return A DeliveryMethodInstance_t if the supplied MRL can be handled or NULL.
 */
DeliveryMethodInstance_t *DeliveryMethodCreate(char *mrl);

/**
 * Retrieve the mrl used to setup the output on the specified filter.
 * @param filter The PIDFilter to retrieve the MRL from.
 * @return The MRL string.
 */
char* DeliveryMethodGetMRL(DeliveryMethodInstance_t *instance);

/**
 * Destory a DeliveryMethodInstance_t previously created by
 * DeliveryMethodCreate().
 * @param instance The instance to free.
 */
void DeliveryMethodDestroy(DeliveryMethodInstance_t *instance);

/**
 * @internal
 * Destroy all DeliveryMethodInstance_t instances.
 * NOTE: This function will be called before plugin manager shutsdown all
 * plugins. This is to ensure all delivery method instances are correctly closed
 * down. Plugins should not attempt to destroy delivery method instances, as the
 * plugin providing the method may have already been shutdown.
 */
void DeliveryMethodDestroyAll();

/**
 * Reserve the specified number of packets at the start of the stream to allow for a header.
 * @param instance The delievery method instance to use.
 * @param nrofPackets The number of packets to reserve.
 */
void DeliveryMethodReserveHeaderSpace(DeliveryMethodInstance_t *instance, int nrofPackets);

/**
 * Set the header packets, previously reserved, to those specified.
 * @param instance The delievery method instance to use.
 * @param packets Pointer to an array of packets.
 * @param nrofPackets The number of packets to set as the header.
 */ 
void DeliveryMethodSetHeader(DeliveryMethodInstance_t *instance, TSPacket_t *packets, int nrofPackets);

/**
 * Output a packet using the specified delivery method.
 * @param instance The delievery method instance to use.
 * @param packet The packet to output.
 */
void DeliveryMethodOutputPacket(DeliveryMethodInstance_t *instance, TSPacket_t* packet);

/**
 * Output a block of data using the specified delivery method.
 * @param instance The delievery method instance to use.
 * @param block Start address of the block of data to output.
 * @param blockLen Length in bytes of the data to output.
 */
void DeliveryMethodOutputBlock(DeliveryMethodInstance_t *instance,  void *block, unsigned long blockLen);
/** @} */
#endif
