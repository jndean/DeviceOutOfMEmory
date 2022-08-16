
#include <Vertex.hpp>

#include "doomtype.h"
#include "ipu_print.h"


typedef uint8_t pixel_t;


extern "C" {
    void AM_LevelInit(void);
    void AM_Drawer(pixel_t*);
};


class AM_LevelInit_Vertex : public poplar::Vertex {
 public:
  bool compute() {
    AM_LevelInit(); 
    return true;
  }
};


class AM_Drawer_Vertex : public poplar::Vertex {
 public:
  poplar::InOut<poplar::Vector<unsigned char>> frame;

  bool compute() {
    AM_Drawer(&frame[0]);
    return true;
  }
};


class IPU_GetPrintBuf_Vertex : public poplar::Vertex {
 public:
  poplar::Output<poplar::Vector<char>> printbuf;

  bool compute() {
    get_ipuprint_data(&printbuf[0], printbuf.size());
    return true;
  }
};


// class G_Ticker_Vertex : public poplar::Vertex { Call G_ticker in D_ProcessEvents
//  public:

//   bool compute() {
//     return true;
//   }
// };

class D_ProcessEvents_Vertex : public poplar::Vertex {
 public:

  bool compute() {
    // Unpack and process events
    return true;
  }
};

