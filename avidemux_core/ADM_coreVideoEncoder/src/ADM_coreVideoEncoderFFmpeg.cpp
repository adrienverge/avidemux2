/***************************************************************************
                          \fn ADM_coreVideoEncoder
                          \brief Base class for video encoder plugin
                             -------------------

    copyright            : (C) 2002/2009 by mean
    email                : fixounet@free.fr
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include "ADM_default.h"
#include "ADM_coreVideoEncoderFFmpeg.h"
#include "prefs.h"
#include "ADM_coreUtils.h"

extern "C"
{
char *av_strdup(const char *s);
void *av_malloc(size_t size) ;
}
//#define TIME_TENTH_MILLISEC
#if 1
    #define aprintf(...) {}
#else
    #define aprintf printf
#endif

#define LAVS(x) Settings.lavcSettings.x

/**
    \fn ADM_coreVideoEncoderFFmpeg
    \brief Constructor

*/

ADM_coreVideoEncoderFFmpeg::ADM_coreVideoEncoderFFmpeg(ADM_coreVideoFilter *src,FFcodecSettings *set,bool globalHeader)
                    : ADM_coreVideoEncoder(src)
{
uint32_t w,h;
_hasSettings=false;

    if(set) {
        memcpy(&Settings,set,sizeof(*set));
        _hasSettings=true;
    }

    targetColorSpace=ADM_COLOR_YV12;
    w=getWidth();
    h=getHeight();

    image=new ADMImageDefault(w,h);
    _frame=av_frame_alloc();
    _frame->pts = AV_NOPTS_VALUE;
    _frame->width=w;
    _frame->height=h;
    rgbByteBuffer.setSize((w+7)*(h+7)*4);
    colorSpace=NULL;
    pass=0;
    statFileName=NULL;
    statFile=NULL;
    _globalHeader=globalHeader;
    _isMT=false;

    uint64_t inc=source->getInfo()->frameIncrement;
    if(inc<30000) // Less than 30 ms , fps > 30 fps it is probably field
     {
            inc*=2;
            ADM_warning("It is probably field encoded, doubling increment\n");
     }
     if(_hasSettings && LAVS(max_b_frames))
            encoderDelay=inc*2;
     else
            encoderDelay=0;
    ADM_info("[Lavcodec] Using a video encoder delay of %d ms\n",(int)(encoderDelay/1000));

}
/**
    \fn ADM_coreVideoEncoderFFmpeg
    \brief Destructor
*/
ADM_coreVideoEncoderFFmpeg::~ADM_coreVideoEncoderFFmpeg()
{
    if (_context)
    {
        if (_isMT )
        {
          printf ("[lavc] killing threads\n");
          _isMT = false;
        }

        avcodec_close(_context);

        av_free (_context);
        _context = NULL;
    }
    
    if (_frame)
    {
        av_frame_free(&_frame);
        _frame=NULL;
    }
    
    if(colorSpace)
    {
        delete colorSpace;
        colorSpace=NULL;
    }
    if(statFile)
    {
        printf("[ffMpeg4Encoder] Closing stat file\n");
        fclose(statFile);
        statFile=NULL;
    }
    if(statFileName) ADM_dealloc(statFileName);
    statFileName=NULL;
}
/**
    \fn prolog
*/

