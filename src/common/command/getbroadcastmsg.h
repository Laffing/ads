#ifndef GETBROADCASTMSG_H
#define GETBROADCASTMSG_H

#include <vector>

#include "abstraction/interfaces.h"
#include "command/pods.h"
#include "default.hpp"

/** \brief Get broadcast command class. */
class GetBroadcastMsg : public BlockCommand {
    public:
        GetBroadcastMsg();
        GetBroadcastMsg(uint16_t abank, uint32_t auser, uint32_t block, uint32_t time);

        /** \brief Disabled copy contructor. */
        GetBroadcastMsg(const GetBroadcastMsg& obj) = delete;

        /** \brief Disabled copy assignment operator. */
        GetBroadcastMsg &operator=(const GetBroadcastMsg&) = delete;

        /** \brief Free responseBuffer. */
        virtual ~GetBroadcastMsg();

        /** \brief Return TXSTYPE_BLG as type . */
        virtual int getType()                                       override;

        /** \brief Return eReadingOnly as command type . */
        virtual CommandType getCommandType()                        override;

        /** \brief Get pointer to command data structure. */
        virtual unsigned char*  getData()                           override;

        /** \brief Get pointer to response data. */
        virtual unsigned char*  getResponse()                       override;

        /**  \brief Get message id. */
        virtual uint32_t getUserMessageId()                         override;

        /** \brief Put data as a char list and put convert it to data structure. */
        virtual void setData(char* data)                            override;

        /** \brief Apply data as a response struct. */
        virtual void setResponse(char* response)                    override;

        /** \brief Get data struct size. Without signature. */
        virtual int getDataSize()                                   override;

        /** \brief Get response data struct size. */
        virtual int getResponseSize()                               override;

        /** \brief Get pointer to signature data. */
        virtual unsigned char*  getSignature()                      override;

        /** \brief Get signature size. */
        virtual int getSignatureSize()                              override;

        /** \brief Sign actual data plus hash using user private and public keys.
         *
         * \param hash  Previous hash operation.
         * \param sk    Pointer to provate key.
         * \param pk    Pointer to public key.
        */
        virtual void sign(const uint8_t* hash, const uint8_t* sk, const uint8_t* pk) override;

        /** \brief Check actual signature.
         *
         * \param hash  Previous hash operation.
         * \param pk    Pointer to public key.
        */
        virtual bool checkSignature(const uint8_t* hash, const uint8_t* pk)  override;

        /** \brief Get time of command. */
        virtual uint32_t        getTime()                                   override;

        /** \brief Get User ID. */
        virtual uint32_t        getUserId()                                 override;

        /** \brief Get Node ID. */
        virtual uint32_t        getBankId()                                 override;

        /** \brief Get command fee. */
        virtual int64_t         getFee()                                    override;

        /** \brief Get change in cash balance after command. */
        virtual int64_t         getDeduct()                                 override;

        /** \brief Send data to the server.
         *
         * \param netClient  Netwrok client implementation of INetworkClient interface.
         * \param pk    Pointer to public key.
        */
        virtual bool            send(INetworkClient& netClient)             override;

        /** \brief Save command response to settings object. */
        virtual void            saveResponse(settings& sts)                 override;

        //IJsonSerialize interface
        virtual std::string  toString(bool pretty)                          override;
        virtual void         toJson(boost::property_tree::ptree &ptree)     override;
        virtual void         txnToJson(boost::property_tree::ptree& ptree)  override;
        virtual std::string  usageHelperToString()                          override;

        virtual void readDataBuffer(unsigned char* dataBuffer, int size);
        virtual void printBlg(GetBroadcastResponse &block, std::string &message, boost::property_tree::ptree &ptree);
        virtual bool loadFromLocalPath();
        virtual void saveCopy(unsigned char* dataBuffer, int size);

      public:

        uint32_t    getBlockTime();

        GetBroadcastData                                            m_data;
        std::vector<std::pair<GetBroadcastResponse, std::string>>   m_response;
        BroadcastFileHeader                                         m_header;
        bool                                                        m_loadedFromLocal;

        commandresponse         m_fakeResponse; // this command do not return user_t
};

#endif // GETBROADCASTMSG_H
