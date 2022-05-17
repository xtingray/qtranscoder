/*
 * Copyright (c) 2013-2018 Andreas Unterweger
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Simple audio converter
 *
 * @example transcode_aac.c
 * Convert an input audio file to AAC in an MP4 container using FFmpeg.
 * Formats other than MP4 are supported based on the output file extension.
 * @author Andreas Unterweger (dustsigns@gmail.com)
 */

#include <QObject>

#ifdef __cplusplus
extern "C" {
    #include <stdio.h>
    #include "libavformat/avformat.h"
    #include "libavformat/avio.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/audio_fifo.h"
    #include "libavutil/avassert.h"
    #include "libavutil/avstring.h"
    #include "libavutil/frame.h"
    #include "libavutil/opt.h"
    #include "libswresample/swresample.h"
}
#endif

/* The output bit rate in bit/s */
#define OUTPUT_BIT_RATE 96000
/* The number of output channels */
#define OUTPUT_CHANNELS 2

class Transcoder: public QObject
{
    Q_OBJECT

    public:
        Transcoder(const char *input, const char *output);
        int processInput();

    private:
        static int openInputFile(const char *filename,
                           AVFormatContext **inputFormatContext,
                           AVCodecContext **inputCodecContext);

        static int openOutputFile(const char *filename,
                            AVCodecContext *inputCodecContext,
                            AVFormatContext **outputFormatContext,
                            AVCodecContext **outputCodecContext);

        static int initPacket(AVPacket **packet);

        static int initInputFrame(AVFrame **frame);

        static int initResampler(AVCodecContext *inputCodecContext,
                          AVCodecContext *outputCodecContext,
                          SwrContext **resampleContext);

        static int initFifo(AVAudioFifo **fifo, AVCodecContext *outputCodecContext);

        static int writeOutputFileHeader(AVFormatContext *outputFormatContext);

        static int decodeAudioFrame(AVFrame *frame,
                              AVFormatContext *inputFormatContext,
                              AVCodecContext *inputCodecContext,
                              int *dataPresent, int *finished);

        static int initConvertedSamples(uint8_t ***convertedInputSamples,
                                  AVCodecContext *outputCodecContext,
                                  int frameSize);

        static int convertSamples(const uint8_t **inputData,
                           uint8_t **convertedData, const int frameSize,
                           SwrContext *resampleContext);

        static int addSamplesToFifo(AVAudioFifo *fifo,
                               uint8_t **convertedInputSamples,
                               const int frameSize);

        static int readDecodeConvertAndStore(AVAudioFifo *fifo,
                                         AVFormatContext *inputFormatContext,
                                         AVCodecContext *inputCodecContext,
                                         AVCodecContext *outputCodecContext,
                                         SwrContext *resamplerContext,
                                         int *finished);

        static int initOutputFrame(AVFrame **frame,
                             AVCodecContext *outputCodecContext,
                             int frameSize);

        static int encodeAudioFrame(AVFrame *frame,
                                      AVFormatContext *outputFormatContext,
                                      AVCodecContext *outputCodecContext,
                                      int *dataPresent);

        static int loadEncodeAndWrite(AVAudioFifo *fifo,
                                         AVFormatContext *outputFormatContext,
                                         AVCodecContext *outputCodecContext);

        static int writeOutputFileTrailer(AVFormatContext *outputFormatContext);

        const char * inputFile;
        const char * outputFile;
};