bool             ADM_coreVideoEncoderFFmpeg::prolog(ADMImage *img)
{
    int w=getWidth();

  switch(targetColorSpace)
    {
        case ADM_COLOR_YV12:    _frame->linesize[0] = img->GetPitch(PLANAR_Y);
                                _frame->linesize[1] = img->GetPitch(PLANAR_U);
                                _frame->linesize[2] = img->GetPitch(PLANAR_V);
                                _frame->format=AV_PIX_FMT_YUV420P;
                                _context->pix_fmt =AV_PIX_FMT_YUV420P;break;
        case ADM_COLOR_YUV422P: _frame->linesize[0] = w;
                                _frame->linesize[1] = w>>1;
                                _frame->linesize[2] = w>>1;
                                _frame->format=AV_PIX_FMT_YUV422P;
                                _context->pix_fmt =AV_PIX_FMT_YUV422P;break;
        case ADM_COLOR_RGB32A : _frame->linesize[0] = w*4;
                                _frame->linesize[1] = 0;//w >> 1;
                                _frame->linesize[2] = 0;//w >> 1;
                                _frame->format=AV_PIX_FMT_RGB32;
                                _context->pix_fmt =AV_PIX_FMT_RGB32;break;
        default: ADM_assert(0);

    }

    // Eval fps
    uint64_t f=source->getInfo()->frameIncrement;
    // Let's put 100 us as time  base

#ifdef TIME_TENTH_MILLISEC
    _context->time_base.num=1;
    _context->time_base.den=10000LL;
#else

    int n,d;

    usSecondsToFrac(f,&n,&d);
 //   printf("[ff] Converted a time increment of %d ms to %d /%d seconds\n",f/1000,n,d);
    _context->time_base.num=n;
    _context->time_base.den=d;
#endif
    timeScaler=1000000.*av_q2d(_context->time_base); // Optimize, can be computed once
    return true;
}
/**
    \fn ADM_coreVideoEncoderFFmpeg
*/
int64_t          ADM_coreVideoEncoderFFmpeg::timingToLav(uint64_t val)
{
    double q=(double)val;
    q+=timeScaler/(double)2.;
    q/=timeScaler;
    
    int64_t v=floor(q);
#if 0      
  printf("Lav in=%d, scale=%lf,",(int)val,timeScaler);
  printf(" q=%lf,out PTS=%lld\n",q,v);
#endif
  return v;
}
/**
    \fn lavToTiming
*/
uint64_t         ADM_coreVideoEncoderFFmpeg::lavToTiming(int64_t val)
{
    float v=(float)val;
    return floor(v*timeScaler);
}

/**
    \fn pre-encoder

*/
bool             ADM_coreVideoEncoderFFmpeg::preEncode(void)
{
    uint32_t nb;
    if(source->getNextFrame(&nb,image)==false)
    {
        printf("[ff] Cannot get next image\n");
        return false;
    }
    prolog(image);

    uint64_t p=image->Pts;
    queueOfDts.push_back(p);
    aprintf("Incoming frame PTS=%" PRIu64", delay=%" PRIu64"\n",p,getEncoderDelay());
    p+=getEncoderDelay();
    _frame->pts= timingToLav(p);    //
    if(!_frame->pts) _frame->pts=AV_NOPTS_VALUE;

    ADM_timeMapping map; // Store real PTS <->lav value mapping
    map.realTS=p;
    map.internalTS=_frame->pts;
    mapper.push_back(map);

    aprintf("Codec> incoming pts=%" PRIu64"\n",image->Pts);
    //printf("--->>[PTS] :%"PRIu64", raw %"PRIu64" num:%"PRIu32" den:%"PRIu32"\n",_frame->pts,image->Pts,_context->time_base.num,_context->time_base.den);
    //
    switch(targetColorSpace)
    {
        case ADM_COLOR_YV12:
                _frame->data[0] = image->GetWritePtr(PLANAR_Y);
                _frame->data[2] = image->GetWritePtr(PLANAR_U);
                _frame->data[1] = image->GetWritePtr(PLANAR_V);
                break;

        case ADM_COLOR_YUV422P:
        {
              int w=getWidth();
              int h=getHeight();

                if(!colorSpace->convertImage(image,rgbByteBuffer.at(0)))
                {
                    printf("[ADM_jpegEncoder::encode] Colorconversion failed\n");
                    return false;
                }
                _frame->data[0] = rgbByteBuffer.at(0);
                _frame->data[2] = rgbByteBuffer.at(0)+(w*h);
                _frame->data[1] = rgbByteBuffer.at(0)+(w*h*3)/2;
                break;
        }
        case ADM_COLOR_RGB32A:
                if(!colorSpace->convertImage(image,rgbByteBuffer.at(0)))
                {
                    printf("[ADM_jpegEncoder::encode] Colorconversion failed\n");
                    return false;
                }
                _frame->data[0] = rgbByteBuffer.at(0);
                _frame->data[2] = NULL;
                _frame->data[1] = NULL;
                break;
        default:
                ADM_assert(0);
    }
    return true;
}

