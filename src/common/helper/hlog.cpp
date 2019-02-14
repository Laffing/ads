#include "hlog.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "command/errorcodes.h"
#include "default.hpp"
#include "blocks.h"

namespace Helper {

Hlog::Hlog() : total(0), data(nullptr) {

}

Hlog::Hlog(char* buffer, uint32_t size)
    : total(size) {
    data = (char*)malloc(size);
    std::memcpy(data, buffer, size);
    SHA256_Init(&sha256);
}

Hlog::Hlog(const Hlog &hlog) : total(hlog.total) {
    if (!data) {
        data = (char*)malloc(hlog.total);
    }
    std::copy(hlog.data, hlog.data+hlog.total, data);
    SHA256_Init(&sha256);
}

Hlog& Hlog::operator=(Hlog hlog) {
    if (!data) {
        data = (char*)malloc(hlog.total);
    }
    std::copy(hlog.data, hlog.data+hlog.total, data);
    total = hlog.total;
    SHA256_Init(&sha256);
    return *this;
}

Hlog::Hlog(uint32_t path) :
    total(0),
    data(NULL) {
    Helper::FileName::getName(filename, sizeof(filename), path, "hlog.hlg");
    SHA256_Init(&sha256);
}

Hlog::Hlog(boost::property_tree::ptree& pt,char* filename) : //log,filename
    total(0),
    data(NULL) {
    SHA256_Init(&sha256);
    fd = std::make_shared<Helper::BlockFileReader>(filename);
    if(!fd->isOpen()) {
        pt.put("error_read","true");
        return;
    }
    uint8_t type;
    boost::property_tree::ptree logs;
    while(fd->read(&type,1)>0) {
        boost::property_tree::ptree log;
        switch(type) {
        case(HLOG_USO):
            read_uso(log);
            break;
        case(HLOG_UOK):
            read_uok(log);
            break;
        case(HLOG_USR):
            read_usr(log);
            break;
        case(HLOG_BKY):
            read_bky(log);
            break;
        case(HLOG_SBS):
            read_sbs(log);
            break;
        case(HLOG_UBS):
            read_ubs(log);
            break;
        case(HLOG_BNK):
            read_bnk(log);
            break;
        case(HLOG_GET):
            read_get(log);
            break;
        default:
            pt.add_child("logs",logs);
            pt.put("error_parse","true");
            return;
        }
        logs.push_back(std::make_pair("",log));
    }
    char hash[65];
    hash[64]='\0';
    hash_t fin;
    finish(fin,total);
    Helper::ed25519_key2text(hash,fin,32);
    pt.add_child("logs",logs);
    pt.put("hash",hash);
    return;
}


Hlog::~Hlog() {
    if(data!=NULL) {
        free(data);
        data=NULL;
    }
}

void Hlog::printJson(boost::property_tree::ptree& pt) {
    char *ptr = data;
    int readDataLength = 0;

    boost::property_tree::ptree logs;
    while (readDataLength < total) {
        boost::property_tree::ptree log;
        uint8_t type = *ptr;
        switch (type) {
            case(HLOG_USO):
                read_uso(log, ptr);
                readDataLength += sizeof(blg_uso_t);
                break;
            case(HLOG_UOK):
                read_uok(log, ptr);
                readDataLength += sizeof(blg_uok_t);
                break;
            case(HLOG_USR):
                read_usr(log, ptr);
                readDataLength += sizeof(blg_usr_t);
                break;
            case(HLOG_BKY):
                read_bky(log, ptr);
                readDataLength += sizeof(blg_bky_t);
                break;
            case(HLOG_SBS):
                read_sbs(log, ptr);
                readDataLength += sizeof(blg_sbs_t);
                break;
            case(HLOG_UBS):
                read_ubs(log, ptr);
                readDataLength += sizeof(blg_ubs_t);
                break;
            case(HLOG_BNK):
                read_bnk(log, ptr);
                readDataLength += sizeof(blg_bnk_t);
                break;
            case(HLOG_GET):
                read_get(log, ptr);
                readDataLength += sizeof(blg_get_t);
                break;
            default:
                pt.clear();
                pt.put(ERROR_TAG, ErrorCodes().getErrorMsg(ErrorCodes::Code::eIncorrectTransaction));
                return;
        }
        logs.push_back(std::make_pair("", log));
        ptr = data + readDataLength;
    }
    char hash[65];
    hash[64]='\0';
    hash_t fin;
    finish(fin,total);
    ed25519_key2text(hash,fin,32);
    pt.add_child("logs",logs);
    pt.put("hash",hash);
}

void Hlog::load() {
    fd = std::make_shared<Helper::BlockFileReader>(filename);
    if(!fd->isOpen()) {
        data=(char*)malloc(4);
        bzero(data,4);
        return;
    }
    total=fd->getSize();
    data=(char*)malloc(4+total);
    fd->read(data + 4,total);
    memcpy(data,&total,4);
}

void Hlog::finish(hash_t &hash,int &l) {
    SHA256_Final(hash, &sha256);
    l=total;
}

bool Hlog::save(char* buf,int len) {
    if (!m_save_file.isOpen()) {
        m_save_file.open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0644, false);
        if (!m_save_file.isOpen()) {
            return false;
        }
    }
    SHA256_Update(&sha256,buf,len);
    total+=len;
    return(m_save_file.write(buf,len)==len);
}

