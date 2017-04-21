#ifndef PEER_HPP
#define PEER_HPP

class peer : public boost::enable_shared_from_this<peer>
{
public:
  peer(server& srv,bool in,servers& srvs,options& opts) :
      svid(0),
      do_sync(1), //remove, use peer_hs.do_sync
      killme(false),
      peer_io_service_(),
      work_(peer_io_service_),
      socket_(peer_io_service_),
      server_(srv),
      incoming_(in),
      srvs_(srvs),
      opts_(opts),
      addr(""),
      port(0),
      files_out(0),
      files_in(0),
      bytes_out(0),
      bytes_in(0),
      BLOCK_MODE_ERROR(0),
      BLOCK_MODE_SERVER(0),
      BLOCK_MODE_PEER(0)
  { read_msg_ = boost::make_shared<message>();
    iothp_= new boost::thread(boost::bind(&peer::iorun,this));
  }

  ~peer()
  { if(port||1){
      uint32_t ntime=time(NULL);
      fprintf(stderr,"%04X PEER destruct %s:%d @%08X log: blk/%03X/%05X/log.txt\n\n",svid,addr.c_str(),port,ntime,srvs_.now>>20,srvs_.now&0xFFFFF);
      }
  }

  void iorun()
  { LOG("%04X PEER IORUN START\n",svid);
    try{
      peer_io_service_.run();
      } //Now we know the server is down.
    catch (std::exception& e){
//FIXME, stop peer after Broken pipe (now does not stop if peer ends with 'assert')
//FIXME, wipe out inactive peers (better solution)
      LOG("%04X PERR IORUN Service.Run error:%s\n",svid,e.what());
      killme=true;}
    LOG("%04X PEER IORUN END\n",svid);
  }

  void stop() // by server only
  { LOG("%04X PEER KILL\n",svid);
    peer_io_service_.stop();
    //LOG("%04X PEER INTERRUPT\n",svid);
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    iothp_->interrupt();
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    //LOG("%04X PEER JOIN\n",svid);
    iothp_->join(); //try joining yourself error
    //LOG("%04X PEER CLOSE\n",svid);
    socket_.close();
    //socket_.release(NULL);
  }

  void leave()
  { LOG("%04X PEER LEAVING\n",svid);
    killme=true;
  }