/**
    \fn configure-context
    \brief To be overriden in classes which need to preform operations to the context prior to opening the codec.
*/
bool             ADM_coreVideoEncoderFFmpeg::configureContext(void)
{
    return true;
}

/**
 * \fn encodeWrapper
 */
int              ADM_coreVideoEncoderFFmpeg::encodeWrapper(AVFrame *in,ADMBitstream *out)
{
    int r,gotData;
        AVPacket pkt;
        av_init_packet(&pkt);
        pkt.data=out->data;
        pkt.size=out->bufferSize;


        r= avcodec_encode_video2 (_context,&pkt,in, &gotData);
        if(r<0)
        {
            ADM_warning("Error %d encoding video\n",r);
            return r;
        }
        if(!gotData)
        {
            ADM_warning("Encoder produced no data\n");
            pkt.size=0;
        }
        return pkt.size;            
}
/**
 * 
 * @param codecId
 * @return 
 */
bool ADM_coreVideoEncoderFFmpeg::setupInternal(AVCodec *codec)
{
    int res;
    _context = avcodec_alloc_context3 (codec);
    ADM_assert (_context);
    _context->width = getWidth();
    _context->height = getHeight();
    _context->strict_std_compliance = -1;

    if(_globalHeader)
    {
                ADM_info("Codec configured to use global header\n");
                _context->flags|=CODEC_FLAG_GLOBAL_HEADER;
    }
   prolog(image);
   printf("[ff] Time base %d/%d\n", _context->time_base.num,_context->time_base.den);
   if(_hasSettings && LAVS(MultiThreaded))
    {
        encoderMT();
    }

   if(!configureContext()) {
     return false;
   }

   res=avcodec_open2(_context, codec, NULL);
   if(res<0)
    {   printf("[ff] Cannot open codec\n");
        return false;
    }

    // Now allocate colorspace
    int w,h;
    FilterInfo *info=source->getInfo();
    w=info->width;
    h=info->height;
    if(targetColorSpace!=ADM_COLOR_YV12)
    {
        colorSpace=new ADMColorScalerSimple(w,h,ADM_COLOR_YV12,targetColorSpace);
        if(!colorSpace)
        {
            printf("[ADM_jpegEncoder] Cannot allocate colorspace\n");
            return false;
        }
    }
    return true;
}
/**
    \fn setup
    \brief put flags before calling setup!
*/
bool ADM_coreVideoEncoderFFmpeg::setup(AVCodecID codecId)
{
   
    AVCodec *codec=avcodec_find_encoder(codecId);
    if(!codec)
    {
        printf("[ff] Cannot find codec\n");
        return false;
    }
    return setupInternal(codec);
}

/**
    \fn setup
    \brief put flags before calling setup!
*/
bool ADM_coreVideoEncoderFFmpeg::setupByName(const char *name)
{
    AVCodec *codec=avcodec_find_encoder_by_name(name);
    if(!codec)
    {
        ADM_warning("[ff] Cannot find codec with name %s\n",name);
        return false;
    }
    return setupInternal(codec);
}
/**
    \fn getExtraData
    \brief

*/
bool             ADM_coreVideoEncoderFFmpeg::getExtraData(uint32_t *l,uint8_t **d)
{
    *l=_context->extradata_size;
    *d=_context->extradata;
    return true;

}