char* Hlog::txid(const uint64_t& ppi) {
    ppi_t *p=(ppi_t*)&ppi;
    snprintf(txid_text, sizeof(txid_text),"%04X%08X%04X",p->v16[2],p->v32[0],p->v16[3]);
    return(txid_text);
}

void Hlog::read_uso(boost::property_tree::ptree& pt, char *buffer) {
    blg_uso_t log;
    memcpy(&log, buffer, sizeof(blg_uso_t));
    SHA256_Update(&sha256,(char*)&log,sizeof(log));
    pt.put("type",HLOG_USO);
    pt.put("name","create_account_confirmed");
    pt.put("auser",log.auser);
    pt.put("buser",log.buser);
    pt.put("atxid",txid(log.atxid));
    pt.put("btxid",txid(log.btxid));
}

void Hlog::read_uso(boost::property_tree::ptree& pt) {
    blg_uso_t log;
    log.type=HLOG_USO;
    fd->read((char*)&log+1,sizeof(log)-1);
    SHA256_Update(&sha256,(char*)&log,sizeof(log));
    pt.put("type",HLOG_USO);
    pt.put("name","create_account_confirmed");
    pt.put("auser",log.auser);
    pt.put("buser",log.buser);
    pt.put("atxid",txid(log.atxid));
    pt.put("btxid",txid(log.btxid));
}

bool Hlog::save_uso(uint32_t auser,uint32_t buser,uint64_t atxid,uint64_t btxid) {
    blg_uso_t log;
    log.type=HLOG_USO;
    log.auser=auser;
    log.buser=buser;
    log.atxid=atxid;
    log.btxid=btxid;
    return(save((char*)&log,sizeof(log)));
}

void Hlog::read_uok(boost::property_tree::ptree& pt, char* buffer) {
    blg_uok_t log;
    memcpy(&log, buffer, sizeof(blg_uok_t));
    SHA256_Update(&sha256,(char*)&log,sizeof(log));
    pt.put("type",HLOG_UOK);
    pt.put("name","create_account_late");
    pt.put("abank",log.abank);
    pt.put("auser",log.auser);
    pt.put("buser",log.buser);
    pt.put("btxid",txid(log.btxid));
}

void Hlog::read_uok(boost::property_tree::ptree& pt) {
    blg_uok_t log;
    log.type=HLOG_UOK;
    fd->read((char*)&log+1,sizeof(log)-1);
    SHA256_Update(&sha256,(char*)&log,sizeof(log));
    pt.put("type",HLOG_UOK);
    pt.put("name","create_account_late");
    pt.put("abank",log.abank);
    pt.put("auser",log.auser);
    pt.put("buser",log.buser);
    pt.put("btxid",txid(log.btxid));
}

