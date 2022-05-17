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

#include "transcoder.h"
#include "QDebug"

/* Global timestamp for the audio frames. */
static int64_t pts = 0;

Transcoder::Transcoder(const char *input, const char *output)
{
    inputFile = input;
    outputFile = output;

    int ret = processInput();
    if (ret == 0) {
        qDebug() << "File has been created successfully -> " << output;
        exit(0);
    }
}

/**
 * Open an input file and the required decoder.
 * @param      filename             File to be opened
 * @param[out] inputFormatContext Format context of opened file
 * @param[out] inputCodecContext  Codec context of opened file
 * @return Error code (0 if successful)
 */
int Transcoder::openInputFile(const char *filename,
                           AVFormatContext **inputFormatContext,
                           AVCodecContext **inputCodecContext)
{
    AVCodecContext *codecContext;
    AVCodec *inputCodec;
    int error;

    /* Open the input file to read from it. */
    if ((error = avformat_open_input(inputFormatContext, filename, nullptr,
                                     nullptr)) < 0) {
        fprintf(stderr, "Could not open input file '%s' (error '%d')\n",
                filename, error);
        *inputFormatContext = nullptr;
        return error;
    }

    /* Get information on the input file (number of streams etc.). */
    if ((error = avformat_find_stream_info(*inputFormatContext, nullptr)) < 0) {
        fprintf(stderr, "Could not open find stream info (error '%d')\n",
                error);
        avformat_close_input(inputFormatContext);
        return error;
    }

    /* Make sure that there is only one stream in the input file. */
    if ((*inputFormatContext)->nb_streams != 1) {
        fprintf(stderr, "Expected one audio input stream, but found %d\n",
                (*inputFormatContext)->nb_streams);
        avformat_close_input(inputFormatContext);
        return AVERROR_EXIT;
    }

    /* Find a decoder for the audio stream. */
    if (!(inputCodec = avcodec_find_decoder((*inputFormatContext)->streams[0]->codecpar->codec_id))) {
        fprintf(stderr, "Could not find input codec\n");
        avformat_close_input(inputFormatContext);
        return AVERROR_EXIT;
    }

    /* Allocate a new decoding context. */
    codecContext = avcodec_alloc_context3(inputCodec);
    if (!codecContext) {
        fprintf(stderr, "Could not allocate a decoding context\n");
        avformat_close_input(inputFormatContext);
        return AVERROR(ENOMEM);
    }

    /* Initialize the stream parameters with demuxer information. */
    error = avcodec_parameters_to_context(codecContext, (*inputFormatContext)->streams[0]->codecpar);
    if (error < 0) {
        avformat_close_input(inputFormatContext);
        avcodec_free_context(&codecContext);
        return error;
    }

    /* Open the decoder for the audio stream to use it later. */
    if ((error = avcodec_open2(codecContext, inputCodec, nullptr)) < 0) {
        fprintf(stderr, "Could not open input codec (error '%d')\n",
                error);
        avcodec_free_context(&codecContext);
        avformat_close_input(inputFormatContext);
        return error;
    }

    /* Save the decoder context for easier access later. */
    *inputCodecContext = codecContext;

    return 0;
}

/**
 * Open an output file and the required encoder.
 * Also set some basic encoder parameters.
 * Some of these parameters are based on the input file's parameters.
 * @param      filename              File to be opened
 * @param      inputCodecContext   Codec context of input file
 * @param[out] outputFormatContext Format context of output file
 * @param[out] outputCodecContext  Codec context of output file
 * @return Error code (0 if successful)
 */
