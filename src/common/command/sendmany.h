#ifndef SENDMANY_H
#define SENDMANY_H

#include <vector>

#include "abstraction/interfaces.h"
#include "command/pods.h"
#include "default.hpp"

class SendMany : public BlockCommand {
    public:
        SendMany();
        SendMany(uint16_t bank, uint32_t user, uint32_t msid,
                 std::vector<SendAmountTxnRecord> &txns_data, uint32_t time);

        /** \brief Return TXSTYPE_MPT as type . */
        virtual int  getType()                                      override;

        /** \brief Return eModifying as command type . */
        virtual CommandType getCommandType()                        override;

        /** \brief Get pointer to command data structure. */
        virtual unsigned char*  getData()                           override;

        virtual unsigned char*  getAdditionalData()                 override;

        /** \brief Get pointer to response data. */
        virtual unsigned char*  getResponse()                       override;

        /** \brief Put data as a char list and put convert it to data structure. Note that
                   in this case it will not set an additional data */
        virtual void setData(char* data)                            override;

        /** \brief Apply data as a response struct. */
        virtual void setResponse(char* response)                    override;

        /** \brief Get data struct size. Without signature. */
        virtual int getDataSize()                                   override;

        virtual int getAdditionalDataSize()                         override;

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

        /** \brief Get user message id. */
        virtual  uint32_t       getUserMessageId()                          override;

        /** \brief Send data to the server.
         *
         * \param netClient  Netwrok client implementation of INetworkClient interface.
         * \param pk    Pointer to public key.
        */
        virtual bool            send(INetworkClient& netClient)             override;

        /** \brief Save command response to settings object. */
        virtual void            saveResponse(settings& sts)                 override;

        /**
         * @brief Checks for target duplicates and does amount is positive value.
         * @return ErrorCodes value, eNone if success.
         */
        virtual void checkForDuplicates();

        /** \brief Retursn transactions vector */
        virtual std::vector<SendAmountTxnRecord> getTransactionsVector();

        //IJsonSerialize interface
        virtual std::string  toString(bool pretty)                          override;
        virtual void         toJson(boost::property_tree::ptree &ptree)     override;
        virtual void         txnToJson(boost::property_tree::ptree& ptree)  override;
        virtual std::string  usageHelperToString()                          override;

      public:
        /**  \brief Get message id. */

        SendManyData          m_data;
        commandresponse       m_response;

    private:

        std::vector<SendAmountTxnRecord> m_transactions;
};

#endif // SENDMANY_H
