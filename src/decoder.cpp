#include "decoder.h"
#include <stdexcept>
#include <mutex>
#include <thread>

#include "ffmpeg_afx.h"
#include "common.h"

#include "encoder.h"

namespace mstream
{

class decoder
{
    AVFormatContext *m_fmt;
    AVCodecContext* m_dec_ctx;
    int m_stream_index;
    unsigned m_frame_num = 0;
    const i_frame_consumer_ptr m_consumer;
    SwsContext * m_sws = nullptr;
    AVFramePtr m_frame;
    const stream_position m_pos;
    AVRational m_tb;
public:
    decoder(i_frame_consumer_ptr consumer, stream_position pos) 
        : m_fmt(nullptr)
        , m_dec_ctx(nullptr)
        , m_stream_index(-1)
        , m_consumer(MANDATORY_PTR(consumer))
        , m_pos(pos)
    {
    }
    
    ~decoder()
    {
        if (m_fmt)
            avformat_close_input(&m_fmt);
        if (m_dec_ctx)
            avcodec_free_context(&m_dec_ctx);
        if (m_sws)
            sws_freeContext(m_sws);
        m_consumer->reset_queue(m_pos);
    }
    
    void init(std::string filename)
    {
        int ret;
        if ((ret = avformat_open_input(&m_fmt, filename.c_str(), NULL, NULL)) < 0)
            THROW_ERR("Cannot open input file " << filename);
        
        if ((ret = avformat_find_stream_info(m_fmt, NULL)) < 0)
            THROW_ERR("Cannot find stream information");
        
        AVCodec *dec;
        m_stream_index = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
        if (m_stream_index < 0)
            THROW_ERR("Cannot find a video stream in the input file");

        m_dec_ctx = avcodec_alloc_context3(dec);
        if (!m_dec_ctx)
            THROW_ERR("Out of memory");
        
        AVStream *stream = m_fmt->streams[m_stream_index];
        avcodec_parameters_to_context(m_dec_ctx, stream->codecpar);
        
        m_dec_ctx->framerate = av_guess_frame_rate(m_fmt, stream, NULL);
        
        if ((ret = avcodec_open2(m_dec_ctx, dec, NULL)) < 0)
            THROW_ERR("Cannot open video decoder");
        
        m_frame = make_frame_ptr(av_frame_alloc());
        
        m_sws = sws_alloc_context();

        av_opt_set_int(m_sws, "srcw", m_dec_ctx->width, 0);
        av_opt_set_int(m_sws, "srch", m_dec_ctx->height, 0);
        av_opt_set_int(m_sws, "src_format", m_dec_ctx->pix_fmt, 0);
        av_opt_set_int(m_sws, "dstw", get_app_config().m_dest_wight/2, 0);
        av_opt_set_int(m_sws, "dsth", get_app_config().m_dest_height/2, 0);
        av_opt_set_int(m_sws, "dst_format", AV_PIX_FMT_YUV420P, 0);
        av_opt_set_int(m_sws, "sws_flags", SWS_BICUBIC, 0);
        
        if (sws_init_context(m_sws, NULL, NULL) < 0)
            THROW_ERR("Cannot init scale context");
    }
    
    double m_last_pts = 0;
    double m_decode_begin = 0;
    
