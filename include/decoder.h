#pragma once

#include <string>
#include <memory>
#include <common.h>

namespace mstream
{

struct i_decoder_context
{
    virtual ~i_decoder_context() = default;
    virtual void set_url(const std::string& url) = 0;
};

typedef std::shared_ptr<i_decoder_context> i_decoder_context_ptr;

struct i_frame_consumer;

i_decoder_context_ptr start_decoder_thread(std::shared_ptr<i_frame_consumer> consumer, stream_position pos);


}
