#ifndef AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H

#include <string>

#include <sys/epoll.h>

#include <memory>
#include <vector>
#include <cassert>
#include <spdlog/logger.h>
#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>
#include <afina/execute/Command.h>
#include <protocol/Parser.h>
#include <mutex>


namespace Afina {
namespace Network {
namespace MTnonblock {
    
    
class Connection {
public:
    Connection(int s, std::shared_ptr<Afina::Storage> ps, std::shared_ptr<spdlog::logger> pl) :
    _socket(s), 
    pstorage(ps), 
    plogger(pl), 
    is_alive(false) 
    {
        std::memset(&_event, 0, sizeof(struct epoll_event));
        _event.data.ptr = this;
        plogger->info("Created mt_nonblocking Connection");
    }

    inline bool isAlive() const { 
        return is_alive.load(); 
    }

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
                        
    friend class ServerImpl;
    friend class Worker;

    int _socket;
    struct epoll_event _event;
    
                      
                                            

                                             

    std::shared_ptr<Afina::Storage> pstorage;
    std::shared_ptr<spdlog::logger> plogger;
    std::atomic<bool> is_alive;
    
    std::size_t arg_remains;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;
                       
                    
    int readed_bytes;
    char client_buffer[4096];
    int written_bytes;
                             
    std::vector<std::string> results;
    
    // для ДЗ №5
    std::mutex connection_mutex;
    
    // ----------------------
};

} // namespace MTnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
