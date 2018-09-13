#include "connected.h"

#include <sstream>

#include "ed25519/ed25519.h"
#include "abstraction/interfaces.h"
#include "helper/json.h"

Connected::Connected()
    : m_data{} {
    m_responseError = ErrorCodes::Code::eNone;
}

Connected::Connected(uint16_t port, uint32_t ip_address, std::string version)
    : m_data(port, ip_address, version.c_str()) {
    m_responseError = ErrorCodes::Code::eNone;
}

unsigned char* Connected::getData() {
    return reinterpret_cast<unsigned char*>(&m_data);
}

unsigned char* Connected::getResponse() {
    return reinterpret_cast<unsigned char*>(&m_response);
}

void Connected::setData(char* data) {
    m_data = *reinterpret_cast<decltype(m_data)*>(data);
}

void Connected::setResponse(char* response) {
    m_response = *reinterpret_cast<decltype(m_response)*>(response);
}

int Connected::getDataSize() {
    return sizeof(m_data);
}

int Connected::getResponseSize() {
    return 0;
}

unsigned char* Connected::getSignature() {
    return (unsigned char*)"";
}

int Connected::getSignatureSize() {
    return 0;
}

int Connected::getType() {
    return TXSTYPE_CON;
}

CommandType Connected::getCommandType() {
    return CommandType::eReadingOnly;
}

void Connected::sign(const uint8_t* /*hash*/, const uint8_t* /*sk*/, const uint8_t* /*pk*/) {
    // no signature
}

bool Connected::checkSignature(const uint8_t* /*hash*/, const uint8_t* /*pk*/) {
    // no signature
    return true;
}

void Connected::saveResponse(settings& /*sts*/) {
}

uint32_t Connected::getUserId() {
    return 0;
}

uint32_t Connected::getBankId() {
    return 0;
}

uint32_t Connected::getTime() {
    return 0;
}

int64_t Connected::getFee() {
    return 0;
}

int64_t Connected::getDeduct() {
    return 0;
}

uint32_t Connected::getUserMessageId() {
    return 0;
}

bool Connected::send(INetworkClient& /*netClient*/) {
    return true;
}

std::string Connected::toString(bool /*pretty*/) {
    return "";
}

void Connected::toJson(boost::property_tree::ptree& /*ptree*/) {
}

void Connected::txnToJson(boost::property_tree::ptree& ptree) {
    std::stringstream ip_address{};
    ip_address << (m_data.ip_address & 0xFF) << "."
      << ((m_data.ip_address >> 8) & 0xFF) << "."
      << ((m_data.ip_address >> 16) & 0xFF) << "." << (m_data.ip_address >> 24);

    char verStr[17];
    uint i=0;
    while (i < sizeof(verStr)-1 && isprint(m_data.version[i]))
    {
        verStr[i] = m_data.version[i];
        i++;
    }
    verStr[i] = '\0';
    using namespace Helper;
    ptree.put(TAG::TYPE, getTxnName(m_data.ttype));
    ptree.put(TAG::PORT, m_data.port);
    ptree.put(TAG::IP_ADDRESS, ip_address.str());
    ptree.put(TAG::NODE_VERSION, verStr);
}

std::string Connected::usageHelperToString() {
    return std::string("");
}
