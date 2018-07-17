#include "common.h"
#include "decoder.h"
#include "encoder.h"

#include <fstream>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

using namespace mstream;

namespace {
void print_help()
{
    std::cout << "Use console commands: " << std::endl
        << "url [1,2,3,4] <url>: set url for stream. url can be system variable $VAR" << std::endl
        << "q or quit: exit programm" << std::endl
        << "cfg : reload from config" << std::endl
        << "help: show this message" << std::endl;
}

enum class process_action
{
    error,
    skip,
    exit,
    cfg,
    open_url,
    help,
};
    
process_action process_cmd(const std::string& input, int& stream_num, std::string& url)
{
    std::istringstream f(input);
    std::string s;
    bool doexit = false;

    enum class cmd_states{
        free,waiting_num,waiting_url
    };
    
    cmd_states state = cmd_states::free;
    
    while (getline(f, s, ' ')) {
        if (cmd_states::free == state ) {
            if (s[0] == '#') // comment
                return process_action::skip;
            if (s == "url") {
                state = cmd_states::waiting_num;
            }
            else
            if (s == "cfg") {
                return process_action::cfg;
            }
            else
            if (s == "help") {
                return process_action::help;
            }
            else
            if (s == "q" || s == "quit") {
                return process_action::exit;
            } else 
                return process_action::error;
        } else
        if (cmd_states::waiting_num == state) {
            try{
                stream_num = std::stoi(s);
            } catch(...) {}
            if (stream_num < 1 && stream_num > 4) {
                std::cout << "Stream num should be between 1 and 4" << std::endl;
                return process_action::error;
            }
            --stream_num;
            state = cmd_states::waiting_url;
        } else
        if (cmd_states::waiting_url == state) {
            if (!s.empty()) {
                if (s.at(0) == '$') {
                    if (getenv(s.c_str()+1))
                        url = getenv(s.c_str()+1);
                }
                else
                    url = s;
            }
            
            return process_action::open_url;
        }
    }
    
    if (cmd_states::waiting_url == state)
        return process_action::open_url; // clear strem
    return process_action::error;
}

void refresh_cfg(std::vector<i_decoder_context_ptr>& decoders)
{
    std::ifstream infile("mstream.conf");
    if (!infile) {
        std::cout << "mstream.conf not found" << std::endl;
    }
    std::string line;
    while (std::getline(infile, line))
    {
        int stream_num = -1;
        std::string url;
        
        auto action = process_cmd(line, stream_num, url);
        if (action == process_action::skip)
            continue;
        if (action != process_action::open_url) {
            std::cout << "wrong config line: " << line << std::endl;
            continue;
        }
        
        decoders[stream_num]->set_url(url);
    }};
}

int main(int argc, char **argv)
{
    initialize_log();
    register_current_thread("main thread");
    
    app_config_ptr config = std::make_shared<app_config>();
    
    i_frame_consumer_master_ptr cons = start_consumer_thread();
    
    std::vector<i_decoder_context_ptr> decoders(4);
    
    decoders[stream_pos_tl] = start_decoder_thread(cons, stream_pos_tl);
    decoders[stream_pos_tr] = start_decoder_thread(cons, stream_pos_tr);
    decoders[stream_pos_bl] = start_decoder_thread(cons, stream_pos_bl);
    decoders[stream_pos_br] = start_decoder_thread(cons, stream_pos_br);
    
    refresh_cfg(decoders);
    print_help();
    
    while(1) {
        std::cout << ">";

        std::string input;
        std::getline(std::cin, input);
        
        int stream_num = -1;
        std::string url;
        
        auto action = process_cmd(input, stream_num, url);
        
        if(process_action::exit == action)
            break;
        
        switch(action)
        {
          case process_action::open_url:
            decoders[stream_num]->set_url(url);
            break;
        case process_action::cfg:
          refresh_cfg(decoders);
          break;
          case process_action::help:
          case process_action::error:
            print_help();
            break;
        }
    }
    
    decoders.clear();
    cons->set_done();
    
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    return 0;
}

