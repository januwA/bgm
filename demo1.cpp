#ifdef _WIN32
#include <windows.h>
#endif

#include <iostream>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
}

using namespace std;

// 将数据包解码为帧
static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext,
                         AVFrame *pFrame);
// 将帧保存到 .pgm 文件中
static void save_gray_frame(unsigned char *buf, int wrap, int xsize, int ysize,
                            char *filename);

int main(int argc, char *argv[]) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif

  const char *url = "C:\\Users\\16418\\Videos\\hmailserver.mp4";

  // AVFormatContext 持有格式（Container）的头信息为此组件分配内存
  AVFormatContext *pFormatContext = avformat_alloc_context();
  if (pFormatContext == nullptr) {
    fprintf(stderr, "ERROR AVFormatContext 分配内存失败\n");
    return -1;
  }

  // 打开文件并读取文件头。 编解码器未打开。
  // AVInputFormat（如果你传递 NULL，它会自动检测）和
  // AVDictionary（这是分离器的选项）
  // http://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#gac05d61a2b492ae3985c658f34622c19d
  if (avformat_open_input(&pFormatContext, url, NULL, NULL) != 0) {
    fprintf(stderr, "ERROR 无法打开文件\n");
    return -1;
  }

  // 现在我们可以访问有关我们文件的一些信息，
  // 因为我们读取了它的标题，我们可以说出它是什么格式（容器）以及与格式本身相关的一些其他信息。
  fprintf(stdout, "Format %s, duration %lld us\n",
          pFormatContext->iformat->long_name, pFormatContext->duration);

  // 从Format中读取Packets获取流信息
  // 此函数填充 pFormatContext->streams（大小等于 pFormatContext->nb_streams）
  // https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#gad42172e27cddafb81096939783b157bb
  if (avformat_find_stream_info(pFormatContext, NULL) < 0) {
    fprintf(stderr, "ERROR 无法获取流信息\n");
    return -1;
  }

  // 知道如何编码和解码流的组件，它是编解码器（音频或视频）
  // http://ffmpeg.org/doxygen/trunk/structAVCodec.html
  const AVCodec *pCodec = NULL;

  // 该组件描述了流 i 使用的编解码器的属性
  // https://ffmpeg.org/doxygen/trunk/structAVCodecParameters.html
  AVCodecParameters *pCodecParameters = NULL;
  int video_stream_index = -1;

  // 遍历所有流并打印其主要信息
  for (int i = 0; i < pFormatContext->nb_streams; i++) {
    AVCodecParameters *pLocalCodecParameters =
        pFormatContext->streams[i]->codecpar;
    fprintf(stdout, "AVStream->time_base 开放编码前 %d/%d\n",
            pFormatContext->streams[i]->time_base.num,
            pFormatContext->streams[i]->time_base.den);
    fprintf(stdout, "AVStream->r_frame_rate 开放编码前 %d/%d\n",
            pFormatContext->streams[i]->r_frame_rate.num,
            pFormatContext->streams[i]->r_frame_rate.den);
    fprintf(stdout, "AVStream->start_time %lld\n",
            pFormatContext->streams[i]->start_time);
    fprintf(stdout, "AVStream->duration %lld\n",
            pFormatContext->streams[i]->duration);

    fprintf(stdout, "找到合适的解码器 (CODEC)\n");

    // 找到编解码器 ID 的注册解码器
    // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga19a0ca553277f019dd5b0fec6e1f9dca
    const AVCodec *pLocalCodec =
        avcodec_find_decoder(pLocalCodecParameters->codec_id);

    if (pLocalCodec == NULL) {
      fprintf(stderr, "ERROR 不支持的编解码器!\n");
      // 在此示例中，如果未找到编解码器，我们将跳过它
      continue;
    }

    // 当流是视频时，我们存储它的索引、编解码器参数和编解码器
    if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
      if (video_stream_index == -1) {
        video_stream_index = i;
        pCodec = pLocalCodec;
        pCodecParameters = pLocalCodecParameters;
      }

      fprintf(stdout, "Video Codec: resolution %d x %d\n",
              pLocalCodecParameters->width, pLocalCodecParameters->height);
    } else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
      fprintf(stdout, "Audio Codec: %d channels, sample rate %d\n",
              pLocalCodecParameters->channels,
              pLocalCodecParameters->sample_rate);
    }

    // 打印其名称、ID 和比特率
    fprintf(stdout, "\tCodec %s ID %d bit_rate %lld\n", pLocalCodec->name,
            pLocalCodec->id, pLocalCodecParameters->bit_rate);
  }

  if (video_stream_index == -1) {
    fprintf(stderr, "文件 %s 不包含视频流!\n", url);
    return -1;
  }

  /*使用编解码器，我们可以为 AVCodecContext
   * 分配内存，它将保存我们的解码/编码过程的上下文，但随后我们需要使用 CODEC
   * 参数填充此编解码器上下文*/

  // https://ffmpeg.org/doxygen/trunk/structAVCodecContext.html
  AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);
  if (!pCodecContext) {
    fprintf(stderr, "无法为 AVCodecContext 分配内存\n");
    return -1;
  }

  // 根据提供的编解码器参数的值填充编解码器上下文
  // https://ffmpeg.org/doxygen/trunk/group__lavc__core.html#gac7b282f51540ca7a99416a3ba6ee0d16
  if (avcodec_parameters_to_context(pCodecContext, pCodecParameters) < 0) {
    fprintf(stderr, "无法将编解码器参数复制到编解码器上下文\n");
    return -1;
  }

  /*填充编解码器上下文后，我们需要打开编解码器*/

  // 初始化 AVCodecContext 以使用给定的 AVCodec
  // https://ffmpeg.org/doxygen/trunk/group__lavc__core.html#ga11f785a188d7d9df71621001465b0f1d
  if (avcodec_open2(pCodecContext, pCodec, NULL) < 0) {
    fprintf(stderr, "无法通过 avcodec_open2 打开编解码器\n");
    return -1;
  }

  /*现在我们将从流中读取数据包并将它们解码为帧*/

  // https://ffmpeg.org/doxygen/trunk/structAVFrame.html
  AVFrame *pFrame = av_frame_alloc();
  if (!pFrame) {
    fprintf(stderr, "无法为 AVFrame 分配内存\n");
    return -1;
  }

  // https://ffmpeg.org/doxygen/trunk/structAVPacket.html
  AVPacket *pPacket = av_packet_alloc();
  if (!pPacket) {
    fprintf(stderr, "无法为 AVPacket 分配内存\n");
    return -1;
  }

  int response = 0;
  int how_many_packets_to_process = 8;

  // 用流中的数据填充数据包
  // https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#ga4fdb3084415a82e3810de6ee60e46a61
  while (av_read_frame(pFormatContext, pPacket) >= 0) {
    // 如果是视频流
    if (pPacket->stream_index == video_stream_index) {
      fprintf(stdout, "AVPacket->pts %lld\n", pPacket->pts);

      response = decode_packet(pPacket, pCodecContext, pFrame);

      if (response < 0) break;

      // 停止它，否则我们将节省数百帧
      if (--how_many_packets_to_process <= 0) break;
    }

    // https://ffmpeg.org/doxygen/trunk/group__lavc__packet.html#ga63d5a489b419bd5d45cfd09091cbcbc2
    av_packet_unref(pPacket);
  }

  fprintf(stdout, "释放所有资源\n");

  avformat_close_input(&pFormatContext);
  av_packet_free(&pPacket);
  av_frame_free(&pFrame);
  avcodec_free_context(&pCodecContext);

  return 0;
}

