#include "encoder.h"
#include "ffmpeg_afx.h"
#include "common.h"

#include <mutex>
#include <thread>
#include <list>
#include <algorithm>

#include <SDL/SDL.h>

#define FF_REFRESH_EVENT (SDL_USEREVENT + 1)

static uint32_t sdl_refresh_timer(uint32_t interval, void *opaque) {
  SDL_Event event;
  event.type = FF_REFRESH_EVENT;
  event.user.data1 = opaque;
  SDL_PushEvent(&event);
  return 0;
}

namespace mstream
{

static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize,
                     char *filename)
{
    FILE *f;
    int i;

    f = fopen(filename,"w");
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}

struct FrameInfo
{
    AVFramePtr m_frame;
    stream_position m_pos;
};


class encoder
{
    SDL_Overlay* m_bmp = nullptr;
    SwsContext*  m_img_convert_context = nullptr;
    AVFramePtr m_frame;
    unsigned m_frame_num = 0;
    int m_linesize[4];
    int m_height[4];
    int m_nb_planes;
    SDL_Rect m_rect;
    
public:
    encoder()
    {}
    
    ~encoder()
    {
        if (m_bmp)
            SDL_FreeYUVOverlay(m_bmp);
        if (m_img_convert_context)
            sws_freeContext(m_img_convert_context);
    }
    
    void init_player()
    {
        m_frame = make_frame_ptr(av_frame_alloc());
        if (!m_frame)
            THROW_ERR("Error frame allocate");

        m_frame->format = AV_PIX_FMT_YUV420P;
        m_frame->width  = get_app_config().m_dest_wight;
        m_frame->height = get_app_config().m_dest_height;
        if (av_frame_get_buffer(m_frame.get(), 0)<0)
            THROW_ERR("Error frame allocate");

        m_nb_planes = av_pix_fmt_count_planes(AV_PIX_FMT_YUV420P);

        av_image_fill_linesizes(m_linesize, AV_PIX_FMT_YUV420P, get_app_config().m_dest_wight/2);
        const AVPixFmtDescriptor* fmt_desc = av_pix_fmt_desc_get(AV_PIX_FMT_YUV420P);

        m_height[1] = m_height[2] = AV_CEIL_RSHIFT(get_app_config().m_dest_height/2, fmt_desc->log2_chroma_h);
        m_height[0] = m_height[3] = get_app_config().m_dest_height/2;
        
        int ret = SDL_Init(SDL_INIT_VIDEO);
        
        if (ret < 0)
            THROW_ERR("Unable to init SDL" << SDL_GetError());

        SDL_Surface* screen = SDL_SetVideoMode(get_app_config().m_dest_wight, get_app_config().m_dest_height, 0, 0);
        if (screen == NULL)
            THROW_ERR("Couldn't set video mode");

        m_rect.x = 0;
        m_rect.y = 0;
        m_rect.w = get_app_config().m_dest_wight;
        m_rect.h = get_app_config().m_dest_height;
        
        m_bmp = SDL_CreateYUVOverlay(get_app_config().m_dest_wight, get_app_config().m_dest_height,
            SDL_YV12_OVERLAY, screen);
        m_img_convert_context = sws_getCachedContext(NULL,
                            get_app_config().m_dest_wight, get_app_config().m_dest_height,
                                            AV_PIX_FMT_YUV420P,
                                            get_app_config().m_dest_wight, get_app_config().m_dest_height,
                                            AV_PIX_FMT_YUV420P, SWS_BICUBIC,
                                            NULL, NULL, NULL);
        
        if (!m_img_convert_context)
            THROW_ERR("Couldn't create img converter");
    }

    void prepare_bmp(AVFramePtr frame, stream_position pos)
    {
        if (!frame)
            return;

        std::function<int(int)> offset;

        switch (pos) {
        case stream_pos_tl:
            offset = [this](int p)->int {return 0;};
            break;
        case stream_pos_tr:
            offset = [this](int p)->int {return m_linesize[p];};
            break;
        case stream_pos_bl:
            offset = [this](int p)->int {return m_height[p] * m_frame->linesize[p];};
            break;
        case stream_pos_br:
            offset = [this](int p)->int {return m_height[p] * m_frame->linesize[p] + m_linesize[p];};
            break;
        default:
            return;
        }
        
        for (int p = 0; p < m_nb_planes; p++) {
            av_image_copy_plane(m_frame->data[p] + offset(p),
                                m_frame->linesize[p],
                                frame->data[p],
                                frame->linesize[p],
                                m_linesize[p], m_height[p]);
        }

        SDL_LockYUVOverlay(m_bmp);
       
        // Convert frame to YV12 pixel format for display in SDL overlay
        uint8_t *pixels[3];
        pixels[0] = m_bmp->pixels[0];
        pixels[1] = m_bmp->pixels[2];  // it's because YV12
        pixels[2] = m_bmp->pixels[1];
        
        int linesize[3];
        linesize[0] = m_bmp->pitches[0];
        linesize[1] = m_bmp->pitches[2];
        linesize[2] = m_bmp->pitches[1];
        
        sws_scale(m_img_convert_context,
                                m_frame->data, m_frame->linesize,
                                0, get_app_config().m_dest_height,
                                pixels, linesize);
       
        SDL_UnlockYUVOverlay(m_bmp);
    }
    