  void accept() //only incoming connections
  { assert(incoming_);
    addr = socket_.remote_endpoint().address().to_string();
    port = socket_.remote_endpoint().port();
    LOG("%04X PEER CONNECT OK %s:%d\n",svid,
      socket_.remote_endpoint().address().to_string().c_str(),socket_.remote_endpoint().port());
    boost::asio::async_read(socket_,
      boost::asio::buffer(read_msg_->data,message::header_length),
      boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
  }

  void connect(const boost::system::error_code& error) //only outgoing connection
  { if(error){
      LOG("%04X PEER ACCEPT ERROR\n",svid);
      killme=true;
      return;}
    assert(!incoming_);
    addr = socket_.remote_endpoint().address().to_string();
    port = socket_.remote_endpoint().port();
    LOG("%04X PEER ACCEPT OK %s:%d\n",svid,
      socket_.remote_endpoint().address().to_string().c_str(),socket_.remote_endpoint().port());
    message_ptr msg=server_.write_handshake(0,sync_hs); // sets sync_hs
    msg->know.insert(svid);
    msg->sent.insert(svid);
    msg->busy.insert(svid);
    msg->print(" HANDSHAKE");
    write_msgs_.push_back(msg);
    boost::asio::async_write(socket_,boost::asio::buffer(msg->data,msg->len),
      boost::bind(&peer::handle_write,shared_from_this(),boost::asio::placeholders::error));
    boost::asio::async_read(socket_,
      boost::asio::buffer(read_msg_->data,message::header_length),
      boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
  }

  boost::asio::ip::tcp::socket& socket()
  { return socket_;
  }

  void print()
  { if(port){
      LOG("%04X Client on     %s:%d\n",svid,addr.c_str(),port);} //LESZEK
    else{
      LOG("%04X Client available\n",svid);}
  }

  void update(message_ptr msg)
  { 
    msg->print("; TRY UPDATE");
    if(do_sync){
      //LOG("%04X HASH %016lX [%016lX] (in sync mode) %04X:%08X\n",svid,put_msg->hash.num,*((uint64_t*)put_msg->data),msg->svid,msg->msid); // could be bad allignment
      msg->print("; NO SYNC");
      return;}
    assert(msg->len>4+64);
    if(msg->peer == svid){
      //LOG("%04X HASH %016lX [%016lX] (noecho) %04X:%08X\n",svid,put_msg->hash.num,*((uint64_t*)put_msg->data),msg->svid,msg->msid); // could be bad allignment
      msg->print("; NO ECHO");
      return;}
    msg->mtx_.lock();
    if(msg->know.find(svid) != msg->know.end()){
      //LOG("%04X HASH %016lX [%016lX] (known) %04X:%08X\n",svid,put_msg->hash.num,*((uint64_t*)put_msg->data),msg->svid,msg->msid); // could be bad allignment
      msg->print("; NO UPDATE");
      msg->mtx_.unlock();
      return;}
    msg->know.insert(svid);
    msg->mtx_.unlock();
    message_ptr put_msg(new message()); // gone ??? !!!
    switch(msg->hashtype()){
      case MSGTYPE_MSG:
        put_msg->data[0]=MSGTYPE_PUT;
        put_msg->data[1]=msg->hashval(svid); //msg->data[4+(svid%64)]; // convert to peer-specific hash
        memcpy(put_msg->data+2,&msg->msid,4);
        memcpy(put_msg->data+6,&msg->svid,2);
        break;
      case MSGTYPE_CND:
        put_msg->data[0]=MSGTYPE_CNP;
        put_msg->data[1]=msg->hashval(svid); //msg->data[4+(svid%64)]; // convert to peer-specific hash
        memcpy(put_msg->data+2,&msg->msid,4);
        memcpy(put_msg->data+6,&msg->svid,2);
        break;
      case MSGTYPE_BLK:
        put_msg->data[0]=MSGTYPE_BLP;
        put_msg->data[1]=msg->hashval(svid); //msg->data[4+(svid%64)]; // convert to peer-specific hash
        memcpy(put_msg->data+2,&msg->msid,4);
        memcpy(put_msg->data+6,&msg->svid,2);
        break;
      case MSGTYPE_DBL:
        put_msg->data[0]=MSGTYPE_DBP;
        put_msg->data[1]=0;
        memcpy(put_msg->data+2,&msg->msid,4);
        memcpy(put_msg->data+6,&msg->svid,2);
        break;
      default:
        LOG("%04X FATAL ERROR: bad message type\n",svid);
        exit(-1);}
    mtx_.lock();
    put_msg->svid=msg->svid;
    put_msg->msid=msg->msid;
    put_msg->hash.num=put_msg->dohash(put_msg->data);
    put_msg->sent.insert(svid);
    put_msg->busy.insert(svid);
    LOG("%04X HASH %016lX [%016lX] (update) %04X:%08X\n",svid,put_msg->hash.num,*((uint64_t*)put_msg->data),msg->svid,msg->msid); // could be bad allignment
    if(BLOCK_MODE_SERVER){
      wait_msgs_.push_back(put_msg);
      mtx_.unlock();
      LOG("%04X HASH %016lX [%016lX] (update in block mode, waiting) %04X:%08X\n",svid,put_msg->hash.num,*((uint64_t*)put_msg->data),msg->svid,msg->msid); // could be bad allignment
      return;}
    bool no_write_in_progress = write_msgs_.empty();
    write_msgs_.push_back(put_msg);
    if (no_write_in_progress) {
      //int len=message_len(write_msgs_.front());
      int len=write_msgs_.front()->len;
      boost::asio::async_write(socket_,boost::asio::buffer(write_msgs_.front()->data,len),
        boost::bind(&peer::handle_write,shared_from_this(),boost::asio::placeholders::error)); }
    mtx_.unlock();
  }

  /*int message_len(message_ptr msg) // shorten candidate vote messages if possible
  { if(do_sync){
      return(msg->len);}
    assert(msg->data!=NULL);
    //if(msg->data[0]==MSGTYPE_CNG && msg->len>4+64+10+sizeof(hash_t)){//shorten message if peer knows this hash
    if(msg->data[0]==MSGTYPE_CND && msg->len>4+64+10+sizeof(hash_t)){//shorten message if peer knows this hash
      //hash_s cand;
      //memcpy(cand.hash,msg->data+4+64+10,sizeof(hash_t));
      //candidate_ptr c_ptr=server_.known_candidate(cand,0);
      hash_s* cand=(hash_s*)(msg->data+4+64+10);
      candidate_ptr c_ptr=server_.known_candidate(*cand,0);
      if(c_ptr!=NULL && c_ptr->peers.find(svid)!=c_ptr->peers.end()){ // send only the hash, not the whole message
//FIXME, is this ok ?
	LOG("%04X WARNING, truncating cnd message\n",svid);
        return(4+64+10+sizeof(hash_t));}}
    //if(msg->data[0]==MSGTYPE_BLK && msg->len>4+64+10){
    if(msg->data[0]==MSGTYPE_BLG && msg->len>4+64+10){ // probably only during sync
      header_t* h=(header_t*)(msg->data+4+64+10);
      if(peer_hs.head.now==h->now && !memcmp(peer_hs.head.nowhash,h->nowhash,SHA256_DIGEST_LENGTH)){
	LOG("%04X WARNING, truncating blk message\n",svid);
        return(4+64+10);}}
    return(msg->len);
  }*/

  void deliver(message_ptr msg)
  { if(do_sync){
      return;}
    //if(msg->status==MSGSTAT_SAV){
    //  if(!msg->load()){
    //    LOG("%04X DELIVER problem for %04X:%08X\n",svid,msg->svid,msg->msid);
    //    msg->mtx_.unlock();
    //    return;}}
    //msg->busy.insert(svid);
    msg->mtx_.lock();//TODO, do this right before sending the message ?
    if(msg->sent.find(svid)!=msg->sent.end()){
      LOG("%04X REJECTING download request for %0X4:%08X (late)\n",svid,msg->svid,msg->msid);
      msg->mtx_.unlock();
      return;}
    msg->know.insert(svid);
    msg->sent.insert(svid);
    msg->busy.insert(svid);
    msg->mtx_.unlock();
    if(msg->len!=message::header_length && !msg->load(svid)){ // sets busy[svid]
      LOG("%04X ERROR failed to load message %0X4:%08X\n",svid,msg->svid,msg->msid);
      return;}
    mtx_.lock();
    if(BLOCK_MODE_SERVER){
      wait_msgs_.push_back(msg);
      mtx_.unlock();
      return;}
    assert(msg->data!=NULL);
    if(msg->data[0]==MSGTYPE_STP){
      LOG("%04X SERV in block mode\n",svid);
      BLOCK_MODE_SERVER=1;} // will wait for delivery and queue other messages to wait_msgs_
    //special handling of CND messages ... FIXME prevent slow search in candidates by each peer
    if(msg->data[0]==MSGTYPE_CND && msg->len>4+64+10+sizeof(hash_t)){
      // this is very innefficient !!! server_.condidates_ should be a map of messages that have peers[] ...
      hash_s* cand=(hash_s*)(msg->data+4+64+10);
      candidate_ptr c_ptr=server_.known_candidate(*cand,0);
      if(c_ptr!=NULL && c_ptr->peers.find(svid)!=c_ptr->peers.end()){
        // send only the hash, not the whole message
        LOG("%04X WARNING, truncating cnd message\n",svid);
        message_ptr put_msg(new message(4+64+10+sizeof(hash_t),msg->data));
        memcpy(put_msg->data+1,&put_msg->len,3); //FIXME, make this a change_length function in message.hpp
        put_msg->svid=msg->svid; //just for reporting
        put_msg->msid=msg->msid; //just for reporting
        put_msg->sent.insert(svid);
        put_msg->busy.insert(svid);
        msg=put_msg;}
      else{
        //FIXME, create new message and remove msids that are identical to our last_msid list
        }}
    bool no_write_in_progress = write_msgs_.empty();
    write_msgs_.push_back(msg);
    if (no_write_in_progress){
      //int len=message_len(write_msgs_.front());
      int len=write_msgs_.front()->len;
      boost::asio::async_write(socket_,boost::asio::buffer(write_msgs_.front()->data,len),
        boost::bind(&peer::handle_write,shared_from_this(),boost::asio::placeholders::error));
    }
    mtx_.unlock();
  }

  void send_sync(message_ptr put_msg)
  {
/*
WAITING for headers 58B95160, maybe need more peers
REQUEST more headers from peer 0001
SENDING block header request for 58B95160
main: peer.hpp:203: void peer::send_sync(message_ptr): Assertion `do_sync' failed.
Aborted
*/
    //assert(do_sync); //FIXME, failed !!!
//FIXME, brakes after handle_read_headers() if there are more headers to load
    put_msg->load(svid);
    //put_msg->busy_insert(svid);//TODO, do this right before sending the message ?
    mtx_.lock(); //most likely no lock needed
    boost::asio::write(socket_,boost::asio::buffer(put_msg->data,put_msg->len));
    mtx_.unlock();
    if(put_msg->len!=message::header_length){
//FIXME, do not unload everything ...
      put_msg->unload(svid);}
  }

  void handle_write(const boost::system::error_code& error) //TODO change this later, dont send each message separately if possible
  {
    //std::cerr << "HANDLE WRITE start\n";
    if (!error) {
      mtx_.lock();
      message* msg=&(*(write_msgs_.front()));
      //msg->busy.erase(svid); // will not work if same message queued 2 times
      msg->mtx_.lock();
      assert(msg->sent.find(svid)!=msg->sent.end()); //WILL fail if we send the same message to 1 peer 2 times
      assert(msg->busy.find(svid)!=msg->busy.end()); //WILL fail if we send the same message to 1 peer 2 times
      assert(msg->data!=NULL);
      //msg->sent.insert(svid);
      uint32_t len=msg->len;
      if(msg->len>64){ //find length submitted to peer
        len=0;
        memcpy(&len,msg->data+1,3);}
      msg->mtx_.unlock();
      bytes_out+=msg->len;
      files_out++;
      if(msg->len>64 && len!=msg->len){
        LOG("%04X DELIVERED MESSAGE %04X:%08X %02X (len:%d<>%d) [total %lub %uf] LENGTH DIFFERS!\n",svid,
          msg->svid,msg->msid,msg->data[0],msg->len,len,bytes_out,files_out);}
      else{
        LOG("%04X DELIVERED MESSAGE %04X:%08X %02X (len:%d) [total %lub %uf]\n",svid,
          msg->svid,msg->msid,msg->data[0],msg->len,bytes_out,files_out);}
      if(msg->data[0]==MSGTYPE_STP){
        LOG("%04X SERV sent STOP\n",svid);
        BLOCK_MODE_SERVER=2; // STOP sent
        //msg->unload(svid);
        write_msgs_.pop_front();
        if(BLOCK_MODE_PEER){ // write_peer_missing_messages already queued
          mtx_.unlock();
          write_peer_missing_messages();
          return;}
        mtx_.unlock();
        return;}
      if(!server_.do_sync && !do_sync && (msg->data[0]==MSGTYPE_PUT || msg->data[0]==MSGTYPE_DBP)){
//FIXME, do not add messages that are in last block
        if(svid_msid_new[msg->svid]<msg->msid){
          svid_msid_new[msg->svid]=msg->msid; // maybe a lock on svid_msid_new would help
          LOG("%04X UPDATE PEER SVID_MSID: %04X:%08X\n",svid,msg->svid,msg->msid);}}
      if(msg->len!=message::header_length){
        msg->unload(svid);}
      else{
        msg->busy_erase(svid);}
      write_msgs_.pop_front();
      if (!write_msgs_.empty()) {
        //FIXME, now load the message from db if needed !!! do not do this when inserting in write_msgs_, unless You do not worry about RAM but worry about speed
        //int len=message_len(write_msgs_.front());
        int len=write_msgs_.front()->len;
        boost::asio::async_write(socket_,boost::asio::buffer(write_msgs_.front()->data,len),
          boost::bind(&peer::handle_write,shared_from_this(),boost::asio::placeholders::error)); }
      mtx_.unlock(); }
    else {
      LOG("%04X WRITE error %d %s\n",svid,error.value(),error.message().c_str());
      leave();
      return;}
  }

  void handle_read_header(const boost::system::error_code& error)
  {
    if(error){
      LOG("%04X READ error %d %s (HEADER)\n",svid,error.value(),error.message().c_str());
      leave();
      return;}
    if(!read_msg_->header(svid)){
      LOG("%04X READ header error\n",svid);
      leave();
      return;}
    bytes_in+=read_msg_->len;
    files_in++;
    read_msg_->know.insert(svid);
    if(read_msg_->len==message::header_length){
      if(!read_msg_->svid || srvs_.nodes.size()<=read_msg_->svid){ //unknown svid
        LOG("%04X ERROR message from unknown server %04X:%08X\n",svid,read_msg_->svid,read_msg_->msid);
        leave();
        return;}
      if(read_msg_->data[0]==MSGTYPE_PUT || read_msg_->data[0]==MSGTYPE_CNP || read_msg_->data[0]==MSGTYPE_BLP || read_msg_->data[0]==MSGTYPE_DBP){
        LOG("%04X HASH %016lX [%016lX]\n",svid,read_msg_->hash.num,*((uint64_t*)read_msg_->data)); // could be bad allignment
        if(read_msg_->data[0]==MSGTYPE_PUT){
          read_msg_->data[0]=MSGTYPE_GET;}
        if(read_msg_->data[0]==MSGTYPE_CNP){
          read_msg_->data[0]=MSGTYPE_CNG;}
        if(read_msg_->data[0]==MSGTYPE_BLP){
          read_msg_->data[0]=MSGTYPE_BLG;}
        if(read_msg_->data[0]==MSGTYPE_DBP){
          read_msg_->data[0]=MSGTYPE_DBG;}
        if(read_msg_->data[0]==MSGTYPE_GET || read_msg_->data[0]==MSGTYPE_DBG){
          mtx_.lock(); //concider locking svid_msid_new
          if(svid_msid_new[read_msg_->svid]<read_msg_->msid){
            svid_msid_new[read_msg_->svid]=read_msg_->msid;
            LOG("%04X UPDATE PEER SVID_MSID: %04X:%08X\n",svid,read_msg_->svid,read_msg_->msid);}
          mtx_.unlock();}
        if(server_.message_insert(read_msg_)>0){ //NEW, make sure to insert in correct containers
          if(read_msg_->data[0]==MSGTYPE_CNG ||
              (read_msg_->data[0]==MSGTYPE_BLG && server_.last_srvs_.now>=read_msg_->msid && (server_.last_srvs_.nodes[read_msg_->svid].status & SERVER_VIP)) ||
              read_msg_->data[0]==MSGTYPE_DBG ||
              (read_msg_->data[0]==MSGTYPE_GET && srvs_.nodes[read_msg_->svid].msid==read_msg_->msid-1 && server_.check_msgs_size()<MAX_CHECKQUE)){
            //std::cerr << "REQUESTING MESSAGE from "<<svid<<" ("<<read_msg_->svid<<":"<<read_msg_->msid<<")\n";
            LOG("%04X REQUESTING MESSAGE (%04X:%08X)\n",svid,read_msg_->svid,read_msg_->msid);
            //read_msg_->busy_insert(svid);
#if BLOCKSEC == 0x20
            boost::this_thread::sleep(boost::posix_time::milliseconds(rand()%1000));
#endif
            deliver(read_msg_);}}} // request message if not known (inserted)
      else if(read_msg_->data[0]==MSGTYPE_GET || read_msg_->data[0]==MSGTYPE_CNG || read_msg_->data[0]==MSGTYPE_BLG || read_msg_->data[0]==MSGTYPE_DBG){
        LOG("%04X HASH %016lX [%016lX]\n",svid,read_msg_->hash.num,*((uint64_t*)read_msg_->data)); // could be bad allignment
	if(do_sync){
          read_msg_->path=peer_path;
          if(read_msg_->load(svid)){
            //std::cerr << "PROVIDING MESSAGE (in sync)\n";
            LOG("%04X PROVIDING MESSAGE %04X:%08X %02X (len:%d) (in sync)\n",svid,read_msg_->svid,read_msg_->msid,read_msg_->hash.dat[1],read_msg_->len);
            send_sync(read_msg_);} //deliver will not work in do_sync mode
          else{
            //std::cerr << "FAILED answering request for "<<read_msg_->svid<<":"<<read_msg_->msid<<" from "<<svid<<" (message not found in:"<<peer_path<<")\n";
            LOG("%04X FAILED answering request for %04X:%08X (not found in %08X)\n",svid,read_msg_->svid,read_msg_->msid,peer_path);}}
        else{
          message_ptr msg=server_.message_find(read_msg_,svid);
          if(msg!=NULL){
            if(msg->len>message::header_length){
              if(msg->sent.find(svid)!=msg->sent.end()){
                //if(peer_hs.head.now+BLOCKSEC < msg->path ){ //FIXME, check correct condition !
                  //std::cerr << "REJECTING download request for "<<msg->svid<<":"<<msg->msid<<" from "<<svid<<"\n";
                  LOG("%04X REJECTING download request for %0X4:%08X\n",svid,msg->svid,msg->msid);
                //}
                //else{
                //  std::cerr << "ACCEPTING download request for "<<msg->svid<<":"<<msg->msid<<" from "<<svid<<"\n";
                //  deliver(msg);}
                }
              else{
                //std::cerr << "PROVIDING MESSAGE\n";
                LOG("%04X PROVIDING MESSAGE %04X:%08X %02X (len:%d)\n",svid,msg->svid,msg->msid,msg->hash.dat[1],msg->len);
                //msg->sent_insert(svid); // handle_write does this
#if BLOCKSEC == 0x20
                boost::this_thread::sleep(boost::posix_time::milliseconds(rand()%1000));
#endif
                deliver(msg);}} // must force deliver without checks
            else{ // no real message available
              //std::cerr << "BAD get request from " << std::to_string(svid) << "\n";
              LOG("%04X BAD get request\n",svid);}}
          else{// concider loading from db if available
            //std::cerr <<"MESSAGE "<< read_msg_->svid <<":"<< read_msg_->msid <<" not found, concider loading from db\n\n\n";
            LOG("%04X MESSAGE %04X:%08X not found, concider loading from db\n\n\n",svid,
              read_msg_->svid,read_msg_->msid);}}}
      else if(read_msg_->data[0]==MSGTYPE_HEA){
	write_headers();}
      else if(read_msg_->data[0]==MSGTYPE_NHR){
	write_headers();}
      else if(read_msg_->data[0]==MSGTYPE_SER){ //servers request
	write_servers();}
      else if(read_msg_->data[0]==MSGTYPE_MSL){ //msg list request
	write_msglist();}
      else if(read_msg_->data[0]==MSGTYPE_USG){
	write_bank();}
      else if(read_msg_->data[0]==MSGTYPE_PAT){ //set current sync block
        memcpy(&peer_path,read_msg_->data+1,4);
	//std::cerr<<"DEBUG, got sync path "<<peer_path<<"\n";
	LOG("%04X DEBUG, got sync path %08X\n",svid,peer_path);}
      else if(read_msg_->data[0]==MSGTYPE_SOK){
        uint32_t now;
        memcpy(&now,read_msg_->data+1,4);
        //std::cerr << "Authenticated, peer in sync at "<<now<<"\n";
        LOG("%04X Authenticated, peer in sync at %08X\n",svid,now);
        update_sync();
        do_sync=0;}
      else{
        int n=read_msg_->data[0];
        //std::cerr << "ERROR message type " << std::to_string(n) << "received \n";
        LOG("%04X ERROR message type %02X received\n",svid,n);
        leave();
        return;}
      read_msg_ = boost::make_shared<message>();
      boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_->data,message::header_length),
        boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));}
    else{
      if(read_msg_->data[0]==MSGTYPE_STP){
        LOG("%04X PEER in block mode\n",svid);
        //could enter a different (sync) read sequence here;
        //read_msg_->data=(uint8_t*)std::realloc(read_msg_->data,read_msg_->len);
        assert(read_msg_->len==1+SHA256_DIGEST_LENGTH);
        boost::asio::async_read(socket_,
          boost::asio::buffer(read_msg_->data+message::header_length,read_msg_->len-message::header_length),
          boost::bind(&peer::handle_read_stop,shared_from_this(),boost::asio::placeholders::error));
	return;}
      if(read_msg_->data[0]==MSGTYPE_MSP){
        LOG("%04X READ msglist header\n",svid);
        boost::asio::async_read(socket_,
          boost::asio::buffer(read_msg_->data+message::header_length,read_msg_->len-message::header_length),
          boost::bind(&peer::handle_read_msglist,shared_from_this(),boost::asio::placeholders::error));
	return;}
      if(read_msg_->data[0]==MSGTYPE_USR){
        //FIXME, accept only if needed !!
        //std::cerr << "READ bank "<<read_msg_->svid<<"["<<read_msg_->len<<"] \n";
        LOG("%04X READ bank %04X [len %08X]\n",svid,read_msg_->svid,read_msg_->len);
        boost::asio::async_read(socket_,
          boost::asio::buffer(read_msg_->data+message::header_length,read_msg_->len*sizeof(user_t)),
          boost::bind(&peer::handle_read_bank,shared_from_this(),boost::asio::placeholders::error));
	return;}
      if(read_msg_->data[0]==MSGTYPE_BLK){
        LOG("%04X READ block header\n",svid);
        assert(read_msg_->len==4+64+10+sizeof(header_t) || read_msg_->len==4+64+10);
        boost::asio::async_read(socket_,
          boost::asio::buffer(read_msg_->data+message::header_length,read_msg_->len-message::header_length),
          boost::bind(&peer::handle_read_block,shared_from_this(),boost::asio::placeholders::error));
	return;}
      if(read_msg_->data[0]==MSGTYPE_NHD){
        LOG("%04X READ next header\n",svid);
        assert(read_msg_->len==8+SHA256_DIGEST_LENGTH+sizeof(headlink_t));
        boost::asio::async_read(socket_,
          boost::asio::buffer(read_msg_->data+message::header_length,read_msg_->len-message::header_length),
          boost::bind(&peer::handle_next_header,shared_from_this(),boost::asio::placeholders::error));
	return;}
      //std::cerr << "WAIT read body\n";
      boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_->data+message::header_length,read_msg_->len-message::header_length),
        boost::bind(&peer::handle_read_body,shared_from_this(),boost::asio::placeholders::error));}
  }

  void update_sync(void) // send current inventory (all msg and dbl messages)
  { std::vector<uint64_t>txs;
    std::vector<uint64_t>dbl;
    server_.update_list(txs,dbl,svid);
    std::string data;
//TODO update svid_msid_new ??? [work without]
    for(auto it=txs.begin();it!=txs.end();it++){
      data.append((const char*)&(*it),sizeof(uint64_t));}
    for(auto it=dbl.begin();it!=dbl.end();it++){
      data.append((const char*)&(*it),sizeof(uint64_t));}
    message_ptr put_msg(new message(data.size()));
    memcpy(put_msg->data,data.c_str(),data.size());
    LOG("%04X SENDING update with %d bytes\n",svid,(int)data.size());
    send_sync(put_msg);
  }

  void write_headers()
  { uint32_t from;
    memcpy(&from,read_msg_->data+1,4);
    uint32_t to=sync_hs.head.now;
    if(from>to){ // expect _NHR
      LOG("%04X SENDING block header %08X\n",svid,from); 
      message_ptr put_msg(new message(8+SHA256_DIGEST_LENGTH+sizeof(headlink_t)));
      headlink_t link;
      servers linkservers;
      linkservers.now=from;
      if(!linkservers.header_get()){
	LOG("%04X ERROR, failed to provide header links\n",svid);
        leave(); // consider updating client
        return;}
      put_msg->data[0]=MSGTYPE_NHD;
      memcpy(put_msg->data+1,&from,4);
      memcpy((char*)put_msg->data+8,(const char*)linkservers.oldhash,SHA256_DIGEST_LENGTH);
      linkservers.filllink(link);
      memcpy((char*)put_msg->data+8+SHA256_DIGEST_LENGTH,(const char*)&link,sizeof(headlink_t));
      send_sync(put_msg);
      return;}
    uint32_t num=((to-from)/BLOCKSEC)-1;
    if(num<=0){
      //std::cerr<<"ERROR, failed to provide correct request (from:"<<from<<" to:"<<to<<")\n";
      LOG("%04X ERROR, failed to provide correct request (from:%08X to:%08X)\n",svid,from,to);
      leave(); // consider updateing client
      return;}
    //std::cerr<<"SENDING block headers starting after "<<from<<" and ending before "<<to<<" ("<<num<<")\n"; 
    LOG("%04X SENDING block headers starting after %08X and ending before %08X (num:%d)\n",svid,from,to,num); 
    message_ptr put_msg(new message(SHA256_DIGEST_LENGTH+sizeof(headlink_t)*num));
    char* data=(char*)put_msg->data;
    for(uint32_t now=from+BLOCKSEC;now<to;now+=BLOCKSEC){
      headlink_t link;
      servers linkservers;
      linkservers.now=now;
      if(!linkservers.header_get()){
	LOG("%04X ERROR, failed to provide header links\n",svid);
        leave(); // consider updateing client
        return;}
      if(now==from+BLOCKSEC){
	memcpy(data,(const char*)linkservers.oldhash,SHA256_DIGEST_LENGTH);
	data+=SHA256_DIGEST_LENGTH;}
      linkservers.filllink(link);
      memcpy(data,(const char*)&link,sizeof(headlink_t));
      data+=sizeof(headlink_t);}
    assert(data==(char*)(put_msg->data+SHA256_DIGEST_LENGTH+sizeof(headlink_t)*num));
    send_sync(put_msg);
  }

  void request_next_headers(uint32_t now) //WARNING, this requests a not validated header
  { if(peer_hs.head.now>=now){ // this peer should send this header anytime soon
      return;}
    LOG("%04X SENDING block header request for %08X\n",svid,now);
    message_ptr put_msg(new message());
    put_msg->data[0]=MSGTYPE_NHR;
    memcpy(put_msg->data+1,&now,4);
    send_sync(put_msg);
  }

