//
// Created by Anders Cedronius on 2019-11-11.
//

#ifndef EFP_EDGEWAREFRAMEPROTOCOL_H
#define EFP_EDGEWAREFRAMEPROTOCOL_H

#include <cstdint>
#include <vector>
#include <iostream>
#include <sstream>
#include <climits>
#include <cstring>
#include <math.h>
#include <thread>
#include <unistd.h>

#define UNIT_TESTS //Enable or disable the APIs used by the unit tests

#define CIRCULAR_BUFFER_SIZE 0b1111111111111 //Must be contiguous set bits defining the size  0b1111111111111 == 8191

//flag defines
#define NO_FLAGS 0b00000000
#define INLINE_PAYLOAD 0b00010000

//FIXME
#define EFP_MAJOR_VERSION 0
#define EFP_MINOR_VERSION 1

namespace EdgewareFrameContentNamespace {

    //Payload data defines ----- START ------
    enum EdgewareFrameContentDefines : uint8_t {
        unknown,                //Standard                      //code
        privateData,            //Any user defined format       //USER (not needed)
        adts,                   //Mpeg-4 AAC ADTS framing       //ADTS (not needed)
        mpegts,                 //ITU-T H.222 188byte TS        //TSDT (not needed)
        mpegpes,                //ITU-T H.222 PES packets       //MPES (not needed)
        jpeg2000,               //ITU-T T.800 Annex M           //J2KV (not needed)
        jpeg,                   //ITU-T.81                      //JPEG (not needed)
        jpegxs,                 //ISO/IEC 21122-3               //JPXS (not needed)
        pcmaudio,               //AES-3 framing                 //AES3 (not needed)
        ndi,                    //*TBD*                         //NNDI (not needed)

        //Formats defined below (MSB='1') must also use 'code' to define the data format in the superframe

        didsdid=0x80,           //FOURCC format                 //(FOURCC) (Must be the fourcc code for the format used)
        sdi,                    //FOURCC format                 //(FOURCC) (Must be the fourcc code for the format used)
        h264,                   //ITU-T H.264                   //ANXB = Annex B framing / AVCC = AVCC framing
        h265                    //ITU-T H.265                   //ANXB = Annex B framing / AVCC = AVCC framing
    };


    //Embedded data defines ----- START ------
    enum EdgewareFrameEmbeddedContentDefines : uint8_t {
        illegal,                //may not be used
        embeddedPrivateData,    //private data
        h222pmt,                //pmt from h222 pids should be trunkated to uint8_t leaving the LSB bits only then map to streams
        mp4FragBox,             //All boxes from a mp4 fragment excluding the payload
        lastEmbeddedContent = 0x80
        //defines below here do not allow following embedded data.
    };

    //Embedded data header ----- START ------
    struct EdgewareEmbeddedHeader {
        uint8_t embeddedFrameType = EdgewareFrameEmbeddedContentDefines::illegal;
        uint16_t size = 0;
    };
}

// EFP Messages
// Negative numbers are errors
// 0 == No error
// Positive numbers are informative
namespace EdgewareFrameMessagesNamespace {
    enum EdgewareFrameMessagesDefines : int16_t {
        tooLargeFrame = -10000,     //The frame is to large for EFP packer to handle
        tooLargeEmbeddedData,       //The embedded data frame is too large.
        unknownFrametype,           //The frame type is unknown by EFP unpacker
        framesizeMismatch,          //The unpacker recieved data less than the header size
        internalCalculationError,   //The packer encountered a condition it can't handle
        endOfPacketError,           //The unpacker recieved a type2 fragment not saying it was the last
        bufferOutOfBounds,          //The unpackers circular buffer has wrapped around and all data in the buffer is from now untrusted also data prior to this may have been wrong.
                                    //This error can be triggered if there is a super high data rate data coming in with a large gap/loss of the incomming fragments in the flow
        bufferOutOfResources,       //This error is indicating there are no more buffer resources. In the unlikely event where all frames miss fragment(s) and the timeout is set high
                                    //then broken superframes will be buffered and new incoming data will claim buffers. When there are no more buffers to claim this error will be triggered.
        reservedPTSValue,           //UINT64_MAX is a EFP reserved value
        reservedCodeValue,          //UINT32_MAX is a EFP reserved value
        reservedStreamValue,        //0 is a EFP reserved value for signaling manifests
        memoryAllocationError,      //Failed allocating system memory. This is fatal and results in unknown behaviour.
        illegalEmbeddedData,        //illegal embedded data
        type1And3SizeError,         //Type1 and Type3 must have the same header size
        wrongMode,                  //mode is set to unpacker when using the class as packer or the other way around
        unpackerNotStarted,         //The EFP unpacker is not running


