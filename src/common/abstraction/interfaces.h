#ifndef INTERFACES_H
#define INTERFACES_H

#include <cstdint>
#include <boost/property_tree/json_parser.hpp>
#include "default.hpp"
#include "settings.hpp"

class office;

/*!
 * \brief Interface for class which is responible for client network connection.
 */
class INetworkClient {
  public:
    /** \brief Connect to server if disconnected. */
    virtual bool connect()      = 0;
    /** \brief Recconect to server. */
    virtual bool reconnect()    = 0;
    /** \brief Disconnect to server. */
    virtual bool disConnect()   = 0;
    /** \brief Send data using pointer to bufor and size. */
    virtual bool sendData(uint8_t* buff, int size)      = 0;
    /** \brief Send data using vector data. */
    virtual bool sendData(std::vector<uint8_t> buff)    = 0;
    /** \brief Read data to buffor. */
    virtual bool readData(uint8_t* buff, int size)      = 0;
    /** \brief Read data to buffor. */
    virtual bool readData(char* buff, int size)         = 0;
    /** \brief Read data to vector. */
    virtual bool readData(std::vector<uint8_t>& buff)   = 0;
    virtual ~INetworkClient() = default;
};

/*!
 * \brief Command Interface. Base class for all command.
 */
class ICommand {
  public:
    /** \brief Get command type. */
    virtual int                     getType()           = 0;
    /** \brief Get pointer to command data structure. */
    virtual unsigned char*          getData()           = 0;
    /** \brief Get additional data. */
    virtual unsigned char*          getAdditionalData() { return nullptr; }
    /** \brief Get pointer to response data. */
    virtual unsigned char*          getResponse()       = 0;
    /** \brief Put data as a char list and put convert it to data structure. */
    virtual void                    setData(char* data) = 0;
    /** \brief Apply data as a response struct. */
    virtual void                    setResponse(char* data) = 0;
    /** \brief Get data struct size. Without signature. */
    virtual int                     getDataSize()       = 0;
    /** \brief Get response data struct size. */
    virtual int                     getResponseSize()   = 0;
    /** \brief Get additional data size. */
    virtual int                     getAdditionalDataSize() { return 0; }
    /** \brief Get pointer to signature data. */
    virtual unsigned char*          getSignature()      = 0;
    /** \brief Get signature size. */
    virtual int                     getSignatureSize()  = 0;
    /** \brief Sign actual data plus hash using user private and public keys.
     *
     * \param hash  Previous hash operation.
     * \param sk    Pointer to provate key.
     * \param pk    Pointer to public key.
    */
    virtual void                    sign(const uint8_t* hash, const uint8_t* sk, const uint8_t* pk)   = 0;
    /** \brief Check actual signature.
     *
     * \param hash  Previous hash operation.
     * \param pk    Pointer to public key.
    */
    virtual bool                    checkSignature(const uint8_t* hash, const uint8_t* pk)      = 0;
    /** \brief Get actual user for which command is performed. */
    virtual user_t&                 getUserInfo()       = 0;
    /** \brief Get time of command. */
    virtual uint32_t                getTime()           = 0;
    /** \brief Get User ID. */
    virtual uint32_t                getUserId()         = 0;
    /** \brief Get Node ID. */
    virtual uint32_t                getBankId()         = 0;
    /** \brief Get command fee. */
    virtual int64_t                 getFee()            = 0;
    /** \brief Get change in cash balance after command. */
    virtual int64_t                 getDeduct()         = 0;


    /** \brief Send data to the server.
     *
     * \param netClient  Netwrok client implementation of INetworkClient interface.
     * \param pk    Pointer to public key.
    */
    virtual bool                    send(INetworkClient& netClient) = 0;

    /** \brief Save command response to settings object. */
    virtual void                    saveResponse(settings& sts)  = 0;

    virtual ~ICommand() = default;
};


//TODO avoid boost in the future. All should based on interfaces.
/*!
 * \brief Interface which allow convert command data to JSON or string. Not used for now.
 */
class IJsonSerialize {
  public:
    virtual boost::property_tree::ptree     toJson()                = 0;
    virtual std::string                     toString(bool preety)   = 0;

    virtual ~IJsonSerialize() = default;
};

/*!
 * \brief Base interface for command. It combain ICommand and IJsonSerialize Interface.
 */
class IBlockCommand : public ICommand, public IJsonSerialize {
};

/*!
 * \brief Command handler Interface.
 */
class ICommandHandler {
  public:

    /** \brief Execute command.
     * \param command  Command which dhould be executed.
     * \param usera    User data.
    */
    virtual void execute(std::unique_ptr<IBlockCommand> command, const user_t& usera) = 0;

    /** \brief Int command event. It prepare data's needed to execute command.
     * If "command" casting is needed it should be performed in it.
     *
     * \param command  Unique Pointer with command which should be executed.
    */
    virtual void onInit(std::unique_ptr<IBlockCommand> command)  = 0;

    /** \brief Execution event. It execute actual command and commiting it to blockchain. */
    virtual void onExecute()  = 0;
    /** \brief Validtaion event. It execute validation of data if it contain all proper value. */
    virtual bool onValidate() = 0;

    //virtual void onCommit(std::unique_ptr<IBlockCommand> command)     = 0;
    //virtual void onSend()     = 0;

    virtual ~ICommandHandler() = default;
};


#endif // INTERFACES_H