bool Hlog::save_uok(uint16_t abank,uint32_t auser,uint32_t buser,uint64_t btxid) {
    blg_uok_t log;
    log.type=HLOG_UOK;
    log.abank=abank;
    log.auser=auser;
    log.buser=buser;
    log.btxid=btxid;
    return(save((char*)&log,sizeof(log)));
}

void Hlog::read_usr(boost::property_tree::ptree& pt, char* buffer) {
    blg_usr_t log;
    memcpy(&log, buffer, sizeof(blg_usr_t));
    SHA256_Update(&sha256,(char*)&log,sizeof(log));
    pt.put("type",HLOG_USR);
    pt.put("name","create_account_failed");
    pt.put("auser",log.auser);
    pt.put("bbank",log.bbank);
    pt.put("atxid",txid(log.atxid));
}

void Hlog::read_usr(boost::property_tree::ptree& pt) {
    blg_usr_t log;
    log.type=HLOG_USR;
    fd->read((char*)&log+1,sizeof(log)-1);
    SHA256_Update(&sha256,(char*)&log,sizeof(log));
    pt.put("type",HLOG_USR);
    pt.put("name","create_account_failed");
    pt.put("auser",log.auser);
    pt.put("bbank",log.bbank);
    pt.put("atxid",txid(log.atxid));
}


bool Hlog::save_usr(uint32_t auser,uint16_t bbank,uint64_t atxid) {
    blg_usr_t log;
    log.type=HLOG_USR;
    log.auser=auser;
    log.bbank=bbank;
    log.atxid=atxid;
    return(save((char*)&log,sizeof(log)));
}

void Hlog::read_bky(boost::property_tree::ptree& pt, char* buffer) {
    blg_bky_t log;
    log.type=HLOG_BKY;
    memcpy(&log, buffer, sizeof(blg_bky_t));
    SHA256_Update(&sha256,(char*)&log,sizeof(log));
    pt.put("type",HLOG_BKY);
    pt.put("name","node_unlocked");
    pt.put("bbank",log.bbank);
    pt.put("atxid",txid(log.atxid));
}

void Hlog::read_bky(boost::property_tree::ptree& pt) {
    blg_bky_t log;
    log.type=HLOG_BKY;
    fd->read((char*)&log+1,sizeof(log)-1);
    SHA256_Update(&sha256,(char*)&log,sizeof(log));
    pt.put("type",HLOG_BKY);
    pt.put("name","node_unlocked");
    pt.put("bbank",log.bbank);
    pt.put("atxid",txid(log.atxid));
}


bool Hlog::save_bky(uint16_t bbank,uint64_t atxid) {
    blg_bky_t log;
    log.type=HLOG_BKY;
    log.bbank=bbank;
    log.atxid=atxid;
    return(save((char*)&log,sizeof(log)));
}

void Hlog::read_sbs(boost::property_tree::ptree& pt, char* buffer) {
    blg_sbs_t log;
    log.type=HLOG_SBS;
    memcpy(&log, buffer, sizeof(blg_sbs_t));
    SHA256_Update(&sha256,(char*)&log,sizeof(log));
    pt.put("type",HLOG_SBS);
    pt.put("name","set_node_status");
    pt.put("bbank",log.bbank);
    pt.put("status",log.status);
    pt.put("atxid",txid(log.atxid));
}

void Hlog::read_sbs(boost::property_tree::ptree& pt) {
    blg_sbs_t log;
    log.type=HLOG_SBS;
    fd->read((char*)&log+1,sizeof(log)-1);
    SHA256_Update(&sha256,(char*)&log,sizeof(log));
    pt.put("type",HLOG_SBS);
    pt.put("name","set_node_status");
    pt.put("bbank",log.bbank);
    pt.put("status",log.status);
    pt.put("atxid",txid(log.atxid));
}


bool Hlog::save_sbs(uint16_t bbank,uint32_t status,uint64_t atxid) {
    blg_sbs_t log;
    log.type=HLOG_SBS;
    log.bbank=bbank;
    log.status=status;
    log.atxid=atxid;
    return(save((char*)&log,sizeof(log)));
}

