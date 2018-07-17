#ifndef ENCODER_H
#define ENCODER_H

#include <memory>
#include <common.h>

DECLARE_PTR_S(AVFrame);

namespace mstream
{
struct app_config;

struct i_frame_consumer
{
    virtual ~i_frame_consumer() = default;
    virtual void append_frame(AVFramePtr frame, stream_position pos) = 0;
    virtual void reset_queue(stream_position pos) = 0;
    virtual bool done() const = 0;
    
};

struct i_frame_consumer_master : public i_frame_consumer
{
    virtual void set_done() = 0;
};


typedef std::shared_ptr<i_frame_consumer> i_frame_consumer_ptr;
typedef std::shared_ptr<i_frame_consumer_master> i_frame_consumer_master_ptr;

i_frame_consumer_master_ptr start_consumer_thread();

}

#endif // ENCODER_H
