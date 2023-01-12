#define MINIAUDIO_IMPLEMENTATION

#ifdef _WIN32
#include <windows.h>
#endif

#include <iostream>
#include <string_view>

#include "miniaudio.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/audio_fifo.h"
#include "libswresample/swresample.h"
}

typedef enum : int {
  BGM_OK = 0,
  BGM_FORMAT_CONTEXT,        /*Allocate an AVFormatContext*/
  BGM_OPEN_INPUT,            /*Open an input stream and read the header*/
  BGM_FIND_STREAM_INFO,      /*Read packets of a media file to get stream
                                information*/
  BGM_FIND_AUDIO_STREAM,     /*audio stream not find*/
  BGM_CODEC,                 /*Codec not found*/
  BGM_CODEC_CONTEXT,         /*Allocate an AVCodecContext*/
  BGM_PARAMETERS_TO_CONTEXT, /*Fill the codec context based on the values
                                    from the supplied codec parameters*/
  BGM_OPEN2,        /*Initialize the AVCodecContext to use the given AVCodec*/
  BGM_PACKET_ALLOC, /*Allocate an AVPacket*/
  BGM_FRAME_ALLOC,  /*Allocate an AVFrame*/
  BGM_SWR_ALLOC,    /*Allocate SwrContext*/
  BGM_FIFO_ALLOC,   /*Allocate an AVAudioFifo*/
  BGM_DEVICE_INIT,  /*device init*/
  BGM_PLAY,         /*play*/
  BGM_PAUSE,        /*pause*/
} bgm_result;

static std::string_view bgm_result_strings[] = {
    "ok",
    "Allocate an AVFormatContext",
    "Open an input stream and read the header",
    "Read packets of a media file to get stream information",
    "audio stream not find",
    "Codec not found",
    "Allocate an AVCodecContext",
    "Fill the codec context based on the values from the supplied codec "
    "parameters",
    "Initialize the AVCodecContext to use the given AVCodec",
    "Allocate an AVPacket",
    "Allocate an AVFrame",
    "Allocate SwrContext",
    "Allocate an AVAudioFifo",
    "device init",
    "play",
    "pause",
};

std::string_view inline bgm_result2str(bgm_result ret) {
  return bgm_result_strings[ret];
};

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput,
                   ma_uint32 frameCount) {
  AVAudioFifo* fifo = (AVAudioFifo*)pDevice->pUserData;
  if (fifo == NULL) {
    return;
  }

  // 从 AVAudioFifo 读取数据
  av_audio_fifo_read(fifo, &pOutput, frameCount);

  (void)pInput;
}

class AbstractBgm {
 public:
  /**
   * 初始化资源
   *
   * params
   * url 有音频流的资源
   *
   * return
   * 0 ok
   */
  virtual bgm_result init(std::string_view url) = 0;

 public:
  /**
   * 销毁资源
   *
   * return
   * 0 ok
   */
  virtual void destroy() = 0;

  /**
   * 播放
   *
   * return
   * 0 ok
   */
  virtual bgm_result play() = 0;

  /**
   * 暂停
   *
   * return
   * 0 ok
   */
  virtual bgm_result pause() = 0;
};

class Bgm : public AbstractBgm {
 private:
  AVFormatContext* pFormatContext{nullptr};
  AVCodecParameters* pCodecParameters{nullptr};
  const AVCodec* pCodec{nullptr};
  AVCodecContext* pCodecContext{nullptr};
  int audio_stream_index = -1;

  AVPacket* pPacket{nullptr};
  AVFrame* pFrame{nullptr};
  SwrContext* swr{nullptr};
  AVAudioFifo* fifo{nullptr};

  ma_device device;

