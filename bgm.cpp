#define MINIAUDIO_IMPLEMENTATION

#include <windows.h>

#include <iostream>

#include "miniaudio.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/audio_fifo.h"
#include "libswresample/swresample.h"
}

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

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("No input file.\n");
    return -1;
  }

  const char* url = argv[1];

  /*打开音频文件*/
  AVFormatContext* pFormatContext = avformat_alloc_context();
  if (pFormatContext == NULL) return -1;

  if (avformat_open_input(&pFormatContext, url, NULL, NULL) != 0) return -1;

  if (avformat_find_stream_info(pFormatContext, NULL) < 0) return -1;

  int audio_stream_index =
      av_find_best_stream(pFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
  if (audio_stream_index < 0) return -1;

  AVCodecParameters* pCodecParameters =

      pFormatContext->streams[audio_stream_index]->codecpar;
  const AVCodec* pCodec =
      avcodec_find_decoder(pCodecParameters->codec_id);  // 获取解码器

  if (!pCodec) {
    fprintf(stderr, "Codec not found\n");
    return -1;
  }

  AVCodecContext* pCodecContext = avcodec_alloc_context3(pCodec);
  if (pCodecContext == NULL) return -1;

  if (avcodec_parameters_to_context(pCodecContext, pCodecParameters) < 0)
    return -1;

  if (avcodec_open2(pCodecContext, pCodec, NULL) < 0) return -1;

  /*解码音频*/
  AVPacket* pPacket = av_packet_alloc();
  if (pPacket == NULL) return -1;

  AVFrame* pFrame = av_frame_alloc();
  if (pFrame == NULL) return -1;

  SwrContext* swr = swr_alloc_set_opts(
      NULL,  // 我们正在分配一个新的上下文
      pCodecParameters->channel_layout,          // out_ch_layout
      AV_SAMPLE_FMT_S16,                         // out_sample_fmt
      pCodecParameters->sample_rate,             // out_sample_rate
      pCodecParameters->channel_layout,          // in_ch_layout
      (AVSampleFormat)pCodecParameters->format,  // in_sample_fmt
      pCodecParameters->sample_rate,             // in_sample_rate
      0,                                         // log_offset
      NULL);                                     // log_ctx
  if (swr == NULL) return -1;

  AVAudioFifo* fifo =
      av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, pCodecParameters->channels,
                          1);  // 音频 FIFO 缓冲区的上下文
  if (fifo == NULL) return -1;

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

  /*后台播放*/
  ma_decoder decoder;
  ma_device_config deviceConfig;
  ma_device device;

  // ma_decoder_init_file(argv[1], NULL, &decoder);

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

  if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
    printf("Failed to open playback device.\n");
    // ma_decoder_uninit(&decoder);
    return -3;
  }

  bool isPlaying = true;

  if (ma_device_start(&device) != MA_SUCCESS) {
    printf("Failed to start playback device.\n");
    ma_device_uninit(&device);
    // ma_decoder_uninit(&decoder);
    return -4;
  }

  avformat_close_input(&pFormatContext);
  av_packet_free(&pPacket);
  av_frame_free(&pFrame);
  avcodec_free_context(&pCodecContext);
  swr_free(&swr);

  auto pevent = false;
  // wait key event
  for (;;) {
    if (GetAsyncKeyState(0x51 /*q*/)) break;

    if (GetAsyncKeyState(0x50 /*p*/) && !pevent) {
      pevent = true;
      int ret = isPlaying ? ma_device_stop(&device) : ma_device_start(&device);

      if (ret != MA_SUCCESS) {
        ma_device_uninit(&device);
        return -4;
      }

      isPlaying = !isPlaying;
      pevent = false;
    }

    Sleep(200);
  }

  ma_device_uninit(&device);
  // ma_decoder_uninit(&decoder);
  av_audio_fifo_free(fifo);

  return 0;
}