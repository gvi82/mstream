#pragma once
#include <string>
#include <sstream>
#include <memory>
#include <functional>
#include <thread>

#define LOG_(ARGS, cc) {std::ostringstream sstr; sstr << ARGS; trace_log(sstr.str(), cc);}
#define LOG(ARGS) LOG_(ARGS, false)
#define LOG_CONS(ARGS) LOG_(ARGS, true)
#define THROW_ERR(ARGS) {std::ostringstream sstr; sstr << ARGS; throw std::logic_error(sstr.str());}

#ifndef NDEBUG
#define LOGD(ARGS) LOG(ARGS)
#else
#define LOGD(ARGS)
#endif
/*#undef LOG
#define LOG(ARGS)*/

#define MANDATORY_PTR(ptr) ptr?ptr:throw std::logic_error("Mandatory ptr is null: "#ptr)
#define DECLARE_PTR_S(name) struct name; typedef std::shared_ptr<name> name##Ptr;

namespace mstream
{
void initialize_log();
void trace_log(const std::string& str, bool copy_to_console = false);

struct app_config
{
    int m_dest_height = 960;
    int m_dest_wight = 1280;
    size_t m_max_frames_in_queue = 1000;
};

typedef std::shared_ptr<app_config> app_config_ptr;

const app_config& get_app_config();

enum stream_position
{
    stream_pos_tl = 0, stream_pos_tr, stream_pos_bl, stream_pos_br
};

void register_thread(const std::thread::id& id, const std::string& name);
void register_current_thread(const std::string& name);

class AutoFree
{
    std::function<void()> m_free;
public:
    AutoFree(const std::function<void()>& free_f)
        :m_free(free_f)
    {}
    
    ~AutoFree()
    {
        if (m_free)
            m_free();
    }
    
    void cancel() {m_free = nullptr;}
};

}



