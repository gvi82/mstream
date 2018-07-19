#include "common.h"
#include <fstream>
#include <chrono>
#include <mutex>
#include <vector>
#include <thread>
#include <iostream>

#include "ffmpeg_afx.h"

extern "C"
{
#include "libavutil/log.h"

}

AVFramePtr make_frame_ptr(AVFrame* ptr)
{
    return AVFramePtr(ptr, [](AVFrame* frame){av_frame_free(&frame);});
}

void av_log_ssream_callback(void *avcl, int level, const char *fmt, va_list vl)
{
    int print_prefix = 1;
    char line[1024];
    int type[2];
    unsigned tint = 0;

    if (level >= 0) {
        tint = level & 0xff00;
        level &= 0xff;
    }

    if (level > av_log_get_level())
        return;

    av_log_format_line(avcl, level, fmt, vl, line, sizeof(line), &print_prefix);
    mstream::trace_log(line);
}

namespace mstream
{

namespace
{
    std::ofstream m_file_log;
    std::mutex g_log_mutex;
    std::mutex g_thred_reg_mx;
}

void initialize_log()
{
    m_file_log.open("mstream.log", std::ofstream::out|std::ofstream::app);
    av_log_set_callback(av_log_ssream_callback);
}

namespace {
// maxinum 10 named threads, for lockfree using
std::vector< std::pair<std::thread::id, std::string > > m_threads(10);
    
std::string thread_name(const std::thread::id& id)
{
    for(size_t i = 0; i < m_threads.size(); ++i) {
        if (m_threads[i].first == id)
            return m_threads[i].second;
    }
    
    return "";
}

}

void register_thread(const std::thread::id& id, const std::string& name)
{
    std::unique_lock<std::mutex> lock(g_thred_reg_mx);
    
    for(size_t i = 0; i < m_threads.size(); ++i) {
        if (m_threads[i].first == std::thread::id()) {
            m_threads[i].first = id;
            m_threads[i].second = name;
            break;
        }
    }
        
}

void register_current_thread(const std::string& name)
{
    auto id = std::this_thread::get_id();
    register_thread(id, name);
}

void trace_log(const std::string& str, bool copy_to_console)
{
    double currtime = (av_gettime() / 1000.0);
    std::unique_lock<std::mutex> lock(g_log_mutex);
    std::string tname = thread_name(std::this_thread::get_id());
    
    auto dump = [&](std::ostream& strm) {
        strm << (int64_t)currtime << " thrd:";
        if (tname.empty())
            strm << std::this_thread::get_id();
        else
            strm << tname;
        
        strm << " " << str << std::endl;
    };
    
    dump(m_file_log);
    
    if (copy_to_console)
        dump(std::cout);
}

const app_config& get_app_config()
{
    static app_config cfg;
    return cfg;
}

}
