#include <Vertex.hpp>

#include "ipu_transfer.h"


extern "C" {
    void P_SetupLevel_pt0(void);
    void P_LoadBlockMap(const unsigned char *buf);
    void P_LoadVertexes(const unsigned char *buf);
    void P_LoadSectors(const unsigned char *buf);
    void P_LoadSideDefs(const unsigned char *buf);
    void P_LoadLineDefs(const unsigned char *buf);
    void P_LoadSubsectors(const unsigned char *buf);
    void IPU_Setup_UnpackMarkNums(const unsigned char* buf);
};


// --------------- P_Setup ----------------- //

class P_SetupLevel_pt0_Vertex : public poplar::Vertex {
  poplar::Output<int> lumpNum;
 public:
  bool compute() {
    P_SetupLevel_pt0();
    *lumpNum = requestedlumpnum;
    return true;
  }
};


class P_LoadBlockMap_Vertex : public poplar::Vertex {
  poplar::Input<poplar::Vector<unsigned char>> lumpBuf;
  poplar::Output<int> lumpNum;
 public:
  bool compute() {
    P_LoadBlockMap(&lumpBuf[0]); 
    *lumpNum = requestedlumpnum;
    return true;
  }
};

class P_LoadVertexes_Vertex : public poplar::Vertex {
  poplar::Input<poplar::Vector<unsigned char>> lumpBuf;
  poplar::Output<int> lumpNum;
 public:
  bool compute() {
    P_LoadVertexes(&lumpBuf[0]); 
    *lumpNum = requestedlumpnum;
    return true;
  }
};

class P_LoadSectors_Vertex : public poplar::Vertex {
  poplar::Input<poplar::Vector<unsigned char>> lumpBuf;
  poplar::Output<int> lumpNum;
 public:
  bool compute() {
    P_LoadSectors(&lumpBuf[0]); 
    *lumpNum = requestedlumpnum;
    return true;
  }
};

class P_LoadSideDefs_Vertex : public poplar::Vertex {
  poplar::Input<poplar::Vector<unsigned char>> lumpBuf;
  poplar::Output<int> lumpNum;
 public:
  bool compute() {
    P_LoadSideDefs(&lumpBuf[0]); 
    *lumpNum = requestedlumpnum;
    return true;
  }
};

class P_LoadLineDefs_Vertex : public poplar::Vertex {
  poplar::Input<poplar::Vector<unsigned char>> lumpBuf;
  poplar::Output<int> lumpNum;
 public:
  bool compute() {
    P_LoadLineDefs(&lumpBuf[0]); 
    *lumpNum = requestedlumpnum;
    return true;
  }
};


class P_LoadSubsectors_Vertex : public poplar::Vertex {
  poplar::Input<poplar::Vector<unsigned char>> lumpBuf;
  poplar::Output<int> lumpNum;
 public:
  bool compute() {
    P_LoadSubsectors(&lumpBuf[0]); 
    *lumpNum = requestedlumpnum;
    return true;
  }
};

// ------------ IPU_Setup ------------ //

class IPU_Setup_UnpackMarknumSprites_Vertex : public poplar::Vertex {
  poplar::Input<poplar::Vector<unsigned char>> buf;
 public:
  bool compute() {
    IPU_Setup_UnpackMarkNums(&buf[0]);     
    return true;
  }
};