static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext,
                         AVFrame *pFrame) {
  // 通过编解码器上下文将原始数据包（压缩帧）发送到解码器。
  // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga58bc4bf1e0ac59e27362597e467efff3
  int response = avcodec_send_packet(pCodecContext, pPacket);

  if (response < 0) {
    cerr << "Error 在向解码器发送数据包时: " << response << endl;
    return response;
  }

  while (response >= 0) {
    // 通过相同的编解码器上下文从解码器接收原始数据帧（未压缩帧）
    // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga11e6542c4e66d3028668788a1a74217c
    response = avcodec_receive_frame(pCodecContext, pFrame);

    if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
      break;
    } else if (response < 0) {
      cerr << "Error 从解码器接收帧时: " << response << endl;
      return response;
    }

    if (response >= 0) {
      fprintf(stdout,
              "Frame %d (type=%c, size=%d bytes, format=%d) pts %d key_frame "
              "%d  [DTS %d]\n",
              pCodecContext->frame_number,
              av_get_picture_type_char(pFrame->pict_type), pFrame->pkt_size,
              pFrame->format, pFrame->pts, pFrame->key_frame,
              pFrame->coded_picture_number);

      char frame_filename[1024];
      snprintf(frame_filename, sizeof(frame_filename), "%s-%d.pgm", "frame",
               pCodecContext->frame_number);

      // 检查帧是否为平面 YUV 4:2:0, 12bpp
      // 这是提供的 .mp4 文件的格式
      // RGB 格式肯定不会给出灰度图像
      // 其他YUV图像可能会这样做，但未经测试，所以给出警告
      if (pFrame->format != AV_PIX_FMT_YUV420P) {
        fprintf(
            stderr,
            "Warning: the generated file may not be a grayscale image, but "
            "could e.g. be just the R component if the video format is RGB\n");
      }

      // 将灰度帧保存到 .pgm 文件中
      save_gray_frame(pFrame->data[0], pFrame->linesize[0], pFrame->width,
                      pFrame->height, frame_filename);
    }
  }
  return 0;
}

static void save_gray_frame(unsigned char *buf, int wrap, int xsize, int ysize,
                            char *filename) {
  FILE *f;
  int i;
  f = fopen(filename, "w");
  // 为 pgm 文件格式编写所需的最少标头
  // 便携式灰度图格式 ->
  // https://en.wikipedia.org/wiki/Netpbm_format#PGM_example
  fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);

  // 逐行书写
  for (i = 0; i < ysize; i++) fwrite(buf + i * wrap, 1, xsize, f);

  fclose(f);
}