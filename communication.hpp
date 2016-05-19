#ifndef _communication_hpp_
#define _communication_hpp_

#include <mpi.h>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/map.hpp>
#include <cereal/archives/binary.hpp>
#include <sstream>
#include <vector>
#include <iterator>
#include <sstream>
#include <cassert>
#include <iostream>

//For non-busy looping
#include <thread>
#include <chrono>

#define _unused(x) ((void)x) //Used for asserts

typedef unsigned long long comm_count_type;
typedef std::vector<char> msg_type;

//Used to keep track of message size statistics
static comm_count_type bytes_sent = 0;
static comm_count_type bytes_recv = 0;

//Wrapper for MPI_Init
void CommInit(int *argc, char ***argv){
  MPI_Init(argc,argv);
}

//Converts up to two objects into a combined serialized representation
template<class T, class U>
msg_type CommPrepare(const T* a, const U* b){
  std::vector<char> omsg;
  std::stringstream ss(std::stringstream::in|std::stringstream::out|std::stringstream::binary);
  ss.unsetf(std::ios_base::skipws);
  cereal::BinaryOutputArchive archive(ss);
  archive(*a);
  if(b!=nullptr)
    archive(*b);

  std::copy(std::istream_iterator<char>(ss), std::istream_iterator<char>(), std::back_inserter(omsg));

  return omsg;
}

//Converts one object into a serialized representation
template<class T>
msg_type CommPrepare(const T* a, std::nullptr_t){
  return CommPrepare(a, (int*)nullptr);
}

//Serializes and sends up to two objects
template<class T, class U>
void CommSend(const T* a, const U* b, int dest, int tag){
  auto omsg = CommPrepare(a,b);

  bytes_sent += omsg.size();

  int ret = MPI_Send(omsg.data(), omsg.size(), MPI_BYTE, dest, tag, MPI_COMM_WORLD);
  assert(ret==MPI_SUCCESS);
  _unused(ret);
}

//Serializes and sends one object
template<class T>
void CommSend(const T* a, std::nullptr_t, int dest, int tag){
  CommSend(a, (int*)nullptr, dest, tag);
}

//Sends a pre-serialized representation of an object using non-blocking
//communication. The object must be pre-serialized because the buffer containing
//the serialization must persist until the communication is complete. It makes
//more sense to manage this buffer outside of this library.
void CommISend(msg_type &msg, int dest, int tag){
  MPI_Request request;
  bytes_sent += msg.size();
  MPI_Isend(msg.data(), msg.size(), MPI_BYTE, dest, tag, MPI_COMM_WORLD, &request);
}

//See what the tag is for the next in-coming message. This is useful for
//determining how to process the message.
int CommGetTag(int from){
  int flag;
  MPI_Status status;

  do {
    MPI_Iprobe(from, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
    std::this_thread::sleep_for(MPI_BUSY_LOOP_SLEEP);
  } while (!flag);

  return status.MPI_TAG;
}

//Get my unique process identifier (i.e. rank)
int CommRank(){
  int rank;
  int ret = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  assert(ret==MPI_SUCCESS);
  _unused(ret);
  return rank;
}

//How many processes are active?
int CommSize(){
  int size;
  int ret = MPI_Comm_size (MPI_COMM_WORLD, &size);
  assert(ret==MPI_SUCCESS);
  _unused(ret);
  return size;
}

//Abort. If any process calls this it will kill all the processes.
void CommAbort(int errorcode){
  MPI_Abort(MPI_COMM_WORLD, errorcode);
}

//Receive up to two deserialized objects
template<class T, class U>
void CommRecv(T* a, U* b, int from){
  MPI_Status status;

  if(from==-1)
    from = MPI_ANY_SOURCE;

  MPI_Probe(from, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

  int msg_size;
  MPI_Get_count(&status, MPI_BYTE, &msg_size);

  std::stringstream ss(std::stringstream::in|std::stringstream::out|std::stringstream::binary);
  ss.unsetf(std::ios_base::skipws);

  // Allocate a buffer to hold the incoming data
  char* buf = (char*)malloc(msg_size);
  assert(buf!=NULL);

  MPI_Recv(buf, msg_size, MPI_BYTE, from, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

  bytes_recv += msg_size;

  ss.write(buf,msg_size);
  free(buf);

  {
    cereal::BinaryInputArchive archive(ss);
    archive(*a);
    if(b!=nullptr)
      archive(*b);
  }
}

//Receive one deserialized object
template<class T>
void CommRecv(T* a, std::nullptr_t, int from){
  CommRecv(a, (int*)nullptr, from);
}

//Send a message to all of the processes
template<class T>
void CommBroadcast(T *datum, int root){
  MPI_Bcast(datum, 1, MPI_INT, root, MPI_COMM_WORLD);
}

//Wrap things up politely
void CommFinalize(){
  MPI_Finalize();
}

//Retrieve message size statistics
comm_count_type CommBytesSent(){
  return bytes_sent;
}

//Retrieve message size statistics
comm_count_type CommBytesRecv(){
  return bytes_recv;
}

//Reset message size statistics
void CommBytesReset(){
  bytes_recv = 0;
  bytes_sent = 0;
}

#endif