        noError = 0,

        notImplemented,             //feature/function/level/method/system aso. not implemented.
        duplicatePacketRecieved,    //If the underlying infrastructure is handing EFP duplicate segments the second packet of the duplicate will generate this error if the
                                    //the superframe is still not delivered to the host system. if it has then tooOldFragment will be returned instead.
        tooOldFragment,             //if the superframe has been delivered 100% complete or fragments of it due to a timeout and a fragment belongning to the superframe arrives then it's
                                    //discarded and the tooOldFragment is triggered.
        unpackerAlreadyStarted,     //The EFP unpacker is already started no need to start it again. (Stop it and start it again to change parameters)
        failedStoppingUnpacker,     //The EFP unpacker failed stopping it's resources.
        parameterError,             //When starting the unpacker the parameters given where not valid.
        type0Frame                  //Type0 frame
    };
}

//The mode set when constructing the class
namespace EdgewareFrameProtocolModeNamespace {
    enum EdgewareFrameProtocolModeDefines : uint8_t {
        unknown,
        packer,
        unpacker,
    };
}

using EdgewareFrameMessages = EdgewareFrameMessagesNamespace::EdgewareFrameMessagesDefines;
using EdgewareFrameContent = EdgewareFrameContentNamespace::EdgewareFrameContentDefines;
using EdgewareEmbeddedFrameContent = EdgewareFrameContentNamespace::EdgewareFrameEmbeddedContentDefines;
using EdgewareFrameMode = EdgewareFrameProtocolModeNamespace::EdgewareFrameProtocolModeDefines;

class EdgewareFrameProtocol {
public:

    //Reserve frame-data aligned 32-byte addresses in memory
    class AllignedFrameData {
    public:
        size_t frameSize = 0;           //Number of bytes in frame
        uint8_t* framedata = nullptr;   //recieved frame data

        AllignedFrameData(const AllignedFrameData&) = delete;
        AllignedFrameData & operator=(const AllignedFrameData &) = delete;

        AllignedFrameData(size_t memAllocSize) {
            posix_memalign((void**)&framedata, 32, memAllocSize);   //32 byte memory alignment for AVX2 processing //Winboze needs some other code.
            if (framedata) frameSize = memAllocSize;
        }

        virtual ~AllignedFrameData() {
            //Free if ever allocated
            if (framedata) free(framedata);
        }
    };

    using pFramePtr = std::shared_ptr<AllignedFrameData>;

    EdgewareFrameProtocol(uint16_t setMTU = 0, EdgewareFrameMode mode = EdgewareFrameMode::unpacker);
    virtual ~EdgewareFrameProtocol();
    //Segment and send
    EdgewareFrameMessages packAndSend(const std::vector<uint8_t> &rPacket, EdgewareFrameContent dataContent, uint64_t pts, uint32_t code, uint8_t stream, uint8_t flags);
    std::function<void(const std::vector<uint8_t> &rSubPacket)> sendCallback = nullptr;
    //Create data from segments
    EdgewareFrameMessages startUnpacker(uint32_t bucketTimeoutMaster, uint32_t holTimeoutMaster);
    EdgewareFrameMessages stopUnpacker();
    EdgewareFrameMessages unpack(const std::vector<uint8_t> &rSubPacket, uint8_t fromSource);
    std::function<void(EdgewareFrameProtocol::pFramePtr &rPacket, EdgewareFrameContent content, bool broken, uint64_t pts, uint32_t code, uint8_t stream, uint8_t flags)> recieveCallback = nullptr;