    void display_frame()
    {
        SDL_DisplayYUVOverlay(m_bmp, &m_rect);
    }
    
    void process_frame_png(AVFrame* frame)
    {
        ++m_frame_num;
        if (!(m_frame_num%20))
            return;
        char buf[255];
        snprintf(buf, sizeof(buf), "%s-%d", "out", ++m_frame_num);
        pgm_save(frame->data[0], frame->linesize[0],
                 frame->width, frame->height, buf);
    }
};

class frame_consumer : public i_frame_consumer_master
        , public std::enable_shared_from_this<frame_consumer>
{
    bool m_done = false;
    std::mutex m_mx;
    std::shared_ptr<encoder> m_encoder;
    std::shared_ptr<std::thread> m_thread;
    std::vector< std::list<AVFramePtr> > m_streams_frames;
    
public:
    frame_consumer()
        : m_streams_frames(4)
    {}
    
    ~frame_consumer()
    {
        if (m_thread)
            m_thread->detach();
        LOG("~frame_consumer " << this);
    }
    
    virtual void append_frame(AVFramePtr frame, stream_position pos)
    {
        std::unique_lock<std::mutex> lock(m_mx);
        
        m_streams_frames[pos].push_back(frame);
    }
    
    virtual void reset_queue(stream_position pos)
    {
        std::unique_lock<std::mutex> lock(m_mx);
        m_streams_frames[pos].clear();
    }
    
    virtual bool done() const
    {
        return m_done;
    }
    
    virtual void set_done()
    {
        m_done = true;
    }
    
    void start_thread()
    {
        auto this_ptr = shared_from_this();
        m_thread = std::make_shared<std::thread>([this_ptr](){
            register_current_thread("Frame consumer");
            
            LOG("Thread consumer started " << std::this_thread::get_id());
            try {
                this_ptr->consume();
            } 
            catch(...) {
                LOG_CONS("consumer thread failed");
            }
            
            LOG("Thread consumer stopped " << std::this_thread::get_id());
        });
    }
    
    bool process_next_frame()
    {
        FrameInfo fi;
        AVFramePtr top_frame;
        size_t ind;
        
        std::unique_lock<std::mutex> lock(m_mx);
        for (size_t i = 0; i < m_streams_frames.size(); ++i) {
            std::list<AVFramePtr>& sframes = m_streams_frames[i];
            if (sframes.empty())
                continue;

            if (sframes.size() > 50)
                LOGD("size for queue " << i << " is " << sframes.size());

            AVFramePtr frame = sframes.front();
            if (!top_frame || frame->pts < top_frame->pts) {
                top_frame = frame;
                ind = i;
            }
        }
        
        if (top_frame)
            m_streams_frames[ind].pop_front();
        
        lock.unlock();

        if (top_frame) {
            int64_t pts = top_frame->pts;
            
            m_encoder->prepare_bmp(top_frame, (stream_position)ind);

            double currtime = (av_gettime() / 1000.0);

            LOGD("fetch frame top_frame->pts " << pts << " currtime " << (int64_t)currtime << " diff " << (pts-(int64_t)currtime));
            
            if (pts > currtime && (pts-(int64_t)currtime)) {
                SDL_AddTimer(pts-(int64_t)currtime, sdl_refresh_timer, (void*)pts);
                return false;
            }
            else
                m_encoder->display_frame();
        }
        
        return true;
    }
    
    void consume()
    {
        try
        {
            std::shared_ptr<encoder> enc = std::make_shared<encoder>();
            enc->init_player();
            m_encoder = enc;
        }
        catch(std::exception& e)
        {
            trace_log(e.what());
            set_done();
            return;
        }
        
        SDL_Event event;
        bool need_next_frame = true;
        
        while(!m_done)
        {
            if (need_next_frame)
            {
                need_next_frame = process_next_frame();
            }
            
            if (SDL_PollEvent(&event)) {
                switch (event.type) {
                    case SDL_QUIT:
                        set_done();
                        break;
                    case FF_REFRESH_EVENT:
                        m_encoder->display_frame();
                        LOGD("show frame with pts " << (int64_t)event.user.data1);
                        need_next_frame = true;
                        break;
                }
            }
        }
    }
};

i_frame_consumer_master_ptr start_consumer_thread()
{
    auto cons = std::make_shared<frame_consumer>();
    cons->start_thread();
    return cons;
}

}

