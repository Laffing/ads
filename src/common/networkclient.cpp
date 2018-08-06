#include "networkclient.h"

#include <iostream>
#include <boost/array.hpp>
#include "helper/json.h"
#include "helper/ascii.h"
#include "helper/socket.h"

#define NETCL_SOCK_TIMEOUT 10
#define NETCL_SOCK_IDLE    15
#define NETCL_SOCK_MAXTRY  3

NetworkClient::NetworkClient(const std::string& address, const std::string& port, bool pretty):
    m_query(address, port.c_str()),
    m_resolver(m_ioService),
    m_prettyJson(pretty)
{
}

NetworkClient::~NetworkClient() {
}

bool NetworkClient::isConnected() {
    return m_connected;
}

bool NetworkClient::connect() {
    if(!m_connected) {
        try {
            boost::asio::ip::tcp::resolver::iterator    end;
            boost::asio::ip::tcp::resolver::iterator    connectpoint    = m_resolver.resolve(m_query);
            boost::system::error_code                   error           = boost::asio::error::host_not_found;

            while (error && connectpoint != end) {
                if(m_socket && m_socket->is_open()) {
                    m_socket->close();
                }

                m_socket.reset(new boost::asio::ip::tcp::socket(m_ioService));

                if(m_socket) {
                    m_socket->connect(*connectpoint++, error);
                    Helper::setSocketTimeout(m_socket, NETCL_SOCK_TIMEOUT, NETCL_SOCK_IDLE, NETCL_SOCK_MAXTRY);
                    Helper::setSocketNoDelay(m_socket, true);
                }
            }

            if (error) {
                Helper::printErrorJson(error.message().c_str(), m_prettyJson);
                return false;
            }
        } catch(std::exception& e) {
            Helper::printErrorJson(e.what(), m_prettyJson);
            m_connected = false;
            return false;
        }

        m_connected = true;
    }

    return true;
}

bool NetworkClient::disConnect() {
    if(m_connected) {
        try {
            if(m_socket) {
                m_socket->close();
            }
            m_socket.reset();
        } catch(std::exception& e) {
            Helper::printErrorJson(e.what(), m_prettyJson);
            m_connected = false;
            return false;
        }

        m_connected = false;
    }

    return true;
}

bool NetworkClient::reconnect() {
    disConnect();
    return connect();
}

bool NetworkClient::sendData(std::vector<uint8_t> buff) {
    try {
        if(m_connected && m_socket) {
            auto size = buff.size();
            if( boost::asio::write(*m_socket.get(), boost::asio::buffer(std::move(buff))) == size ) {
                return true;
            }
        }
    } catch(std::exception& e) {
        Helper::printErrorJson(e.what(), m_prettyJson);
        m_connected = false;
    }

    return false;
}

bool NetworkClient::sendData(uint8_t* buff, int size) {

    try {
        if(m_connected && m_socket) {
            if( boost::asio::write(*m_socket.get(), boost::asio::buffer(buff, size)) == (std::size_t)size ) {
                return true;
            }
        }
    } catch(std::exception& e) {
        Helper::printErrorJson(e.what(), m_prettyJson);
        m_connected = false;
    }

    return false;
}

bool NetworkClient::readData(std::vector<uint8_t>& buff) {
    try {
        if(m_connected && m_socket) {
            auto size = buff.size();
            if( boost::asio::read(*m_socket.get(), boost::asio::buffer(buff)) == size ) {
                return true;
            }
        }
    } catch(std::exception& e) {
        Helper::printErrorJson(e.what(), m_prettyJson);
        m_connected = false;
    }

    return false;
}

bool NetworkClient::readData(uint8_t* buff, int size) {
    try {
        if(m_connected && m_socket) {
            if( boost::asio::read(*m_socket.get(), boost::asio::buffer(buff, size)) == (size_t)size ) {
                return true;
            }
        }
    } catch(std::exception& e) {
        Helper::printErrorJson(e.what(), m_prettyJson);
        m_connected = false;
    }

    return false;
}

bool NetworkClient::readData(char* buff, int size) {
    try {
        if(m_connected && m_socket) {
            if( boost::asio::read(*m_socket.get(), boost::asio::buffer(buff, size)) == (size_t)size ) {
                return true;
            }
        }
    } catch(std::exception& e) {
        Helper::printErrorJson(e.what(), m_prettyJson);
        m_connected = false;
    }

    return false;
}

bool NetworkClient::readData(int32_t *buff, int size) {
    try {
        if(m_connected && m_socket) {
            if (boost::asio::read(*m_socket.get(), boost::asio::buffer(buff, size)) == (size_t)size) {
                return true;
            }
        }
    } catch(std::exception& e) {
        Helper::printErrorJson(e.what(), m_prettyJson);
        m_connected = false;
    }

    return false;
}