    //Delete copy and move constructors and assign operators
    EdgewareFrameProtocol(EdgewareFrameProtocol const &) = delete;              // Copy construct
    EdgewareFrameProtocol(EdgewareFrameProtocol &&) = delete;                   // Move construct
    EdgewareFrameProtocol &operator=(EdgewareFrameProtocol const &) = delete;   // Copy assign
    EdgewareFrameProtocol &operator=(EdgewareFrameProtocol &&) = delete;        // Move assign

    //Help methods ----------- START ----------
    EdgewareFrameMessages addEmbeddedData(std::vector<uint8_t> *packet, void  *privateData, size_t privateDataSize, EdgewareEmbeddedFrameContent content = EdgewareEmbeddedFrameContent::illegal, bool isLast=false);
    EdgewareFrameMessages extractEmbeddedData(pFramePtr &rPacket, std::vector<std::vector<uint8_t>> *embeddedDataList,
                                              std::vector<uint8_t> *dataContent, size_t *payloadDataPosition);
    //Help methods ----------- END ----------

    //Used by unitTests ---------------------
#ifdef UNIT_TESTS
    size_t geType1Size();
    size_t geType2Size();
#endif

private:
    //Bucket  ----- START ------
    class Bucket {
    public:
        bool mActive = false;
        EdgewareFrameContent mDataContent = EdgewareFrameContent::unknown;
        uint16_t mSavedSuperFrameNo = 0; //the SuperFrameNumber using this bucket.
        uint32_t mTimeout = 0;
        uint16_t mFragmentCounter = 0;
        uint16_t mOfFragmentNo = 0;
        uint64_t mDeliveryOrder = UINT64_MAX;
        size_t mFragmentSize = 0;
        uint64_t mPts = UINT64_MAX;
        uint32_t mCode = UINT32_MAX;
        uint8_t mStream;
        uint8_t mFlags;
        std::bitset<UINT16_MAX> mHaveRecievedPacket;
        pFramePtr mBucketData = nullptr;
    };
    //Bucket ----- END ------

    //Private methods ----- START ------
    void sendData(const std::vector<uint8_t> &rSubPacket);
    void gotData(EdgewareFrameProtocol::pFramePtr &rPacket, EdgewareFrameContent content, bool broken, uint64_t pts, uint32_t code, uint8_t stream, uint8_t flags);
    EdgewareFrameMessages unpackType1(const std::vector<uint8_t> &rSubPacket, uint8_t fromSource);
    EdgewareFrameMessages unpackType2LastFrame(const std::vector<uint8_t> &rSubPacket, uint8_t fromSource);
    EdgewareFrameMessages unpackType3(const std::vector<uint8_t> &rSubPacket, uint8_t fromSource);
    void unpackerWorker(uint32_t timeout);
    uint64_t superFrameRecalculator(uint16_t superFrame);
    //Private methods ----- END ------

    //Internal lists and variables ----- START ------
    Bucket mBucketList[CIRCULAR_BUFFER_SIZE + 1]; //Internal queue
    uint32_t mBucketTimeout = 0; //time out passed to reciever
    uint32_t mHeadOfLineBlockingTimeout = 0; //HOL time out passed to reciever
    std::mutex mNetMtx; //Mutex protecting the queue
    uint32_t mCurrentMTU = 0; //current MTU used by the packer
    //various counters to keep track of the different frames
    uint16_t mSuperFrameNoGenerator = 0;
    uint16_t mOldSuperframeNumber = 0;
    uint64_t mSuperFrameRecalc = 0;
    bool mSuperFrameFirstTime = true;
    //Reciever thread management
    std::atomic_bool mIsThreadActive;
    std::atomic_bool mThreadActive;
    //Mutex for thread safety
    std::mutex mPackkMtx; //Mutex protecting the pack part
    std::mutex mUnpackMtx; //Mutex protecting the unpack part
    EdgewareFrameMode mCurrentMode = EdgewareFrameMode::unknown;
    //Internal lists and variables ----- END ------
};

#endif //EFP_EDGEWAREFRAMEPROTOCOL_H