 private:
  /**
   * 打开音频文件
   *
   * params
   * url 有音频流的资源
   *
   * return
   * 0 ok
   */
  bgm_result _open_src(std::string_view url) {
    if ((pFormatContext = avformat_alloc_context()) == nullptr)
      return BGM_FORMAT_CONTEXT;

    if (avformat_open_input(&pFormatContext, url.data(), NULL, NULL) != 0)
      return BGM_OPEN_INPUT;

    if (avformat_find_stream_info(pFormatContext, NULL) < 0)
      return BGM_FIND_STREAM_INFO;

    audio_stream_index = av_find_best_stream(pFormatContext, AVMEDIA_TYPE_AUDIO,
                                             -1, -1, NULL, 0);
    if (audio_stream_index < 0) return BGM_FIND_AUDIO_STREAM;

    pCodecParameters = pFormatContext->streams[audio_stream_index]->codecpar;
    pCodec = avcodec_find_decoder(pCodecParameters->codec_id);  // 获取解码器
    if (!pCodec) return BGM_CODEC;

    pCodecContext = avcodec_alloc_context3(pCodec);
    if (pCodecContext == nullptr) return BGM_CODEC_CONTEXT;

    if (avcodec_parameters_to_context(pCodecContext, pCodecParameters) < 0)
      return BGM_PARAMETERS_TO_CONTEXT;

    if (avcodec_open2(pCodecContext, pCodec, NULL) < 0) return BGM_OPEN2;

    return BGM_OK;
  };

  /**
   * 解码音频
   *
   * return
   * 0 ok
   */
  bgm_result _decoder() {
    if ((pPacket = av_packet_alloc()) == nullptr) return BGM_PACKET_ALLOC;
    if ((pFrame = av_frame_alloc()) == nullptr) return BGM_FRAME_ALLOC;

    swr = swr_alloc_set_opts(
        NULL,  // 我们正在分配一个新的上下文
        pCodecParameters->channel_layout,          // out_ch_layout
        AV_SAMPLE_FMT_S16,                         // out_sample_fmt
        pCodecParameters->sample_rate,             // out_sample_rate
        pCodecParameters->channel_layout,          // in_ch_layout
        (AVSampleFormat)pCodecParameters->format,  // in_sample_fmt
        pCodecParameters->sample_rate,             // in_sample_rate
        0,                                         // log_offset
        NULL);                                     // log_ctx
    if (swr == nullptr) return BGM_SWR_ALLOC;

    fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, pCodecParameters->channels,
                               1);  // 音频 FIFO 缓冲区的上下文
    if (fifo == nullptr) return BGM_FIFO_ALLOC;

    // 用流中的数据填充数据包
    while (av_read_frame(pFormatContext, pPacket) >= 0) {
      if (pPacket->stream_index == audio_stream_index) {
        // 将原始包发送到解码器上下文
        int response = avcodec_send_packet(pCodecContext, pPacket);
        if (response < 0) break;

        while ((response = avcodec_receive_frame(pCodecContext, pFrame)) >= 0) {
          AVFrame* resample_frame = av_frame_alloc();
          resample_frame->sample_rate = pFrame->sample_rate;
          resample_frame->channel_layout = pFrame->channel_layout;
          resample_frame->channels = pFrame->channels;
          resample_frame->format = AV_SAMPLE_FMT_S16;

          // 转换输入 AVFrame 中的样本并将它们写入输出 AVFrame
          swr_convert_frame(swr, resample_frame, pFrame);
          av_frame_unref(pFrame);

          av_audio_fifo_write(fifo, (void**)resample_frame->data,
                              resample_frame->nb_samples);
          av_frame_free(&resample_frame);
        }
      }

      av_packet_unref(pPacket);
    }

    return BGM_OK;
  }

  /**
   * 初始化 ma_device
   *
   * return
   * 0 ok
   */
  bgm_result _ma_device() {
    ma_device_config deviceConfig;
    /*
    解码器是一个数据源，
    这意味着我们只需使用 ma_data_source_set_looping() 来设置循环状态。
    我们将在数据回调中使用 ma_data_source_read_pcm_frames() 读取数据
    */
    // ma_data_source_set_looping(&decoder, MA_TRUE);

    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_s16;
    deviceConfig.playback.channels = pCodecParameters->channels;
    deviceConfig.sampleRate = pCodecParameters->sample_rate;
    deviceConfig.dataCallback = data_callback;
    deviceConfig.pUserData = fifo;

    if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS)
      return BGM_DEVICE_INIT;

    return BGM_OK;
  };

 public:
  bool isPlaying = false;

 public:
  virtual bgm_result init(std::string_view url) override {
    bgm_result ret = BGM_OK;

    if ((ret = _open_src(url)) != BGM_OK) return ret;
    if ((ret = _decoder()) != BGM_OK) return ret;
    if ((ret = _ma_device()) != BGM_OK) return ret;

    avformat_close_input(&pFormatContext);
    av_packet_free(&pPacket);
    av_frame_free(&pFrame);
    avcodec_free_context(&pCodecContext);
    swr_free(&swr);

    return ret;
  }

  virtual void destroy() override {
    ma_device_uninit(&device);
    if (fifo != nullptr) av_audio_fifo_free(fifo);
  }

  virtual bgm_result play() override {
    if (ma_device_start(&device) != MA_SUCCESS) return BGM_PLAY;
    isPlaying = true;
    return BGM_OK;
  }

  virtual bgm_result pause() override {
    if (ma_device_stop(&device) != MA_SUCCESS) return BGM_PAUSE;
    isPlaying = false;
    return BGM_OK;
  }

  bgm_result inline switch_play_pause() { return isPlaying ? pause() : play(); }
};