int Transcoder::openOutputFile(const char *filename,
                            AVCodecContext *inputCodecContext,
                            AVFormatContext **ouputFormatContext,
                            AVCodecContext **ouputCodecContext)
{
    AVCodecContext *avctx       = nullptr;
    AVIOContext *ouputIOContext = nullptr;
    AVStream *stream            = nullptr;
    AVCodec *outputCodec        = nullptr;
    int error;

    /* Open the output file to write to it. */
    if ((error = avio_open(&ouputIOContext, filename,
                           AVIO_FLAG_WRITE)) < 0) {
        fprintf(stderr, "Could not open output file '%s' (error '%d')\n",
                filename, error);
        return error;
    }

    /* Create a new format context for the output container format. */
    if (!(*ouputFormatContext = avformat_alloc_context())) {
        fprintf(stderr, "Could not allocate output format context\n");
        return AVERROR(ENOMEM);
    }

    /* Associate the output file (pointer) with the container format context. */
    (*ouputFormatContext)->pb = ouputIOContext;

    /* Guess the desired container format based on the file extension. */
    if (!((*ouputFormatContext)->oformat = av_guess_format(nullptr, filename,
                                                              nullptr))) {
        fprintf(stderr, "Could not find output file format\n");
        goto cleanup;
    }

    if (!((*ouputFormatContext)->url = av_strdup(filename))) {
        fprintf(stderr, "Could not allocate url.\n");
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

    /* Find the encoder to be used by its name. */
    if (!(outputCodec = avcodec_find_encoder(AV_CODEC_ID_AAC))) {
        fprintf(stderr, "Could not find an AAC encoder.\n");
        goto cleanup;
    }

    /* Create a new audio stream in the output file container. */
    if (!(stream = avformat_new_stream(*ouputFormatContext, nullptr))) {
        fprintf(stderr, "Could not create new stream\n");
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

    avctx = avcodec_alloc_context3(outputCodec);
    if (!avctx) {
        fprintf(stderr, "Could not allocate an encoding context\n");
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

    /* Set the basic encoder parameters.
     * The input file's sample rate is used to avoid a sample rate conversion. */
    avctx->channels       = OUTPUT_CHANNELS;
    avctx->channel_layout = av_get_default_channel_layout(OUTPUT_CHANNELS);
    avctx->sample_rate    = inputCodecContext->sample_rate;
    avctx->sample_fmt     = outputCodec->sample_fmts[0];
    avctx->bit_rate       = OUTPUT_BIT_RATE;

    /* Allow the use of the experimental AAC encoder. */
    avctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

    /* Set the sample rate for the container. */
    stream->time_base.den = inputCodecContext->sample_rate;
    stream->time_base.num = 1;

    /* Some container formats (like MP4) require global headers to be present.
     * Mark the encoder so that it behaves accordingly. */
    if ((*ouputFormatContext)->oformat->flags & AVFMT_GLOBALHEADER)
        avctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    /* Open the encoder for the audio stream to use it later. */
    if ((error = avcodec_open2(avctx, outputCodec, nullptr)) < 0) {
        fprintf(stderr, "Could not open output codec (error '%d')\n",
                error);
        goto cleanup;
    }

    error = avcodec_parameters_from_context(stream->codecpar, avctx);
    if (error < 0) {
        fprintf(stderr, "Could not initialize stream parameters\n");
        goto cleanup;
    }

    /* Save the encoder context for easier access later. */
    *ouputCodecContext = avctx;

    return 0;

    cleanup:
        avcodec_free_context(&avctx);
        avio_closep(&(*ouputFormatContext)->pb);
        avformat_free_context(*ouputFormatContext);
        *ouputFormatContext = nullptr;
        return error < 0 ? error : AVERROR_EXIT;
}

/**
 * Initialize one data packet for reading or writing.
 * @param[out] packet Packet to be initialized
 * @return Error code (0 if successful)
 */
int Transcoder::initPacket(AVPacket **packet)
{
    if (!(*packet = av_packet_alloc())) {
        fprintf(stderr, "Could not allocate packet\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

/**
 * Initialize one audio frame for reading from the input file.
 * @param[out] frame Frame to be initialized
 * @return Error code (0 if successful)
 */
int Transcoder::initInputFrame(AVFrame **frame)
{
    if (!(*frame = av_frame_alloc())) {
        fprintf(stderr, "Could not allocate input frame\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

/**
 * Initialize the audio resampler based on the input and output codec settings.
 * If the input and output sample formats differ, a conversion is required
 * libswresample takes care of this, but requires initialization.
 * @param      inputCodecContext  Codec context of the input file
 * @param      outputCodecContext Codec context of the output file
 * @param[out] resampleContext    Resample context for the required conversion
 * @return Error code (0 if successful)
 */
int Transcoder::initResampler(AVCodecContext *inputCodecContext,
                          AVCodecContext *outputCodecContext,
                          SwrContext **resampleContext)
{
    int error;

    /*
     * Create a resampler context for the conversion.
     * Set the conversion parameters.
     * Default channel layouts based on the number of channels
     * are assumed for simplicity (they are sometimes not detected
     * properly by the demuxer and/or decoder).
     */
    *resampleContext = swr_alloc_set_opts(nullptr,
                                          av_get_default_channel_layout(outputCodecContext->channels),
                                          outputCodecContext->sample_fmt,
                                          outputCodecContext->sample_rate,
                                          av_get_default_channel_layout(inputCodecContext->channels),
                                          inputCodecContext->sample_fmt,
                                          inputCodecContext->sample_rate,
                                          0, nullptr);
    if (!*resampleContext) {
        fprintf(stderr, "Could not allocate resample context\n");
        return AVERROR(ENOMEM);
    }
    /*
    * Perform a sanity check so that the number of converted samples is
    * not greater than the number of samples to be converted.
    * If the sample rates differ, this case has to be handled differently
    */
    av_assert0(outputCodecContext->sample_rate == inputCodecContext->sample_rate);

    /* Open the resampler with the specified parameters. */
    if ((error = swr_init(*resampleContext)) < 0) {
        fprintf(stderr, "Could not open resample context\n");
        swr_free(resampleContext);
        return error;
    }
    return 0;
}

/**
 * Initialize a FIFO buffer for the audio samples to be encoded.
 * @param[out] fifo                 Sample buffer
 * @param      outputCodecContext   Codec context of the output file
 * @return Error code (0 if successful)
 */
int Transcoder::initFifo(AVAudioFifo **fifo, AVCodecContext *outputCodecContext)
{
    /* Create the FIFO buffer based on the specified output sample format. */
    if (!(*fifo = av_audio_fifo_alloc(outputCodecContext->sample_fmt,
                                      outputCodecContext->channels, 1))) {
        fprintf(stderr, "Could not allocate FIFO\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

/**
 * Write the header of the output file container.
 * @param outputFormatContext Format context of the output file
 * @return Error code (0 if successful)
 */
int Transcoder::writeOutputFileHeader(AVFormatContext *outputFormatContext)
{
    int error;
    if ((error = avformat_write_header(outputFormatContext, nullptr)) < 0) {
        fprintf(stderr, "Could not write output file header (error '%d')\n",
                error);
        return error;
    }
    return 0;
}

/**
 * Decode one audio frame from the input file.
 * @param      frame                Audio frame to be decoded
 * @param      inputFormatContext Format context of the input file
 * @param      inputCodecContext  Codec context of the input file
 * @param[out] dataPresent         Indicates whether data has been decoded
 * @param[out] finished             Indicates whether the end of file has
 *                                  been reached and all data has been
 *                                  decoded. If this flag is false, there
 *                                  is more data to be decoded, i.e., this
 *                                  function has to be called again.
 * @return Error code (0 if successful)
 */
int Transcoder::decodeAudioFrame(AVFrame *frame,
                              AVFormatContext *inputFormatContext,
                              AVCodecContext *inputCodecContext,
                              int *dataPresent, int *finished)
{
    /* Packet used for temporary storage. */
    AVPacket *inputPacket;
    int error;

    error = initPacket(&inputPacket);
    if (error < 0)
        return error;

    /* Read one audio frame from the input file into a temporary packet. */
    if ((error = av_read_frame(inputFormatContext, inputPacket)) < 0) {
        /* If we are at the end of the file, flush the decoder below. */
        if (error == AVERROR_EOF)
            *finished = 1;
        else {
            fprintf(stderr, "Could not read frame (error '%d')\n",
                    error);
            goto cleanup;
        }
    }

    /* Send the audio frame stored in the temporary packet to the decoder.
     * The input audio stream decoder is used to do this. */
    if ((error = avcodec_send_packet(inputCodecContext, inputPacket)) < 0) {
        fprintf(stderr, "Could not send packet for decoding (error '%d')\n",
                error);
        goto cleanup;
    }

    /* Receive one frame from the decoder. */
    error = avcodec_receive_frame(inputCodecContext, frame);
    /* If the decoder asks for more data to be able to decode a frame,
     * return indicating that no data is present. */
    if (error == AVERROR(EAGAIN)) {
        error = 0;
        goto cleanup;
    /* If the end of the input file is reached, stop decoding. */
    } else if (error == AVERROR_EOF) {
        *finished = 1;
        error = 0;
        goto cleanup;
    } else if (error < 0) {
        fprintf(stderr, "Could not decode frame (error '%d')\n",
                error);
        goto cleanup;
    /* Default case: Return decoded data. */
    } else {
        *dataPresent = 1;
        goto cleanup;
    }

cleanup:
    av_packet_free(&inputPacket);
    return error;
}

/**
 * Initialize a temporary storage for the specified number of audio samples.
 * The conversion requires temporary storage due to the different format.
 * The number of audio samples to be allocated is specified in frame_size.
 * @param[out] convertedInputSamples   Array of converted samples. The
 *                                     dimensions are reference, channel
 *                                     (for multi-channel audio), sample.
 * @param      outputCodecContext      Codec context of the output file
 * @param      frameSize               Number of samples to be converted in
 *                                     each round
 * @return Error code (0 if successful)
 */
int Transcoder::initConvertedSamples(uint8_t ***convertedInputSamples,
                                  AVCodecContext *outputCodecContext,
                                  int frameSize)
{
    int error;

    /* Allocate as many pointers as there are audio channels.
     * Each pointer will later point to the audio samples of the corresponding
     * channels (although it may be nullptr for interleaved formats).
     */
    if (!(*convertedInputSamples = (uint8_t **) calloc(outputCodecContext->channels,
                                            sizeof(**convertedInputSamples)))) {
        fprintf(stderr, "Could not allocate converted input sample pointers\n");
        return AVERROR(ENOMEM);
    }

    /* Allocate memory for the samples of all channels in one consecutive
     * block for convenience. */
    if ((error = av_samples_alloc(*convertedInputSamples, nullptr,
                                  outputCodecContext->channels,
                                  frameSize,
                                  outputCodecContext->sample_fmt, 0)) < 0) {
        fprintf(stderr,
                "Could not allocate converted input samples (error '%d')\n",
                error);
        av_freep(&(*convertedInputSamples)[0]);
        free(*convertedInputSamples);
        return error;
    }
    return 0;
}

/**
 * Convert the input audio samples into the output sample format.
 * The conversion happens on a per-frame basis, the size of which is
 * specified by frame_size.
 * @param      inputData        Samples to be decoded. The dimensions are
 *                              channel (for multi-channel audio), sample.
 * @param[out] convertedData    Converted samples. The dimensions are channel
 *                              (for multi-channel audio), sample.
 * @param      frameSize        Number of samples to be converted
 * @param      resampleContext  Resample context for the conversion
 * @return Error code (0 if successful)
 */
int Transcoder::convertSamples(const uint8_t **inputData,
                           uint8_t **convertedData, const int frameSize,
                           SwrContext *resampleContext)
{
    int error;

    /* Convert the samples using the resampler. */
    if ((error = swr_convert(resampleContext,
                             convertedData, frameSize,
                             inputData    , frameSize)) < 0) {
        fprintf(stderr, "Could not convert input samples (error '%d')\n",
                error);
        return error;
    }

    return 0;
}

/**
 * Add converted input audio samples to the FIFO buffer for later processing.
 * @param fifo                    Buffer to add the samples to
 * @param convertedInputSamples   Samples to be added. The dimensions are channel
 *                                (for multi-channel audio), sample.
 * @param frameSize               Number of samples to be converted
 * @return Error code (0 if successful)
 */
int Transcoder::addSamplesToFifo(AVAudioFifo *fifo,
                               uint8_t **convertedInputSamples,
                               const int frameSize)
{
    int error;

    /* Make the FIFO as large as it needs to be to hold both,
     * the old and the new samples. */
    if ((error = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + frameSize)) < 0) {
        fprintf(stderr, "Could not reallocate FIFO\n");
        return error;
    }

    /* Store the new samples in the FIFO buffer. */
    if (av_audio_fifo_write(fifo, (void **)convertedInputSamples,
                            frameSize) < frameSize) {
        fprintf(stderr, "Could not write data to FIFO\n");
        return AVERROR_EXIT;
    }
    return 0;
}

/**
 * Read one audio frame from the input file, decode, convert and store
 * it in the FIFO buffer.
 * @param      fifo                 Buffer used for temporary storage
 * @param      input_format_context Format context of the input file
 * @param      input_codec_context  Codec context of the input file
 * @param      output_codec_context Codec context of the output file
 * @param      resampler_context    Resample context for the conversion
 * @param[out] finished             Indicates whether the end of file has
 *                                  been reached and all data has been
 *                                  decoded. If this flag is false,
 *                                  there is more data to be decoded,
 *                                  i.e., this function has to be called
 *                                  again.
 * @return Error code (0 if successful)
 */
int Transcoder::readDecodeConvertAndStore(AVAudioFifo *fifo,
                                         AVFormatContext *inputFormatContext,
                                         AVCodecContext *inputCodecContext,
                                         AVCodecContext *outputCodecContext,
                                         SwrContext *resamplerContext,
                                         int *finished)
{
    /* Temporary storage of the input samples of the frame read from the file. */
    AVFrame *inputFrame = nullptr;
    /* Temporary storage for the converted input samples. */
    uint8_t **convertedInputSamples = nullptr;
    int dataPresent = 0;
    int ret = AVERROR_EXIT;

    /* Initialize temporary storage for one input frame. */
    if (initInputFrame(&inputFrame))
        goto cleanup;
    /* Decode one frame worth of audio samples. */
    if (decodeAudioFrame(inputFrame, inputFormatContext,
                           inputCodecContext, &dataPresent, finished))
        goto cleanup;
    /* If we are at the end of the file and there are no more samples
     * in the decoder which are delayed, we are actually finished.
     * This must not be treated as an error. */
    if (*finished) {
        ret = 0;
        goto cleanup;
    }
    /* If there is decoded data, convert and store it. */
    if (dataPresent) {
        /* Initialize the temporary storage for the converted input samples. */
        if (initConvertedSamples(&convertedInputSamples, outputCodecContext,
                                   inputFrame->nb_samples))
            goto cleanup;

        /* Convert the input samples to the desired output sample format.
         * This requires a temporary storage provided by converted_input_samples. */
        if (convertSamples((const uint8_t**)inputFrame->extended_data, convertedInputSamples,
                            inputFrame->nb_samples, resamplerContext))
            goto cleanup;

        /* Add the converted input samples to the FIFO buffer for later processing. */
        if (addSamplesToFifo(fifo, convertedInputSamples,
                                inputFrame->nb_samples))
            goto cleanup;
    }
    ret = 0;

    cleanup:
        if (convertedInputSamples) {
            // av_freep(&convertedInputSamples[0]);
            free(convertedInputSamples);
        }
        av_frame_free(&inputFrame);

    return ret;
}

/**
 * Initialize one input frame for writing to the output file.
 * The frame will be exactly frame_size samples large.
 * @param[out] frame                Frame to be initialized
 * @param      output_codec_context Codec context of the output file
 * @param      frame_size           Size of the frame
 * @return Error code (0 if successful)
 */
int Transcoder::initOutputFrame(AVFrame **frame,
                             AVCodecContext *outputCodecContext,
                             int frameSize)
{
    int error;

    /* Create a new frame to store the audio samples. */
    if (!(*frame = av_frame_alloc())) {
        fprintf(stderr, "Could not allocate output frame\n");
        return AVERROR_EXIT;
    }

    /* Set the frame's parameters, especially its size and format.
     * av_frame_get_buffer needs this to allocate memory for the
     * audio samples of the frame.
     * Default channel layouts based on the number of channels
     * are assumed for simplicity. */
    (*frame)->nb_samples     = frameSize;
    (*frame)->channel_layout = outputCodecContext->channel_layout;
    (*frame)->format         = outputCodecContext->sample_fmt;
    (*frame)->sample_rate    = outputCodecContext->sample_rate;

    /* Allocate the samples of the created frame. This call will make
     * sure that the audio frame can hold as many samples as specified. */
    if ((error = av_frame_get_buffer(*frame, 0)) < 0) {
        fprintf(stderr, "Could not allocate output frame samples (error '%d')\n",
                error);
        av_frame_free(frame);
        return error;
    }

    return 0;
}

/**
 * Encode one frame worth of audio to the output file.
 * @param      frame                 Samples to be encoded
 * @param      output_format_context Format context of the output file
 * @param      output_codec_context  Codec context of the output file
 * @param[out] data_present          Indicates whether data has been
 *                                   encoded
 * @return Error code (0 if successful)
 */
int Transcoder::encodeAudioFrame(AVFrame *frame,
                              AVFormatContext *outputFormatContext,
                              AVCodecContext *outputCodecContext,
                              int *dataPresent)
{
    /* Packet used for temporary storage. */
    AVPacket *outputPacket;
    int error;

    error = initPacket(&outputPacket);
    if (error < 0)
        return error;

    /* Set a timestamp based on the sample rate for the container. */
    if (frame) {
        frame->pts = pts;
        pts += frame->nb_samples;
    }

    /* Send the audio frame stored in the temporary packet to the encoder.
     * The output audio stream encoder is used to do this. */
    error = avcodec_send_frame(outputCodecContext, frame);
    /* The encoder signals that it has nothing more to encode. */
    if (error == AVERROR_EOF) {
        error = 0;
        goto cleanup;
    } else if (error < 0) {
        fprintf(stderr, "Could not send packet for encoding (error '%d')\n",
                error);
        goto cleanup;
    }

    /* Receive one encoded frame from the encoder. */
    error = avcodec_receive_packet(outputCodecContext, outputPacket);
    /* If the encoder asks for more data to be able to provide an
     * encoded frame, return indicating that no data is present. */
    if (error == AVERROR(EAGAIN)) {
        error = 0;
        goto cleanup;
    /* If the last frame has been encoded, stop encoding. */
    } else if (error == AVERROR_EOF) {
        error = 0;
        goto cleanup;
    } else if (error < 0) {
        fprintf(stderr, "Could not encode frame (error '%d')\n",
                error);
        goto cleanup;
    /* Default case: Return encoded data. */
    } else {
        *dataPresent = 1;
    }

    /* Write one audio frame from the temporary packet to the output file. */
    if (*dataPresent &&
        (error = av_write_frame(outputFormatContext, outputPacket)) < 0) {
        fprintf(stderr, "Could not write frame (error '%d')\n",
                error);
        goto cleanup;
    }

cleanup:
    av_packet_free(&outputPacket);
    return error;
}

/**
 * Load one audio frame from the FIFO buffer, encode and write it to the
 * output file.
 * @param fifo                  Buffer used for temporary storage
 * @param output_format_context Format context of the output file
 * @param output_codec_context  Codec context of the output file
 * @return Error code (0 if successful)
 */
int Transcoder::loadEncodeAndWrite(AVAudioFifo *fifo,
                                 AVFormatContext *outputFormatContext,
                                 AVCodecContext *outputCodecContext)
{
    /* Temporary storage of the output samples of the frame written to the file. */
    AVFrame *outputFrame;
    /* Use the maximum number of possible samples per frame.
     * If there is less than the maximum possible frame size in the FIFO
     * buffer use this number. Otherwise, use the maximum possible frame size. */
    const int frame_size = FFMIN(av_audio_fifo_size(fifo),
                                 outputCodecContext->frame_size);
    int dataWritten;

    /* Initialize temporary storage for one output frame. */
    if (initOutputFrame(&outputFrame, outputCodecContext, frame_size))
        return AVERROR_EXIT;

    /* Read as many samples from the FIFO buffer as required to fill the frame.
     * The samples are stored in the frame temporarily. */
    if (av_audio_fifo_read(fifo, (void **)outputFrame->data, frame_size) < frame_size) {
        fprintf(stderr, "Could not read data from FIFO\n");
        av_frame_free(&outputFrame);
        return AVERROR_EXIT;
    }

    /* Encode one frame worth of audio samples. */
    if (encodeAudioFrame(outputFrame, outputFormatContext,
                           outputCodecContext, &dataWritten)) {
        av_frame_free(&outputFrame);
        return AVERROR_EXIT;
    }
    av_frame_free(&outputFrame);
    return 0;
}

/**
 * Write the trailer of the output file container.
 * @param output_format_context Format context of the output file
 * @return Error code (0 if successful)
 */
int Transcoder::writeOutputFileTrailer(AVFormatContext *outputFormatContext)
{
    int error;
    if ((error = av_write_trailer(outputFormatContext)) < 0) {
        fprintf(stderr, "Could not write output file trailer (error '%d')\n",
                error);
        return error;
    }
    return 0;
}

int Transcoder::processInput()
{
    AVFormatContext *inputFormatContext = nullptr;
    AVFormatContext *outputFormatContext = nullptr;
    AVCodecContext *inputCodecContext = nullptr;
    AVCodecContext *outputCodecContext = nullptr;
    SwrContext *resampleContext = nullptr;
    AVAudioFifo *fifo = nullptr;
    int ret = AVERROR_EXIT;

    /* Open the input file for reading. */
    if (openInputFile(inputFile, &inputFormatContext,
                        &inputCodecContext))
        goto cleanup;
    /* Open the output file for writing. */
    if (openOutputFile(outputFile, inputCodecContext,
                         &outputFormatContext, &outputCodecContext))
        goto cleanup;
    /* Initialize the resampler to be able to convert audio sample formats. */
    if (initResampler(inputCodecContext, outputCodecContext,
                       &resampleContext))
        goto cleanup;
    /* Initialize the FIFO buffer to store audio samples to be encoded. */
    if (initFifo(&fifo, outputCodecContext))
        goto cleanup;
    /* Write the header of the output file container. */
    if (writeOutputFileHeader(outputFormatContext))
        goto cleanup;

    /* Loop as long as we have input samples to read or output samples
     * to write; abort as soon as we have neither. */
    while (1) {
        /* Use the encoder's desired frame size for processing. */
        const int output_frame_size = outputCodecContext->frame_size;
        int finished                = 0;

        /* Make sure that there is one frame worth of samples in the FIFO
         * buffer so that the encoder can do its work.
         * Since the decoder's and the encoder's frame size may differ, we
         * need to FIFO buffer to store as many frames worth of input samples
         * that they make up at least one frame worth of output samples. */
        while (av_audio_fifo_size(fifo) < output_frame_size) {
            /* Decode one frame worth of audio samples, convert it to the
             * output sample format and put it into the FIFO buffer. */
            if (readDecodeConvertAndStore(fifo, inputFormatContext,
                                              inputCodecContext,
                                              outputCodecContext,
                                              resampleContext, &finished))
                goto cleanup;

            /* If we are at the end of the input file, we continue
             * encoding the remaining audio samples to the output file. */
            if (finished)
                break;
        }

        /* If we have enough samples for the encoder, we encode them.
         * At the end of the file, we pass the remaining samples to
         * the encoder. */
        while (av_audio_fifo_size(fifo) >= output_frame_size ||
               (finished && av_audio_fifo_size(fifo) > 0))
            /* Take one frame worth of audio samples from the FIFO buffer,
             * encode it and write it to the output file. */
            if (loadEncodeAndWrite(fifo, outputFormatContext,
                                      outputCodecContext))
                goto cleanup;

        /* If we are at the end of the input file and have encoded
         * all remaining samples, we can exit this loop and finish. */
        if (finished) {
            int data_written;
            /* Flush the encoder as it may have delayed frames. */
            do {
                data_written = 0;
                if (encodeAudioFrame(nullptr, outputFormatContext,
                                       outputCodecContext, &data_written))
                    goto cleanup;
            } while (data_written);
            break;
        }
    }

    /* Write the trailer of the output file container. */
    if (writeOutputFileTrailer(outputFormatContext))
        goto cleanup;
    ret = 0;

cleanup:
    if (fifo)
        av_audio_fifo_free(fifo);
    swr_free(&resampleContext);
    if (outputCodecContext)
        avcodec_free_context(&outputCodecContext);
    if (outputFormatContext) {
        avio_closep(&outputFormatContext->pb);
        avformat_free_context(outputFormatContext);
    }
    if (inputCodecContext)
        avcodec_free_context(&inputCodecContext);
    if (inputFormatContext)
        avformat_close_input(&inputFormatContext);

    return ret;
}