//FIXME, this will fail because there is another read queued :-(
//must send a new message type (not _HEA)
/*
    int len=SHA256_DIGEST_LENGTH+sizeof(headlink_t);
    read_msg_ = boost::make_shared<message>(len);
    boost::asio::async_read(socket_,
      boost::asio::buffer(read_msg_->data,len),
      boost::bind(&peer::handle_next_headers,shared_from_this(),boost::asio::placeholders::error,now,nowhash));
    //char* data=(char*)malloc(SHA256_DIGEST_LENGTH+sizeof(headlink_t));
    //ERROR, this is blocking !!!
    //int len=boost::asio::read(socket_,boost::asio::buffer(data,SHA256_DIGEST_LENGTH+sizeof(headlink_t)));
  }
*/

  //void handle_next_headers(const boost::system::error_code& error,uint32_t next_now,uint8_t* next_nowhash)
  void handle_next_header(const boost::system::error_code& error)
  { if(error){
      LOG("%04X ERROR reading next headers\n",svid);
      leave();
      return;}
    //if(read_msg_->len!=(int)(SHA256_DIGEST_LENGTH+sizeof(headlink_t)+8)){
    //  std::cerr << "READ next headers error\n";
    //  leave();
    //  return;}
    uint32_t from;
    memcpy(&from,read_msg_->data+1,4);
    char* data=(char*)read_msg_->data+8;
    LOG("%04X PROCESSING block header request\n",svid);
    servers peer_ls;
    headlink_t* link=(headlink_t*)(data+SHA256_DIGEST_LENGTH);
    peer_ls.loadlink(*link,from,data);
    /*if(memcmp(peer_ls.oldhash,next_nowhash,SHA256_DIGEST_LENGTH)){
      std::cerr << "ERROR, initial oldhash mismatch :-(\n";
      char hash[2*SHA256_DIGEST_LENGTH];
      ed25519_key2text(hash,peer_ls.oldhash,SHA256_DIGEST_LENGTH);
      LOG("%04X NOWHASH got  %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);
      ed25519_key2text(hash,next_nowhash,SHA256_DIGEST_LENGTH);
      LOG("%04X NOWHASH have %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);
      leave();
      std::cerr << "\nMaybe start syncing from an older block (peer will disconnect)\n\n";
      leave();
      return;}*/
    server_.peer_.lock();
    if(server_.headers.back().now==from-BLOCKSEC &&
       !memcmp(peer_ls.oldhash,server_.headers.back().nowhash,SHA256_DIGEST_LENGTH)){
      server_.headers.insert(server_.headers.end(),peer_ls);}
    server_.peer_.unlock();
    read_msg_ = boost::make_shared<message>(); // continue with a fresh message container
    boost::asio::async_read(socket_,
      boost::asio::buffer(read_msg_->data,message::header_length),
      boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
    return;
  }

  void handle_read_headers()
  { uint32_t to=peer_hs.head.now;
    LOG("%04X READ HEADERS\n",svid);
    servers sync_ls; //FIXME, use only the header data not "servers"
    sync_ls.loadhead(sync_hs.head);
    server_.peer_.lock();
    if(server_.headers.size()){
      LOG("%04X USE last header\n",svid);
      sync_ls=server_.headers.back();} // FIXME, use pointers/references maybe
    server_.peer_.unlock();
    uint32_t from=sync_ls.now;
    if(to<BLOCKSEC+from){
      return;}
    uint32_t num=(to-from)/BLOCKSEC;
    assert(num>0);
    std::vector<servers> headers(num); //TODO, consider changing this to a list
    if(num>1){
      //if(server_.slow_sync(false,headers)<0){
      //  return;}
      LOG("%04X SENDING block headers request\n",svid);
      message_ptr put_msg(new message());
      put_msg->data[0]=MSGTYPE_HEA;
      memcpy(put_msg->data+1,&from,4);
      send_sync(put_msg);
      //std::cerr<<"READING block headers starting after "<<from<<" and ending before "<<to<<" ("<<(num-1)<<")\n"; 
      LOG("%04X READING block headers starting after %08X and ending before %08X (num:%d)\n",svid,from,to,num-1); 
      char* data=(char*)malloc(SHA256_DIGEST_LENGTH+sizeof(headlink_t)*(num-1)); assert(data!=NULL);
      int len=boost::asio::read(socket_,boost::asio::buffer(data,SHA256_DIGEST_LENGTH+sizeof(headlink_t)*(num-1)));
      if(len!=(int)(SHA256_DIGEST_LENGTH+sizeof(headlink_t)*(num-1))){
        LOG("%04X READ headers error\n",svid);
        free(data);
        leave();
        return;}
      //if(memcmp(data,sync_hs.head.oldhash,SHA256_DIGEST_LENGTH)){
      //  std::cerr << "ERROR, initial oldhash mismatch :-(\n";
      //  char hash[2*SHA256_DIGEST_LENGTH];
      //  ed25519_key2text(hash,data,SHA256_DIGEST_LENGTH);
      //  LOG("%04X OLDHASH got  %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);
      //  ed25519_key2text(hash,sync_hs.head.oldhash,SHA256_DIGEST_LENGTH);
      //  LOG("%04X OLDHASH have %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);
      //  free(data);
      //  leave();
      //  return;}
      char* d=data+SHA256_DIGEST_LENGTH;
      //reed hashes and compare
      for(uint32_t i=0,now=from+BLOCKSEC;now<to;now+=BLOCKSEC,i++){
        headlink_t* link=(headlink_t*)d;
        d+=sizeof(headlink_t);
        if(!i){
          headers[i].loadlink(*link,now,data);}
        else{
          headers[i].loadlink(*link,now,(char*)headers[i-1].nowhash);}
        headers[i].header_print();
        } //assert(i==num-1);
      free(data);
      if(memcmp(headers[num-2].nowhash,peer_hs.head.oldhash,SHA256_DIGEST_LENGTH)){
        LOG("%04X ERROR, hashing header chain :-(\n",svid);
        char hash[2*SHA256_DIGEST_LENGTH];
        ed25519_key2text(hash,headers[num-2].nowhash,SHA256_DIGEST_LENGTH);
        LOG("%04X NOWHASH nowhash %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);
        ed25519_key2text(hash,peer_hs.head.oldhash,SHA256_DIGEST_LENGTH);
        LOG("%04X NOWHASH oldhash %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);
        leave();
        return;}}
    headers[num-1].loadhead(peer_hs.head);
    //if(memcmp(headers[num-1].nowhash,peer_hs.head.nowhash,SHA256_DIGEST_LENGTH)){
    //  std::cerr << "ERROR, hashing header chain end :-(\n";
    //  leave();
    //  return;}
    if(memcmp(headers[0].oldhash,sync_ls.nowhash,SHA256_DIGEST_LENGTH)){
      LOG("%04X ERROR, initial oldhash mismatch :-(\n",svid);
      char hash[2*SHA256_DIGEST_LENGTH];
      ed25519_key2text(hash,headers[0].oldhash,SHA256_DIGEST_LENGTH);
      LOG("%04X NOWHASH got  %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);
      ed25519_key2text(hash,sync_ls.nowhash,SHA256_DIGEST_LENGTH);
      LOG("%04X NOWHASH have %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);
      leave();
      LOG("%04X Maybe start syncing from an older block (peer will disconnect)\n\n",svid);
      return;}
    LOG("%04X HASHES loaded\n",svid);
    //server_.slow_sync(true,headers);
    server_.add_headers(headers);
    // send current sync path
    server_.peer_.lock();
    if(srvs_.now && server_.do_sync){
      message_ptr put_msg(new message());
      put_msg->data[0]=MSGTYPE_PAT;
      memcpy(put_msg->data+1,&srvs_.now,4);
      send_sync(put_msg);}
    server_.peer_.unlock();
    return;
  }

  void write_msglist()
  { servers header;
    assert(read_msg_->data!=NULL);
    memcpy(&header.now,read_msg_->data+1,4);
    if(!header.header_get()){
      //std::cerr<<"FAILED to read header "<<header.now<<" for svid:"<<svid<<"\n"; //TODO, send error
      LOG("%04X FAILED to read header %08X for svid: %04X\n",svid,header.now,svid); //TODO, send error
      return;}
    int len=8+SHA256_DIGEST_LENGTH+header.msg*(2+4+SHA256_DIGEST_LENGTH);
    message_ptr put_msg(new message(len));
    put_msg->data[0]=MSGTYPE_MSP;
    memcpy(put_msg->data+1,&len,3); //bigendian
    memcpy(put_msg->data+4,&header.now,4);
    if(header.msg_get((char*)(put_msg->data+8))!=(int)(SHA256_DIGEST_LENGTH+header.msg*(2+4+SHA256_DIGEST_LENGTH))){
      //std::cerr<<"FAILED to read msglist "<<header.now<<" for svid:"<<svid<<"\n"; //TODO, send error
      LOG("%04X FAILED to read msglist %08X\n",svid,header.now); //TODO, send error
      return;}
    //std::cerr<<"SENDING block msglist for block "<<header.now<<" to svid:"<<svid<<"\n";
    LOG("%04X SENDING block msglist %08X\n",svid,header.now); //TODO, send error
    send_sync(put_msg);
  }

  void handle_read_msglist(const boost::system::error_code& error)
  { if(error){
      LOG("%04X ERROR reading msglist\n",svid);
      leave();
      return;}
    servers header;
    assert(read_msg_->data!=NULL);
    memcpy(&header.now,read_msg_->data+4,4);
    if(server_.get_msglist!=header.now){
      LOG("%04X ERROR got wrong msglist id\n",svid); // consider updating server
      read_msg_ = boost::make_shared<message>();
      boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_->data,message::header_length),
        boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
      return;}
    header.header_get();
    if(read_msg_->len!=(8+SHA256_DIGEST_LENGTH+header.msg*(2+4+SHA256_DIGEST_LENGTH))){
      LOG("%04X ERROR got wrong msglist length\n",svid); // consider updating server
      read_msg_ = boost::make_shared<message>();
      boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_->data,message::header_length),
        boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
      return;}
//FIXME, validate list has correct hash !!! (calculate hash again)
    if(memcmp(read_msg_->data+8,header.msghash,SHA256_DIGEST_LENGTH)){
      LOG("%04X ERROR got wrong msglist msghash\n",svid); // consider updating server
      char hash[2*SHA256_DIGEST_LENGTH];
      ed25519_key2text(hash,read_msg_->data+8,SHA256_DIGEST_LENGTH);
      LOG("%04X MSGHASH got  %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);
      ed25519_key2text(hash,header.msghash,SHA256_DIGEST_LENGTH);
      LOG("%04X MSGHASH have %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);
      read_msg_ = boost::make_shared<message>();
      boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_->data,message::header_length),
        boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
      return;}
    std::map<uint64_t,message_ptr> map;
    header.msg_map((char*)(read_msg_->data+8),map,opts_.svid);
    if(!header.msg_check(map)){
      LOG("%04X ERROR msghash check failed\n",svid); // consider updating server
      read_msg_ = boost::make_shared<message>();
      boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_->data,message::header_length),
        boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
      return;}
    server_.put_msglist(header.now,map);
    read_msg_ = boost::make_shared<message>();
    boost::asio::async_read(socket_,
      boost::asio::buffer(read_msg_->data,message::header_length),
      boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
  }

  void write_bank()
  { uint32_t path=read_msg_->msid;
    uint16_t bank=read_msg_->svid;

//FIXME !!!
//FIXME !!! check if the first undo directory is needed !!!
//FIXME !!! if peer starts at 'path' and receives messages in 'path' then it has to start from state before path
//FIXME !!! then the path/und files are needed to start in correct position
//FIXME !!!

    if(1+(srvs_.now-path)/BLOCKSEC>MAX_UNDO){
      LOG("%04X ERROR, too old sync point\n",svid);
      leave();
      return;}
    //check the numer of users at block time
    servers s;
    s.get(path);
    if(bank>=s.nodes.size()){
      LOG("%04X ERROR, bad bank at sync point\n",svid);
      leave();
      return;}
    std::vector<int> ud;
    uint32_t users=s.nodes[bank].users;
    //TODO, consider checking that the final hash is correct
    char filename[64];
    sprintf(filename,"usr/%04X.dat",bank);
    int fd=open(filename,O_RDONLY);
    int ld=0;
    if(fd<0){
      LOG("%04X ERROR, failed to open bank, weird !!!\n",svid);
      leave();
      return;}
    for(uint32_t block=path+BLOCKSEC;block<=srvs_.now;block++){
      sprintf(filename,"blk/%03X/%05X/und/%04X.dat",block>>20,block&0xFFFFF,bank);
      int fd=open(filename,O_RDONLY);
      if(fd<0){
        continue;}
      LOG("%04X USING bank %04X block %08X undo %s\n",svid,bank,path,filename);
      ud.push_back(fd);}
    if(ud.size()){
      ld=ud.back();}
    int msid=0;
     int64_t weight=0;
    //SHA256_CTX sha256;
    //SHA256_Init(&sha256);
    uint64_t csum[4]={0,0,0,0};
    for(uint32_t user=0;user<users;msid++){
      uint32_t end=user+MESSAGE_CHUNK;
      if(end>users){
        end=users;}
      int len=end-user;
      message_ptr put_msg(new message(8+len*sizeof(user_t))); //6Mb working space
      put_msg->data[0]=MSGTYPE_USR;
      memcpy(put_msg->data+1,&len,3); // this is number of users (max 0x10000)
      memcpy(put_msg->data+4,&msid,2); // this is the chunk id
      memcpy(put_msg->data+6,&bank,2); // this is the bank
      user_t* u=(user_t*)(put_msg->data+8);
      for(;user<end;user++,u++){
        u->msid=0;
        int junk=0;
        for(auto it=ud.begin();it!=ud.end();it++,junk++){
          read(*it,(char*)u,sizeof(user_t));
          if(u->msid){
            LOG("%04X USING bank %04X undo user %08X file %d\n",svid,bank,user,junk);
            lseek(fd,sizeof(user_t),SEEK_CUR);
            for(it++;it!=ud.end();it++){
              lseek(*it,sizeof(user_t),SEEK_CUR);}
            goto NEXTUSER;}}
        read(fd,u,sizeof(user_t));
        if(ld){ //confirm again that the undo file has not changed
          user_t v;
          v.msid=0;
          lseek(ld,-sizeof(user_t),SEEK_CUR);
          read(ld,&v,sizeof(user_t));
          if(v.msid){ //overwrite user info
            memcpy((char*)u,&v,sizeof(user_t));}}
        NEXTUSER:;
        //print user
        LOG("%04X USER:%04X m:%04X t:%08X s:%04X b:%04X u:%04X l:%08X r:%08X v:%016lX h:%08X\n",svid,
          user,u->msid,u->time,u->stat,u->node,u->user,u->lpath,u->rpath,u->weight,*((uint32_t*)(u->csum)));
        weight+=u->weight;
        //SHA256_Update(&sha256,u,sizeof(user_t));
        //FIXME, debug only !!!
        { user_t n;
          memcpy(&n,u,sizeof(user_t));
          server_.last_srvs_.user_csum(n,bank,user);
          if(memcmp(n.csum,u->csum,32)){
            LOG("%04X ERROR !!!, checksum mismatch for user %08X [%08X<>%08X]\n",svid,user,
              *((uint32_t*)(n.csum)),*((uint32_t*)(u->csum)));
            //exit(-1);
            leave();
            return;}
        }
        server_.last_srvs_.xor4(csum,u->csum);}
      LOG("%04X SENDING bank %04X block %08X chunk %08X max user %08X sum %016lX hash %08X\n",svid,bank,path,msid,user,s.nodes[bank].weight,*((uint32_t*)csum));
      send_sync(put_msg); // send even if we have errors
      if(user==users){
        //uint8_t hash[32];
        //SHA256_Final(hash,&sha256);
//FIXME, should compare with previous hash !!!
        if(s.nodes[bank].weight!=weight){
          //unlink(filename); //TODO, enable this later
          LOG("%04X ERROR sending bank %04X bad sum %016lX<>%016lX\n",svid,bank,s.nodes[bank].weight,weight);
          leave();
          return;}
        if(memcmp(s.nodes[bank].hash,csum,32)){
          //unlink(filename); //TODO, enable this later
          LOG("%04X ERROR sending bank %04X (bad hash)\n",svid,bank);
          leave();
          return;}}}
  }

  void handle_read_bank(const boost::system::error_code& error)
  { static uint16_t last_bank=0;
    static uint16_t last_msid=0;
    static  int64_t weight=0;
    //static SHA256_CTX sha256;
    static uint64_t csum[4]={0,0,0,0};
    int fd;
    if(error){
      LOG("%04X ERROR reading message\n",svid);
      leave();
      return;}
    if(!server_.do_sync){
      LOG("%04X DEBUG ignore usr message\n",svid);
      read_msg_ = boost::make_shared<message>();
      boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_->data,message::header_length),
        boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
      return;}
    //read_msg_->read_head();
    uint16_t bank=read_msg_->svid;
    if(!bank || bank>=server_.last_srvs_.nodes.size()){
      //std::cerr << "ERROR reading bank "<<bank<<" (bad svid)\n";
      LOG("%04X ERROR reading bank %04X (bad num)\n",svid,bank);
      leave();
      return;}
    //if(!read_msg_->msid || read_msg_->msid>=server_.last_srvs_.now){
    //  std::cerr << "ERROR reading bank "<<bank<<" (bad block)\n";
    //  leave();
    //  return;}
    if(read_msg_->len+0x10000*read_msg_->msid>server_.last_srvs_.nodes[bank].users){
      //std::cerr << "ERROR reading bank "<<bank<<" (too many users)\n";
      LOG("%04X ERROR reading bank %04X (too many users)\n",svid,bank);
      leave();
      return;}
    //std::cerr << "NEED bank "<<bank<<" ?\n";
    //LOG("%04X NEED bank %04X ?\n",svid,bank);
    uint64_t hnum=server_.need_bank(bank);
    if(!hnum){
      //std::cerr << "NEED bank "<<bank<<" NO\n";
      LOG("%04X NO NEED for bank %04X\n",svid,bank);
      read_msg_ = boost::make_shared<message>();
      boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_->data,message::header_length),
        boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
      return;}
    //std::cerr << "PROCESSING bank "<<bank<<"\n";
    LOG("%04X PROCESSING bank %04X\n",svid,bank);
    char filename[64];
    sprintf(filename,"usr/%04X.dat.%04X",bank,svid);
    if(!read_msg_->msid){
      last_bank=bank;
      last_msid=0;
      weight=0;
      bzero(csum,4*sizeof(uint64_t));
      //SHA256_Init(&sha256);
      fd=open(filename,O_WRONLY|O_CREAT|O_TRUNC,0644);}
    else{
      if(last_bank!=bank||last_msid!=read_msg_->msid-1){
        unlink(filename);
        //std::cerr << "ERROR reading bank "<<bank<<" (incorrect message order)\n";
        LOG("%04X ERROR reading bank %04X (incorrect message order)\n",svid,bank);
        leave();
        return;}
      last_msid=read_msg_->msid;
      fd=open(filename,O_WRONLY|O_APPEND,0644);}
    if(fd<0){ //trow or something :-)
      //std::cerr << "ERROR creating bank "<<bank<<" file\n";
      LOG("%04X ERROR creating bank %04X file\n",svid,bank);
      exit(-1);}
    //if sending user_t without csum, must split this into individual writes
    if((int)(read_msg_->len*sizeof(user_t))!=write(fd,read_msg_->data+8,read_msg_->len*sizeof(user_t))){
      close(fd);
      unlink(filename);
      //std::cerr << "ERROR writing bank "<<bank<<" file\n";
      LOG("%04X ERROR writing bank %04X file\n",svid,bank);
      exit(-1);}
    close(fd);
    user_t* u=(user_t*)(read_msg_->data+8);
    uint32_t uid=0x10000*read_msg_->msid;
    for(uint32_t i=0;i<read_msg_->len;i++,u++,uid++){
      //LOG("%04X USER:%08X m:%08X t:%08X s:%04X b:%04X u:%08X l:%08X r:%08X v:%016lX\n",svid,
      //  uid,u->msid,u->time,u->stat,u->node,u->user,u->lpath,u->rpath,u->weight);
      LOG("%04X USER:%04X m:%04X t:%08X s:%04X b:%04X u:%04X l:%08X r:%08X v:%016lX h:%08X\n",svid,
        uid,u->msid,u->time,u->stat,u->node,u->user,u->lpath,u->rpath,u->weight,*((uint32_t*)(u->csum)));
      weight+=u->weight;
      //SHA256_Update(&sha256,u,sizeof(user_t));
      server_.last_srvs_.user_csum(*u,bank,uid); //overwrite u.csum (TODO consider not sending over network!!!)
      server_.last_srvs_.xor4(csum,u->csum);}
    if(read_msg_->len+0x10000*read_msg_->msid<server_.last_srvs_.nodes[bank].users){
      read_msg_ = boost::make_shared<message>();
      boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_->data,message::header_length),
        boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
      return;}
    LOG("%04X GOT bank %04X users %08X sum %016lX hash %08X\n",svid,bank,uid,weight,*((uint32_t*)csum));
    //uint8_t hash[32];
    //SHA256_Final(hash,&sha256);
    //if(memcmp(server_.last_srvs_.nodes[bank].hash,hash,32))
    if(server_.last_srvs_.nodes[bank].weight!=weight){
      //unlink(filename); //TODO, enable this later
      //std::cerr << "ERROR reading bank "<<bank<<" (bad sum)\n";
      LOG("%04X ERROR reading bank %04X (bad sum)\n",svid,bank);
      leave();
      return;}
    if(memcmp(server_.last_srvs_.nodes[bank].hash,csum,4*sizeof(uint64_t))){
      //unlink(filename); //TODO, enable this later
      //std::cerr << "ERROR reading bank "<<bank<<" (bad hash)\n";
      LOG("%04X ERROR reading bank %04X (bad hash) [%08X<>%08X]\n",svid,bank,
        *((uint32_t*)server_.last_srvs_.nodes[bank].hash),*((uint32_t*)csum));
      leave();
      return;}
    char new_name[64];
    sprintf(new_name,"usr/%04X.dat",bank);
    rename(filename,new_name);
    //std::cerr << "PROCESSED bank "<<bank<<"\n";
    LOG("%04X PROCESSED bank %04X\n",svid,bank);
    server_.have_bank(hnum);
    LOG("%04X CONTINUE after bank processing\n",svid);
    read_msg_ = boost::make_shared<message>();
    boost::asio::async_read(socket_,
      boost::asio::buffer(read_msg_->data,message::header_length),
      boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
  }

  void write_servers()
  { uint32_t now;
    assert(read_msg_->data!=NULL);
    memcpy(&now,read_msg_->data+1,4);
    LOG("%04X SENDING block servers for block %08X\n",svid,now);
    if(server_.last_srvs_.now!=now){
      //FIXME, try getting data from repository
      LOG("%04X ERROR, bad time %08X<>%08X\n",svid,server_.last_srvs_.now,now);
      leave();
      return;}
    message_ptr put_msg(new message(server_.last_srvs_.nod*sizeof(node_t)));
    if(!server_.last_srvs_.copy_nodes((node_t*)put_msg->data,server_.last_srvs_.nod)){
      LOG("%04X ERROR, failed to copy nodes :-(\n",svid);
      leave();
      return;}
    send_sync(put_msg);
  }

  void handle_read_servers()
  { 
    if(server_.fast_sync(false,peer_hs.head,peer_nods,peer_svsi)<0){
      return;}
    //send 
    LOG("%04X SENDING block servers request\n",svid);
    message_ptr put_msg(new message());
    put_msg->data[0]=MSGTYPE_SER;
    memcpy(put_msg->data+1,&peer_hs.head.now,4);
    send_sync(put_msg);
    peer_nods=(node_t*)malloc(peer_hs.head.nod*sizeof(node_t)); assert(peer_nods!=NULL);
    int len=boost::asio::read(socket_,boost::asio::buffer(peer_nods,peer_hs.head.nod*sizeof(node_t)));
    if(len!=(int)(peer_hs.head.nod*sizeof(node_t))){
      LOG("%04X READ servers error\n",svid);
      free(peer_svsi);
      free(peer_nods);
      leave();
      return;}
    if(server_.last_srvs_.check_nodes(peer_nods,peer_hs.head.nod,peer_hs.head.nodhash)){
      LOG("%04X SERVERS incompatible with hash\n",svid);
      char hash[2*SHA256_DIGEST_LENGTH];
      ed25519_key2text(hash,sync_hs.head.nodhash,SHA256_DIGEST_LENGTH);
      LOG("%04X NODHASH peer %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);
      free(peer_svsi);
      free(peer_nods);
      leave();
      return;}
    LOG("%04X FINISH SYNC\n",svid);
    server_.fast_sync(true,peer_hs.head,peer_nods,peer_svsi); // should use last_srvs_ instead of sync_...
    free(peer_svsi);
    free(peer_nods);
  }

  int authenticate() //FIXME, don't send last block because signatures are missing; send the one before last
  { uint32_t now=time(NULL);
    uint32_t blocknow=now-(now%BLOCKSEC);
    msid=read_msg_->msid;
    svid=read_msg_->svid;
    LOG("%04X PEER HEADER %04X:\n",svid,svid);
    assert(read_msg_->data!=NULL);
    memcpy(&peer_hs,read_msg_->data+4+64+10,sizeof(handshake_t));
    srvs_.header_print(peer_hs.head);
    //memcpy(&sync_head,&peer_hs.head,sizeof(header_t));
    if(read_msg_->svid==opts_.svid){ LOG("%04X ERROR: connecting to myself\n",svid);
      return(0);}
    if(server_.duplicate(shared_from_this())){ LOG("%04X ERROR: server already connected\n",svid);
      return(0);}
    if(peer_hs.head.nod>srvs_.nodes.size() && incoming_){ LOG("%04X ERROR: too high number of servers for incoming connection\n",svid);
      return(0);}
    if(read_msg_->now>now+2 || read_msg_->now<now-2){
      LOG("%04X ERROR: bad time %08X<>%08X\n",svid,read_msg_->now,now);
      return(0);}
    if(peer_hs.msid>server_.msid_){ //FIXME, we will check this later, maybe not needed here
      //std::cerr<<"WARNING, possible message loss, run full resync ("<<peer_hs.msid<<">"<<server_.msid_<<")\n";
      LOG("%04X WARNING, possible message loss (my msid peer:%08X>me:%08X)\n",svid,peer_hs.msid,server_.msid_);}
    if(peer_hs.msid && peer_hs.msid==server_.msid_ && server_.msid_==srvs_.nodes[opts_.svid].msid &&
       memcmp(peer_hs.msha,srvs_.nodes[opts_.svid].msha,SHA256_DIGEST_LENGTH)){ //FIXME, we should check this later, maybe not needed
      char hash[2*SHA256_DIGEST_LENGTH];
      LOG("%04X WARNING, last message hash mismatch, should run full resync\n",svid);
      ed25519_key2text(hash,srvs_.nodes[opts_.svid].msha,SHA256_DIGEST_LENGTH);
      LOG("%04X HASH have %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);
      ed25519_key2text(hash,peer_hs.msha,SHA256_DIGEST_LENGTH);
      LOG("%04X HASH got  %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);}
    if(incoming_){
      message_ptr sync_msg=server_.write_handshake(svid,sync_hs); // sets sync_hs
      sync_msg->print("; send welcome");
      send_sync(sync_msg);}
    if(peer_hs.head.now==sync_hs.head.now){
      if(memcmp(peer_hs.head.oldhash,sync_hs.head.oldhash,SHA256_DIGEST_LENGTH)){
        char hash[2*SHA256_DIGEST_LENGTH];
        LOG("%04X ERROR oldhash mismatch, FIXME, move back more blocks to sync\n",svid);
        ed25519_key2text(hash,sync_hs.head.oldhash,SHA256_DIGEST_LENGTH);
        LOG("%04X HASH have %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);
        ed25519_key2text(hash,peer_hs.head.oldhash,SHA256_DIGEST_LENGTH);
        LOG("%04X HASH got  %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);
        return(0);}
      if(memcmp(peer_hs.head.nowhash,sync_hs.head.nowhash,SHA256_DIGEST_LENGTH)){
        char hash[2*SHA256_DIGEST_LENGTH];
        LOG("%04X WARNING nowhash mismatch, not tested :-( move back one block to sync\n",svid);
        ed25519_key2text(hash,sync_hs.head.nowhash,SHA256_DIGEST_LENGTH);
        LOG("%04X HASH have %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);
        ed25519_key2text(hash,peer_hs.head.nowhash,SHA256_DIGEST_LENGTH);
        LOG("%04X HASH got  %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);
        return(0);}}
    //if(!server_.do_sync){ //we are in sync
    if(!sync_hs.do_sync){ //we are in sync
      if(peer_hs.head.now>sync_hs.head.now){
        LOG("%04X ERROR not ready to connect with this server\n",svid);
        return(0);}
      //if(peer_hs.head.now<sync_hs.head.now){
      if(peer_hs.do_sync){
        LOG("%04X Authenticated, provide sync data\n",svid);
        //write_sync(); // peer will disconnect if peer does not want the data
	int vok=sync_hs.head.vok;
	int vno=sync_hs.head.vno;
        int size=sizeof(svsi_t)*(vok+vno);
        message_ptr put_msg(new message(size));
        server_.last_srvs_.get_signatures(sync_hs.head.now,put_msg->data,vok,vno);//TODO, check if server_.last_srvs_ has public keys
        LOG("%04X SENDING last block signatures ok:%d+no:%d (%d bytes)\n",svid,vok,vno,size);
        send_sync(put_msg);
        return(1);}
      else{
        LOG("%04X Authenticated, peer in sync\n",svid);
        update_sync();
        do_sync=0;}
      return(1);}
    // try syncing from this server
    //if(peer_hs.head.now!=srvs_.now-BLOCKSEC)
    if(peer_hs.head.now!=blocknow-BLOCKSEC || peer_hs.do_sync){
      LOG("%04X PEER not in sync\n",svid);
      return(0);}
    if(peer_hs.head.vok<server_.vip_max/2 && (!opts_.mins || peer_hs.head.vok<opts_.mins)){
      LOG("%04X PEER not enough signatures\n",svid);
      return(0);}
    LOG("%04X Authenticated, expecting sync data (%u bytes)\n",svid,
      (uint32_t)((peer_hs.head.vok+peer_hs.head.vno)*sizeof(svsi_t)));
    peer_svsi=(svsi_t*)malloc((peer_hs.head.vok+peer_hs.head.vno)*sizeof(svsi_t)); //FIXME, send only vok
    int len=boost::asio::read(socket_,boost::asio::buffer(peer_svsi,(peer_hs.head.vok+peer_hs.head.vno)*sizeof(svsi_t)));
    if(len!=(int)((peer_hs.head.vok+peer_hs.head.vno)*sizeof(svsi_t))){
      LOG("%04X READ block signatures error\n",svid);
      free(peer_svsi);
      return(0);}
    LOG("%04X BLOCK sigatures recieved ok:%d no:%d\n",svid,peer_hs.head.vok,peer_hs.head.vno);
    server_.last_srvs_.check_signatures(peer_hs.head,peer_svsi);//TODO, check if server_.last_srvs_ has public keys
    if(peer_hs.head.vok<server_.vip_max/2 && (!opts_.mins || peer_hs.head.vok<opts_.mins)){
      LOG("%04X READ not enough signatures after validaiton\n",svid);
      free(peer_svsi);
      return(0);}
    //now decide if You want to sync to last stage first ; or load missing blocks and messages first, You can decide based on size of databases and time to next block
    //the decision should be in fact made at the beginning by the server
    server_.add_electors(peer_hs.head,peer_svsi);
    if(opts_.fast){
      handle_read_servers();
      server_.last_srvs_.header(sync_hs.head);} // set new starting point for headers synchronisation
    handle_read_headers();
    //FIXME, brakes assert in send_sync() !!!
    do_sync=0; // set peer in sync, we are not in sync (server_.do_sync==1)
    return(1);
  }

  void handle_read_body(const boost::system::error_code& error)
  {
    if(error){
      LOG("%04X ERROR reading message\n",svid);
      leave();
      return;}
    read_msg_->read_head();
    if(!read_msg_->svid || read_msg_->svid>=srvs_.nodes.size()){
      LOG("%04X ERROR reading head\n",svid);
      leave();
      return;}
    uint8_t* msha=srvs_.nodes[read_msg_->svid].msha;
    if(read_msg_->data[0]==MSGTYPE_MSG){
      if(srvs_.nodes[read_msg_->svid].msid<read_msg_->msid-1){
        message_ptr prev=server_.message_svidmsid(read_msg_->svid,read_msg_->msid-1);
        if(prev!=NULL){
          LOG("%04X \nLOADING future message %04X:%08X>%08X+1\n",svid,
            read_msg_->svid,read_msg_->msid,srvs_.nodes[read_msg_->svid].msid);
          msha=prev->sigh;}
        else{
          LOG("%04X \nERROR LOADING future message %04X:%08X>%08X+1\n",svid,
            read_msg_->svid,read_msg_->msid,srvs_.nodes[read_msg_->svid].msid);
          read_msg_ = boost::make_shared<message>();
          boost::asio::async_read(socket_,
            boost::asio::buffer(read_msg_->data,message::header_length),
            boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
          return;}}
      //FIXME, add detection of double spend messages
      if(srvs_.nodes[read_msg_->svid].msid>=read_msg_->msid){
        LOG("%04X \nIGNORE message with old msid %04X:%08X<=%08X //FIXME !!!\n\n",svid, //FIXME, DO NOT! need to detect double spend
          read_msg_->svid,read_msg_->msid,srvs_.nodes[read_msg_->svid].msid);
        read_msg_ = boost::make_shared<message>();
        boost::asio::async_read(socket_,
          boost::asio::buffer(read_msg_->data,message::header_length),
          boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
        return;}}
    if(read_msg_->check_signature(srvs_.nodes[read_msg_->svid].pk,opts_.svid,msha)){
      //FIXME, this can be also a double spend, do not loose it
      LOG("%04X BAD signature %04X:%08X (last msid:%08X) %016lX!!!\n\n",svid,read_msg_->svid,read_msg_->msid,
        srvs_.nodes[read_msg_->svid].msid,read_msg_->hash.num);
      //ed25519_printkey(srvs_.nodes[read_msg_->svid].pk,32);
      leave();
      return;}
    if(!svid){ // FIXME, move this to 'start()'
      if(!authenticate()){
        LOG("%04X NOT authenticated\n",svid);
        leave();
        return;}
      LOG("%04X CONTINUE after authentication\n",svid);
      read_msg_ = boost::make_shared<message>();
      boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_->data,message::header_length),
        boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
      return;}
    assert(read_msg_->hash.dat[1]!=MSGTYPE_BLK);
    //if this is a candidate vote
    if(read_msg_->hash.dat[1]==MSGTYPE_CND){
//FIXME, move this to handle_read_candidate
      if(!parse_vote()){
        LOG("%04X PARSE vote FAILED\n",svid);
        leave();
        return;}}
    //TODO, check if correct server message number ! and update the server
    //std::cerr << "INSERT message\n";
    if(server_.message_insert(read_msg_)==-1){ //NEW, insert in correct containers
      LOG("%04X INSERT message FAILED\n",svid);
      leave();
      return;}
    // consider waiting here if we have too many messages to process
    read_msg_ = boost::make_shared<message>();
    boost::asio::async_read(socket_,
      boost::asio::buffer(read_msg_->data,message::header_length),
      boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
  }

  void handle_read_block(const boost::system::error_code& error)
  { LOG("%04X BLOCK, got BLOCK\n",svid);
    if(error || !svid){
      LOG("%04X READ error %d %s (BLOCK)\n",svid,error.value(),error.message().c_str());
      leave();
      return;}
    if(read_msg_->len==4+64+10){ //adding missing data
      if(server_.last_srvs_.now!=read_msg_->msid){
        LOG("%04X BLOCK READ problem, ignoring short BLK message due to msid mismatch, msid:%08X block:%08X\n",svid,read_msg_->msid,server_.last_srvs_.now);
        read_msg_ = boost::make_shared<message>();
        boost::asio::async_read(socket_,
          boost::asio::buffer(read_msg_->data,message::header_length),
          boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
        return;}
      header_t* h=(header_t*)(read_msg_->data+4+64+10);
      read_msg_->len+=sizeof(header_t);
      read_msg_->data=(uint8_t*)realloc(read_msg_->data,read_msg_->len); // throw if no RAM ???
      server_.last_srvs_.header(*h);}
    read_msg_->read_head();
    if(!read_msg_->svid || read_msg_->svid>=srvs_.nodes.size()){
      LOG("%04X ERROR reading head\n",svid);
      leave();
      return;}
    if(read_msg_->check_signature(srvs_.nodes[read_msg_->svid].pk,opts_.svid,srvs_.nodes[read_msg_->svid].msha)){
      LOG("%04X BLOCK signature error\n",svid);
      leave();
      return;}
    if((read_msg_->msid!=server_.last_srvs_.now && read_msg_->msid!=srvs_.now) || read_msg_->now<read_msg_->msid || read_msg_->now>=read_msg_->msid+2*BLOCKSEC){
      LOG("%04X BLOCK TIME error now:%08X msid:%08X block[-1].now:%08X block[].now:%08X \n",svid,read_msg_->now,read_msg_->msid,server_.last_srvs_.now,srvs_.now);
      leave();
      return;}
    if(read_msg_->svid==svid){
      //std::cerr << "BLOCK from peer "<<svid<<"\n";
      LOG("%04X BLOCK\n",svid);
      assert(read_msg_->data!=NULL);
      memcpy(&peer_hs.head,read_msg_->data+4+64+10,sizeof(header_t));
      char hash[2*SHA256_DIGEST_LENGTH];
      //ed25519_key2text(hash,oldhash,SHA256_DIGEST_LENGTH);
      ed25519_key2text(hash,peer_hs.head.nowhash,SHA256_DIGEST_LENGTH);
      LOG("%04X BLOCK %08X %.*s from PEER\n",svid,peer_hs.head.now,2*SHA256_DIGEST_LENGTH,hash);}
    LOG("%04X INSERT BLOCK message\n",svid);
    if(server_.message_insert(read_msg_)==-1){ //NEW, insert in correct containers
      LOG("%04X INSERT BLOCK message FAILED\n",svid);
      leave();
      return;}
    read_msg_ = boost::make_shared<message>();
    boost::asio::async_read(socket_,
      boost::asio::buffer(read_msg_->data,message::header_length),
      boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
  }

  void handle_read_stop(const boost::system::error_code& error)
  {
    LOG("%04X PEER got STOP\n",svid);
    if(error){
      LOG("%04X READ error %d %s (STOP)\n",svid,error.value(),error.message().c_str());
      leave();
      return;}
    assert(read_msg_->data!=NULL);
    memcpy(last_message_hash,read_msg_->data+1,SHA256_DIGEST_LENGTH);
      char hash[2*SHA256_DIGEST_LENGTH]; hash[2*SHA256_DIGEST_LENGTH-1]='?';
      ed25519_key2text(hash,last_message_hash,SHA256_DIGEST_LENGTH);
      LOG("%04X LAST HASH got %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash);
    boost::asio::async_read(socket_,
      boost::asio::buffer(read_msg_->data,2),
      boost::bind(&peer::read_peer_missing_header,shared_from_this(),boost::asio::placeholders::error));
    mtx_.lock();
    BLOCK_MODE_PEER=1;
    if(BLOCK_MODE_SERVER>1){
      mtx_.unlock();
      write_peer_missing_messages();
      return;}
    mtx_.unlock();
  }

  void svid_msid_rollback(message_ptr msg) // remove deleted message from known messages map
  { 

//FIXME, this will insert svid_msid_new[msg->svid]=0 :-( !!!

    assert(0); //this causes potential problems
    auto it=svid_msid_new.find(msg->svid);
    if(it==svid_msid_new.end()){
      return;}
    if(it->second>msg->msid-1){
      LOG("%04X roll back to %04X:%08X->%08X\n",svid,msg->svid,it->second,msg->msid-1);
      it->second=msg->msid-1;}
    //if(svid_msid_new[msg->svid]>msg->msid-1){
    //  svid_msid_new[msg->svid]=msg->msid-1;}
  }

  void write_peer_missing_messages()
  { std::string data;
    uint16_t server_changed=0;

    LOG("%04X SYNC start (no UPDATE PEER SVID_MSID after this)\n",svid);
    svmsha_serv_changed.clear();
    //assume svid_msid_new is the same for server and peer, send differnce between this and last_svid_msgs_ (server block state)
    for(auto st=server_.last_svid_msgs.begin();st!=server_.last_svid_msgs.end();++st){ //svid_msid_new and last_svid_msgs must not change after thread_clock entered block mode
      uint32_t msid=0;
      auto pt=svid_msid_new.find(st->first);
      if(pt!=svid_msid_new.end()){
        msid=pt->second;}
      if(st->second->msid!=msid || BLOCK_MODE_ERROR){
        svidmsidhash_t svmsha;
        svmsha.svid=st->first;
        svmsha.msid=st->second->msid;
        memcpy(svmsha.sigh,st->second->sigh,sizeof(hash_t));
        svmsha_serv_changed.push_back(svmsha);
        data.append((const char*)&svmsha,sizeof(svidmsidhash_t));
        //data.append((const char*)&st->first,2);
        //data.append((const char*)&msid,4);
        //svidmsidhash_t svmsha;
        //svmsha.svid=st->first;
        //svmsha.msid=msid;
        //svmsha_serv_changed.push_back(svmsha);
        char sigh[2*SHA256_DIGEST_LENGTH];
        ed25519_key2text(sigh,svmsha.sigh,SHA256_DIGEST_LENGTH);
        LOG("%04X SERV %04X:%08X->%08X %.*s\n",svid,st->first,msid,svmsha.msid,2*SHA256_DIGEST_LENGTH,sigh);
        server_changed++;}}
    for(auto it=svid_msid_new.begin();it!=svid_msid_new.end();++it){ //svid_msid_new and last_svid_msgs must not change after thread_clock entered block mode
      //assert(it->first<=server_.last_srvs_.nodes.size());
      if(server_.last_svid_msgs.find(it->first)!=server_.last_svid_msgs.end()){ //already processed
        continue;}
      svidmsidhash_t svmsha={it->first,0,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}};
      svmsha_serv_changed.push_back(svmsha);
      data.append((const char*)&svmsha,sizeof(svidmsidhash_t));
      //msid=0;
      //data.append((const char*)&it->first,2);
      //data.append((const char*)&msid,4);
      //svidmsidhash_t svmsha;
      //svmsha.svid=it->first;
      //svmsha.msid=msid;
      //svmsha_serv_changed.push_back(svmsha);
      LOG("%04X SERV %04X:%08X->00000000 missed\n",svid,it->first,it->second);
      server_changed++;}
    LOG("%04X SERVER changed %d in total (from:%d|%d)\n",svid,
      (int)server_changed,(int)server_.last_svid_msgs.size(),(int)svid_msid_new.size());
    message_ptr put_msg(new message(2+sizeof(svidmsidhash_t)*server_changed));
    memcpy(put_msg->data,&server_changed,2);
    if(server_changed){
      memcpy(put_msg->data+2,data.c_str(),sizeof(svidmsidhash_t)*server_changed);}
    put_msg->sent.insert(svid); //only to prevent assert(false)
    put_msg->busy.insert(svid); //only to prevent assert(false)
    mtx_.lock();
    write_msgs_.push_front(put_msg); // to prevent data loss
    mtx_.unlock();
    boost::asio::async_write(socket_,boost::asio::buffer(put_msg->data,put_msg->len),
      boost::bind(&peer::write_peer_missing_hashes,shared_from_this(),boost::asio::placeholders::error)); 
  }

  void read_peer_missing_header(const boost::system::error_code& error)
  { if(error){
      LOG("%04X READ read_peer_missing_header error\n",svid);
      leave();
      return;}
    assert(read_msg_->data!=NULL);
    memcpy(&peer_changed,read_msg_->data,2);
    if(peer_changed){
      LOG("%04X PEER changed %d in total\n",svid,peer_changed);
      free(read_msg_->data);
      read_msg_->data=(uint8_t*)malloc(sizeof(svidmsidhash_t)*peer_changed);
      read_msg_->len=sizeof(svidmsidhash_t)*peer_changed; //not used
      boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_->data,sizeof(svidmsidhash_t)*peer_changed),
        boost::bind(&peer::read_peer_missing_messages,shared_from_this(),boost::asio::placeholders::error));}
    else{
      LOG("%04X PEER changed none\n",svid);
      svmsha_peer_changed.clear();
      write_peer_missing_hashes(error);}
  }

  void read_peer_missing_messages(const boost::system::error_code& error)
  { if(error){
      LOG("%04X READ read_peer_missing_messages error\n",svid);
      leave();
      return;}
    LOG("%04X PEER read peer missing messages\n",svid);
    // create peer_missing list;
    svmsha_peer_changed.clear();
    svmsha_peer_changed.reserve(peer_changed);
    assert(read_msg_->data!=NULL);
    for(int i=0;i<peer_changed;i++){
      svidmsidhash_t svmsha;
      memcpy(&svmsha,read_msg_->data+sizeof(svidmsidhash_t)*i,sizeof(svidmsidhash_t));
      svmsha_peer_changed.push_back(svmsha);
      //svidmsid_t svms;
      //memcpy(&svms.svid,read_msg_->data+6*i,2);
      //memcpy(&svms.msid,read_msg_->data+6*i+2,4);
      if(svmsha.svid>=srvs_.nodes.size()){
        LOG("%04X ERROR read_peer_missing_messages peersvid\n",svid);
        leave();
        return;}
      //LOG("%04X PEER changed %04X:%08X<-xxxxxxxx\n",svid,svmsha.svid,svmsha.msid);
      char sigh[2*SHA256_DIGEST_LENGTH];
      ed25519_key2text(sigh,svmsha.sigh,SHA256_DIGEST_LENGTH);
      LOG("%04X PEER: %04X:xxxxxxxx->%08X %.*s\n",svid,svmsha.svid,svmsha.msid,2*SHA256_DIGEST_LENGTH,sigh);}
    write_peer_missing_hashes(error);
  }

  void write_peer_missing_hashes(const boost::system::error_code& error)
  { if(error){
      LOG("%04X ERROR write_peer_missing_hashes error\n",svid);
      leave();
      return;}
    mtx_.lock();
    //LOG("%04X PEER write peer missing hashes\n",svid);
    if(BLOCK_MODE_PEER<2){ // must be called 2 times, from write_peer_missing_messages and from read_peer_missing_messages; second call writes response
      BLOCK_MODE_PEER=2;
      mtx_.unlock();
      return;}
    write_msgs_.pop_front();
    mtx_.unlock();
    //LOG("%04X PEER write peer missing hashes run\n",svid);
    // we have svmsha_peer_changed defined, lets send missing hashes
    // first let's check if the msids are correct (must be!) ... later we can consider not sending them, but we will need to make sure we know about all double spends
    if(peer_changed){ // TODO, send also server_changed
      int i=0;
      message_ptr put_msg(new message(sizeof(svidmsidhash_t)*peer_changed));
      assert(put_msg->data!=NULL);
      for(auto it=svmsha_peer_changed.begin();it!=svmsha_peer_changed.end();++it,i++){
        auto mp=server_.last_svid_msgs.find(it->svid);
        if(mp==server_.last_svid_msgs.end()){
          svidmsidhash_t svmsha={it->svid,0,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}};
          memcpy(put_msg->data+i*sizeof(svidmsidhash_t),(const char*)&svmsha,sizeof(svidmsidhash_t));
          LOG("%04X SEND: %04X:%08X->00000000 missed\n",svid,it->svid,it->msid);
          continue;}
        svidmsidhash_t svmsha;
        svmsha.svid=mp->first;
        svmsha.msid=mp->second->msid;
        memcpy(svmsha.sigh,mp->second->sigh,sizeof(hash_t));
        memcpy(put_msg->data+i*sizeof(svidmsidhash_t),(const char*)&svmsha,sizeof(svidmsidhash_t));
        char sigh[2*SHA256_DIGEST_LENGTH];
        ed25519_key2text(sigh,svmsha.sigh,SHA256_DIGEST_LENGTH);
        LOG("%04X SEND: %04X:%08X->%08X %.*s\n",svid,it->svid,it->msid,svmsha.msid,2*SHA256_DIGEST_LENGTH,sigh);}
      put_msg->sent.insert(svid);
      put_msg->busy.insert(svid);
      assert(i==peer_changed);
      assert(write_msgs_.empty());
      mtx_.lock(); // needed ?
      write_msgs_.push_front(put_msg);
      mtx_.unlock(); // needed ?
      LOG("%04X SENDING %u hashes to PEER [len: %d]\n",svid,peer_changed,write_msgs_.front()->len);
      boost::asio::async_write(socket_,boost::asio::buffer(write_msgs_.front()->data,write_msgs_.front()->len),
        boost::bind(&peer::send_ok,shared_from_this(),boost::asio::placeholders::error));}
    else{
      BLOCK_MODE_SERVER=3;}
    if(svmsha_serv_changed.size()>0){
      //BLOCK_MODE_SERVER=3;
      // prepera buffer to read missing 
      free(read_msg_->data);
      read_msg_->len=sizeof(svidmsidhash_t)*svmsha_serv_changed.size();
      read_msg_->data=(uint8_t*)malloc(read_msg_->len);
      LOG("%04X WAITING for %lu hashes from PEER [len: %d]\n",svid,svmsha_serv_changed.size(),read_msg_->len);
      boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_->data,read_msg_->len),
        boost::bind(&peer::read_peer_missing_hashes,shared_from_this(),boost::asio::placeholders::error));}
    else{ // go back to standard write mode
      read_peer_missing_hashes(error);}
  }

  void message_phash(uint8_t* mhash,std::map<uint16_t,msidhash_t>& map)
  { SHA256_CTX sha256;
    SHA256_Init(&sha256);
    for(std::map<uint16_t,msidhash_t>::iterator it=map.begin();it!=map.end();++it){
      if(!it->second.msid){
        LOG("%04X HASH %04X:00000000<-%08X@%08X no change, ignore\n",svid,it->first,
          server_.last_srvs_.nodes[it->first].msid,server_.last_srvs_.now);
        continue;}
      char sigh[2*SHA256_DIGEST_LENGTH];
      ed25519_key2text(sigh,it->second.sigh,SHA256_DIGEST_LENGTH);
      LOG("%04X HASH %04X:%08X<-%08X@%08X %.*s\n",svid,(int)(it->first),(int)(it->second.msid),
        server_.last_srvs_.nodes[it->first].msid,server_.last_srvs_.now,2*SHA256_DIGEST_LENGTH,sigh);
      SHA256_Update(&sha256,it->second.sigh,sizeof(hash_t));}
    SHA256_Final(mhash, &sha256);
  }

  void read_peer_missing_hashes(const boost::system::error_code& error)
  { if(error){
      LOG("%04X ERROR read_peer_missing_hashes error\n",svid);
      leave();
      return;}
    svid_msha.clear();
    for(auto it=server_.last_svid_msgs.begin();it!=server_.last_svid_msgs.end();++it){
      msidhash_t msha;
      msha.msid=it->second->msid;
      memcpy(msha.sigh,it->second->sigh,sizeof(hash_t));
      LOG("%04X SERV %04X:%08X<-%08X@%08X start\n",svid,it->first,it->second->msid,
        server_.last_srvs_.nodes[it->first].msid,server_.last_srvs_.now);
      svid_msha[it->first]=msha;}
    //LOG("%04X PEER read peer missing hashes (svid_msha.size=%d)\n",svid,(int)svid_msha.size());

    // newer hashes from peer
    assert(!svmsha_serv_changed.size() || read_msg_->data!=NULL);
    int i=0;
    for(auto it=svmsha_serv_changed.begin();it!=svmsha_serv_changed.end();++it,i++){
      svidmsidhash_t svmsha;
      memcpy(&svmsha,read_msg_->data+sizeof(svidmsidhash_t)*i,sizeof(svidmsidhash_t));
      msidhash_t msha;
      msha.msid=svmsha.msid;
      memcpy(msha.sigh,svmsha.sigh,sizeof(hash_t));
      //memcpy(&msha.msid,read_msg_->data+i*(4+SHA256_DIGEST_LENGTH),4);
      //memcpy(msha.sigh,read_msg_->data+i*(4+SHA256_DIGEST_LENGTH)+4,SHA256_DIGEST_LENGTH);
      svid_msha[it->svid]=msha;
      //if(msha.msid){
      //  svid_msha[it->svid]=msha;}
      //else{
      //  svid_msha.erase(it->svid);}
      char hash[2*SHA256_DIGEST_LENGTH];
      ed25519_key2text(hash,msha.sigh,SHA256_DIGEST_LENGTH);
      LOG("%04X SERV %04X:%08X<-%08X missed  %.*s\n",svid,it->svid,msha.msid,it->msid,2*SHA256_DIGEST_LENGTH,hash);}

    // other peer changes
    for(auto it=svmsha_peer_changed.begin();it!=svmsha_peer_changed.end();++it){
      if(!it->msid){
        msidhash_t msha;
        bzero(&msha,sizeof(msidhash_t)); //send 0 to indicate hash from last block
        svid_msha[it->svid]=msha;
        LOG("%04X PEER %04X:00000000<-xxxxxxxx changed\n",svid,it->svid);
        continue;}
      //message_ptr pm=server_.message_svidmsid(it->svid,it->msid);
      //if(pm==NULL){
      //  LOG("%04X PEER %04X:%08X<-xxxxxxxx changed, MISSING PEER HASH!\n",svid,it->svid,it->msid);
      //  //leave(); // do not die yet
      //  continue;}
      msidhash_t msha;
      msha.msid=it->msid;
      memcpy(msha.sigh,it->sigh,sizeof(hash_t));
      svid_msha[it->svid]=msha;
      char hash[2*SHA256_DIGEST_LENGTH];
      ed25519_key2text(hash,msha.sigh,SHA256_DIGEST_LENGTH);
      LOG("%04X PEER %04X:%08X<-xxxxxxxx changed %.*s\n",svid,it->svid,it->msid,2*SHA256_DIGEST_LENGTH,hash);}

    message_phash(peer_cand.hash,svid_msha);
    // create new map and
    char hash1[2*SHA256_DIGEST_LENGTH];
    char hash2[2*SHA256_DIGEST_LENGTH];
    ed25519_key2text(hash1,last_message_hash,SHA256_DIGEST_LENGTH);
    ed25519_key2text(hash2,peer_cand.hash,SHA256_DIGEST_LENGTH);
    LOG("%04X HASH check:\n  %.*s vs\n  %.*s\n",svid,2*SHA256_DIGEST_LENGTH,hash1,2*SHA256_DIGEST_LENGTH,hash2);
    if(memcmp(last_message_hash,peer_cand.hash,SHA256_DIGEST_LENGTH)){
      BLOCK_MODE_ERROR|=1;
      if(BLOCK_MODE_ERROR & 0xC){
        LOG("%04X HASH server check ERROR (%X) again, leaving\n",svid,BLOCK_MODE_ERROR);
        leave();
        return;}
      LOG("%04X HASH server check ERROR (%X)\n",svid,BLOCK_MODE_ERROR);
      return;}
    else{
      LOG("%04X HASH server check OK (%X)\n",svid,BLOCK_MODE_ERROR);
//#if BLOCKSEC == 0x20
//      if(opts_.svid==4 && !BLOCK_MODE_ERROR){ //check this backup protocoll
//        BLOCK_MODE_ERROR|=1;
//        LOG("%04X HASH server check overwrite to ERROR (%X)\n",svid,BLOCK_MODE_ERROR);}
//#endif
      }
    message_ptr put_msg(new message());
    assert(put_msg->len==message::header_length);
    put_msg->data[0]=BLOCK_MODE_ERROR<<1;
    put_msg->sent.insert(svid); //only to prevent assert(false)
    put_msg->busy.insert(svid); //only to prevent assert(false)
    mtx_.lock();
    write_msgs_.push_front(put_msg); // to prevent data loss
    mtx_.unlock();
    boost::asio::async_write(socket_,boost::asio::buffer(put_msg->data,put_msg->len),
      boost::bind(&peer::peer_block_check,shared_from_this(),boost::asio::placeholders::error)); 
  }

  void send_ok(const boost::system::error_code& error)
  { if(error){
      LOG("%04X ERROR send_ok error\n",svid);
      leave();}
    return;
  }

  void peer_block_check(const boost::system::error_code& error)
  { if(error){
      LOG("%04X ERROR peer_block_check error\n",svid);
      leave();}
    read_msg_ = boost::make_shared<message>(); // continue with a fresh message container
    boost::asio::async_read(socket_,
      boost::asio::buffer(read_msg_->data,message::header_length),
      boost::bind(&peer::peer_block_finish,shared_from_this(),boost::asio::placeholders::error));
  }
   
  void peer_block_finish(const boost::system::error_code& error)
  { if(error){
      LOG("%04X ERROR peer_block_finish error\n",svid);
      leave();
      return;}
    BLOCK_MODE_ERROR|=read_msg_->data[0];
    if(BLOCK_MODE_ERROR & 0x2){
      if(BLOCK_MODE_ERROR & 0xC){
        LOG("%04X HASH peer check ERROR (%X) again, leaving\n",svid,BLOCK_MODE_ERROR);
        leave();
        return;}
      LOG("%04X HASH peer check ERROR (%X)\n",svid,BLOCK_MODE_ERROR);}
    else{
      LOG("%04X HASH peer check OK (%X)\n",svid,BLOCK_MODE_ERROR);}
    if(BLOCK_MODE_ERROR & 0x3){
      LOG("%04X SYNC error (%X), trying again\n",svid,BLOCK_MODE_ERROR);
      BLOCK_MODE_ERROR=0x4;
      BLOCK_MODE_PEER=1;
      BLOCK_MODE_SERVER=2;
      read_msg_ = boost::make_shared<message>(); // continue with a fresh message container
      boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_->data,2),
        boost::bind(&peer::read_peer_missing_header,shared_from_this(),boost::asio::placeholders::error));
      //must wait for previous write to finish
      mtx_.lock();
      write_msgs_.clear(); //TODO, is this needed ???
      mtx_.unlock();
      write_peer_missing_messages();
      return;}
    LOG("%04X SYNC OK\n",svid);
    mtx_.lock();
    server_.save_candidate(peer_cand,svid_msha,svid);
    write_msgs_.clear(); //TODO, is this needed ???
    for(auto me=wait_msgs_.begin();me!=wait_msgs_.end();me++){
      //(*me)->sent.insert(svid); //just in case
      //(*me)->busy.insert(svid); //just in case
      write_msgs_.push_back(*me);}
    wait_msgs_.clear();
    BLOCK_MODE_ERROR=0;
    BLOCK_MODE_SERVER=0;
    BLOCK_MODE_PEER=0;
    if(!write_msgs_.empty()){
      //int len=message_len(write_msgs_.front());
      int len=write_msgs_.front()->len;
      boost::asio::async_write(socket_,boost::asio::buffer(write_msgs_.front()->data,len),
        boost::bind(&peer::handle_write,shared_from_this(),boost::asio::placeholders::error)); }
    read_msg_ = boost::make_shared<message>(); // continue with a fresh message container
    boost::asio::async_read(socket_,
      boost::asio::buffer(read_msg_->data,message::header_length),
      boost::bind(&peer::handle_read_header,shared_from_this(),boost::asio::placeholders::error));
    mtx_.unlock();
  }

  int parse_vote() //TODO, make this function similar to handle_read_block (make this handle_read_candidate)
  { if((read_msg_->msid!=server_.last_srvs_.now && read_msg_->msid!=srvs_.now) || read_msg_->now<read_msg_->msid || read_msg_->now>=read_msg_->msid+2*BLOCKSEC){
      LOG("%04X BLOCK TIME error now:%08X msid:%08X block[-1].now:%08X block[].now:%08X \n",svid,read_msg_->now,read_msg_->msid,server_.last_srvs_.now,srvs_.now);
      return(0);}
    hash_s cand;
    assert(read_msg_->data!=NULL);
    memcpy(cand.hash,read_msg_->data+message::data_offset,sizeof(hash_t));
    candidate_ptr c_ptr=server_.known_candidate(cand,svid);
    char hash[2*SHA256_DIGEST_LENGTH];
    ed25519_key2text(hash,cand.hash,SHA256_DIGEST_LENGTH);
    LOG("%04X CAND %04X %.*s (len:%d)\n",svid,read_msg_->svid,2*SHA256_DIGEST_LENGTH,hash,read_msg_->len);
    if(c_ptr==NULL){
      LOG("%04X PARSE --NEW-- vote for --NEW-- candidate\n",svid);
      if(read_msg_->len<4+64+10+sizeof(hash_t)+2){
        LOG("%04X PARSE vote short message read FATAL\n",svid);
        return(0);}
      uint16_t changed; //TODO, confirm no need to change this to uint32_t
      memcpy(&changed,read_msg_->data+4+64+10+sizeof(hash_t),2);
      if(!changed){
        LOG("%04X PARSE vote empty change list FATAL\n",svid);
        return(0);}
      uint32_t len=changed*(2+4+sizeof(hash_t));
      uint8_t changes[len],*d=changes;
      if(read_msg_->len<4+64+10+sizeof(hash_t)+2+len){
        LOG("%04X PARSE vote bad message length read FATAL (len:%d)\n",svid,len);
        return(0);}
      memcpy(changes,read_msg_->data+4+64+10+sizeof(hash_t)+2,len);
      std::map<uint16_t,msidhash_t> new_svid_msha(svid_msha);
      for(int i=0;i<changed;i++,d+=2+4+sizeof(hash_t)){
        uint16_t psvid;
        msidhash_t msha;
        memcpy(&psvid,d,2);
        memcpy(&msha.msid,d+2,4);
        memcpy(&msha.sigh,d+6,sizeof(hash_t));
        if(psvid>=srvs_.nodes.size()){
          LOG("%04X PARSE bad psvid %04X, FATAL\n",svid,psvid);
          return(0);}
        new_svid_msha[psvid]=msha;}
      hash_t tmp_hash;
      message_phash(tmp_hash,new_svid_msha);
      if(memcmp(cand.hash,tmp_hash,SHA256_DIGEST_LENGTH)){
        LOG("%04X ERROR parsing hash from peer\n",svid);
        return(0);}
      LOG("%04X OK candidate from peer\n",svid);
      //std::map<uint16_t,msidhash_t> have;
      c_ptr=server_.save_candidate(cand,new_svid_msha,svid);} // only new_svid_msha needed
    else{
      LOG("%04X PARSE vote for known candidate\n",svid);}
    //modify tail from message
    uint16_t changed=c_ptr->svid_miss.size()+c_ptr->svid_have.size(); //FIXME, this can be more than 0xFFFF !!!!
    uint32_t oldlen=read_msg_->len;
    if(changed){
//FIXME, does not enter this place !!!
      read_msg_->len=message::data_offset+sizeof(hash_t)+2+(changed*(2+4+sizeof(hash_t)));
      read_msg_->data=(uint8_t*)realloc(read_msg_->data,read_msg_->len); // throw if no RAM ???
      memcpy(read_msg_->data+message::data_offset+sizeof(hash_t),&changed,2);
      uint8_t* d=read_msg_->data+message::data_offset+sizeof(hash_t)+2;
      for(auto it=c_ptr->svid_have.begin();it!=c_ptr->svid_have.end();it++,d+=2+4+sizeof(hash_t)){ //have first!
        memcpy(d,&it->first,2);
        memcpy(d+2,&it->second.msid,4);
        memcpy(d+6,&it->second.sigh,sizeof(hash_t));}
      for(auto it=c_ptr->svid_miss.begin();it!=c_ptr->svid_miss.end();it++,d+=2+4+sizeof(hash_t)){ //miss second
        memcpy(d,&it->first,2);
        memcpy(d+2,&it->second.msid,4);
        memcpy(d+6,&it->second.sigh,sizeof(hash_t));}
      LOG("%04X CHANGE CAND LENGTH! [len:%d->%d]\n",svid,oldlen,read_msg_->len);
      assert(d-read_msg_->data==read_msg_->len);}
    else{
      read_msg_->len=message::data_offset+sizeof(hash_t);
      read_msg_->data=(uint8_t*)realloc(read_msg_->data,read_msg_->len);}
    //NEW, store in data[] too !!!
    if(oldlen!=read_msg_->len){
      LOG("%04X STORE DIFFERENT CAND LENGTH [len:%d->%d]\n",svid,oldlen,read_msg_->len);
      memcpy(read_msg_->data+1,&read_msg_->len,3);}
    return(1);
  }

  uint32_t svid;
  int do_sync; // needed by server::get_more_headers , FIXME, remove this, user peer_hs.do_sync
  bool killme;
private:
  boost::asio::io_service peer_io_service_;	//TH
  boost::asio::io_service::work work_;		//TH
  boost::asio::ip::tcp::socket socket_;
  boost::thread *iothp_;			//TH
  server& server_;
  bool incoming_;
  servers& srvs_; //FIXME ==server_.srvs_
  options& opts_; //FIXME ==server_.opts_

  // data from peer
  std::string addr;
  uint32_t port; // not needed
  uint32_t msid;

  uint32_t peer_path; //used to load data when syncing

  handshake_t sync_hs;
  handshake_t peer_hs;
  svsi_t* peer_svsi;
  node_t* peer_nods;
  //handshake_t handshake; // includes the header now
  //header_t sync_head;
  //int sync_nok;
  //int sync_nno;
  //int sync_vok;

  message_ptr read_msg_;
  message_queue write_msgs_;
  message_queue wait_msgs_;
  boost::mutex mtx_;
  // statistics
  uint32_t files_out;
  uint32_t files_in;
  uint64_t bytes_out;
  uint64_t bytes_in;
  //uint32_t ipv4; // not needed
  //uint32_t srvn;
  //uint8_t oldhash[SHA256_DIGEST_LENGTH]; //used in authentication
  uint8_t last_message_hash[SHA256_DIGEST_LENGTH]; //used in block building
  // block hash of messages
  std::map<uint16_t,uint32_t> svid_msid_new; // highest msid for each svid known to peer or server
  uint16_t peer_changed;
  std::vector<svidmsidhash_t> svmsha_serv_changed;
  std::vector<svidmsidhash_t> svmsha_peer_changed;
  std::map<uint16_t,msidhash_t> svid_msha; // peer last hash status
  hash_s peer_cand; // candidate by peer
  uint8_t BLOCK_MODE_ERROR;
  uint8_t BLOCK_MODE_SERVER;
  uint8_t BLOCK_MODE_PEER;
  //bool io_on;
  //peer_ptr myself; // to block destructor
};

#endif // PEER_HPP