void Hlog::read_ubs(boost::property_tree::ptree& pt, char* buffer) {
    blg_ubs_t log;
    log.type=HLOG_UBS;
    memcpy(&log, buffer, sizeof(blg_ubs_t));
    SHA256_Update(&sha256,(char*)&log,sizeof(log));
    pt.put("type",HLOG_UBS);
    pt.put("name","unset_node_status");
    pt.put("bbank",log.bbank);
    pt.put("status",log.status);
    pt.put("atxid",txid(log.atxid));
}

void Hlog::read_ubs(boost::property_tree::ptree& pt) {
    blg_ubs_t log;
    log.type=HLOG_UBS;
    fd->read((char*)&log+1,sizeof(log)-1);
    SHA256_Update(&sha256,(char*)&log,sizeof(log));
    pt.put("type",HLOG_UBS);
    pt.put("name","unset_node_status");
    pt.put("bbank",log.bbank);
    pt.put("status",log.status);
    pt.put("atxid",txid(log.atxid));
}


bool Hlog::save_ubs(uint16_t bbank,uint32_t status,uint64_t atxid) {
    blg_ubs_t log;
    log.type=HLOG_UBS;
    log.bbank=bbank;
    log.status=status;
    log.atxid=atxid;
    return(save((char*)&log,sizeof(log)));
}

void Hlog::read_bnk(boost::property_tree::ptree& pt, char *buffer) {
    blg_bnk_t log;
    log.type=HLOG_BNK;
    memcpy(&log, buffer, sizeof(blg_bnk_t));
    SHA256_Update(&sha256,(char*)&log,sizeof(log));
    pt.put("type",HLOG_BNK);
    pt.put("name","create_node");
    pt.put("auser",log.auser);
    pt.put("bbank",log.bbank);
    pt.put("atxid",txid(log.atxid));
}

void Hlog::read_bnk(boost::property_tree::ptree& pt) {
    blg_bnk_t log;
    log.type=HLOG_BNK;
    fd->read((char*)&log+1,sizeof(log)-1);
    SHA256_Update(&sha256,(char*)&log,sizeof(log));
    pt.put("type",HLOG_BNK);
    pt.put("name","create_node");
    pt.put("auser",log.auser);
    pt.put("bbank",log.bbank);
    pt.put("atxid",txid(log.atxid));
}


bool Hlog::save_bnk(uint32_t auser,uint16_t bbank,uint64_t atxid) {
    blg_bnk_t log;
    log.type=HLOG_BNK;
    log.auser=auser;
    log.bbank=bbank;
    log.atxid=atxid;
    return(save((char*)&log,sizeof(log)));
}

void Hlog::read_get(boost::property_tree::ptree& pt, char* buffer) {
    blg_get_t log;
    log.type=HLOG_GET;
    memcpy(&log, buffer, sizeof(blg_get_t));
    SHA256_Update(&sha256,(char*)&log,sizeof(log));
    pt.put("type",HLOG_GET);
    pt.put("name","retrieve_funds");
    pt.put("auser",log.auser);
    pt.put("bbank",log.bbank);
    pt.put("buser",log.buser);
    pt.put("atxid",txid(log.atxid));
}

void Hlog::read_get(boost::property_tree::ptree& pt) {
    blg_get_t log;
    log.type=HLOG_GET;
    fd->read((char*)&log+1,sizeof(log)-1);
    SHA256_Update(&sha256,(char*)&log,sizeof(log));
    pt.put("type",HLOG_GET);
    pt.put("name","retrieve_funds");
    pt.put("auser",log.auser);
    pt.put("bbank",log.bbank);
    pt.put("buser",log.buser);
    pt.put("atxid",txid(log.atxid));
}


bool Hlog::save_get(uint32_t auser,uint16_t bbank,uint32_t buser,uint64_t atxid) {
    blg_get_t log;
    log.type=HLOG_BNK;
    log.auser=auser;
    log.bbank=bbank;
    log.buser=buser;
    log.atxid=atxid;
    return(save((char*)&log,sizeof(log)));
}

}