/**
    \fn loadStatFile
    \brief load the stat file from pass 1
*/
bool ADM_coreVideoEncoderFFmpeg::loadStatFile(const char *file)
{
  printf("[FFmpeg] Loading stat file :%s\n",file);
  FILE *_statfile = ADM_fopen (file, "rb");
  int statSize;

  if (!_statfile)
    {
      printf ("[ffmpeg] internal file does not exists ?\n");
      return false;
    }

  fseek (_statfile, 0, SEEK_END);
  statSize = ftello (_statfile);
  fseek (_statfile, 0, SEEK_SET);
  _context->stats_in = (char *) av_malloc(statSize+1);
  _context->stats_in[statSize] = 0;
  fread (_context->stats_in, statSize, 1, _statfile);
  fclose(_statfile);


    int i;
    char *p=_context->stats_in;
   for(i=-1; p; i++){
            p= strchr(p+1, ';');
        }
  printf("[FFmpeg] stat file loaded ok, %d frames found\n",i);
  return true;
}
/**
        \fn postEncode
        \brief update bitstream info from output of lavcodec
*/
bool ADM_coreVideoEncoderFFmpeg::postEncode(ADMBitstream *out, uint32_t size)
{
    int pict_type=AV_PICTURE_TYPE_P;
    int keyframe=false;
    if(_context->coded_frame)
    {
        pict_type=_context->coded_frame->pict_type;
        keyframe=_context->coded_frame->key_frame;
    }else
    {
        out->len=0;
        ADM_warning("No picture...\n");
        return false;
    }
    aprintf("[ffMpeg4] Out Quant :%d, pic type %d keyf %d\n",out->out_quantizer,pict_type,keyframe);
    out->len=size;
    out->flags=0;
    if(keyframe)
        out->flags=AVI_KEY_FRAME;
    else if(pict_type==AV_PICTURE_TYPE_B)
        out->flags=AVI_B_FRAME;

    // Update PTS/Dts
    if(!_context->max_b_frames)
    {
            out->dts=out->pts=queueOfDts[0];
            mapper.erase(mapper.begin());
            queueOfDts.erase(queueOfDts.begin());
    } else
    if(!getRealPtsFromInternal(_context->coded_frame->pts,&(out->dts),&(out->pts)))
        return false;
    // update lastDts
    lastDts=out->dts;

    aprintf("Codec>Out pts=%" PRIu64" us, out Dts=%" PRIu64"\n",out->pts,out->dts);

    // Update quant
    if(!_context->coded_frame->quality)
      out->out_quantizer=(int) floor (_frame->quality / (float) FF_QP2LAMBDA);
    else
      out->out_quantizer =(int) floor (_context->coded_frame->quality / (float) FF_QP2LAMBDA);



    // Update stats
    if(Settings.params.mode==COMPRESS_2PASS   || Settings.params.mode==COMPRESS_2PASS_BITRATE)
    {
        if(pass==1)
            if (_context->stats_out)
                fprintf (statFile, "%s", _context->stats_out);
    }
    return true;
}