#define CHECK_BMG_RESULT(get_ret)                                     \
  {                                                                   \
    auto ret = get_ret;                                               \
    if (ret != BGM_OK) {                                              \
      fprintf(stderr, "BGM Error: %s\n", bgm_result2str(ret).data()); \
      return -1;                                                      \
    }                                                                 \
  }

class Controller {
 public:
  /**
   * 退出
   *
   * return
   * 0 ok, -1 quit controller
   */
  virtual int quit() = 0;

  /**
   * 播放或暂停
   *
   * return
   * 0 ok, -1 quit controller
   */
  virtual int switch_play_pause() = 0;

  /**
   * 帮助信息
   *
   * return
   * 0 ok, -1 quit controller
   */
  virtual int help() = 0;
};

class BgmController : public Controller {
 private:
  Bgm* _bgm;

 public:
  BgmController(Bgm* bgm) : _bgm{bgm} {}

  virtual int quit() override { return -1; }

  virtual int switch_play_pause() override {
    return _bgm->switch_play_pause() != BGM_OK ? -1 : 0;
  }

  /**
   * 阻塞等待事件
   */
  virtual void run() = 0;
};

#ifdef _WIN32
class WinController : public BgmController {
#define VM_H 0x48
#define VM_Q 0x51
#define VM_P 0x50

 public:
  WinController(Bgm* bgm) : BgmController(bgm) {}

  virtual void run() override {
    Sleep(200);

    for (;;) {
      if (GetAsyncKeyState(VM_Q)) {
        if (quit() != 0) break;
      };

      if (GetAsyncKeyState(VM_P)) {
        if (switch_play_pause() != 0) break;
      }

      if (GetAsyncKeyState(VM_H)) {
        if (help() != 0) break;
      }

      Sleep(200);
    }
  }

  virtual int help() override {
    std::cout << "bgm controller:\n"
              << "\tq Quit\n"
              << "\tp Play/Pause\n"
              << "\th help\n"
              << std::endl;
    return 0;
  }
};
#endif

BgmController* createBgmController(Bgm* bgm) {
#ifdef _WIN32
  return new WinController(bgm);
#endif

  return nullptr;
};

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("No input file.\n");
    return -1;
  }

  std::string_view url = argv[1];

  Bgm bgm;
  CHECK_BMG_RESULT(bgm.init(url));
  // CHECK_BMG_RESULT(bgm.play());

  BgmController* bc = createBgmController(&bgm);

  if (bc == nullptr) {
    fprintf(stderr, "create controller failed");
    bgm.destroy();
    return -1;
  }

  bc->help();
  bc->run();

  delete bc;

  bgm.destroy();

  return 0;
}

// 检索设备 ID
// int ma_retrieve_device() {
//   ma_context context;

//   if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) return -1;

//   ma_device_info* pPlaybackInfos;
//   ma_uint32 playbackCount;
//   ma_device_info* pCaptureInfos;
//   ma_uint32 captureCount;

//   if (ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount,
//                              &pCaptureInfos, &captureCount) != MA_SUCCESS)
//     return -1;

//   for (ma_uint32 iDevice = 0; iDevice < playbackCount; iDevice += 1) {
//     printf("%d - %s\n", iDevice, pPlaybackInfos[iDevice].name);
//   }

//   return 0;
// }