    void decode_frame(i_frame_consumer& consumer, stream_position pos)
    {
        AVPacket packet = {};
        AutoFree free_packet([&packet](){av_packet_unref(&packet);});
        
        double currtime = (av_gettime() / 1000.0);

        if (m_last_pts - currtime > 2000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return;
        }

        int ret;
        if ((ret = av_read_frame(m_fmt, &packet)) < 0)
            throw std::logic_error("Error read frame");
        
        if (packet.stream_index != m_stream_index)
            return;
        
        ret = avcodec_send_packet(m_dec_ctx, &packet);
        if (ret < 0)
            throw std::logic_error("Error sending a packet for decoding");
        
        while (ret >= 0) {
            ret = avcodec_receive_frame(m_dec_ctx, m_frame.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                return;
            else if (ret < 0)
                throw std::logic_error("Error during decoding");
            
            uint8_t *input[4];
            int px[4], py[4];
            
            for (int k = 0; m_frame->data[k]; k++)
                input[k] = m_frame->data[k] + py[k] * m_frame->linesize[k] + px[k];
            
            AVFramePtr frame = make_frame_ptr(av_frame_alloc());

            frame->format = AV_PIX_FMT_YUV420P;
            frame->width  = get_app_config().m_dest_wight/2;
            frame->height = get_app_config().m_dest_height/2;
            
            ret = av_frame_get_buffer(frame.get(), 0);
            if (ret < 0)
                throw std::logic_error("Error allocate buffer");
            
            // prepare frame with need size in decode thread
            sws_scale(m_sws,
                        m_frame->data, m_frame->linesize,
                        0, m_dec_ctx->height, 
                        frame->data, frame->linesize);

            AVRational tb = m_dec_ctx->framerate;
            if (!tb.den) {
                tb.num = 24; // try guess
                tb.den = 1;
            }
            AVFrame* f;
            
            /*m_frame->coded_picture_number*/
            double pts = m_frame_num/av_q2d(tb)*1000;
            //double pts = f->best_effort_timestamp;
            //pts*=av_q2d(tb);
            //double pts = m_frame->best_ /av_q2d(tb)*1000;
            m_frame_num++;
            
            double currtime = av_gettime() / 1000.0;
            if (!m_decode_begin) {
                m_decode_begin = currtime;
            }
            
            frame->pts = pts + m_decode_begin;
            
            /*if (currtime - frame->pts > 2000) {
                m_decode_begin += (currtime - frame->pts);
            }*/
                
            m_last_pts = frame->pts;
            
            LOGD("frame pts " << pts << " show frame time " << frame->pts << " pic num " <<  m_frame->coded_picture_number);
            consumer.append_frame(frame, pos);
        }
    }
};

typedef std::shared_ptr<decoder> decoder_ptr;

class decoder_context : public i_decoder_context
        , public std::enable_shared_from_this<decoder_context>
{
    const i_frame_consumer_ptr m_consumer;
    std::mutex m_mx;
    unsigned m_last_url_check;
    std::string m_current_url;
    std::string m_new_url;
    decoder_ptr m_decoder;
    std::shared_ptr<std::thread> m_thread;
    const stream_position m_pos;
public:
    decoder_context(std::shared_ptr<i_frame_consumer> consumer, stream_position pos)
        : m_consumer(consumer)
        , m_last_url_check(0)
        , m_thread()
        , m_pos(pos)
    {}
    
    ~decoder_context()
    {
        if (m_thread)
            m_thread->detach();
        LOG("~decoder_context " << this);
    }
    
    virtual void set_url(const std::string& url)
    {
        std::unique_lock<std::mutex> lock(m_mx);
        m_new_url = url;
    }
    
    void try_new_url()
    {
        bool need_reinit = false;
        if (m_last_url_check != time(NULL)) {
            m_last_url_check == time(NULL);
            std::unique_lock<std::mutex> lock(m_mx);
            if (m_new_url != m_current_url) {
                m_current_url = m_new_url;
                need_reinit = true;
            }
        }

        if (need_reinit)
            init_decoder();
        
    }
    
    void produce()
    {
        while(1) {
            try_new_url();
            
            if (m_consumer->done())
                return;
            
            if (!m_decoder)
                continue;
            
            try
            {
                m_decoder->decode_frame(*m_consumer, m_pos);
            }
            catch(std::exception& e)
            {
                LOG_CONS("Exception decode frame, producing stop " << e.what());
                set_url("");
            }
        }
    }
    
    void start_thread()
    {
        auto this_ptr = shared_from_this();
        m_thread = std::make_shared<std::thread>([this_ptr](){
            register_current_thread(std::string("Decoder")+std::to_string((int)this_ptr->m_pos));
            try
            {
                this_ptr->produce();
            }
            catch(std::exception& e)
            {
                LOG_CONS("Exception on consumer thread " << e.what());
            }
            
            LOG("Thread stopped " << std::this_thread::get_id());
        });
    }
    
    void init_decoder()
    {
        m_decoder.reset();
        if (m_current_url.empty())
            return;
        
        try
        {
            decoder_ptr dec_ptr = std::make_shared<decoder>(m_consumer, m_pos);
            dec_ptr->init(m_current_url);
            m_decoder = dec_ptr;
        }
        catch(std::exception& e)
        {
            LOG_CONS(e.what());
        }
    }
};

i_decoder_context_ptr start_decoder_thread(std::shared_ptr<i_frame_consumer> consumer, stream_position pos)
{
    std::shared_ptr<decoder_context> decoder_ctx = std::make_shared<decoder_context>(consumer, pos);
    decoder_ctx->start_thread();
    return decoder_ctx;
}

}