/**
    \fn presetContext
    \brief put sensible values into context
*/
bool ADM_coreVideoEncoderFFmpeg::presetContext(FFcodecSettings *set)
{
	  //_context->gop_size = 250;

#define SETX(x) _context->x=set->lavcSettings.x; printf("[LAVCODEC]"#x" : %d\n",set->lavcSettings.x);

      SETX (me_method);
      SETX (qmin);
      SETX (qmax);
      SETX (max_b_frames);
      SETX (mpeg_quant);
      SETX (max_qdiff);
      SETX (gop_size);

#undef SETX

#define SETX(x)  _context->x=set->lavcSettings.x; printf("[LAVCODEC]"#x" : %f\n",set->lavcSettings.x);
#define SETX_COND(x)  if(set->lavcSettings.is_##x) {_context->x=set->lavcSettings.x; printf("[LAVCODEC]"#x" : %f\n",set->lavcSettings.x);} else  \
									{printf("[LAVCODEC]"#x" No activated\n");}
      SETX_COND (lumi_masking);
      SETX_COND (dark_masking);
      SETX (qcompress);
      SETX (qblur);
      SETX_COND (temporal_cplx_masking);
      SETX_COND (spatial_cplx_masking);

#undef SETX
#undef SETX_COND

#define SETX(x) if(set->lavcSettings.x){ _context->flags|=CODEC_FLAG##x;printf("[LAVCODEC]"#x" is set\n");}
      SETX (_GMC);


    switch (set->lavcSettings.mb_eval)
	{
        case 0:
          _context->mb_decision = FF_MB_DECISION_SIMPLE;
          break;
        case 1:
          _context->mb_decision = FF_MB_DECISION_BITS;
          break;
        case 2:
          _context->mb_decision = FF_MB_DECISION_RD;
          break;
        default:
          ADM_assert (0);
	}

      SETX (_4MV);
      SETX (_QPEL);
      if(set->lavcSettings._TRELLIS_QUANT) _context->trellis=1;
      //SETX(_HQ);
      //SETX (_NORMALIZE_AQP);

      if (set->lavcSettings.widescreen)
        {
          _context->sample_aspect_ratio.num = 16;
          _context->sample_aspect_ratio.den = 9;
          printf ("[LAVCODEC]16/9 aspect ratio is set.\n");

        }
#undef SETX
  _context->bit_rate_tolerance = 8000000;
  _context->b_quant_factor = 1.25;
  _context->rc_strategy = 2;
  _context->b_frame_strategy = 0;
  _context->b_quant_offset = 1.25;
  _context->rtp_payload_size = 0;
  _context->strict_std_compliance = 0;
  _context->i_quant_factor = 0.8;
  _context->i_quant_offset = 0.0;
//  _context->rc_qsquish = 1.0;
//  _context->rc_qmod_amp = 0;
//  _context->rc_qmod_freq = 0;
//  _context->rc_eq = av_strdup("tex^qComp");
  _context->rc_max_rate = 000;
  _context->rc_min_rate = 000;
  _context->rc_buffer_size = 000;
//  _context->rc_buffer_aggressivity = 1.0;
//  _context->rc_initial_cplx = 0;
  _context->dct_algo = 0;
  _context->idct_algo = 0;
  _context->p_masking = 0.0;

  // Set frame rate den/num
  prolog(image);
  return true;
}

/**
    \fn setLogFile
*/
 bool         ADM_coreVideoEncoderFFmpeg::setPassAndLogFile(int pass,const char *name)
{
    if(!pass || pass >2) return false;
    if(!name) return false;
    this->pass=pass;
    statFileName=ADM_strdup(name);
    return true;
}
/**
    \fn setupPass
    \brief Setup in case of multipass

*/
bool ADM_coreVideoEncoderFFmpeg::setupPass(void)
{
    int averageBitrate; // Fixme

    // Compute average bitrate

        if(Settings.params.mode==COMPRESS_2PASS_BITRATE) averageBitrate=Settings.params.avg_bitrate*1000;
            else
            {
                uint64_t duration=source->getInfo()->totalDuration; // in us
                uint32_t avg;
                if(false==ADM_computeAverageBitrateFromDuration(duration, Settings.params.finalsize,
                                &avg))
                {
                    printf("[ffMpeg4] No source duration!\n");
                    return false;
                }
                averageBitrate=(uint32_t)avg*1000; // convert from kb/s to b/s
            }

        printf("[ffmpeg4] Average bitrate =%" PRIu32" kb/s\n",averageBitrate/1000);
        _context->bit_rate=averageBitrate;
        switch(pass)
        {
                case 1:
                    printf("[ffMpeg4] Setup-ing Pass 1\n");
                    _context->flags |= CODEC_FLAG_PASS1;
                    // Open stat file
                    statFile=ADM_fopen(statFileName,"wt");
                    if(!statFile)
                    {
                        printf("[ffmpeg] Cannot open statfile %s for writing\n",statFileName);
                        return false;
                    }
                    break;
                case 2:
                    printf("[ffMpeg4] Setup-ing Pass 2\n");
                    _context->flags |= CODEC_FLAG_PASS2;
                    if(false==loadStatFile(statFileName))
                    {
                        printf("[ffmpeg4] Cannot load stat file\n");
                        return false;
                    }
                    break;
                default:
                        printf("[ffmpeg] Pass=0, fail\n");
                        return false;
                    break;

        }
        return true;
}
/**
    \fn encoderMT
    \brief handle multithreaded encoding
*/
bool ADM_coreVideoEncoderFFmpeg::encoderMT (void)
{

  uint32_t threads =    LAVS(MultiThreaded);
  switch(threads)
  {
    case 99:threads = ADM_cpu_num_processors();break;
    case 1: threads=0;
    break;
  }
  if (threads)
  {
      printf ("[lavc] Enabling MT encoder with %u threads\n", threads);
      _context->thread_count=threads;
      _isMT = 1;
  }
  return true;
}


// EOF
